#include <Arduino.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ctype.h>
#include <esp_err.h>
#include <esp_now.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
#include "espnow_gamepad_protocol.h"
#include "network_security.h"
#include "ota_version.h"
#include "serial_controller.h"

#ifndef DIJI_GAMEPAD_FIRMWARE_VERSION
#define DIJI_GAMEPAD_FIRMWARE_VERSION "0.1.2"
#endif
#ifndef DIJI_GAMEPAD_OTA_CHANNEL
#define DIJI_GAMEPAD_OTA_CHANNEL "esp32c3-gamepad"
#endif
#ifndef DIJI_GAMEPAD_A_PIN
#define DIJI_GAMEPAD_A_PIN 10
#endif
#ifndef DIJI_GAMEPAD_B_PIN
#define DIJI_GAMEPAD_B_PIN 4
#endif
#ifndef DIJI_GAMEPAD_SELECT_PIN
#define DIJI_GAMEPAD_SELECT_PIN 5
#endif
#ifndef DIJI_GAMEPAD_START_PIN
#define DIJI_GAMEPAD_START_PIN 6
#endif
#ifndef DIJI_GAMEPAD_UP_PIN
#define DIJI_GAMEPAD_UP_PIN 0
#endif
#ifndef DIJI_GAMEPAD_DOWN_PIN
#define DIJI_GAMEPAD_DOWN_PIN 1
#endif
#ifndef DIJI_GAMEPAD_LEFT_PIN
#define DIJI_GAMEPAD_LEFT_PIN 3
#endif
#ifndef DIJI_GAMEPAD_RIGHT_PIN
#define DIJI_GAMEPAD_RIGHT_PIN 7
#endif
#ifndef DIJI_GAMEPAD_LED_PIN
#define DIJI_GAMEPAD_LED_PIN 8
#endif
#ifndef DIJI_GAMEPAD_LED_ACTIVE_LEVEL
#define DIJI_GAMEPAD_LED_ACTIVE_LEVEL 1
#endif
#ifndef DIJI_GAMEPAD_BATTERY_PIN
#define DIJI_GAMEPAD_BATTERY_PIN -1
#endif
#ifndef DIJI_GAMEPAD_RUMBLE_PIN
#define DIJI_GAMEPAD_RUMBLE_PIN -1
#endif

static constexpr uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static constexpr uint32_t ACTIVE_STATE_INTERVAL_MS = 16;
static constexpr uint32_t IDLE_STATE_INTERVAL_MS = 100;
static constexpr uint32_t CONNECTED_IDLE_AFTER_MS = 10000;
static constexpr uint32_t DEEP_SLEEP_AFTER_MS = 5UL * 60UL * 1000UL;
static constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 10000;
static constexpr uint8_t BATTERY_SAMPLE_COUNT = 8;
static constexpr uint32_t OFFLINE_SCAN_PAUSE_MS = 1500;
static constexpr uint32_t PAIR_HOLD_MS = 1500;
// 与主机的 15 秒配对窗口保持一致，避免用户刚松开组合键时手柄已经停止广播。
static constexpr uint32_t PAIR_SEND_MS = 15000;
static constexpr uint8_t PAIR_BUTTON_MASK = DIJI_BTN_SELECT | DIJI_BTN_START;
static constexpr uint32_t OTA_WIFI_TIMEOUT_MS = 15000;
static constexpr uint32_t OTA_HTTP_TIMEOUT_MS = 20000;
static constexpr size_t OTA_BUFFER_SIZE = 4096;

static uint32_t sequence_number = 0;
static uint32_t last_state_ms = 0;
static uint32_t last_user_activity_ms = 0;
static uint8_t previous_buttons = 0;
static uint8_t last_sent_buttons = 0xFF;
static uint32_t pair_pressed_ms = 0;
static uint32_t pair_mode_until_ms = 0;
static bool pair_was_pressed = false;
static volatile uint32_t last_host_packet_ms = 0;
static volatile uint8_t pending_rumble_strength = 0;
static volatile uint16_t pending_rumble_duration_ms = 0;
static uint32_t rumble_until_ms = 0;
static uint8_t paired_host_mac[6] = {0};
static volatile bool paired_host_valid = false;
static volatile bool pending_host_save = false;
static volatile bool pending_host_peer = false;
static volatile bool pending_channel_save = false;
static volatile uint8_t current_wifi_channel = 1;
static uint8_t saved_wifi_channel = 1;
static volatile uint8_t channel_scan_index = 0;
static uint32_t last_channel_scan_ms = 0;
static uint32_t channel_scan_pause_until_ms = 0;
static bool management_hello_sent = false;
static volatile bool pending_update_command = false;
static volatile uint32_t pending_update_session_id = 0;
static volatile bool pending_update_offer_valid = false;
static char pending_update_ssid[33] = {0};
static char pending_update_password[65] = {0};
static char pending_update_version[24] = {0};
static char pending_update_url[129] = {0};
static char pending_update_sha256[65] = {0};
static uint32_t pending_update_size = 0;
static bool ota_active = false;
static volatile bool state_send_in_flight = false;
static volatile uint32_t state_send_started_ms = 0;
static volatile uint32_t tx_delivery_ok = 0;
static volatile uint32_t tx_delivery_fail = 0;
static uint32_t tx_state_enqueued = 0;
static uint32_t tx_state_skipped = 0;
static uint32_t tx_enqueue_errors = 0;
static volatile uint32_t tx_callback_max_ms = 0;
static volatile bool pending_latency_pong = false;
static volatile uint32_t pending_latency_token = 0;
static volatile bool pending_time_sync_response = false;
static volatile uint32_t pending_time_sync_t1 = 0;
static volatile uint32_t pending_time_sync_t2 = 0;
static uint32_t last_diagnostics_ms = 0;
static uint32_t last_state_packet_ms = 0;
static uint32_t max_state_packet_gap_ms = 0;
static bool last_reported_connected = false;
static uint8_t last_reported_buttons = 0;
static uint8_t cached_battery_percent = 0;
static bool cached_battery_valid = false;
static uint32_t last_battery_sample_ms = 0;
static uint8_t last_reported_power_mode = 0xFF;
static uint32_t pair_request_enqueued = 0;
static uint32_t pair_request_enqueue_errors = 0;

static bool sameMac(const uint8_t* a, const uint8_t* b) {
    return a && b && memcmp(a, b, 6) == 0;
}

static void loadPairedHost() {
    Preferences prefs;
    if (!prefs.begin("gamepad", true)) return;
    if (prefs.getBytesLength("host") == sizeof(paired_host_mac)) {
        paired_host_valid =
            prefs.getBytes("host", paired_host_mac, sizeof(paired_host_mac)) ==
            sizeof(paired_host_mac);
    }
    uint8_t channel = prefs.getUChar("channel", 1);
    if (channel >= 1 && channel <= 13) saved_wifi_channel = channel;
    prefs.end();
}

static void savePairedHost() {
    Preferences prefs;
    if (!prefs.begin("gamepad", false)) return;
    prefs.putBytes("host", paired_host_mac, sizeof(paired_host_mac));
    prefs.putUChar("channel", current_wifi_channel);
    saved_wifi_channel = current_wifi_channel;
    prefs.end();
}

static bool setWifiChannel(uint8_t channel) {
    if (channel < 1 || channel > 13) return false;
    if (esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE) != ESP_OK) return false;
    current_wifi_channel = channel;
    return true;
}

static void addPeerIfNeeded(const uint8_t* mac) {
    if (!mac || esp_now_is_peer_exist(mac)) return;
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, sizeof(peer.peer_addr));
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
}

static void setStatusLed(bool on) {
    if (DIJI_GAMEPAD_LED_PIN < 0) {
        return;
    }
    digitalWrite(DIJI_GAMEPAD_LED_PIN,
                 on ? DIJI_GAMEPAD_LED_ACTIVE_LEVEL : !DIJI_GAMEPAD_LED_ACTIVE_LEVEL);
}

static bool pairModeActive() {
    return pair_mode_until_ms != 0 && (int32_t)(millis() - pair_mode_until_ms) < 0;
}

static bool hostConnected() {
    return last_host_packet_ms != 0 && (uint32_t)(millis() - last_host_packet_ms) < 2200;
}

enum class GamepadPowerMode : uint8_t {
    Active = 0,
    ConnectedIdle = 1,
    Searching = 2,
};

static GamepadPowerMode currentPowerMode(uint8_t buttons, uint32_t now) {
    if (!hostConnected()) return GamepadPowerMode::Searching;
    if (buttons != 0 ||
        (uint32_t)(now - last_user_activity_ms) < CONNECTED_IDLE_AFTER_MS) {
        return GamepadPowerMode::Active;
    }
    return GamepadPowerMode::ConnectedIdle;
}

static const char* powerModeName(GamepadPowerMode mode) {
    switch (mode) {
        case GamepadPowerMode::Active: return "活动60Hz";
        case GamepadPowerMode::ConnectedIdle: return "连接空闲10Hz";
        case GamepadPowerMode::Searching: return "离线间歇扫频";
    }
    return "未知";
}

static void updateStatusLed(uint8_t buttons) {
    if (DIJI_GAMEPAD_LED_PIN < 0) {
        return;
    }

    uint32_t now = millis();
    if (pairModeActive()) {
        setStatusLed(((now / 120) & 1) == 0);
        return;
    }

    if (buttons != 0) {
        setStatusLed(true);
        return;
    }

    if (hostConnected()) {
        setStatusLed((now % 2500) < 40);
    } else {
        setStatusLed((now % 900) < 60);
    }
}

static bool packetLooksValid(const DijiEspNowGamepadPacket& packet) {
    return packet.magic == DIJI_ESPNOW_GAMEPAD_MAGIC &&
           packet.version == DIJI_ESPNOW_GAMEPAD_VERSION;
}

static void updateBatterySample(bool force = false);
static bool readBattery(uint8_t& percent);

static bool managementPacketLooksValid(const DijiEspNowManagementPacket& packet) {
    return packet.magic == DIJI_ESPNOW_MANAGEMENT_MAGIC &&
           packet.version == DIJI_ESPNOW_MANAGEMENT_VERSION;
}

static void sendManagementPacket(DijiEspNowManagementType type,
                                 DijiGamepadUpdateStatus status,
                                 uint32_t sessionId, uint8_t progress) {
    if (!paired_host_valid) return;
    addPeerIfNeeded(paired_host_mac);
    DijiEspNowManagementPacket packet;
    packet.type = (uint8_t)type;
    packet.status = (uint8_t)status;
    packet.sessionId = sessionId;
    packet.progress = progress;
    snprintf(packet.firmwareVersion, sizeof(packet.firmwareVersion), "%s",
             DIJI_GAMEPAD_FIRMWARE_VERSION);
    uint8_t battery = 0;
    if (readBattery(battery)) {
        packet.batteryPercent = battery;
        packet.flags |= DIJI_GAMEPAD_FLAG_BATTERY_VALID;
    }
    esp_now_send(paired_host_mac, (const uint8_t*)&packet, sizeof(packet));
}

static void onEspNowSend(const uint8_t*, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) tx_delivery_ok++;
    else tx_delivery_fail++;
    if (state_send_in_flight) {
        uint32_t elapsed = millis() - state_send_started_ms;
        if (elapsed > tx_callback_max_ms) tx_callback_max_ms = elapsed;
        state_send_in_flight = false;
    }
}

static void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (!mac || !data || len < (int)sizeof(uint32_t)) {
        return;
    }
    const uint32_t receivedAtMs = millis();

    uint32_t magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (magic == DIJI_ESPNOW_MANAGEMENT_MAGIC) {
        if (len < 6 || !paired_host_valid || !sameMac(mac, paired_host_mac)) {
            return;
        }
        const uint8_t type = data[5];
        if (type == (uint8_t)DijiEspNowManagementType::UpdateOffer) {
            if (len < (int)sizeof(DijiEspNowUpdateOfferPacket)) return;
            DijiEspNowUpdateOfferPacket offer;
            memcpy(&offer, data, sizeof(offer));
            if (offer.version != DIJI_ESPNOW_MANAGEMENT_VERSION ||
                !offer.firmwareVersion[0] || !offer.url[0] ||
                strlen(offer.sha256) != 64 || offer.firmwareSize == 0 ||
                compareOtaVersions(DIJI_GAMEPAD_FIRMWARE_VERSION,
                                   offer.firmwareVersion) >= 0 ||
                pending_update_command || ota_active) {
                return;
            }
            pending_update_session_id = offer.sessionId;
            pending_update_size = offer.firmwareSize;
            snprintf(pending_update_version, sizeof(pending_update_version),
                     "%s", offer.firmwareVersion);
            snprintf(pending_update_url, sizeof(pending_update_url), "%s", offer.url);
            snprintf(pending_update_sha256, sizeof(pending_update_sha256),
                     "%s", offer.sha256);
            pending_update_offer_valid = true;
            sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                                 DijiGamepadUpdateStatus::OfferReceived,
                                 offer.sessionId, 0);
            return;
        }
        if (type != (uint8_t)DijiEspNowManagementType::WifiCredentials ||
            len < (int)sizeof(DijiEspNowManagementPacket)) {
            return;
        }
        DijiEspNowManagementPacket management;
        memcpy(&management, data, sizeof(management));
        if (!managementPacketLooksValid(management) ||
            !management.ssid[0] || !pending_update_offer_valid ||
            management.sessionId != pending_update_session_id ||
            pending_update_command || ota_active) {
            return;
        }
        snprintf(pending_update_ssid, sizeof(pending_update_ssid), "%s",
                 management.ssid);
        snprintf(pending_update_password, sizeof(pending_update_password), "%s",
                 management.password);
        pending_update_command = true;
        last_host_packet_ms = millis();
        return;
    }
    if (len < (int)sizeof(DijiEspNowGamepadPacket)) return;

    DijiEspNowGamepadPacket packet;
    memcpy(&packet, data, sizeof(packet));
    if (!packetLooksValid(packet)) {
        return;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::LatencyPing) {
        if (paired_host_valid && sameMac(mac, paired_host_mac)) {
            pending_latency_token = packet.sequence;
            pending_latency_pong = true;
            last_host_packet_ms = receivedAtMs;
        }
        return;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::TimeSyncRequest) {
        if (paired_host_valid && sameMac(mac, paired_host_mac)) {
            pending_time_sync_t1 = packet.syncT1;
            pending_time_sync_t2 = receivedAtMs;
            pending_time_sync_response = true;
            last_host_packet_ms = receivedAtMs;
        }
        return;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::PairAck && pairModeActive()) {
        memcpy(paired_host_mac, mac, sizeof(paired_host_mac));
        paired_host_valid = true;
        pending_host_save = true;
        pending_host_peer = true;
    } else if (!paired_host_valid || !sameMac(mac, paired_host_mac)) {
        return;
    }

    if (packet.wifiChannel >= 1 && packet.wifiChannel <= 13) {
        // Receiving this packet proves that the radio is already on the
        // advertised channel; only update/persist our channel model here.
        current_wifi_channel = packet.wifiChannel;
        if (packet.wifiChannel != saved_wifi_channel) pending_channel_save = true;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::Rumble) {
        pending_rumble_strength = packet.rumbleStrength;
        pending_rumble_duration_ms = packet.rumbleDurationMs;
    }
    if (packet.type == (uint8_t)DijiEspNowPacketType::PairAck ||
        packet.type == (uint8_t)DijiEspNowPacketType::HostHeartbeat ||
        packet.type == (uint8_t)DijiEspNowPacketType::ChannelAck ||
        packet.type == (uint8_t)DijiEspNowPacketType::Rumble) {
        last_host_packet_ms = millis();
        pair_mode_until_ms = 0;
        channel_scan_index = 0;
        channel_scan_pause_until_ms = 0;
    }
}

static void setupButton(int pin) {
    if (pin >= 0) {
        pinMode(pin, INPUT_PULLUP);
    }
}

static uint8_t readButtonBit(int pin, uint8_t bit) {
    if (pin < 0) {
        return 0;
    }
    return digitalRead(pin) == LOW ? bit : 0;
}

static uint8_t readButtons() {
    uint8_t state = 0;
    state |= readButtonBit(DIJI_GAMEPAD_A_PIN, DIJI_BTN_A);
    state |= readButtonBit(DIJI_GAMEPAD_B_PIN, DIJI_BTN_B);
    state |= readButtonBit(DIJI_GAMEPAD_SELECT_PIN, DIJI_BTN_SELECT);
    state |= readButtonBit(DIJI_GAMEPAD_START_PIN, DIJI_BTN_START);
    state |= readButtonBit(DIJI_GAMEPAD_UP_PIN, DIJI_BTN_UP);
    state |= readButtonBit(DIJI_GAMEPAD_DOWN_PIN, DIJI_BTN_DOWN);
    state |= readButtonBit(DIJI_GAMEPAD_LEFT_PIN, DIJI_BTN_LEFT);
    state |= readButtonBit(DIJI_GAMEPAD_RIGHT_PIN, DIJI_BTN_RIGHT);
    return state;
}

static uint8_t batteryPercentForMillivolts(uint32_t mv) {
    struct BatteryPoint {
        uint16_t millivolts;
        uint8_t percent;
    };
    static constexpr BatteryPoint curve[] = {
        {3400, 0}, {3500, 5}, {3650, 20}, {3750, 40},
        {3850, 60}, {4000, 80}, {4150, 100},
    };
    if (mv <= curve[0].millivolts) return curve[0].percent;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (mv <= curve[i].millivolts) {
            uint32_t voltageSpan = curve[i].millivolts - curve[i - 1].millivolts;
            uint32_t voltageOffset = mv - curve[i - 1].millivolts;
            uint32_t percentSpan = curve[i].percent - curve[i - 1].percent;
            return (uint8_t)(curve[i - 1].percent +
                             voltageOffset * percentSpan / voltageSpan);
        }
    }
    return 100;
}

static void updateBatterySample(bool force) {
    if (DIJI_GAMEPAD_BATTERY_PIN < 0) {
        cached_battery_valid = false;
        return;
    }
    uint32_t now = millis();
    if (!force && cached_battery_valid &&
        (uint32_t)(now - last_battery_sample_ms) < BATTERY_SAMPLE_INTERVAL_MS) {
        return;
    }

    uint32_t totalMv = 0;
    for (uint8_t i = 0; i < BATTERY_SAMPLE_COUNT; i++) {
        totalMv += analogReadMilliVolts(DIJI_GAMEPAD_BATTERY_PIN);
        delayMicroseconds(150);
    }
    uint32_t batteryMv = (totalMv / BATTERY_SAMPLE_COUNT) * 2u;
    cached_battery_percent = batteryPercentForMillivolts(batteryMv);
    cached_battery_valid = true;
    last_battery_sample_ms = now;
}

static bool readBattery(uint8_t& percent) {
    updateBatterySample(false);
    if (!cached_battery_valid) return false;
    percent = cached_battery_percent;
    return true;
}

static bool isSha256(const String& value) {
    if (value.length() != 64) return false;
    for (size_t i = 0; i < value.length(); i++) {
        const char c = value[i];
        if (!isxdigit((unsigned char)c)) return false;
    }
    return true;
}

struct GamepadOtaInfo {
    String version;
    String url;
    String sha256;
    size_t size = 0;
};

static bool installGamepadUpdate(const GamepadOtaInfo& info,
                                 uint32_t sessionId, String& error) {
    const esp_partition_t* partition = esp_ota_get_next_update_partition(nullptr);
    if (!partition || info.size > partition->size) {
        error = "OTA分区空间不足";
        return false;
    }

    WiFiClientSecure client;
    configureHashedDownloadTls(client);
    HTTPClient http;
    if (!http.begin(client, info.url)) {
        error = "无法打开固件地址";
        return false;
    }
    http.setTimeout(OTA_HTTP_TIMEOUT_MS);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = String("固件下载失败: ") + code;
        http.end();
        return false;
    }
    int contentLength = http.getSize();
    if (contentLength <= 0 || (size_t)contentLength != info.size) {
        error = "固件大小不匹配";
        http.end();
        return false;
    }
    if (!Update.begin(info.size, U_FLASH)) {
        error = String("无法开始OTA: ") + Update.getError();
        http.end();
        return false;
    }

    uint8_t* buffer = (uint8_t*)malloc(OTA_BUFFER_SIZE);
    if (!buffer) {
        error = "OTA缓冲区不足";
        Update.abort();
        http.end();
        return false;
    }

    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    WiFiClient* stream = http.getStreamPtr();
    size_t downloaded = 0;
    uint32_t lastDataMs = millis();
    uint8_t lastProgress = 0;
    bool failed = false;

    while (downloaded < info.size) {
        size_t available = stream->available();
        if (!available) {
            if (!http.connected() ||
                (uint32_t)(millis() - lastDataMs) > OTA_HTTP_TIMEOUT_MS) {
                error = "固件下载中断";
                failed = true;
                break;
            }
            delay(1);
            continue;
        }
        size_t wanted = min(available, min(OTA_BUFFER_SIZE, info.size - downloaded));
        int readLen = stream->readBytes(buffer, wanted);
        if (readLen <= 0) {
            error = "读取固件失败";
            failed = true;
            break;
        }
        if (Update.write(buffer, (size_t)readLen) != (size_t)readLen) {
            error = String("写入OTA分区失败: ") + Update.getError();
            failed = true;
            break;
        }
        mbedtls_sha256_update_ret(&sha, buffer, (size_t)readLen);
        downloaded += (size_t)readLen;
        lastDataMs = millis();
        uint8_t progress = (uint8_t)(downloaded * 100 / info.size);
        if (progress >= (uint8_t)(lastProgress + 10) || progress == 100) {
            lastProgress = progress;
            sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                                 DijiGamepadUpdateStatus::Downloading,
                                 sessionId, progress);
        }
    }

    http.end();
    free(buffer);
    uint8_t digest[32] = {0};
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);

    if (failed || downloaded != info.size) {
        Update.abort();
        return false;
    }

    char actualSha[65] = {0};
    for (size_t i = 0; i < sizeof(digest); i++) {
        snprintf(actualSha + i * 2, 3, "%02x", digest[i]);
    }
    if (!info.sha256.equalsIgnoreCase(actualSha)) {
        error = "OTA SHA-256校验失败";
        Update.abort();
        return false;
    }
    if (!Update.end() || !Update.isFinished()) {
        error = String("OTA镜像验证失败: ") + Update.getError();
        return false;
    }
    return true;
}

static void runPendingGamepadUpdate() {
    char ssid[sizeof(pending_update_ssid)] = {0};
    char password[sizeof(pending_update_password)] = {0};
    snprintf(ssid, sizeof(ssid), "%s", pending_update_ssid);
    snprintf(password, sizeof(password), "%s", pending_update_password);
    const uint32_t sessionId = pending_update_session_id;
    GamepadOtaInfo info;
    info.version = pending_update_version;
    info.url = pending_update_url;
    info.sha256 = pending_update_sha256;
    info.size = pending_update_size;
    pending_update_command = false;
    pending_update_offer_valid = false;
    memset(pending_update_password, 0, sizeof(pending_update_password));
    ota_active = true;

    uint8_t battery = 0;
    updateBatterySample(true);
    if (readBattery(battery) && battery < 30) {
        sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                             DijiGamepadUpdateStatus::LowBattery,
                             sessionId, 0);
        ota_active = false;
        memset(password, 0, sizeof(password));
        return;
    }

    sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                         DijiGamepadUpdateStatus::Accepted, sessionId, 0);
    sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                         DijiGamepadUpdateStatus::ConnectingWifi, sessionId, 0);
    WiFi.begin(ssid, password);
    memset(password, 0, sizeof(password));
    uint32_t started = millis();
    while (WiFi.status() != WL_CONNECTED &&
           (uint32_t)(millis() - started) < OTA_WIFI_TIMEOUT_MS) {
        delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("手柄 OTA WiFi连接失败：SSID=%s 状态=%d\n",
                      ssid, (int)WiFi.status());
        sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                             DijiGamepadUpdateStatus::Failed, sessionId, 0);
        WiFi.disconnect(false, false);
        setWifiChannel(saved_wifi_channel);
        ota_active = false;
        return;
    }

    String error;
    bool metadataValid =
        isValidOtaVersion(info.version.c_str()) &&
        info.url.startsWith("https://") && isSha256(info.sha256) &&
        compareOtaVersions(DIJI_GAMEPAD_FIRMWARE_VERSION,
                           info.version.c_str()) < 0;
    if (!metadataValid) {
        error = "主机下发的OTA信息无效";
    }
    if (!metadataValid || !ensureGameBoxNetworkTime(error) ||
        !installGamepadUpdate(info, sessionId, error)) {
        Serial.printf("手柄 OTA 安装失败：%s\n", error.c_str());
        sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                             DijiGamepadUpdateStatus::Failed, sessionId, 0);
    } else {
        Serial.printf("手柄 OTA 安装完成：%s -> %s\n",
                      DIJI_GAMEPAD_FIRMWARE_VERSION, info.version.c_str());
        sendManagementPacket(DijiEspNowManagementType::UpdateStatus,
                             DijiGamepadUpdateStatus::Installed, sessionId, 100);
        delay(500);
        ESP.restart();
    }

    WiFi.disconnect(false, false);
    setWifiChannel(saved_wifi_channel);
    ota_active = false;
    management_hello_sent = false;
}

static void sendPacket(DijiEspNowPacketType type, uint8_t buttons,
                       uint32_t sampleTimeMs = 0) {
    if (type == DijiEspNowPacketType::State && state_send_in_flight) {
        tx_state_skipped++;
        return;
    }
    DijiEspNowGamepadPacket packet;
    packet.type = (uint8_t)type;
    packet.buttons = buttons;
    packet.sequence = sequence_number++;
    packet.wifiChannel = current_wifi_channel;
    if (type == DijiEspNowPacketType::State) {
        packet.sampleTimeMs = sampleTimeMs;
    }
    uint8_t battery = 0;
    if (readBattery(battery)) {
        packet.batteryPercent = battery;
        packet.flags |= DIJI_GAMEPAD_FLAG_BATTERY_VALID;
    }
    const uint8_t* destination =
        type == DijiEspNowPacketType::PairRequest || !paired_host_valid
            ? BROADCAST_MAC
            : paired_host_mac;
    if (type == DijiEspNowPacketType::State) {
        state_send_in_flight = true;
        state_send_started_ms = millis();
    }
    esp_err_t result = esp_now_send(destination, (const uint8_t*)&packet, sizeof(packet));
    if (type == DijiEspNowPacketType::PairRequest) {
        if (result == ESP_OK) pair_request_enqueued++;
        else pair_request_enqueue_errors++;
    }
    if (type == DijiEspNowPacketType::State) {
        if (result == ESP_OK) {
            tx_state_enqueued++;
            uint32_t now = millis();
            if (last_state_packet_ms != 0) {
                uint32_t gap = now - last_state_packet_ms;
                if (gap > max_state_packet_gap_ms) max_state_packet_gap_ms = gap;
            }
            last_state_packet_ms = now;
        } else {
            state_send_in_flight = false;
            tx_enqueue_errors++;
        }
    }
}

static void sendLatencyPongIfPending() {
    if (!pending_latency_pong || !paired_host_valid || state_send_in_flight) return;
    uint32_t token = pending_latency_token;
    pending_latency_pong = false;
    DijiEspNowGamepadPacket packet;
    packet.type = (uint8_t)DijiEspNowPacketType::LatencyPong;
    packet.sequence = token;
    packet.wifiChannel = current_wifi_channel;
    state_send_in_flight = true;
    state_send_started_ms = millis();
    if (esp_now_send(paired_host_mac, (const uint8_t*)&packet,
                     sizeof(packet)) != ESP_OK) {
        state_send_in_flight = false;
        tx_enqueue_errors++;
    }
}

static void sendTimeSyncResponseIfPending() {
    if (!pending_time_sync_response || !paired_host_valid ||
        state_send_in_flight) {
        return;
    }
    uint32_t t1 = pending_time_sync_t1;
    uint32_t t2 = pending_time_sync_t2;
    pending_time_sync_response = false;
    DijiEspNowGamepadPacket packet;
    packet.type = (uint8_t)DijiEspNowPacketType::TimeSyncResponse;
    packet.syncT1 = t1;
    packet.syncT2 = t2;
    packet.syncT3 = millis();
    packet.wifiChannel = current_wifi_channel;
    state_send_in_flight = true;
    state_send_started_ms = packet.syncT3;
    if (esp_now_send(paired_host_mac, (const uint8_t*)&packet,
                     sizeof(packet)) != ESP_OK) {
        state_send_in_flight = false;
        tx_enqueue_errors++;
    }
}

static void printDiagnostics(uint8_t buttons) {
    uint32_t now = millis();
    bool connected = hostConnected();
    GamepadPowerMode powerMode = currentPowerMode(buttons, now);
    if ((uint8_t)powerMode != last_reported_power_mode) {
        Serial.printf("手柄功耗模式：%s\n", powerModeName(powerMode));
        last_reported_power_mode = (uint8_t)powerMode;
    }
    if (connected != last_reported_connected) {
        Serial.printf("手柄连接状态：%s 信道=%u 主机包间隔=%u毫秒\n",
                      connected ? "已连接" : "离线",
                      (unsigned)current_wifi_channel,
                      last_host_packet_ms ? (unsigned)(now - last_host_packet_ms) : 0);
        last_reported_connected = connected;
    }
    if (buttons != last_reported_buttons) {
        Serial.printf("手柄按键变化：0x%02X -> 0x%02X 运行时间=%u毫秒\n",
                      last_reported_buttons, buttons, (unsigned)now);
        last_reported_buttons = buttons;
    }
    if (last_diagnostics_ms != 0 &&
        (uint32_t)(now - last_diagnostics_ms) < 2000) {
        return;
    }
    last_diagnostics_ms = now;
    uint8_t battery = 0;
    bool batteryValid = readBattery(battery);
    Serial.printf("手柄通信统计：模式=%s 连接=%s CH=%u 主机包年龄=%u毫秒 "
                  "状态包=%u 跳过=%u 入队错误=%u 送达=%u 失败=%u "
                  "配对请求=%u 配对入队错误=%u "
                  "最大发送回调=%u毫秒 最大状态间隔=%u毫秒 按键=0x%02X "
                  "电量=%s%u%%\n",
                  powerModeName(powerMode), connected ? "是" : "否",
                  (unsigned)current_wifi_channel,
                  last_host_packet_ms ? (unsigned)(now - last_host_packet_ms) : 0,
                  (unsigned)tx_state_enqueued, (unsigned)tx_state_skipped,
                  (unsigned)tx_enqueue_errors, (unsigned)tx_delivery_ok,
                  (unsigned)tx_delivery_fail,
                  (unsigned)pair_request_enqueued,
                  (unsigned)pair_request_enqueue_errors,
                  (unsigned)tx_callback_max_ms,
                  (unsigned)max_state_packet_gap_ms, buttons,
                  batteryValid ? "" : "未知/", (unsigned)battery);
}

static void updatePairMode(uint8_t buttons) {
    bool pair_pressed = (buttons & PAIR_BUTTON_MASK) == PAIR_BUTTON_MASK;
    uint32_t now = millis();

    if (pair_pressed && !pair_was_pressed) {
        pair_pressed_ms = now;
    }
    pair_was_pressed = pair_pressed;

    if (pair_pressed && pair_pressed_ms != 0 &&
        (uint32_t)(now - pair_pressed_ms) >= PAIR_HOLD_MS) {
        pair_mode_until_ms = now + PAIR_SEND_MS;
        pair_pressed_ms = 0;
        channel_scan_index = 0;
        last_channel_scan_ms = 0;
        channel_scan_pause_until_ms = 0;
        pair_request_enqueued = 0;
        pair_request_enqueue_errors = 0;
        Serial.printf("手柄进入配对模式：时长=%u毫秒 协议=%u 包长=%u MAC=%s\n",
                      (unsigned)PAIR_SEND_MS,
                      (unsigned)DIJI_ESPNOW_GAMEPAD_VERSION,
                      (unsigned)sizeof(DijiEspNowGamepadPacket),
                      WiFi.macAddress().c_str());
    }

}

static void updateChannelDiscovery(uint8_t buttons) {
    bool searching = pairModeActive() || (paired_host_valid && !hostConnected());
    if (!searching) return;
    uint32_t now = millis();
    if (!pairModeActive() && channel_scan_pause_until_ms != 0 &&
        (int32_t)(now - channel_scan_pause_until_ms) < 0) {
        return;
    }
    if (last_channel_scan_ms != 0 &&
        (uint32_t)(now - last_channel_scan_ms) < DIJI_ESPNOW_CHANNEL_SCAN_INTERVAL_MS) return;
    last_channel_scan_ms = now;
    uint8_t channel = DIJI_ESPNOW_CHANNEL_SCAN_ORDER[channel_scan_index];
    channel_scan_index = (uint8_t)((channel_scan_index + 1) %
        (sizeof(DIJI_ESPNOW_CHANNEL_SCAN_ORDER) /
         sizeof(DIJI_ESPNOW_CHANNEL_SCAN_ORDER[0])));
    if (!pairModeActive() && channel_scan_index == 0) {
        channel_scan_pause_until_ms = now + OFFLINE_SCAN_PAUSE_MS;
    }
    if (!setWifiChannel(channel)) return;
    // esp_wifi_set_channel 返回后给射频一个很短的稳定窗口，再发配对广播。
    delay(3);
    sendPacket(pairModeActive() ? DijiEspNowPacketType::PairRequest
                                : DijiEspNowPacketType::ChannelProbe,
               buttons);
}

static void addDeepSleepWakePin(uint64_t& mask, int pin) {
    // ESP32-C3 deep-sleep GPIO wake is available on the RTC-powered GPIO0-5.
    if (pin >= 0 && pin <= 5) mask |= 1ULL << pin;
}

static uint64_t gamepadDeepSleepWakeMask() {
    uint64_t mask = 0;
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_A_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_B_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_SELECT_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_START_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_UP_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_DOWN_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_LEFT_PIN);
    addDeepSleepWakePin(mask, DIJI_GAMEPAD_RIGHT_PIN);
    return mask;
}

static void enterGamepadDeepSleep() {
    uint64_t wakeMask = gamepadDeepSleepWakeMask();
    if (wakeMask == 0) {
        Serial.println("手柄深睡已跳过：没有可用的GPIO0-5唤醒按键");
        last_user_activity_ms = millis();
        return;
    }

    setStatusLed(false);
    if (DIJI_GAMEPAD_RUMBLE_PIN >= 0) {
        analogWrite(DIJI_GAMEPAD_RUMBLE_PIN, 0);
    }
    esp_err_t wakeResult =
        esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (wakeResult != ESP_OK) {
        Serial.printf("手柄深睡已取消：按键唤醒配置失败=%s mask=0x%llX\n",
                      esp_err_to_name(wakeResult),
                      (unsigned long long)wakeMask);
        last_user_activity_ms = millis();
        return;
    }

    Serial.printf("手柄进入深睡：空闲=%u秒，唤醒GPIO mask=0x%llX\n",
                  (unsigned)(DEEP_SLEEP_AFTER_MS / 1000),
                  (unsigned long long)wakeMask);
    Serial.flush();
    esp_deep_sleep_start();
}

static void updateUserActivityAndSleep(uint8_t buttons, uint32_t now) {
    if (buttons != previous_buttons) {
        previous_buttons = buttons;
        last_user_activity_ms = now;
    }
    if (buttons == 0 && !pairModeActive() && !ota_active &&
        !pending_update_command &&
        (uint32_t)(now - last_user_activity_ms) >= DEEP_SLEEP_AFTER_MS) {
        enterGamepadDeepSleep();
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);

    esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
    if (wakeCause != ESP_SLEEP_WAKEUP_UNDEFINED) {
        Serial.printf("手柄从深睡唤醒：原因=%d GPIO状态=0x%llX\n",
                      (int)wakeCause,
                      (unsigned long long)esp_sleep_get_gpio_wakeup_status());
    }
    last_user_activity_ms = millis();

    if (DIJI_GAMEPAD_LED_PIN >= 0) {
        pinMode(DIJI_GAMEPAD_LED_PIN, OUTPUT);
        setStatusLed(true);
    }
    if (DIJI_GAMEPAD_RUMBLE_PIN >= 0) {
        pinMode(DIJI_GAMEPAD_RUMBLE_PIN, OUTPUT);
        analogWrite(DIJI_GAMEPAD_RUMBLE_PIN, 0);
    }

    setupButton(DIJI_GAMEPAD_A_PIN);
    setupButton(DIJI_GAMEPAD_B_PIN);
    setupButton(DIJI_GAMEPAD_SELECT_PIN);
    setupButton(DIJI_GAMEPAD_START_PIN);
    setupButton(DIJI_GAMEPAD_UP_PIN);
    setupButton(DIJI_GAMEPAD_DOWN_PIN);
    setupButton(DIJI_GAMEPAD_LEFT_PIN);
    setupButton(DIJI_GAMEPAD_RIGHT_PIN);

    loadPairedHost();
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
    setWifiChannel(saved_wifi_channel);
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        return;
    }

    esp_now_register_recv_cb(onEspNowReceive);
    esp_now_register_send_cb(onEspNowSend);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, BROADCAST_MAC, 6);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    if (paired_host_valid) addPeerIfNeeded(paired_host_mac);

    Serial.printf("XJ-NES C3 gamepad ready, mac=%s channel=%u\n",
                  WiFi.macAddress().c_str(), (unsigned)current_wifi_channel);
    Serial.println("Hold SELECT + START for pairing");
    setStatusLed(false);

    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running &&
        esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("手柄 OTA 新固件已确认有效");
    }
}

void loop() {
    if (pending_update_command && !ota_active) {
        runPendingGamepadUpdate();
    }

    uint8_t buttons = readButtons();
    uint32_t buttonSampleMs = millis();
    bool buttonsChanged = buttons != previous_buttons;
    updateUserActivityAndSleep(buttons, buttonSampleMs);
    updatePairMode(buttons);
    updateChannelDiscovery(buttons);
    updateStatusLed(buttons);
    sendTimeSyncResponseIfPending();
    sendLatencyPongIfPending();
    printDiagnostics(buttons);

    if (pending_host_peer) {
        pending_host_peer = false;
        addPeerIfNeeded(paired_host_mac);
    }
    if (pending_host_save || pending_channel_save) {
        bool channelChanged = pending_channel_save;
        pending_host_save = false;
        pending_channel_save = false;
        savePairedHost();
        if (channelChanged) {
            Serial.printf("ESP-NOW channel locked: %u\n", (unsigned)current_wifi_channel);
        }
    }

    if (pending_rumble_duration_ms != 0) {
        uint8_t strength = pending_rumble_strength;
        uint16_t duration = pending_rumble_duration_ms;
        pending_rumble_duration_ms = 0;
        rumble_until_ms = millis() + duration;
        if (DIJI_GAMEPAD_RUMBLE_PIN >= 0) analogWrite(DIJI_GAMEPAD_RUMBLE_PIN, strength);
    }
    if (rumble_until_ms && (int32_t)(millis() - rumble_until_ms) >= 0) {
        rumble_until_ms = 0;
        if (DIJI_GAMEPAD_RUMBLE_PIN >= 0) analogWrite(DIJI_GAMEPAD_RUMBLE_PIN, 0);
    }

    uint32_t now = millis();
    bool connected = hostConnected();
    if (!connected) {
        management_hello_sent = false;
    } else if (paired_host_valid && !management_hello_sent) {
        management_hello_sent = true;
        sendManagementPacket(DijiEspNowManagementType::Hello,
                             DijiGamepadUpdateStatus::Idle, 0, 0);
    }

    GamepadPowerMode powerMode = currentPowerMode(buttons, now);
    uint32_t stateInterval = powerMode == GamepadPowerMode::ConnectedIdle
                                 ? IDLE_STATE_INTERVAL_MS
                                 : ACTIVE_STATE_INTERVAL_MS;
    if (connected &&
        (buttonsChanged || last_sent_buttons == 0xFF ||
         (uint32_t)(now - last_state_ms) >= stateInterval)) {
        last_state_ms = now;
        last_sent_buttons = buttons;
        sendPacket(DijiEspNowPacketType::State, buttons, buttonSampleMs);
    }
    delay(2);
}
