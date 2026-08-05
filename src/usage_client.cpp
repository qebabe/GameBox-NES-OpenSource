#include "usage_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <cstring>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "device_identity.h"
#include "device_auth.h"
#include "network_security.h"
#include "ota_service.h"
#include "remote_config.h"
#include "service_config.h"

namespace {

constexpr const char* kUsageEventUrl = DIJI_USAGE_EVENT_URL;
constexpr const char* kDeviceHeartbeatUrl = DIJI_DEVICE_HEARTBEAT_URL;
constexpr uint32_t kUsageHttpTimeoutMs = 8000;
constexpr uint32_t kUsageTaskStackBytes = 7168;
constexpr uint32_t kUsageOnlineRetryMs = 60000;

struct UsageEvent {
    char name[24];
    char propertyName[24];
    char propertyValue[97];
};

std::atomic<bool> gUsageTaskRunning{false};
std::atomic<bool> gUsageOnlineReported{false};
std::atomic<uint32_t> gUsageOnlineAttemptedMs{0};

void copyField(char* destination, size_t size, const char* source) {
    if (!destination || size == 0) return;
    snprintf(destination, size, "%s", source ? source : "");
}

void usageEventTask(void* argument) {
    UsageEvent* event = static_cast<UsageEvent*>(argument);
    int code = 0;
    if (event && WiFi.status() == WL_CONNECTED) {
        String timeError;
        if (ensureGameBoxNetworkTime(timeError)) {
            String authError;
            deviceAuthEnsureRegistered(authError);
            WiFiClientSecure client;
            configureGameBoxTls(client);
            HTTPClient http;
            const bool heartbeat = strcmp(event->name, "device_online") == 0;
            const char* url = heartbeat ? kDeviceHeartbeatUrl : kUsageEventUrl;
            if (http.begin(client, url)) {
                http.setTimeout(kUsageHttpTimeoutMs);
                http.addHeader("Content-Type", "application/json");
                http.addHeader("User-Agent", String("GameBox/") + otaCurrentVersion());
                deviceAuthAddHeaders(http);

                JsonDocument document;
                document["device_id"] = gameBoxDeviceId();
                document["event"] = event->name;
                document["firmware_version"] = otaCurrentVersion();
                document["channel"] = otaCurrentChannel();
                if (event->propertyName[0] && event->propertyValue[0]) {
                    document["properties"][event->propertyName] = event->propertyValue;
                }
                String body;
                serializeJson(document, body);
                code = http.POST(body);
                deviceAuthHandleHttpStatus(code);
                http.end();
            }
        }
    }
    const bool onlineEvent = event && strcmp(event->name, "device_online") == 0;
    const bool accepted = onlineEvent ? code == HTTP_CODE_OK : code == HTTP_CODE_NO_CONTENT;
    if (onlineEvent && accepted) {
        gUsageOnlineReported.store(true);
    }
    if (event) delete event;
    if (!accepted) {
        Serial.printf("使用统计 上报未完成：HTTP状态码=%d\n", code);
    }
    gUsageTaskRunning.store(false);
    vTaskDelete(nullptr);
}

}  // namespace

bool usageReportEvent(const char* eventName) {
    return usageReportEvent(eventName, nullptr, nullptr);
}

bool usageReportEvent(const char* eventName, const char* propertyName,
                      const char* propertyValue) {
    if (!remoteConfigUsageReportingEnabled()) return true;
    if (!eventName || !eventName[0] || WiFi.status() != WL_CONNECTED) return false;
    bool expected = false;
    if (!gUsageTaskRunning.compare_exchange_strong(expected, true)) return false;

    UsageEvent* event = new (std::nothrow) UsageEvent{};
    if (!event) {
        gUsageTaskRunning.store(false);
        return false;
    }
    copyField(event->name, sizeof(event->name), eventName);
    copyField(event->propertyName, sizeof(event->propertyName), propertyName);
    copyField(event->propertyValue, sizeof(event->propertyValue), propertyValue);

    BaseType_t created = xTaskCreate(
        usageEventTask, "usage-report", kUsageTaskStackBytes, event, 1, nullptr
    );
    if (created != pdPASS) {
        delete event;
        gUsageTaskRunning.store(false);
        return false;
    }
    return true;
}

bool usageReportOnline() {
    if (!remoteConfigUsageReportingEnabled()) return true;
    if (gUsageOnlineReported.load()) return true;
    const uint32_t now = millis();
    const uint32_t lastAttempt = gUsageOnlineAttemptedMs.load();
    if (lastAttempt != 0 && (uint32_t)(now - lastAttempt) < kUsageOnlineRetryMs) {
        return false;
    }
    if (!usageReportEvent("device_online")) return false;
    gUsageOnlineAttemptedMs.store(now ? now : 1);
    return false;
}
