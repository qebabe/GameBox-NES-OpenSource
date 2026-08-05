#include "remote_config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <time.h>

#include "device_auth.h"
#include "network_security.h"
#include "service_config.h"

namespace {

constexpr const char* kConfigUrl = DIJI_REMOTE_CONFIG_URL;
constexpr const char* kPreferencesNamespace = "remote-config";
constexpr uint32_t kDefaultTtlSeconds = 6 * 60 * 60;
constexpr uint32_t kRetrySeconds = 5 * 60;
constexpr time_t kMinimumValidEpoch = 1704067200;  // 2024-01-01 UTC
constexpr time_t kMaximumCacheAgeSeconds = 7 * 24 * 60 * 60;

std::atomic<bool> gLoaded{false};
std::atomic<bool> gFetchRunning{false};
std::atomic<bool> gStoreEnabled{true};
std::atomic<bool> gCloudSavesEnabled{true};
std::atomic<bool> gUsageReportingEnabled{true};
std::atomic<uint32_t> gRevision{0};
std::atomic<uint32_t> gNextFetchMs{0};
uint64_t gFetchedAt = 0;

void applySafeDefaults() {
    gStoreEnabled.store(true);
    gCloudSavesEnabled.store(true);
    gUsageReportingEnabled.store(true);
    gRevision.store(0);
}

bool cacheIsExpired(uint64_t fetchedAt) {
    const time_t now = time(nullptr);
    if (now < kMinimumValidEpoch || fetchedAt < (uint64_t)kMinimumValidEpoch) return false;
    return (uint64_t)now > fetchedAt + (uint64_t)kMaximumCacheAgeSeconds;
}

void loadCachedConfig() {
    bool expected = false;
    if (!gLoaded.compare_exchange_strong(expected, true)) return;
    applySafeDefaults();
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, true)) return;
    const bool valid = preferences.getBool("valid", false);
    gFetchedAt = preferences.getULong64("fetched", 0);
    if (valid && !cacheIsExpired(gFetchedAt)) {
        gStoreEnabled.store(preferences.getBool("store", true));
        gCloudSavesEnabled.store(preferences.getBool("cloud", true));
        gUsageReportingEnabled.store(preferences.getBool("usage", true));
        gRevision.store(preferences.getUInt("revision", 0));
    }
    preferences.end();
}

bool saveCachedConfig(bool storeEnabled, bool cloudEnabled, bool usageEnabled,
                      uint32_t revision, uint64_t fetchedAt) {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return false;
    bool ok = preferences.putBool("store", storeEnabled);
    ok = preferences.putBool("cloud", cloudEnabled) && ok;
    ok = preferences.putBool("usage", usageEnabled) && ok;
    ok = preferences.putUInt("revision", revision) == sizeof(uint32_t) && ok;
    ok = preferences.putULong64("fetched", fetchedAt) == sizeof(uint64_t) && ok;
    ok = preferences.putBool("valid", true) && ok;
    preferences.end();
    return ok;
}

void expireStaleCacheIfNeeded() {
    if (!cacheIsExpired(gFetchedAt)) return;
    applySafeDefaults();
    gFetchedAt = 0;
    Preferences preferences;
    if (preferences.begin(kPreferencesNamespace, false)) {
        preferences.putBool("valid", false);
        preferences.end();
    }
    Serial.println("远程配置 缓存超过 7 天，已恢复安全默认值");
}

void scheduleNextFetch(uint32_t seconds) {
    gNextFetchMs.store(millis() + seconds * 1000UL);
}

void fetchConfigTask(void*) {
    String authError;
    if (!deviceAuthEnsureRegistered(authError)) {
        Serial.printf("远程配置 设备认证未就绪：%s\n", authError.c_str());
        expireStaleCacheIfNeeded();
        scheduleNextFetch(kRetrySeconds);
        gFetchRunning.store(false);
        vTaskDelete(nullptr);
        return;
    }

    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    if (!http.begin(client, kConfigUrl)) {
        scheduleNextFetch(kRetrySeconds);
        gFetchRunning.store(false);
        vTaskDelete(nullptr);
        return;
    }
    http.setTimeout(12000);
    deviceAuthAddHeaders(http);
    const int code = http.GET();
    deviceAuthHandleHttpStatus(code);
    if (code != HTTP_CODE_OK) {
        Serial.printf("远程配置 拉取失败：HTTP %d\n", code);
        http.end();
        expireStaleCacheIfNeeded();
        scheduleNextFetch(kRetrySeconds);
        gFetchRunning.store(false);
        vTaskDelete(nullptr);
        return;
    }

    JsonDocument document;
    const DeserializationError jsonError = deserializeJson(document, http.getStream());
    http.end();
    JsonObject config = document["config"].as<JsonObject>();
    if (jsonError || config.isNull() || !config["store_enabled"].is<bool>() ||
        !config["cloud_saves_enabled"].is<bool>() ||
        !config["usage_reporting_enabled"].is<bool>()) {
        Serial.println("远程配置 响应格式无效，继续使用缓存");
        scheduleNextFetch(kRetrySeconds);
        gFetchRunning.store(false);
        vTaskDelete(nullptr);
        return;
    }

    const bool storeEnabled = config["store_enabled"].as<bool>();
    const bool cloudEnabled = config["cloud_saves_enabled"].as<bool>();
    const bool usageEnabled = config["usage_reporting_enabled"].as<bool>();
    const uint32_t revision = document["revision"] | 0;
    const uint32_t ttl = constrain(document["ttl_seconds"] | kDefaultTtlSeconds, 300UL, 86400UL);
    const time_t now = time(nullptr);
    const uint64_t fetchedAt = now >= kMinimumValidEpoch ? (uint64_t)now : 0;

    gStoreEnabled.store(storeEnabled);
    gCloudSavesEnabled.store(cloudEnabled);
    gUsageReportingEnabled.store(usageEnabled);
    gRevision.store(revision);
    gFetchedAt = fetchedAt;
    if (!saveCachedConfig(storeEnabled, cloudEnabled, usageEnabled, revision, fetchedAt)) {
        Serial.println("远程配置 已应用，但 NVS 缓存保存失败");
    }
    Serial.printf("远程配置 已更新：版本=%u 商店=%s 云存档=%s 统计=%s TTL=%u秒\n",
                  (unsigned)revision, storeEnabled ? "开" : "关",
                  cloudEnabled ? "开" : "关", usageEnabled ? "开" : "关",
                  (unsigned)ttl);
    scheduleNextFetch(ttl);
    gFetchRunning.store(false);
    vTaskDelete(nullptr);
}

}  // namespace

void remoteConfigUpdate() {
    loadCachedConfig();
    if (WiFi.status() != WL_CONNECTED || gFetchRunning.load()) return;
    const uint32_t now = millis();
    const uint32_t next = gNextFetchMs.load();
    if (next && (int32_t)(now - next) < 0) return;
    bool expected = false;
    if (!gFetchRunning.compare_exchange_strong(expected, true)) return;
    if (xTaskCreate(fetchConfigTask, "remote_config", 8192, nullptr, 1, nullptr) != pdPASS) {
        gFetchRunning.store(false);
        scheduleNextFetch(kRetrySeconds);
    }
}

bool remoteConfigStoreEnabled() {
    loadCachedConfig();
    return gStoreEnabled.load();
}

bool remoteConfigCloudSavesEnabled() {
    loadCachedConfig();
    return gCloudSavesEnabled.load();
}

bool remoteConfigUsageReportingEnabled() {
    loadCachedConfig();
    return gUsageReportingEnabled.load();
}

uint32_t remoteConfigRevision() {
    loadCachedConfig();
    return gRevision.load();
}
