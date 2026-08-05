#pragma once

#include <Arduino.h>
#include <vector>

struct GameStoreItem {
    String id;
    String title;
    String filename;
    String downloadUrl;
    String coverUrl;
    String sha256;
    size_t size = 0;
    bool supported = false;
};

typedef void (*GameStoreProgressCallback)(size_t downloaded, size_t total, void* userData);

bool gameStoreConnectSavedWifi(String& ssid, String& error);
void gameStoreDisconnectWifi();
bool gameStoreFetchPage(int page, int pageSize, std::vector<GameStoreItem>& items,
                        int& total, String& error);
bool gameStoreHasCachedIndex();
bool gameStoreLoadCachedPage(int page, int pageSize, std::vector<GameStoreItem>& items,
                             int& total, String& error);
bool gameStoreLoadCachedBatch(size_t offset, int limit,
                              std::vector<GameStoreItem>& items,
                              size_t& nextOffset, bool& hasMore,
                              String& error);
bool gameStoreLoadCachedWindow(int centerPage, int pageSize, int radius,
                               std::vector<GameStoreItem>& items,
                               int& startPage, int& total, String& error);
bool gameStoreMergeCachedItems(const std::vector<GameStoreItem>& items, String& error);
bool gameStoreRefreshCache(int pageSize, int& total, String& error,
                           GameStoreProgressCallback progress, void* userData);
bool gameStoreDownloadFile(const String& url, const char* localPath, size_t expectedSize,
                           const String& expectedSha256,
                           GameStoreProgressCallback progress, void* userData,
                           String& error);
bool gameStoreHasLocalRom(const char* filename);
bool gameStoreFindCachedByFilename(const char* filename, GameStoreItem& item);
bool gameStoreFindCachedByRomPath(const char* romPath, GameStoreItem& item);
