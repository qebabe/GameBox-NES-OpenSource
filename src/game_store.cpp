#include "game_store.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "device_auth.h"
#include "network_security.h"
#include "rom_storage.h"
#include "service_config.h"
#include "storage.h"
#include "wifi_provisioning.h"

static constexpr const char* kGameStoreBaseUrl = DIJI_GAME_STORE_BASE_URL;
static constexpr uint32_t kWifiConnectTimeoutMs = 20000;
static constexpr size_t kDownloadBufferSize = 1024;
static constexpr size_t kLittleFsReserveBytes = 64 * 1024;
static constexpr const char* kStoreCacheDir = "/store";
static constexpr const char* kStoreCachePath = "/store/games.tsv";
static constexpr const char* kStoreCacheTempPath = "/store/games.tmp";
static constexpr const char* kStoreCacheBackupPath = "/store/games.bak";
static constexpr uint32_t kDownloadIdleTimeoutMs = 20000;
static constexpr int kCacheRefreshPageSizeMax = 25;

static bool ensureCacheDirectories() {
    return DIJI_ROMFS.mkdir("/rom") || dijiLittleFsPathExistsQuiet("/rom");
}

static bool ensureDownloadDirectories() {
    bool ok = ensureCacheDirectories();
    ok = (DIJI_ROMFS.mkdir("/rom/downloads") || dijiLittleFsPathExistsQuiet("/rom/downloads")) && ok;
    ok = (DIJI_ROMFS.mkdir("/covers") || dijiLittleFsPathExistsQuiet("/covers")) && ok;
    ok = (DIJI_ROMFS.mkdir("/saves") || dijiLittleFsPathExistsQuiet("/saves")) && ok;
    return ok;
}

static bool ensureStoreCacheDirectory() {
    return DIJI_ROMFS.mkdir(kStoreCacheDir) || dijiLittleFsPathExistsQuiet(kStoreCacheDir);
}

static String cleanCacheField(String value) {
    value.replace('\t', ' ');
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    return value;
}

static bool parseCacheLine(String line, GameStoreItem& item) {
    line.trim();
    int p1 = line.indexOf('\t');
    int p2 = p1 >= 0 ? line.indexOf('\t', p1 + 1) : -1;
    int p3 = p2 >= 0 ? line.indexOf('\t', p2 + 1) : -1;
    int p4 = p3 >= 0 ? line.indexOf('\t', p3 + 1) : -1;
    int p5 = p4 >= 0 ? line.indexOf('\t', p4 + 1) : -1;
    if (p1 <= 0 || p2 <= p1 || p3 <= p2 || p4 <= p3) {
        return false;
    }

    item = GameStoreItem{};
    item.id = line.substring(0, p1);
    item.size = (size_t)line.substring(p1 + 1, p2).toInt();
    item.supported = line.substring(p2 + 1, p3).toInt() != 0;
    item.filename = line.substring(p3 + 1, p4);
    item.title = p5 >= 0 ? line.substring(p4 + 1, p5) : line.substring(p4 + 1);
    item.sha256 = p5 >= 0 ? line.substring(p5 + 1) : "";
    item.downloadUrl = String(kGameStoreBaseUrl) + "/games/" + item.id + "/download";
    item.coverUrl = String(kGameStoreBaseUrl) + "/games/" + item.id + "/cover";
    return item.id.length() && item.filename.length();
}

static String makeCacheLine(const GameStoreItem& item) {
    String line;
    line.reserve(item.id.length() + item.filename.length() + item.title.length() +
                 item.sha256.length() + 32);
    line += cleanCacheField(item.id);
    line += '\t';
    line += String((unsigned)item.size);
    line += '\t';
    line += (item.supported ? '1' : '0');
    line += '\t';
    line += cleanCacheField(item.filename);
    line += '\t';
    line += cleanCacheField(item.title.length() ? item.title : item.filename);
    line += '\t';
    line += cleanCacheField(item.sha256);
    line += '\n';
    return line;
}

static bool writeCacheLine(File& file, const GameStoreItem& item) {
    if (!file) {
        return false;
    }
    String line = makeCacheLine(item);
    size_t actual = file.write((const uint8_t*)line.c_str(), line.length());
    return actual == line.length();
}

static size_t cacheLineSize(const GameStoreItem& item) {
    return makeCacheLine(item).length();
}

static size_t storeCacheFileSize(const char* path) {
    File file = DIJI_ROMFS.open(path, FILE_READ);
    if (!file) return 0;
    size_t size = file.size();
    file.close();
    return size;
}

static void logStoreFsState(const char* phase) {
    size_t total = DIJI_ROMFS.totalBytes();
    size_t used = DIJI_ROMFS.usedBytes();
    size_t freeBytes = total > used ? total - used : 0;
    Serial.printf("商店缓存 %s：文件系统总量=%u 已用=%u 剩余=%u 可用堆=%u\n",
                  phase,
                  (unsigned)total,
                  (unsigned)used,
                  (unsigned)freeBytes,
                  (unsigned)ESP.getFreeHeap());
}

static void recoverStoreCacheBackup() {
    if (!dijiLittleFsPathExistsQuiet(kStoreCachePath) &&
        dijiLittleFsPathExistsQuiet(kStoreCacheBackupPath)) {
        bool restored = DIJI_ROMFS.rename(kStoreCacheBackupPath, kStoreCachePath);
        Serial.printf("商店缓存 恢复备份：%s\n", restored ? "成功" : "失败");
    }
}

static bool hasLittleFsSpace(size_t bytesNeeded) {
    size_t total = DIJI_ROMFS.totalBytes();
    size_t used = DIJI_ROMFS.usedBytes();
    if (total <= used) {
        return false;
    }
    size_t freeBytes = total - used;
    return freeBytes > bytesNeeded + kLittleFsReserveBytes;
}

bool gameStoreConnectSavedWifi(String& ssid, String& error) {
    error = "";
    char ssidBuffer[64];
    char passBuffer[96];
    const uint32_t operationStartedMs = millis();
    Serial.printf("商店联网 准备连接：WiFi状态码=%d 已保存网络=%d 运行时间=%u毫秒 可用堆=%u\n",
                  (int)WiFi.status(), wifiProvisioningSavedNetworkCount(),
                  (unsigned)operationStartedMs, (unsigned)ESP.getFreeHeap());
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    delay(100);
    const uint32_t credentialStartedMs = millis();
    bool matchedVisibleNetwork =
        wifiProvisioningGetMatchingSavedCredentials(ssidBuffer, sizeof(ssidBuffer),
                                                    passBuffer, sizeof(passBuffer));
    Serial.printf("商店联网 选择凭据：匹配到可见网络=%d 耗时=%u毫秒 WiFi状态码=%d\n",
                  matchedVisibleNetwork ? 1 : 0,
                  (unsigned)(millis() - credentialStartedMs), (int)WiFi.status());
    if (!matchedVisibleNetwork) {
        // If the AP is temporarily absent, retain one saved station profile so
        // the WiFi driver can keep retrying after this foreground attempt ends.
        if (!wifiProvisioningGetSavedCredentials(ssidBuffer, sizeof(ssidBuffer),
                                                 passBuffer, sizeof(passBuffer))) {
            error = "未保存 WiFi，请先配网";
            Serial.printf("商店联网 没有可用凭据：耗时=%u毫秒\n",
                          (unsigned)(millis() - operationStartedMs));
            return false;
        }
    }

    ssid = ssidBuffer;
    Serial.printf("商店联网 开始连接：SSID=%s 凭据来源=%s 初始状态码=%d\n",
                  ssidBuffer, matchedVisibleNetwork ? "扫描匹配" : "已保存备用项",
                  (int)WiFi.status());
    WiFi.begin(ssidBuffer, passBuffer);

    uint32_t start = millis();
    uint32_t lastProgressMs = 0;
    while (WiFi.status() != WL_CONNECTED) {
        const uint32_t elapsedMs = millis() - start;
        if (elapsedMs - lastProgressMs >= 2000) {
            lastProgressMs = elapsedMs;
            Serial.printf("商店联网 等待连接：已等待=%u毫秒 WiFi状态码=%d 可用堆=%u\n",
                          (unsigned)elapsedMs, (int)WiFi.status(),
                          (unsigned)ESP.getFreeHeap());
        }
        if (elapsedMs > kWifiConnectTimeoutMs) {
            error = "WiFi 暂时不可用，将后台重连";
            Serial.printf("商店联网 连接超时：SSID=%s WiFi状态码=%d 已等待=%u毫秒\n",
                          ssidBuffer, (int)WiFi.status(), (unsigned)elapsedMs);
            WiFi.disconnect(false, false);
            return false;
        }
        delay(100);
    }
    String ip = WiFi.localIP().toString();
    Serial.printf("商店联网 连接成功：SSID=%s 信道=%d 信号=%d IP=%s "
                  "连接耗时=%u毫秒 总耗时=%u毫秒\n",
                  WiFi.SSID().c_str(), WiFi.channel(), WiFi.RSSI(), ip.c_str(),
                  (unsigned)(millis() - start),
                  (unsigned)(millis() - operationStartedMs));
    const bool timeReady = ensureGameBoxNetworkTime(error);
    Serial.printf("商店联网 时间检查：可用=%d 时间戳=%lld 总耗时=%u毫秒 错误=%s\n",
                  timeReady ? 1 : 0, (long long)time(nullptr),
                  (unsigned)(millis() - operationStartedMs), error.c_str());
    return timeReady;
}

void gameStoreDisconnectWifi() {
    WiFi.disconnect(false, false);
    WiFi.mode(WIFI_STA);
}

bool gameStoreFetchPage(int page, int pageSize, std::vector<GameStoreItem>& items,
                        int& total, String& error) {
    error = "";
    items.clear();
    total = 0;

    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi 未连接";
        return false;
    }

    String authError;
    deviceAuthEnsureRegistered(authError);

    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    String url = String(kGameStoreBaseUrl) + "/games?supported=true&page=" +
                 String(page) + "&page_size=" + String(pageSize);
    Serial.printf("商店接口 请求列表：页码=%d 每页=%d 可用堆=%u\n",
                  page, pageSize, (unsigned)ESP.getFreeHeap());
    if (!http.begin(client, url)) {
        error = "无法打开商店地址";
        Serial.printf("商店接口 创建请求失败：地址=%s\n", url.c_str());
        return false;
    }
    http.setTimeout(12000);
    deviceAuthAddHeaders(http);

    int code = http.GET();
    deviceAuthHandleHttpStatus(code);
    if (code != HTTP_CODE_OK) {
        error = String("列表请求失败: ") + code;
        Serial.printf("商店接口 列表请求失败：页码=%d HTTP状态码=%d\n", page, code);
        http.end();
        return false;
    }

    JsonDocument doc;
    DeserializationError jsonError = deserializeJson(doc, http.getStream());
    http.end();
    if (jsonError) {
        error = "列表解析失败";
        Serial.printf("商店接口 JSON解析失败：页码=%d 原因=%s\n",
                      page, jsonError.c_str());
        return false;
    }

    total = doc["total"] | 0;
    JsonArray array = doc["items"].as<JsonArray>();
    for (JsonObject obj : array) {
        GameStoreItem item;
        item.id = obj["id"] | "";
        item.title = obj["title"] | "";
        item.filename = obj["filename"] | "";
        item.downloadUrl = obj["download_url"] | "";
        item.coverUrl = obj["cover_url"] | "";
        item.sha256 = obj["sha256"] | "";
        item.size = obj["size"] | 0;
        item.supported = obj["supported"] | false;
        if (item.id.length() && item.filename.length() && item.downloadUrl.length()) {
            items.push_back(item);
        }
    }

    Serial.printf("商店接口 列表请求成功：页码=%d 本页数量=%u 总数量=%d 可用堆=%u\n",
                  page, (unsigned)items.size(), total, (unsigned)ESP.getFreeHeap());

    return true;
}

bool gameStoreHasCachedIndex() {
    recoverStoreCacheBackup();
    return dijiLittleFsPathExistsQuiet(kStoreCachePath);
}

bool gameStoreFindCachedByFilename(const char* filename, GameStoreItem& item) {
    if (!filename || !filename[0]) return false;
    File file = DIJI_ROMFS.open(kStoreCachePath, FILE_READ);
    if (!file) return false;
    while (file.available()) {
        GameStoreItem candidate;
        if (parseCacheLine(file.readStringUntil('\n'), candidate) &&
            candidate.filename.equalsIgnoreCase(filename)) {
            item = candidate;
            file.close();
            return true;
        }
    }
    file.close();
    return false;
}

bool gameStoreFindCachedByRomPath(const char* romPath, GameStoreItem& item) {
    if (!romPath || !romPath[0]) return false;
    const char* localPath = romStorageLocalPath(romPath);
    const char* slash = strrchr(localPath, '/');
    const char* localName = slash ? slash + 1 : localPath;
    if (gameStoreFindCachedByFilename(localName, item)) return true;

    File file = DIJI_ROMFS.open(kStoreCachePath, FILE_READ);
    if (!file) return false;
    while (file.available()) {
        GameStoreItem candidate;
        char expectedPath[128];
        if (parseCacheLine(file.readStringUntil('\n'), candidate) &&
            romStorageMakeDownloadRomPath(candidate.filename.c_str(), expectedPath,
                                          sizeof(expectedPath)) &&
            strcmp(localPath, expectedPath) == 0) {
            item = candidate;
            file.close();
            return true;
        }
    }
    file.close();
    return false;
}

bool gameStoreLoadCachedPage(int page, int pageSize, std::vector<GameStoreItem>& items,
                             int& total, String& error) {
    items.clear();
    total = 0;
    if (page < 1) {
        page = 1;
    }
    if (pageSize < 1) {
        pageSize = 1;
    }

    File file = DIJI_ROMFS.open(kStoreCachePath, FILE_READ);
    if (!file) {
        error = "商店列表未缓存";
        return false;
    }

    int start = (page - 1) * pageSize;
    int end = start + pageSize;
    int index = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        GameStoreItem item;
        if (!parseCacheLine(line, item)) {
            continue;
        }
        if (index >= start && index < end) {
            items.push_back(item);
        }
        index++;
    }
    file.close();

    total = index;
    if (total == 0) {
        error = "商店列表为空";
        return false;
    }
    return true;
}

bool gameStoreLoadCachedBatch(size_t offset, int limit,
                              std::vector<GameStoreItem>& items,
                              size_t& nextOffset, bool& hasMore,
                              String& error) {
    items.clear();
    nextOffset = offset;
    hasMore = false;
    error = "";
    if (limit < 1) limit = 1;

    File file = DIJI_ROMFS.open(kStoreCachePath, FILE_READ);
    if (!file) {
        error = "商店列表未缓存";
        return false;
    }
    if (offset > file.size() || !file.seek(offset)) {
        file.close();
        error = "商店缓存位置无效";
        return false;
    }

    while (file.available() && (int)items.size() < limit) {
        GameStoreItem item;
        if (parseCacheLine(file.readStringUntil('\n'), item)) {
            items.push_back(item);
        }
    }
    nextOffset = file.position();
    hasMore = file.available();
    file.close();
    if (items.empty() && offset == 0) {
        error = "商店列表为空";
        return false;
    }
    return !items.empty() || offset > 0;
}

bool gameStoreLoadCachedWindow(int centerPage, int pageSize, int radius,
                               std::vector<GameStoreItem>& items,
                               int& startPage, int& total, String& error) {
    items.clear();
    total = 0;
    if (centerPage < 1) {
        centerPage = 1;
    }
    if (pageSize < 1) {
        pageSize = 1;
    }
    if (radius < 0) {
        radius = 0;
    }

    startPage = centerPage > radius ? centerPage - radius : 1;
    int endPage = centerPage + radius;
    int startIndex = (startPage - 1) * pageSize;
    int endIndex = endPage * pageSize;

    File file = DIJI_ROMFS.open(kStoreCachePath, FILE_READ);
    if (!file) {
        error = "商店列表未缓存";
        return false;
    }

    int index = 0;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        GameStoreItem item;
        if (!parseCacheLine(line, item)) {
            continue;
        }
        if (index >= startIndex && index < endIndex) {
            items.push_back(item);
        }
        index++;
    }
    file.close();

    total = index;
    if (total == 0) {
        error = "商店列表为空";
        return false;
    }
    return true;
}

bool gameStoreMergeCachedItems(const std::vector<GameStoreItem>& items, String& error) {
    error = "";
    if (items.empty()) return true;
    if (!ensureStoreCacheDirectory()) {
        error = "无法创建缓存目录";
        return false;
    }

    recoverStoreCacheBackup();
    dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
    bool hadCurrent = dijiLittleFsPathExistsQuiet(kStoreCachePath);
    File output = DIJI_ROMFS.open(kStoreCacheTempPath, FILE_WRITE);
    if (!output) {
        error = "无法打开列表缓存";
        return false;
    }

    std::vector<uint8_t> merged(items.size(), 0);
    bool writeOk = true;
    File current;
    if (hadCurrent) current = DIJI_ROMFS.open(kStoreCachePath, FILE_READ);
    if (hadCurrent && !current) {
        output.close();
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "无法读取旧列表缓存";
        return false;
    }
    while (current && current.available() && writeOk) {
        GameStoreItem cached;
        if (!parseCacheLine(current.readStringUntil('\n'), cached)) continue;

        const GameStoreItem* value = &cached;
        for (size_t i = 0; i < items.size(); i++) {
            if (!merged[i] && items[i].id == cached.id) {
                value = &items[i];
                merged[i] = 1;
                break;
            }
        }
        writeOk = writeCacheLine(output, *value);
    }
    if (current) current.close();
    for (size_t i = 0; i < items.size() && writeOk; i++) {
        if (!merged[i]) writeOk = writeCacheLine(output, items[i]);
    }
    output.flush();
    output.close();

    if (!writeOk) {
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "写入列表缓存失败";
        return false;
    }

    dijiLittleFsRemoveIfPresent(kStoreCacheBackupPath);
    if (hadCurrent && !DIJI_ROMFS.rename(kStoreCachePath, kStoreCacheBackupPath)) {
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "无法备份旧列表缓存";
        return false;
    }
    if (!DIJI_ROMFS.rename(kStoreCacheTempPath, kStoreCachePath)) {
        if (hadCurrent) DIJI_ROMFS.rename(kStoreCacheBackupPath, kStoreCachePath);
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "无法保存列表缓存";
        return false;
    }
    if (hadCurrent) dijiLittleFsRemoveIfPresent(kStoreCacheBackupPath);
    return true;
}

bool gameStoreRefreshCache(int pageSize, int& total, String& error,
                           GameStoreProgressCallback progress, void* userData) {
    total = 0;
    error = "";
    if (pageSize > kCacheRefreshPageSizeMax) {
        Serial.printf("商店缓存 自动降低分页大小：请求值=%d 实际值=%d\n",
                      pageSize, kCacheRefreshPageSizeMax);
        pageSize = kCacheRefreshPageSizeMax;
    }
    if (pageSize < 1) pageSize = 1;
    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi 未连接";
        return false;
    }
    if (!ensureStoreCacheDirectory()) {
        error = "无法创建缓存目录";
        return false;
    }

    recoverStoreCacheBackup();
    dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
    if (dijiLittleFsPathExistsQuiet(kStoreCachePath)) {
        dijiLittleFsRemoveIfPresent(kStoreCacheBackupPath);
    }
    logStoreFsState("开始");
    Serial.printf("商店缓存 开始更新：SSID=%s 信号=%d 每页=%d 旧缓存大小=%u\n",
                  WiFi.SSID().c_str(), WiFi.RSSI(), pageSize,
                  (unsigned)storeCacheFileSize(kStoreCachePath));

    int page = 1;
    int expectedTotal = 0;
    int written = 0;
    size_t expectedWrittenBytes = 0;
    bool cachePrepared = false;
    while (true) {
        std::vector<GameStoreItem> pageItems;
        String fetchError;
        if (!gameStoreFetchPage(page, pageSize, pageItems, expectedTotal, fetchError)) {
            dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
            error = fetchError;
            Serial.printf("商店缓存 获取页面失败：页码=%d 已写入=%d 原因=%s\n",
                          page, written, fetchError.c_str());
            return false;
        }
        Serial.printf("商店缓存 处理页面：页码=%d 本页数量=%u 预计总数=%d 已写入=%d\n",
                      page, (unsigned)pageItems.size(), expectedTotal, written);
        if (pageItems.empty()) {
            break;
        }

        if (!cachePrepared) {
            size_t sampleBytes = 0;
            for (const GameStoreItem& item : pageItems) sampleBytes += cacheLineSize(item);
            size_t estimatedBytes = sampleBytes;
            if (!pageItems.empty() && expectedTotal > (int)pageItems.size()) {
                estimatedBytes = (sampleBytes * (size_t)expectedTotal + pageItems.size() - 1) /
                                 pageItems.size();
            }
            // Leave room for title-length variance and LittleFS metadata.
            size_t requiredBytes = estimatedBytes + estimatedBytes / 4 + 16 * 1024;
            size_t fsTotal = DIJI_ROMFS.totalBytes();
            size_t fsUsed = DIJI_ROMFS.usedBytes();
            size_t freeBytes = fsTotal > fsUsed ? fsTotal - fsUsed : 0;
            size_t oldCacheBytes = storeCacheFileSize(kStoreCachePath);
            Serial.printf("商店缓存 空间估算：样本大小=%u 预计大小=%u 所需空间=%u 剩余空间=%u 旧缓存=%u\n",
                          (unsigned)sampleBytes,
                          (unsigned)estimatedBytes,
                          (unsigned)requiredBytes,
                          (unsigned)freeBytes,
                          (unsigned)oldCacheBytes);

            if (freeBytes < requiredBytes) {
                if (oldCacheBytes > 0 && freeBytes + oldCacheBytes >= requiredBytes) {
                    if (!dijiLittleFsRemoveIfPresent(kStoreCachePath)) {
                        error = "无法释放旧列表缓存";
                        Serial.println("商店缓存 回收失败：无法删除旧缓存");
                        return false;
                    }
                    Serial.printf("商店缓存 已回收旧索引：%u 字节\n",
                                  (unsigned)oldCacheBytes);
                    logStoreFsState("回收旧缓存后");
                } else {
                    error = "列表缓存空间不足";
                    Serial.printf("商店缓存 空间不足：需要=%u 包含旧缓存可用=%u\n",
                                  (unsigned)requiredBytes,
                                  (unsigned)(freeBytes + oldCacheBytes));
                    return false;
                }
            }

            cachePrepared = true;
        }

        // Do not keep a LittleFS handle alive across the next TLS request.
        // Keeping both the 100-item JSON/vector and the file object alive left
        // only ~11 KB heap and invalidated the file handle on page two.
        File file = DIJI_ROMFS.open(kStoreCacheTempPath,
                                    written == 0 ? FILE_WRITE : FILE_APPEND);
        if (!file) {
            error = "无法打开列表缓存";
            Serial.printf("商店缓存 打开文件失败：模式=%s 页码=%d 已写入=%d\n",
                          written == 0 ? "write" : "append", page, written);
            logStoreFsState("打开失败");
            return false;
        }
        for (const GameStoreItem& item : pageItems) {
            size_t before = file.position();
            size_t expectedLineBytes = cacheLineSize(item);
            if (!writeCacheLine(file, item)) {
                size_t after = file.position();
                file.close();
                dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
                error = "写入列表缓存失败";
                Serial.printf("商店缓存 写入失败：页码=%d 条目=%d ID=%s 写入前=%u 写入后=%u 行大小=%u\n",
                              page, written, item.id.c_str(),
                              (unsigned)before,
                              (unsigned)after,
                              (unsigned)expectedLineBytes);
                logStoreFsState("写入失败");
                return false;
            }
            written++;
            expectedWrittenBytes += expectedLineBytes;
        }
        file.flush();
        file.close();
        if (progress) {
            progress((size_t)written, (size_t)expectedTotal, userData);
        }
        if (expectedTotal > 0 && written >= expectedTotal) {
            break;
        }
        page++;
        delay(5);
    }

    if (written <= 0) {
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "在线列表为空";
        return false;
    }
    size_t temporaryBytes = storeCacheFileSize(kStoreCacheTempPath);
    if (temporaryBytes != expectedWrittenBytes) {
        error = "列表缓存大小校验失败";
        Serial.printf("商店缓存 文件大小不一致：预计=%u 实际=%u 条目数=%d\n",
                      (unsigned)expectedWrittenBytes,
                      (unsigned)temporaryBytes,
                      written);
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        logStoreFsState("文件大小不一致");
        return false;
    }

    dijiLittleFsRemoveIfPresent(kStoreCacheBackupPath);
    bool hadPreviousCache = dijiLittleFsPathExistsQuiet(kStoreCachePath);
    if (hadPreviousCache &&
        !DIJI_ROMFS.rename(kStoreCachePath, kStoreCacheBackupPath)) {
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "无法备份旧列表缓存";
        Serial.println("商店缓存 替换失败：无法将当前缓存改名为备份");
        logStoreFsState("备份失败");
        return false;
    }
    if (!DIJI_ROMFS.rename(kStoreCacheTempPath, kStoreCachePath)) {
        if (hadPreviousCache) {
            DIJI_ROMFS.rename(kStoreCacheBackupPath, kStoreCachePath);
        }
        dijiLittleFsRemoveIfPresent(kStoreCacheTempPath);
        error = "无法保存列表缓存";
        Serial.println("商店缓存 替换失败：无法安装临时缓存");
        logStoreFsState("安装临时缓存失败");
        return false;
    }
    if (hadPreviousCache) {
        dijiLittleFsRemoveIfPresent(kStoreCacheBackupPath);
    }
    total = written;
    Serial.printf("商店缓存 更新完成：条目数=%d 文件大小=%u\n",
                  written, (unsigned)storeCacheFileSize(kStoreCachePath));
    logStoreFsState("完成");
    return true;
}

bool gameStoreDownloadFile(const String& url, const char* localPath, size_t expectedSize,
                           const String& expectedSha256,
                           GameStoreProgressCallback progress, void* userData,
                           String& error) {
    error = "";
    if (!localPath || localPath[0] == '\0') {
        error = "本地路径无效";
        return false;
    }
    if (!url.startsWith("https://")) {
        error = "拒绝非 HTTPS 下载地址";
        return false;
    }
    if (WiFi.status() != WL_CONNECTED) {
        error = "WiFi 未连接";
        return false;
    }
    if (!ensureDownloadDirectories()) {
        error = "无法创建下载目录";
        return false;
    }
    if (expectedSize > 0 && !hasLittleFsSpace(expectedSize)) {
        error = "存储空间不足";
        return false;
    }

    WiFiClientSecure client;
    configureGameBoxTls(client);
    HTTPClient http;
    if (!http.begin(client, url)) {
        error = "无法打开下载地址";
        return false;
    }
    http.setTimeout(20000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        error = String("下载请求失败: ") + code;
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (expectedSize > 0 && contentLength > 0 && (size_t)contentLength != expectedSize) {
        error = "服务器文件大小不匹配";
        http.end();
        return false;
    }
    size_t total = expectedSize > 0 ? expectedSize : (contentLength > 0 ? (size_t)contentLength : 0);
    if (total > 0 && !hasLittleFsSpace(total)) {
        error = "存储空间不足";
        http.end();
        return false;
    }

    char tempPath[160];
    int tempLen = snprintf(tempPath, sizeof(tempPath), "%s.part", localPath);
    if (tempLen <= 0 || (size_t)tempLen >= sizeof(tempPath)) {
        error = "下载路径过长";
        http.end();
        return false;
    }
    dijiLittleFsRemoveIfPresent(tempPath);
    File file = DIJI_ROMFS.open(tempPath, FILE_WRITE);
    if (!file) {
        error = "无法创建文件";
        http.end();
        return false;
    }

    uint8_t buffer[kDownloadBufferSize];
    mbedtls_sha256_context shaContext;
    mbedtls_sha256_init(&shaContext);
    mbedtls_sha256_starts_ret(&shaContext, 0);
    WiFiClient* stream = http.getStreamPtr();
    size_t written = 0;
    uint32_t lastProgressMs = 0;
    uint32_t lastDataMs = millis();
    while (http.connected() && (contentLength < 0 || written < (size_t)contentLength)) {
        size_t available = stream->available();
        if (!available) {
            if ((uint32_t)(millis() - lastDataMs) > kDownloadIdleTimeoutMs) {
                error = "下载等待超时";
                break;
            }
            delay(1);
            continue;
        }
        int readLen = stream->readBytes(buffer, available > sizeof(buffer) ? sizeof(buffer) : available);
        if (readLen <= 0) {
            break;
        }
        if (file.write(buffer, readLen) != (size_t)readLen) {
            error = "写入文件失败";
            file.close();
            http.end();
            dijiLittleFsRemoveIfPresent(tempPath);
            mbedtls_sha256_free(&shaContext);
            return false;
        }
        mbedtls_sha256_update_ret(&shaContext, buffer, (size_t)readLen);
        written += readLen;
        lastDataMs = millis();
        uint32_t now = millis();
        if (progress && ((uint32_t)(now - lastProgressMs) > 250 || (total > 0 && written >= total))) {
            lastProgressMs = now;
            progress(written, total, userData);
        }
    }

    file.close();
    http.end();

    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&shaContext, digest);
    mbedtls_sha256_free(&shaContext);

    if (error.length() || written == 0 || (total > 0 && written != total)) {
        if (!error.length() && written == 0) error = "下载内容为空";
        if (!error.length()) error = "下载大小不匹配";
        dijiLittleFsRemoveIfPresent(tempPath);
        return false;
    }

    if (expectedSha256.length()) {
        if (expectedSha256.length() != 64) {
            error = "服务器哈希格式无效";
            dijiLittleFsRemoveIfPresent(tempPath);
            return false;
        }
        char actualSha256[65];
        for (size_t i = 0; i < sizeof(digest); i++) {
            snprintf(actualSha256 + i * 2, 3, "%02x", digest[i]);
        }
        actualSha256[64] = '\0';
        if (!expectedSha256.equalsIgnoreCase(actualSha256)) {
            error = "文件完整性校验失败";
            dijiLittleFsRemoveIfPresent(tempPath);
            return false;
        }
    }

    if (String(localPath).endsWith(".nes")) {
        File rom = DIJI_ROMFS.open(tempPath, FILE_READ);
        uint8_t header[4] = {0};
        bool validHeader = rom && rom.read(header, sizeof(header)) == sizeof(header) &&
                           header[0] == 'N' && header[1] == 'E' &&
                           header[2] == 'S' && header[3] == 0x1A;
        if (rom) rom.close();
        if (!validHeader) {
            error = "下载文件不是有效 NES ROM";
            dijiLittleFsRemoveIfPresent(tempPath);
            return false;
        }
    }

    dijiLittleFsRemoveIfPresent(localPath);
    if (!DIJI_ROMFS.rename(tempPath, localPath)) {
        error = "无法保存下载文件";
        dijiLittleFsRemoveIfPresent(tempPath);
        return false;
    }
    if (progress) {
        progress(written, total, userData);
    }
    return true;
}

bool gameStoreHasLocalRom(const char* filename) {
    char path[128];
    if (!romStorageMakeDownloadRomPath(filename, path, sizeof(path))) {
        return false;
    }
    return dijiLittleFsPathExistsQuiet(path);
}
