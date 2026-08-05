#include "device_auth.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "device_identity.h"
#include "network_security.h"
#include "ota_service.h"
#include "service_config.h"

namespace {

constexpr const char* kDeviceRegisterUrl = DIJI_DEVICE_REGISTER_URL;
constexpr const char* kPreferencesNamespace = "device-auth";
constexpr const char* kTokenKey = "token";
constexpr const char* kRegisteredKey = "registered";
constexpr uint32_t kRegisterTimeoutMs = 10000;

SemaphoreHandle_t gAuthMutex = nullptr;
String gDeviceToken;
bool gAuthLoaded = false;
bool gRegistered = false;

SemaphoreHandle_t authMutex() {
    if (!gAuthMutex) gAuthMutex = xSemaphoreCreateMutex();
    return gAuthMutex;
}

void makeRandomToken(String& token) {
    uint8_t randomBytes[32];
    esp_fill_random(randomBytes, sizeof(randomBytes));
    token = "";
    token.reserve(64);
    char pair[3];
    for (uint8_t value : randomBytes) {
        snprintf(pair, sizeof(pair), "%02x", value);
        token += pair;
    }
}

bool saveAuthState() {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return false;
    bool ok = preferences.putString(kTokenKey, gDeviceToken) == gDeviceToken.length();
    ok = preferences.putBool(kRegisteredKey, gRegistered) && ok;
    preferences.end();
    return ok;
}

bool loadAuthState() {
    if (gAuthLoaded) return gDeviceToken.length() == 64;
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return false;
    gDeviceToken = preferences.getString(kTokenKey, "");
    gRegistered = preferences.getBool(kRegisteredKey, false);
    if (gDeviceToken.length() != 64) {
        makeRandomToken(gDeviceToken);
        gRegistered = false;
        preferences.putString(kTokenKey, gDeviceToken);
        preferences.putBool(kRegisteredKey, false);
    }
    preferences.end();
    gAuthLoaded = true;
    return gDeviceToken.length() == 64;
}

}  // namespace

bool deviceAuthEnsureRegistered(String& error) {
    error = "";
    SemaphoreHandle_t mutex = authMutex();
    if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(15000)) != pdTRUE) {
        error = "设备认证正忙";
        return false;
    }
    if (!loadAuthState()) {
        xSemaphoreGive(mutex);
        error = "无法创建设备认证令牌";
        return false;
    }
    if (gRegistered) {
        xSemaphoreGive(mutex);
        return true;
    }
    if (WiFi.status() != WL_CONNECTED) {
        xSemaphoreGive(mutex);
        error = "WiFi 未连接";
        return false;
    }
    if (!ensureGameBoxNetworkTime(error)) {
        xSemaphoreGive(mutex);
        return false;
    }

    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    if (!http.begin(client, kDeviceRegisterUrl)) {
        xSemaphoreGive(mutex);
        error = "无法连接设备注册服务";
        return false;
    }
    http.setTimeout(kRegisterTimeoutMs);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("User-Agent", String("GameBox/") + otaCurrentVersion());

    JsonDocument request;
    request["device_id"] = gameBoxDeviceId();
    request["token"] = gDeviceToken;
    request["firmware_version"] = otaCurrentVersion();
    request["channel"] = otaCurrentChannel();
    String body;
    serializeJson(request, body);
    int code = http.POST(body);
    http.end();
    if (code != HTTP_CODE_OK && code != HTTP_CODE_CREATED) {
        xSemaphoreGive(mutex);
        error = code == HTTP_CODE_CONFLICT
                    ? "设备认证冲突，请在服务端重置认证"
                    : String("设备注册失败: ") + code;
        return false;
    }

    gRegistered = true;
    bool saved = saveAuthState();
    xSemaphoreGive(mutex);
    if (!saved) {
        error = "设备认证状态保存失败";
        return false;
    }
    Serial.printf("设备认证 注册成功：设备=%s\n", gameBoxDeviceId().c_str());
    return true;
}

void deviceAuthAddHeaders(HTTPClient& http) {
    http.addHeader("X-Device-ID", gameBoxDeviceId());
    http.addHeader("X-Firmware-Version", otaCurrentVersion());
    http.addHeader("X-Device-Channel", otaCurrentChannel());
    SemaphoreHandle_t mutex = authMutex();
    if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    loadAuthState();
    if (gRegistered && gDeviceToken.length() == 64) {
        http.addHeader("X-Device-Token", gDeviceToken);
    }
    xSemaphoreGive(mutex);
}

void deviceAuthHandleHttpStatus(int statusCode) {
    if (statusCode != HTTP_CODE_UNAUTHORIZED) return;
    SemaphoreHandle_t mutex = authMutex();
    if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return;
    if (loadAuthState() && gRegistered) {
        gRegistered = false;
        saveAuthState();
    }
    xSemaphoreGive(mutex);
}

bool deviceAuthIsRegistered() {
    SemaphoreHandle_t mutex = authMutex();
    if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    bool ready = loadAuthState() && gRegistered;
    xSemaphoreGive(mutex);
    return ready;
}
