#include "cloud_save_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "device_auth.h"
#include "game_store.h"
#include "network_security.h"
#include "remote_config.h"
#include "rom_storage.h"
#include "service_config.h"
#include "storage.h"

static constexpr const char* kCloudBaseUrl = DIJI_CLOUD_BASE_URL;
static constexpr const char* kServiceOrigin = DIJI_SERVICE_ORIGIN;
static constexpr const char* kCloudQueuePath = "/saves/cloud-queue.txt";
static constexpr const char* kCloudQueueTempPath = "/saves/cloud-queue.tmp";
static constexpr const char* kCloudQueueBackupPath = "/saves/cloud-queue.bak";
static constexpr const char* kAllSlots = "all";
static constexpr uint32_t kCloudSyncRetryDelayMs = 30000;
static constexpr uint32_t kCloudSyncTaskStackBytes = 12288;

static SemaphoreHandle_t gCloudQueueMutex = nullptr;
static std::atomic<uint32_t> gCloudQueueRevision{0};
static std::atomic<bool> gCloudSyncRequested{false};
static std::atomic<uint32_t> gCloudSyncDueMs{0};
static std::atomic<bool> gCloudSyncTaskRunning{false};
static std::atomic<bool> gCloudSyncOperationActive{false};
static std::atomic<uint8_t> gCloudSyncPauseDepth{0};

struct PendingCloudSave {
    String romPath;
    String slot;
};

static SemaphoreHandle_t cloudQueueMutex() {
    if (!gCloudQueueMutex) gCloudQueueMutex = xSemaphoreCreateMutex();
    return gCloudQueueMutex;
}

static bool lockCloudQueue() {
    SemaphoreHandle_t mutex = cloudQueueMutex();
    return mutex && xSemaphoreTake(mutex, portMAX_DELAY) == pdTRUE;
}

static void unlockCloudQueue() {
    if (gCloudQueueMutex) xSemaphoreGive(gCloudQueueMutex);
}

static bool validCloudSlot(const String& slot) {
    return slot == "slot1" || slot == "slot2" || slot == "slot3" ||
           slot == "auto" || slot == "battery" || slot == kAllSlots;
}

static void prepareCloudDeviceAuth() {
    String authError;
    deviceAuthEnsureRegistered(authError);
}

static bool cloudCurrentRevision(const String& gameId,
                                 const char* slot, int& revision, String& error) {
    revision = 0;
    prepareCloudDeviceAuth();
    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    String url = String(kCloudBaseUrl) + "/saves/" + gameId + "/" + slot;
    if (!http.begin(client, url)) {
        error = "无法查询云存档";
        return false;
    }
    http.setTimeout(12000);
    deviceAuthAddHeaders(http);
    int code = http.GET();
    deviceAuthHandleHttpStatus(code);
    if (code == 404) {
        http.end();
        return true;
    }
    if (code != 200) {
        error = String("查询云存档失败: ") + code;
        http.end();
        return false;
    }
    JsonDocument response;
    DeserializationError jsonError = deserializeJson(response, http.getStream());
    http.end();
    if (jsonError) {
        error = "云存档元数据损坏";
        return false;
    }
    revision = response["revision"] | 0;
    return true;
}

static bool writeText(File& output, const String& text) {
    return output.write((const uint8_t*)text.c_str(), text.length()) == text.length();
}

static bool copyStorageFile(const char* path, File& output, String& error) {
    File input = dijiOpenStoragePath(path, FILE_READ);
    if (!input) {
        error = "无法读取本地存档";
        return false;
    }
    uint8_t buffer[1024];
    while (input.available()) {
        size_t count = input.read(buffer, sizeof(buffer));
        if (!count || output.write(buffer, count) != count) {
            input.close();
            error = "构建云存档上传包失败";
            return false;
        }
    }
    input.close();
    return true;
}

static bool uploadSlot(const String& gameId, const char* slot,
                       const char* savePath, const char* thumbnailPath, String& error) {
    int baseRevision = 0;
    if (!cloudCurrentRevision(gameId, slot, baseRevision, error)) return false;
    DIJI_ROMFS.mkdir("/saves");
    const char* bodyPath = "/saves/cloud-upload.tmp";
    DIJI_ROMFS.remove(bodyPath);
    File body = DIJI_ROMFS.open(bodyPath, FILE_WRITE);
    if (!body) {
        error = "云存档临时空间不足";
        return false;
    }
    String boundary = String("----GameBox") + String((uint32_t)esp_random(), HEX);
    bool ok = writeText(body, "--" + boundary + "\r\n"
                              "Content-Disposition: form-data; name=\"base_revision\"\r\n\r\n" +
                              String(baseRevision) + "\r\n") &&
              writeText(body, "--" + boundary + "\r\n"
                              "Content-Disposition: form-data; name=\"save\"; filename=\"save.bin\"\r\n"
                              "Content-Type: application/octet-stream\r\n\r\n") &&
              copyStorageFile(savePath, body, error) && writeText(body, "\r\n");
    if (ok && thumbnailPath && dijiStoragePathExists(thumbnailPath)) {
        ok = writeText(body, "--" + boundary + "\r\n"
                             "Content-Disposition: form-data; name=\"thumbnail\"; filename=\"save.thumb\"\r\n"
                             "Content-Type: application/octet-stream\r\n\r\n") &&
             copyStorageFile(thumbnailPath, body, error) && writeText(body, "\r\n");
    }
    ok = ok && writeText(body, "--" + boundary + "--\r\n");
    body.close();
    if (!ok) {
        DIJI_ROMFS.remove(bodyPath);
        return false;
    }

    prepareCloudDeviceAuth();
    body = DIJI_ROMFS.open(bodyPath, FILE_READ);
    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    String url = String(kCloudBaseUrl) + "/saves/" + gameId + "/" + slot;
    if (!body || !http.begin(client, url)) {
        if (body) body.close();
        DIJI_ROMFS.remove(bodyPath);
        error = "无法上传云存档";
        return false;
    }
    http.setTimeout(30000);
    deviceAuthAddHeaders(http);
    http.addHeader("Content-Type", String("multipart/form-data; boundary=") + boundary);
    int code = http.sendRequest("POST", &body, body.size());
    deviceAuthHandleHttpStatus(code);
    body.close();
    http.end();
    DIJI_ROMFS.remove(bodyPath);
    if (code != 201) {
        error = code == 409 ? "云端版本已变化，请重试" : String("上传云存档失败: ") + code;
        return false;
    }
    return true;
}

static bool uploadRomSlot(const GameStoreItem& game, const char* romPath,
                          const String& slot, String& error) {
    char savePath[160];
    char thumbPath[160];
    thumbPath[0] = '\0';
    bool pathOk = false;
    if (slot.startsWith("slot") && slot.length() == 5) {
        int index = slot[4] - '1';
        pathOk = index >= 0 && index < 3 &&
                 romStorageMakeSlotSavePath(romPath, index, savePath, sizeof(savePath));
        if (pathOk) romStorageMakeSlotThumbnailPath(romPath, index, thumbPath, sizeof(thumbPath));
    } else if (slot == "auto") {
        pathOk = romStorageMakeAutoSavePath(romPath, savePath, sizeof(savePath));
        if (pathOk) romStorageMakeAutoThumbnailPath(romPath, thumbPath, sizeof(thumbPath));
    } else if (slot == "battery") {
        pathOk = romStorageMakeBatterySavePath(romPath, savePath, sizeof(savePath));
    }
    if (!pathOk || !dijiStoragePathExists(savePath)) {
        error = "待同步的本地存档不存在";
        return false;
    }
    return uploadSlot(game.id, slot.c_str(), savePath,
                      thumbPath[0] ? thumbPath : nullptr, error);
}

bool cloudSaveUploadRomSaves(const char* romPath, int& uploaded, String& error) {
    uploaded = 0;
    if (!remoteConfigCloudSavesEnabled()) {
        error = "云存档已由服务端暂时关闭";
        return false;
    }
    GameStoreItem game;
    if (!gameStoreFindCachedByRomPath(romPath, game)) {
        error = "该游戏没有商店身份，无法关联云存档";
        return false;
    }
    for (int index = 0; index < 3; index++) {
        char savePath[160];
        char thumbPath[160];
        if (romStorageMakeSlotSavePath(romPath, index, savePath, sizeof(savePath)) &&
            dijiStoragePathExists(savePath)) {
            romStorageMakeSlotThumbnailPath(romPath, index, thumbPath, sizeof(thumbPath));
            String slot = String("slot") + (index + 1);
            if (!uploadSlot(game.id, slot.c_str(), savePath, thumbPath, error)) return false;
            uploaded++;
        }
    }
    char autoPath[160];
    char autoThumb[160];
    if (romStorageMakeAutoSavePath(romPath, autoPath, sizeof(autoPath)) &&
        dijiStoragePathExists(autoPath)) {
        romStorageMakeAutoThumbnailPath(romPath, autoThumb, sizeof(autoThumb));
        if (!uploadSlot(game.id, "auto", autoPath, autoThumb, error)) return false;
        uploaded++;
    }
    char batteryPath[160];
    if (romStorageMakeBatterySavePath(romPath, batteryPath, sizeof(batteryPath)) &&
        dijiStoragePathExists(batteryPath)) {
        if (!uploadSlot(game.id, "battery", batteryPath, nullptr, error)) return false;
        uploaded++;
    }
    if (uploaded == 0) error = "当前游戏没有本地存档";
    return uploaded > 0;
}

static bool cloudSlotMetadata(const String& gameId, const char* slot,
                              JsonDocument& metadata, bool& exists, String& error) {
    exists = false;
    prepareCloudDeviceAuth();
    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    String url = String(kCloudBaseUrl) + "/saves/" + gameId + "/" + slot;
    if (!http.begin(client, url)) {
        error = "无法查询云存档";
        return false;
    }
    http.setTimeout(12000);
    deviceAuthAddHeaders(http);
    int code = http.GET();
    deviceAuthHandleHttpStatus(code);
    if (code == 404) {
        http.end();
        return true;
    }
    if (code != 200) {
        error = String("查询云存档失败: ") + code;
        http.end();
        return false;
    }
    DeserializationError jsonError = deserializeJson(metadata, http.getStream());
    http.end();
    if (jsonError) {
        error = "云存档元数据损坏";
        return false;
    }
    exists = true;
    return true;
}

static bool downloadCloudContent(const String& relativeUrl,
                                 const char* targetPath, size_t expectedSize,
                                 const String& expectedSha256, String& error) {
    if (!relativeUrl.startsWith("/api/v1/cloud/") || expectedSha256.length() != 64 ||
        !targetPath || !targetPath[0]) {
        error = "云存档下载元数据无效";
        return false;
    }
    char tempPath[168];
    char backupPath[168];
    int tempLength = snprintf(tempPath, sizeof(tempPath), "%s.cloud", targetPath);
    int backupLength = snprintf(backupPath, sizeof(backupPath), "%s.bak", targetPath);
    if (tempLength <= 0 || (size_t)tempLength >= sizeof(tempPath) ||
        backupLength <= 0 || (size_t)backupLength >= sizeof(backupPath)) {
        error = "云存档路径过长";
        return false;
    }
    dijiEnsureSaveDirectory(targetPath);
    dijiRemoveStoragePath(tempPath);
    File output = dijiOpenStoragePath(tempPath, FILE_WRITE);
    if (!output) {
        error = "无法创建云存档临时文件";
        return false;
    }
    prepareCloudDeviceAuth();
    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    if (!http.begin(client, String(kServiceOrigin) + relativeUrl)) {
        output.close();
        dijiRemoveStoragePath(tempPath);
        error = "无法下载云存档";
        return false;
    }
    http.setTimeout(30000);
    deviceAuthAddHeaders(http);
    int code = http.GET();
    deviceAuthHandleHttpStatus(code);
    if (code != 200) {
        output.close();
        http.end();
        dijiRemoveStoragePath(tempPath);
        error = String("下载云存档失败: ") + code;
        return false;
    }
    WiFiClient* stream = http.getStreamPtr();
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts_ret(&sha, 0);
    uint8_t buffer[1024];
    size_t received = 0;
    uint32_t lastData = millis();
    while (http.connected() && received < expectedSize) {
        size_t available = stream->available();
        if (!available) {
            if ((uint32_t)(millis() - lastData) > 20000) break;
            delay(1);
            continue;
        }
        size_t request = available > sizeof(buffer) ? sizeof(buffer) : available;
        int count = stream->readBytes(buffer, request);
        if (count <= 0) break;
        if (output.write(buffer, count) != (size_t)count) {
            error = "写入云存档失败";
            break;
        }
        mbedtls_sha256_update_ret(&sha, buffer, count);
        received += count;
        lastData = millis();
    }
    output.close();
    http.end();
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha, digest);
    mbedtls_sha256_free(&sha);
    char actual[65];
    for (size_t i = 0; i < sizeof(digest); i++) snprintf(actual + i * 2, 3, "%02x", digest[i]);
    actual[64] = '\0';
    if (error.length() || received != expectedSize || !expectedSha256.equalsIgnoreCase(actual)) {
        if (!error.length()) error = received != expectedSize ? "云存档大小不匹配" : "云存档完整性校验失败";
        dijiRemoveStoragePath(tempPath);
        return false;
    }
    dijiRemoveStoragePath(backupPath);
    bool hadPrevious = dijiStoragePathExists(targetPath);
    if (hadPrevious && !dijiRenameStoragePath(targetPath, backupPath)) {
        dijiRemoveStoragePath(tempPath);
        error = "无法备份本地存档";
        return false;
    }
    if (!dijiRenameStoragePath(tempPath, targetPath)) {
        if (hadPrevious) dijiRenameStoragePath(backupPath, targetPath);
        dijiRemoveStoragePath(tempPath);
        error = "无法安装云存档";
        return false;
    }
    if (hadPrevious) dijiRemoveStoragePath(backupPath);
    return true;
}

bool cloudSaveRestoreRomSaves(const char* romPath, int& restored, String& error) {
    restored = 0;
    if (!remoteConfigCloudSavesEnabled()) {
        error = "云存档已由服务端暂时关闭";
        return false;
    }
    GameStoreItem game;
    if (!gameStoreFindCachedByRomPath(romPath, game)) {
        error = "该游戏没有商店身份，无法关联云存档";
        return false;
    }
    const char* slots[] = {"slot1", "slot2", "slot3", "auto", "battery"};
    for (int index = 0; index < 5; index++) {
        JsonDocument metadata;
        bool exists = false;
        if (!cloudSlotMetadata(game.id, slots[index], metadata, exists, error)) return false;
        if (!exists) continue;
        char savePath[160];
        char thumbPath[160];
        bool pathOk = index < 3 ? romStorageMakeSlotSavePath(romPath, index, savePath, sizeof(savePath)) :
                      index == 3 ? romStorageMakeAutoSavePath(romPath, savePath, sizeof(savePath)) :
                                   romStorageMakeBatterySavePath(romPath, savePath, sizeof(savePath));
        if (!pathOk || !downloadCloudContent(
                metadata["content_url"] | "", savePath,
                metadata["save_size"] | 0, metadata["save_sha256"] | "", error)) return false;
        const char* thumbnailUrl = metadata["thumbnail_content_url"] | "";
        if (index < 4 && thumbnailUrl[0]) {
            bool thumbOk = index < 3 ?
                romStorageMakeSlotThumbnailPath(romPath, index, thumbPath, sizeof(thumbPath)) :
                romStorageMakeAutoThumbnailPath(romPath, thumbPath, sizeof(thumbPath));
            if (!thumbOk || !downloadCloudContent(
                    thumbnailUrl, thumbPath, metadata["thumbnail_size"] | 0,
                    metadata["thumbnail_sha256"] | "", error)) return false;
        }
        restored++;
    }
    if (restored == 0) error = "云端没有该游戏的存档";
    return restored > 0;
}

static bool loadPendingSaves(std::vector<PendingCloudSave>& saves) {
    saves.clear();
    if (!dijiLittleFsPathExistsQuiet(kCloudQueuePath)) return true;
    File file = DIJI_ROMFS.open(kCloudQueuePath, FILE_READ);
    if (!file) return true;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;
        int separator = line.indexOf('\t');
        PendingCloudSave item;
        if (separator > 0) {
            item.slot = line.substring(0, separator);
            item.romPath = line.substring(separator + 1);
        } else {
            // Queue v1 contained only a ROM path and meant "upload every slot".
            item.slot = kAllSlots;
            item.romPath = line;
        }
        item.slot.trim();
        item.romPath.trim();
        if (!validCloudSlot(item.slot) || !item.romPath.length()) continue;
        bool duplicate = false;
        for (const PendingCloudSave& existing : saves) {
            if (existing.romPath == item.romPath && existing.slot == item.slot) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) saves.push_back(item);
    }
    file.close();
    return true;
}

static bool storePendingSaves(const std::vector<PendingCloudSave>& saves, size_t startIndex = 0) {
    DIJI_ROMFS.mkdir("/saves");
    dijiLittleFsRemoveIfPresent(kCloudQueueTempPath);
    File file = DIJI_ROMFS.open(kCloudQueueTempPath, FILE_WRITE);
    if (!file) return false;
    bool ok = true;
    for (size_t i = startIndex; ok && i < saves.size(); i++) {
        const PendingCloudSave& item = saves[i];
        ok = file.print(item.slot) == item.slot.length() && file.print('\t') == 1 &&
             file.print(item.romPath) == item.romPath.length() && file.print('\n') == 1;
    }
    file.close();
    if (!ok) {
        dijiLittleFsRemoveIfPresent(kCloudQueueTempPath);
        return false;
    }
    if (startIndex >= saves.size()) {
        dijiLittleFsRemoveIfPresent(kCloudQueuePath);
        dijiLittleFsRemoveIfPresent(kCloudQueueTempPath);
        return true;
    }
    dijiLittleFsRemoveIfPresent(kCloudQueueBackupPath);
    bool hadPrevious = dijiLittleFsPathExistsQuiet(kCloudQueuePath);
    if (hadPrevious && !DIJI_ROMFS.rename(kCloudQueuePath, kCloudQueueBackupPath)) {
        dijiLittleFsRemoveIfPresent(kCloudQueueTempPath);
        return false;
    }
    if (!DIJI_ROMFS.rename(kCloudQueueTempPath, kCloudQueuePath)) {
        if (hadPrevious) DIJI_ROMFS.rename(kCloudQueueBackupPath, kCloudQueuePath);
        dijiLittleFsRemoveIfPresent(kCloudQueueTempPath);
        return false;
    }
    if (hadPrevious) dijiLittleFsRemoveIfPresent(kCloudQueueBackupPath);
    return true;
}

bool cloudSaveMarkPending(const char* romPath, const char* slot) {
    if (!romPath || !romPath[0]) return false;
    String normalized = romPath;
    normalized.replace("\r", "");
    normalized.replace("\n", "");
    normalized.replace("\t", "");
    if (!normalized.length()) return false;
    String normalizedSlot = slot && slot[0] ? String(slot) : String(kAllSlots);
    normalizedSlot.toLowerCase();
    normalizedSlot.trim();
    if (!validCloudSlot(normalizedSlot)) return false;
    if (!lockCloudQueue()) return false;
    std::vector<PendingCloudSave> saves;
    loadPendingSaves(saves);
    for (const PendingCloudSave& item : saves) {
        if (item.romPath != normalized) continue;
        if (item.slot == kAllSlots || item.slot == normalizedSlot) {
            // Even a duplicate queue entry can represent newer local bytes
            // written while an older copy is being uploaded.
            gCloudQueueRevision.fetch_add(1);
            unlockCloudQueue();
            cloudSaveRequestBackgroundSync();
            Serial.printf("CloudSave: refreshed pending slot=%s rom=%s\n",
                          normalizedSlot.c_str(), normalized.c_str());
            return true;
        }
    }
    if (normalizedSlot == kAllSlots) {
        for (auto it = saves.begin(); it != saves.end();) {
            if (it->romPath == normalized) it = saves.erase(it);
            else ++it;
        }
    }
    saves.push_back({normalized, normalizedSlot});
    bool stored = storePendingSaves(saves);
    if (stored) gCloudQueueRevision.fetch_add(1);
    unlockCloudQueue();
    if (stored) {
        cloudSaveRequestBackgroundSync();
        Serial.printf("CloudSave: queued slot=%s rom=%s\n",
                      normalizedSlot.c_str(), normalized.c_str());
    }
    return stored;
}

bool cloudSaveHasPending() {
    if (!lockCloudQueue()) return false;
    bool pending = false;
    if (dijiLittleFsPathExistsQuiet(kCloudQueuePath)) {
        File file = DIJI_ROMFS.open(kCloudQueuePath, FILE_READ);
        pending = file && file.size() > 0;
        if (file) file.close();
    }
    unlockCloudQueue();
    return pending;
}

bool cloudSaveSyncPending(int& uploaded, String& error) {
    uploaded = 0;
    if (!remoteConfigCloudSavesEnabled()) {
        error = "云存档已由服务端暂时关闭";
        return false;
    }
    bool expectedInactive = false;
    if (!gCloudSyncOperationActive.compare_exchange_strong(expectedInactive, true)) {
        error = "云存档正在后台同步";
        return false;
    }
    error = "";
    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi 未连接";
        gCloudSyncOperationActive.store(false);
        return false;
    }
    std::vector<PendingCloudSave> saves;
    if (!lockCloudQueue()) {
        error = "无法读取云存档队列";
        gCloudSyncOperationActive.store(false);
        return false;
    }
    bool loaded = loadPendingSaves(saves);
    unlockCloudQueue();
    if (!loaded) {
        error = "无法读取云存档队列";
        gCloudSyncOperationActive.store(false);
        return false;
    }
    for (size_t index = 0; index < saves.size(); index++) {
        int count = 0;
        const PendingCloudSave& item = saves[index];
        uint32_t revisionBeforeUpload = gCloudQueueRevision.load();
        bool ok = false;
        if (item.slot == kAllSlots) {
            ok = cloudSaveUploadRomSaves(item.romPath.c_str(), count, error);
        } else {
            GameStoreItem game;
            if (!gameStoreFindCachedByRomPath(item.romPath.c_str(), game)) {
                error = "该游戏没有商店身份，无法关联云存档";
            } else {
                ok = uploadRomSlot(game, item.romPath.c_str(), item.slot, error);
                if (ok) count = 1;
            }
        }
        if (!ok) {
            gCloudSyncOperationActive.store(false);
            return false;
        }
        uploaded += count;

        if (!lockCloudQueue()) {
            error = "无法更新云存档待同步队列";
            gCloudSyncOperationActive.store(false);
            return false;
        }
        std::vector<PendingCloudSave> current;
        bool queueLoaded = loadPendingSaves(current);
        // If any save was written during this upload, leave the entry queued;
        // the next pass will upload the newer local file.
        if (queueLoaded && gCloudQueueRevision.load() == revisionBeforeUpload) {
            for (auto it = current.begin(); it != current.end(); ++it) {
                if (it->romPath == item.romPath && it->slot == item.slot) {
                    current.erase(it);
                    break;
                }
            }
            queueLoaded = storePendingSaves(current);
        }
        unlockCloudQueue();
        if (!queueLoaded) {
            error = "无法更新云存档待同步队列";
            gCloudSyncOperationActive.store(false);
            return false;
        }
    }
    gCloudSyncOperationActive.store(false);
    return true;
}

void cloudSaveRequestBackgroundSync(uint32_t delayMs) {
    gCloudSyncDueMs.store(millis() + delayMs);
    gCloudSyncRequested.store(true);
}

bool cloudSaveSyncInProgress() {
    return gCloudSyncTaskRunning.load() || gCloudSyncOperationActive.load();
}

void cloudSavePauseBackgroundSync() {
    gCloudSyncPauseDepth.fetch_add(1);
}

void cloudSaveResumeBackgroundSync() {
    uint8_t depth = gCloudSyncPauseDepth.load();
    while (depth > 0 &&
           !gCloudSyncPauseDepth.compare_exchange_weak(depth, (uint8_t)(depth - 1))) {
    }
    if (depth == 1 && cloudSaveHasPending()) {
        cloudSaveRequestBackgroundSync(500);
    }
}

static void cloudSaveBackgroundTask(void*) {
    Serial.println("CloudSave: background sync started");
    int uploaded = 0;
    String error;
    bool ok = cloudSaveSyncPending(uploaded, error);
    if (ok) {
        Serial.printf("CloudSave: background sync complete, uploaded=%d\n", uploaded);
    } else {
        Serial.printf("CloudSave: background sync failed: %s\n", error.c_str());
    }
    gCloudSyncTaskRunning.store(false);
    if (cloudSaveHasPending()) {
        uint32_t retryDelay = ok ? 1500 : kCloudSyncRetryDelayMs;
        if (!ok && error.startsWith("该游戏没有商店身份")) {
            retryDelay = 5 * 60 * 1000;
        }
        cloudSaveRequestBackgroundSync(retryDelay);
    }
    vTaskDelete(nullptr);
}

void cloudSaveUpdate() {
    if (!remoteConfigCloudSavesEnabled() || gCloudSyncPauseDepth.load() != 0 ||
        !gCloudSyncRequested.load() || gCloudSyncTaskRunning.load() ||
        WiFi.status() != WL_CONNECTED) {
        return;
    }
    uint32_t now = millis();
    if ((int32_t)(now - gCloudSyncDueMs.load()) < 0) return;

    bool expectedStopped = false;
    if (!gCloudSyncTaskRunning.compare_exchange_strong(expectedStopped, true)) return;
    gCloudSyncRequested.store(false);
    BaseType_t created = xTaskCreate(cloudSaveBackgroundTask,
                                     "cloud_save_sync",
                                     kCloudSyncTaskStackBytes,
                                     nullptr,
                                     1,
                                     nullptr);
    if (created != pdPASS) {
        gCloudSyncTaskRunning.store(false);
        Serial.println("CloudSave: failed to create background sync task");
        cloudSaveRequestBackgroundSync(5000);
    }
}
