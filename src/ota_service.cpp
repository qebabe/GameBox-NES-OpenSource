#include "ota_service.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_app_format.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>

#include "device_identity.h"
#include "device_auth.h"
#include "espnow_host.h"
#include "network_security.h"
#include "ota_version.h"
#include "service_config.h"

static constexpr uint32_t kOtaHttpTimeoutMs = 20000;
static constexpr uint32_t kOtaIdleTimeoutMs = 20000;
static constexpr size_t kOtaBufferSize = 4096;
static constexpr const char* kOtaEventUrl = DIJI_OTA_EVENT_URL;
static constexpr uint32_t kRollbackReportRetryMs = 60000;
static uint32_t gRollbackReportLastAttemptMs = 0;
static bool gRollbackReportComplete = false;

const char* otaCurrentVersion() {
    return DIJI_FIRMWARE_VERSION;
}

const char* otaCurrentChannel() {
    return DIJI_OTA_CHANNEL;
}

static bool isSha256(const String& value) {
    if (value.length() != 64) return false;
    for (size_t i = 0; i < value.length(); i++) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

static bool reportOtaEvent(const char* eventName, const String& targetVersion,
                           const char* errorCode = nullptr) {
    if (!eventName || WiFi.status() != WL_CONNECTED) return false;
    String authError;
    deviceAuthEnsureRegistered(authError);
    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    if (!http.begin(client, kOtaEventUrl)) return false;
    http.setTimeout(3000);
    http.addHeader("Content-Type", "application/json");
    deviceAuthAddHeaders(http);
    JsonDocument request;
    request["device_id"] = gameBoxDeviceId();
    request["event"] = eventName;
    request["current_version"] = otaCurrentVersion();
    request["target_version"] = targetVersion;
    request["channel"] = otaCurrentChannel();
    if (errorCode && errorCode[0]) request["error_code"] = errorCode;
    String body;
    serializeJson(request, body);
    int code = http.POST(body);
    deviceAuthHandleHttpStatus(code);
    http.end();
    if (code != HTTP_CODE_NO_CONTENT) {
        Serial.printf("OTA 事件上报未完成：事件=%s HTTP状态码=%d\n", eventName, code);
    }
    return code == HTTP_CODE_NO_CONTENT;
}

bool otaCheckForUpdate(OtaUpdateInfo& info, String& error) {
    info = OtaUpdateInfo{};
    error = "";
    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi 未连接";
        return false;
    }
    if (!ensureGameBoxNetworkTime(error)) {
        return false;
    }
    String authError;
    deviceAuthEnsureRegistered(authError);

    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    if (!http.begin(client, DIJI_OTA_CHECK_URL)) {
        error = "无法打开 OTA 地址";
        return false;
    }
    http.setTimeout(kOtaHttpTimeoutMs);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Device-Id", gameBoxDeviceId());
    http.addHeader("Current-Version", otaCurrentVersion());
    http.addHeader("OTA-Channel", otaCurrentChannel());
    http.addHeader("User-Agent", String("GameBox/") + otaCurrentVersion());
    deviceAuthAddHeaders(http);

    JsonDocument request;
    request["device_id"] = gameBoxDeviceId();
    request["mac_address"] = gameBoxMacAddress();
    request["current_version"] = otaCurrentVersion();
    request["channel"] = otaCurrentChannel();
    request["free_space"] = ESP.getFreeSketchSpace();
    request["flash_size"] = ESP.getFlashChipSize();
    request["direct_oss"] = true;
    JsonArray gamepads = request["gamepads"].to<JsonArray>();
    for (uint8_t player = 0; player < 2; player++) {
        char version[24] = {0};
        char mac[24] = {0};
        if (!espNowHostGetFirmwareVersion(player, version, sizeof(version)) ||
            !espNowHostGetPairedMacString(player, mac, sizeof(mac))) {
            continue;
        }
        JsonObject gamepad = gamepads.add<JsonObject>();
        gamepad["player"] = player;
        gamepad["mac_address"] = mac;
        gamepad["current_version"] = version;
        gamepad["channel"] = "esp32c3-gamepad";
        gamepad["direct_oss"] =
            compareOtaVersions(version, "0.1.2") >= 0;
    }
    String body;
    serializeJson(request, body);

    int code = http.POST(body);
    deviceAuthHandleHttpStatus(code);
    if (code != HTTP_CODE_OK) {
        error = String("检查更新失败: ") + code;
        http.end();
        return false;
    }

    JsonDocument response;
    DeserializationError jsonError = deserializeJson(response, http.getStream());
    http.end();
    if (jsonError) {
        error = "OTA 响应解析失败";
        return false;
    }

    JsonObject firmware = response["firmware"].as<JsonObject>();
    if (firmware.isNull()) {
        info.message = response["message"] | "当前已是最新版本";
    } else {
        info.version = firmware["version"] | "";
        info.url = firmware["url"] | "";
        info.sha256 = firmware["sha256"] | "";
        info.size = firmware["size"] | 0;
        info.forced = firmware["force"] | false;
        info.message = firmware["message"] | "";
        String channel = firmware["channel"] | otaCurrentChannel();

        if (channel != otaCurrentChannel()) {
            error = "OTA 固件型号不匹配";
            return false;
        }
        if (!info.url.startsWith("https://")) {
            error = "OTA 下载地址不是 HTTPS";
            return false;
        }
        if (!isSha256(info.sha256) || info.size == 0 ||
            !isValidOtaVersion(info.version.c_str())) {
            error = "OTA 固件信息不完整";
            return false;
        }

        info.available =
            info.forced ||
            compareOtaVersions(otaCurrentVersion(), info.version.c_str()) < 0;
        if (!info.available && info.message.isEmpty()) {
            info.message = "当前已是最新版本";
        }
    }

    JsonArray gamepadResponses = response["gamepads"].as<JsonArray>();
    for (JsonObject gamepadResponse : gamepadResponses) {
        int player = gamepadResponse["player"] | -1;
        if (player < 0 || player >= 2) continue;
        OtaUpdateInfo::GamepadUpdate& update = info.gamepads[player];
        update.present = true;
        update.player = (uint8_t)player;
        update.currentVersion = gamepadResponse["current_version"] | "";
        JsonObject gamepadFirmware = gamepadResponse["firmware"].as<JsonObject>();
        if (gamepadFirmware.isNull()) continue;
        update.version = gamepadFirmware["version"] | "";
        update.url = gamepadFirmware["url"] | "";
        update.sha256 = gamepadFirmware["sha256"] | "";
        update.size = gamepadFirmware["size"] | 0;
        update.message = gamepadFirmware["message"] | "";
        String gamepadChannel = gamepadFirmware["channel"] | "";
        if (gamepadChannel != "esp32c3-gamepad" ||
            !update.url.startsWith("https://") ||
            !isSha256(update.sha256) || update.size == 0 ||
            !isValidOtaVersion(update.version.c_str()) ||
            !isValidOtaVersion(update.currentVersion.c_str())) {
            error = String("P") + (player + 1) + " 手柄 OTA 固件信息不完整";
            return false;
        }
        update.available =
            compareOtaVersions(update.currentVersion.c_str(),
                               update.version.c_str()) < 0;
    }
    return true;
}

static bool validateImageHeader(const uint8_t* data, size_t size,
                                String& imageVersion, String& error) {
    constexpr size_t kAppDescOffset = sizeof(esp_image_header_t) +
                                      sizeof(esp_image_segment_header_t);
    constexpr size_t kRequired = kAppDescOffset + sizeof(esp_app_desc_t);
    if (!data || size < kRequired || data[0] != ESP_IMAGE_HEADER_MAGIC) {
        error = "OTA 镜像头无效";
        return false;
    }

    esp_app_desc_t appDesc;
    memcpy(&appDesc, data + kAppDescOffset, sizeof(appDesc));
    if (appDesc.magic_word != ESP_APP_DESC_MAGIC_WORD) {
        error = "OTA 应用描述无效";
        return false;
    }
    char version[sizeof(appDesc.version) + 1] = {0};
    memcpy(version, appDesc.version, sizeof(appDesc.version));
    imageVersion = version;
    return true;
}

bool otaInstallUpdate(const OtaUpdateInfo& info, OtaProgressCallback progress,
                      void* userData, String& error) {
    error = "";
    if (!info.available || info.size == 0 || !isSha256(info.sha256)) {
        error = "没有可安装的 OTA 固件";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi 未连接";
        return false;
    }

    const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (!updatePartition || info.size > updatePartition->size) {
        error = "OTA 分区空间不足";
        return false;
    }

    reportOtaEvent("download_started", info.version);

    WiFiClientSecure client;
    configureHashedDownloadTls(client);
    HTTPClient http;
    if (!http.begin(client, info.url)) {
        error = "无法打开固件下载地址";
        reportOtaEvent("download_failed", info.version, "http_begin");
        return false;
    }
    http.setTimeout(kOtaHttpTimeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = String("固件下载失败: ") + code;
        http.end();
        reportOtaEvent("download_failed", info.version, "http_status");
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0 || (size_t)contentLength != info.size) {
        error = "固件大小与清单不一致";
        http.end();
        reportOtaEvent("download_failed", info.version, "content_length");
        return false;
    }

    uint8_t* buffer = (uint8_t*)heap_caps_malloc(kOtaBufferSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buffer) {
        error = "OTA 缓冲区分配失败";
        http.end();
        reportOtaEvent("download_failed", info.version, "buffer_alloc");
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);

    WiFiClient* stream = http.getStreamPtr();
    size_t buffered = 0;
    size_t downloaded = 0;
    size_t recentBytes = 0;
    uint32_t lastDataMs = millis();
    uint32_t lastProgressMs = lastDataMs;
    bool updateStarted = false;
    bool failed = false;

    constexpr size_t kHeaderRequired = sizeof(esp_image_header_t) +
                                       sizeof(esp_image_segment_header_t) +
                                       sizeof(esp_app_desc_t);

    while (downloaded < info.size) {
        size_t available = stream->available();
        if (!available) {
            if (!http.connected() || (uint32_t)(millis() - lastDataMs) > kOtaIdleTimeoutMs) {
                error = "固件下载中断";
                failed = true;
                break;
            }
            delay(1);
            continue;
        }

        size_t room = kOtaBufferSize - buffered;
        size_t remaining = info.size - downloaded;
        size_t wanted = min(available, min(room, remaining));
        int readLen = stream->readBytes(buffer + buffered, wanted);
        if (readLen <= 0) {
            error = "读取固件数据失败";
            failed = true;
            break;
        }

        mbedtls_sha256_update_ret(&sha, buffer + buffered, (size_t)readLen);
        buffered += (size_t)readLen;
        downloaded += (size_t)readLen;
        recentBytes += (size_t)readLen;
        lastDataMs = millis();

        if (!updateStarted && buffered >= kHeaderRequired) {
            String imageVersion;
            if (!validateImageHeader(buffer, buffered, imageVersion, error)) {
                failed = true;
                break;
            }
            Serial.printf("OTA image version: %s, manifest version: %s\n",
                          imageVersion.c_str(), info.version.c_str());
            if (!Update.begin(info.size, U_FLASH)) {
                error = String("无法开始 OTA: ") + Update.getError();
                failed = true;
                break;
            }
            updateStarted = true;
        }

        if (updateStarted && (buffered == kOtaBufferSize || downloaded == info.size)) {
            if (Update.write(buffer, buffered) != buffered) {
                error = String("写入 OTA 分区失败: ") + Update.getError();
                failed = true;
                break;
            }
            buffered = 0;
        }

        uint32_t now = millis();
        if (progress && ((uint32_t)(now - lastProgressMs) >= 1000 || downloaded == info.size)) {
            uint32_t elapsed = now - lastProgressMs;
            size_t speed = elapsed > 0 ? recentBytes * 1000 / elapsed : recentBytes;
            progress(downloaded, info.size, speed, userData);
            recentBytes = 0;
            lastProgressMs = now;
        }
    }

    http.end();
    heap_caps_free(buffer);

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (failed || !updateStarted || downloaded != info.size) {
        if (updateStarted) Update.abort();
        if (error.isEmpty()) error = "OTA 下载不完整";
        reportOtaEvent("download_failed", info.version, "stream_or_write");
        return false;
    }

    char actualSha[65];
    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(actualSha + i * 2, 3, "%02x", digest[i]);
    }
    actualSha[64] = '\0';
    if (!info.sha256.equalsIgnoreCase(actualSha)) {
        Update.abort();
        error = "OTA SHA-256 校验失败";
        reportOtaEvent("download_failed", info.version, "sha256");
        return false;
    }

    if (!Update.end() || !Update.isFinished()) {
        error = String("OTA 镜像验证失败: ") + Update.getError();
        reportOtaEvent("install_failed", info.version, "image_finalize");
        return false;
    }
    Preferences otaPreferences;
    if (otaPreferences.begin("ota-report", false)) {
        otaPreferences.putString("pending", info.version);
        otaPreferences.putString("pending_part", updatePartition->label);
        otaPreferences.end();
    }
    reportOtaEvent("download_completed", info.version);
    reportOtaEvent("install_completed", info.version);
    return true;
}

bool otaCurrentFirmwarePendingVerify() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (!running) return false;
    esp_ota_img_states_t state;
    return esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void otaMarkCurrentFirmwareValid() {
    if (otaCurrentFirmwarePendingVerify()) {
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

void otaReportCurrentFirmwareBootOk() {
    reportOtaEvent("boot_ok", otaCurrentVersion());
    Preferences preferences;
    if (preferences.begin("ota-report", false)) {
        preferences.remove("pending");
        preferences.remove("pending_part");
        preferences.end();
    }
}

void otaUpdateReporting() {
    if (gRollbackReportComplete || WiFi.status() != WL_CONNECTED) return;
    const uint32_t now = millis();
    if (gRollbackReportLastAttemptMs != 0 &&
        (uint32_t)(now - gRollbackReportLastAttemptMs) < kRollbackReportRetryMs) {
        return;
    }
    gRollbackReportLastAttemptMs = now ? now : 1;

    Preferences preferences;
    if (!preferences.begin("ota-report", false)) return;
    String pendingVersion = preferences.getString("pending", "");
    String pendingPartition = preferences.getString("pending_part", "");
    String reported = preferences.getString("rollback", "");
    preferences.end();
    if (pendingVersion.isEmpty() || pendingPartition.isEmpty()) {
        gRollbackReportComplete = true;
        return;
    }
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running && pendingPartition == running->label) {
        // The new image is still inside its boot-health observation window.
        return;
    }

    const esp_partition_t* invalid = esp_ota_get_last_invalid_partition();
    if (!invalid) return;
    if (pendingPartition != invalid->label) {
        gRollbackReportComplete = true;
        return;
    }
    String marker = pendingVersion + "@" + pendingPartition;
    if (reported == marker) {
        gRollbackReportComplete = true;
        return;
    }

    if (!reportOtaEvent("rollback", pendingVersion, "boot_rollback")) return;
    if (preferences.begin("ota-report", false)) {
        preferences.putString("rollback", marker);
        preferences.remove("pending");
        preferences.remove("pending_part");
        preferences.end();
    }
    gRollbackReportComplete = true;
}
