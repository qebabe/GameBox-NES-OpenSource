static String formatStoreSize(size_t size) {
    if (size >= 1024 * 1024) {
        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u.%uM",
                 (unsigned)(size / (1024 * 1024)),
                 (unsigned)((size % (1024 * 1024)) * 10 / (1024 * 1024)));
        return String(buffer);
    }
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%uK", (unsigned)((size + 1023) / 1024));
    return String(buffer);
}

static void drawStoreSoftbar() {
    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);

    const char* labels[] = {"更新", "下载", "返回", "顶部", "更多"};
    const int labelX[] = {18, 18, 18, 18, 18};
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setTextSize(1);
    tft.setFont(&fonts::efontCN_14);
    for (int i = 0; i < 5; i++) {
        int buttonX = i * MENU_SOFTBAR_ITEM_WIDTH;
        bool focused = storeScreen.softbarFocused &&
                       storeScreen.softbarIndex == i;
        if (focused) {
            tft.fillRect(buttonX + 2, MENU_SOFTBAR_BUTTON_Y,
                         MENU_SOFTBAR_ITEM_WIDTH - 4, MENU_SOFTBAR_BUTTON_H,
                         MENU_HIGHLIGHT_BG);
        }
        tft.drawRect(buttonX + 2, MENU_SOFTBAR_BUTTON_Y,
                     MENU_SOFTBAR_ITEM_WIDTH - 4, MENU_SOFTBAR_BUTTON_H,
                     focused ? MENU_TITLE_COLOR :
                     (i == 1 ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR));
        tft.setTextColor(focused ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                         focused ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
        tft.setCursor(buttonX + labelX[i], MENU_SOFTBAR_Y + 9);
        tft.print(labels[i]);
    }
    tft.setFont(&fonts::Font0);
}

static int storeMaxScrollY() {
    int contentHeight = (int)storeItems.size() * STORE_ROW_HEIGHT;
    return contentHeight > STORE_LIST_HEIGHT ? contentHeight - STORE_LIST_HEIGHT : 0;
}

template <typename Surface>
static void renderStoreList(Surface& surface, int scrollY, int leftX, int topY) {
    surface.fillRect(leftX, topY, STORE_LIST_WIDTH, STORE_LIST_HEIGHT, MENU_BG_COLOR);
    surface.setClipRect(leftX, topY, STORE_LIST_WIDTH, STORE_LIST_HEIGHT);
    for (int i = 0; i < (int)storeItems.size(); i++) {
        int y = topY + i * STORE_ROW_HEIGHT - scrollY;
        if (y + STORE_ROW_HEIGHT <= topY || y >= topY + STORE_LIST_HEIGHT) continue;
        const GameStoreItem& item = storeItems[i];
        bool selected = i == storeScreen.selectedIndex;
        bool downloaded = i < (int)storeDownloadedCache.size() && storeDownloadedCache[i];
        uint16_t rowColor = selected ? MENU_HIGHLIGHT_BG : MENU_ROW_COLOR;
        surface.fillRoundRect(leftX + 2, y + 1, STORE_LIST_WIDTH - 7,
                              STORE_ROW_HEIGHT - 3, 6, rowColor);
        surface.drawFastVLine(leftX + 2, y + 4, STORE_ROW_HEIGHT - 9,
                              selected ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR);
        surface.setFont(&fonts::efontCN_14);
        surface.setTextSize(1);
        surface.setTextColor(selected ? MENU_TITLE_COLOR : MENU_TEXT_COLOR, rowColor);
        String title = item.title.length() ? item.title : item.filename;
        while (title.length() > 1 && surface.textWidth(title) > 202) {
            removeLastUtf8Codepoint(title);
        }
        surface.setCursor(leftX + 14, y + 4);
        surface.print(title);
        surface.setTextColor(downloaded ? 0x07E0 : MENU_HINT_COLOR, rowColor);
        String sizeLabel = downloaded ? "已下载" : formatStoreSize(item.size);
        surface.setCursor(leftX + 226, y + 4);
        surface.print(sizeLabel);
        surface.setTextColor(MENU_HINT_COLOR, rowColor);
        surface.setCursor(leftX + 14, y + 20);
        surface.print(item.supported ? "Mapper 支持" : "Mapper 未知");
    }
    surface.clearClipRect();

    int maxScrollY = storeMaxScrollY();
    if (maxScrollY > 0) {
        constexpr int trackH = STORE_LIST_HEIGHT - 8;
        int thumbH = trackH * STORE_LIST_HEIGHT /
                     ((int)storeItems.size() * STORE_ROW_HEIGHT);
        if (thumbH < 18) thumbH = 18;
        int bounded = scrollY;
        if (bounded < 0) bounded = 0;
        if (bounded > maxScrollY) bounded = maxScrollY;
        int thumbY = topY + 4 + (trackH - thumbH) * bounded / maxScrollY;
        surface.fillRoundRect(leftX + STORE_LIST_WIDTH - 3, topY + 4, 3, trackH, 2, MENU_BORDER_COLOR);
        surface.fillRoundRect(leftX + STORE_LIST_WIDTH - 3, thumbY, 3, thumbH, 2, MENU_ACCENT_COLOR);
    }
    surface.setFont(&fonts::Font0);
}

static void drawStoreList() {
    int scrollY = (int)roundf(storeScreen.scrollPosition);
    if (!storeScreen.listCanvasReady) {
        storeScreen.listCanvas.setColorDepth(16);
        storeScreen.listCanvas.setPsram(true);
        storeScreen.listCanvasReady =
            storeScreen.listCanvas.createSprite(STORE_LIST_WIDTH, STORE_LIST_HEIGHT) != nullptr;
    }
    if (storeScreen.listCanvasReady) {
        renderStoreList(storeScreen.listCanvas, scrollY, 0, 0);
        storeScreen.listCanvas.pushSprite(STORE_LIST_X, STORE_LIST_Y);
    } else {
        tft.startWrite();
        renderStoreList(tft, scrollY, STORE_LIST_X, STORE_LIST_Y);
        tft.endWrite();
    }
}

static void drawStoreScreen(const String& status) {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("游戏商店");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    if (status.length()) {
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        drawClippedText(status, 16, 40, 288);
    }

    if (storeItems.empty()) {
        tft.fillRect(STORE_LIST_X, STORE_LIST_Y, STORE_LIST_WIDTH, STORE_LIST_HEIGHT, MENU_BG_COLOR);
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        tft.setCursor(92, 118);
        tft.print("没有在线游戏");
    } else {
        drawStoreList();
    }

    drawStoreSoftbar();
    tft.setFont(&fonts::Font0);
}

static bool storeAppendCachedBatch(int limit, bool showError) {
    String error;
    std::vector<GameStoreItem> batch;
    size_t nextOffset = storeScreen.cacheNextOffset;
    bool hasMore = false;
    if (!gameStoreLoadCachedBatch(storeScreen.cacheNextOffset, limit, batch,
                                  nextOffset, hasMore, error)) {
        if (showError) {
            drawStoreScreen(error);
            delay(1200);
        }
        return false;
    }
    storeScreen.cacheNextOffset = nextOffset;
    storeScreen.cacheHasMore = hasMore;
    for (const GameStoreItem& item : batch) {
        storeItems.push_back(item);
        storeDownloadedCache.push_back(
            gameStoreHasLocalRom(item.filename.c_str()) ? 1 : 0);
    }
    return true;
}

static bool storeLoadInitialCached(bool showError, int desiredCount = STORE_LAZY_BATCH_SIZE) {
    storeItems.clear();
    storeDownloadedCache.clear();
    storeScreen.cacheNextOffset = 0;
    storeScreen.cacheHasMore = false;
    if (!storeAppendCachedBatch(desiredCount, showError)) return false;
    if (storeScreen.selectedIndex >= (int)storeItems.size()) {
        storeScreen.selectedIndex = (int)storeItems.size() - 1;
    }
    if (storeScreen.selectedIndex < 0) storeScreen.selectedIndex = 0;
    int maxScrollY = storeMaxScrollY();
    if (storeScreen.scrollPosition < 0.0f) storeScreen.scrollPosition = 0.0f;
    if (storeScreen.scrollPosition > maxScrollY) storeScreen.scrollPosition = maxScrollY;
    storeScreen.scrollVelocity = 0.0f;
    return true;
}

struct StorePageTaskRequest {
    uint32_t session = 0;
    int startPage = 1;
    int pageCount = 1;
};

static std::atomic<bool> storeMenuActive{false};
static std::atomic<uint32_t> storeMenuSession{0};
static std::atomic<bool> storePageTaskRunning{false};
static std::atomic<bool> storePageTaskCompleted{false};
static std::atomic<bool> storePageTaskCancel{false};
static StorePageTaskRequest storePageTaskRequest;
static uint32_t storePageResultSession = 0;
static int storePageResultStartPage = 1;
static int storePageResultFetchedPages = 0;
static int storePageResultLastPageItems = 0;
static int storePageResultTotal = 0;
static bool storePageResultFetchFailed = false;
static String storePageResultError;
static std::vector<GameStoreItem> storePageResultItems;
static String storeQueuedDownloadId;
static bool storeShowingCachedList = false;

static void storePageTask(void* parameter) {
    cloudSavePauseBackgroundSync();
    StorePageTaskRequest request = *(StorePageTaskRequest*)parameter;
    std::vector<GameStoreItem> resultItems;
    String error;
    String ssid;
    int total = 0;
    int fetchedPages = 0;
    int lastPageItems = 0;
    bool fetchFailed = false;

    Serial.printf("商店预取 任务开始：起始页=%d 页数=%d 会话=%u\n",
                  request.startPage, request.pageCount, (unsigned)request.session);
    if (!wirelessManager.connectSavedWifi(ssid, error)) {
        fetchFailed = true;
        wirelessManager.releaseNetworkTask();
        storeWifiConnected.store(false);
    } else {
        storeWifiConnected.store(true);
        for (int i = 0; i < request.pageCount; i++) {
            if (storePageTaskCancel.load()) break;
            int page = request.startPage + i;
            std::vector<GameStoreItem> pageItems;
            String pageError;
            int pageTotal = total;
            if (!gameStoreFetchPage(page, STORE_NETWORK_PAGE_SIZE,
                                    pageItems, pageTotal, pageError)) {
                error = pageError;
                fetchFailed = true;
                break;
            }
            total = pageTotal;
            lastPageItems = (int)pageItems.size();
            if (pageItems.empty()) break;
            resultItems.insert(resultItems.end(), pageItems.begin(), pageItems.end());
            fetchedPages++;
            if ((total > 0 && page * STORE_NETWORK_PAGE_SIZE >= total) ||
                storePageTaskCancel.load()) {
                break;
            }
        }
    }

    if (!resultItems.empty()) {
        String cacheError;
        if (!gameStoreMergeCachedItems(resultItems, cacheError)) {
            Serial.printf("商店预取 缓存失败：%s\n", cacheError.c_str());
        }
    }

    size_t resultCount = resultItems.size();
    storePageResultSession = request.session;
    storePageResultStartPage = request.startPage;
    storePageResultFetchedPages = fetchedPages;
    storePageResultLastPageItems = lastPageItems;
    storePageResultTotal = total;
    storePageResultFetchFailed = fetchFailed;
    storePageResultError = error;
    storePageResultItems.swap(resultItems);

    Serial.printf("商店预取 任务完成：起始页=%d 已取页数=%d 条目=%u 总数=%d 失败=%d\n",
                  request.startPage, fetchedPages,
                  (unsigned)resultCount, total,
                  fetchFailed ? 1 : 0);
    storePageTaskCompleted.store(true);
    storePageTaskRunning.store(false);
    if (!storeMenuActive.load() && storeWifiConnected.exchange(false)) {
        wirelessManager.releaseNetworkTask();
    }
    cloudSaveResumeBackgroundSync();
    vTaskDelete(nullptr);
}

static bool storeStartPageTask(int startPage, int pageCount) {
    if (startPage < 1 || pageCount < 1 || storePageTaskRunning.load()) {
        return false;
    }
    if (!wifiProvisioningHasSavedConfig()) {
        storeScreen.networkLoadFailed = true;
        return false;
    }

    storePageTaskRequest.session = storeMenuSession.load();
    storePageTaskRequest.startPage = startPage;
    storePageTaskRequest.pageCount = pageCount;
    storePageTaskCancel.store(false);
    storePageTaskCompleted.store(false);
    storePageTaskRunning.store(true);
    BaseType_t ok = xTaskCreatePinnedToCore(storePageTask,
                                            "store_page_fetch",
                                            8192,
                                            &storePageTaskRequest,
                                            1,
                                            nullptr,
                                            0);
    if (ok != pdPASS) {
        storePageTaskRunning.store(false);
        storeScreen.networkLoadFailed = true;
        Serial.println("商店预取 无法创建后台任务");
        return false;
    }
    storeScreen.pageLoading = true;
    return true;
}

static bool storeProcessPageTask() {
    if (!storePageTaskCompleted.exchange(false)) return false;
    if (storePageResultSession != storeMenuSession.load() || !storeMenuActive.load()) {
        storePageResultItems.clear();
        return false;
    }

    storeScreen.pageLoading = false;
    std::vector<GameStoreItem> resultItems;
    resultItems.swap(storePageResultItems);
    if (resultItems.empty()) {
        if (storePageResultFetchFailed) {
            storeScreen.networkLoadFailed = true;
            drawStoreScreen(storePageResultError.length()
                                ? storePageResultError
                                : "商店后台加载失败");
        }
        return false;
    }

    String selectedId;
    if (storeScreen.selectedIndex >= 0 &&
        storeScreen.selectedIndex < (int)storeItems.size()) {
        selectedId = storeItems[storeScreen.selectedIndex].id;
    }
    int resultLastPage = storePageResultStartPage +
                         storePageResultFetchedPages - 1;
    if (storeShowingCachedList &&
        storePageResultStartPage <= STORE_INITIAL_PAGE_COUNT) {
        size_t offset = (size_t)(storePageResultStartPage - 1) *
                        STORE_NETWORK_PAGE_SIZE;
        for (size_t i = 0; i < resultItems.size(); i++) {
            size_t index = offset + i;
            uint8_t downloaded =
                gameStoreHasLocalRom(resultItems[i].filename.c_str()) ? 1 : 0;
            if (index < storeItems.size()) {
                storeItems[index] = resultItems[i];
                storeDownloadedCache[index] = downloaded;
            } else {
                storeItems.push_back(resultItems[i]);
                storeDownloadedCache.push_back(downloaded);
            }
        }
        if (resultLastPage >= STORE_INITIAL_PAGE_COUNT ||
            (storePageResultTotal > 0 &&
             resultLastPage * STORE_NETWORK_PAGE_SIZE >= storePageResultTotal)) {
            storeShowingCachedList = false;
        }
    } else if (storePageResultStartPage == 1) {
        storeItems.swap(resultItems);
        storeDownloadedCache.clear();
        for (const GameStoreItem& item : storeItems) {
            storeDownloadedCache.push_back(
                gameStoreHasLocalRom(item.filename.c_str()) ? 1 : 0);
        }
    } else {
        for (const GameStoreItem& item : resultItems) {
            bool duplicate = false;
            for (const GameStoreItem& loaded : storeItems) {
                if (loaded.id == item.id) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                storeItems.push_back(item);
                storeDownloadedCache.push_back(
                    gameStoreHasLocalRom(item.filename.c_str()) ? 1 : 0);
            }
        }
    }

    if (selectedId.length()) {
        for (size_t i = 0; i < storeItems.size(); i++) {
            if (storeItems[i].id == selectedId) {
                storeScreen.selectedIndex = (int)i;
                break;
            }
        }
    }

    if (storeScreen.selectedIndex >= (int)storeItems.size()) {
        storeScreen.selectedIndex = storeItems.empty() ? 0 : (int)storeItems.size() - 1;
    }
    int maxScrollY = storeMaxScrollY();
    if (storeScreen.scrollPosition > maxScrollY) storeScreen.scrollPosition = maxScrollY;
    storeTotal = storePageResultTotal;
    storeScreen.nextNetworkPage = resultLastPage + 1;
    storeScreen.onlinePaging = true;
    storeScreen.networkLoadFailed = storePageResultFetchFailed;
    storeScreen.networkHasMore = storePageResultFetchedPages > 0 &&
                                 (storeTotal > 0
                                      ? resultLastPage * STORE_NETWORK_PAGE_SIZE < storeTotal
                                      : storePageResultLastPageItems >= STORE_NETWORK_PAGE_SIZE);
    drawStoreScreen(String("已加载 ") + (unsigned)storeItems.size() +
                    " / " + storeTotal);
    return true;
}

static bool storeMaybeLoadMore(bool force = false) {
    bool hasMore = storeScreen.onlinePaging ? storeScreen.networkHasMore
                                            : storeScreen.cacheHasMore;
    if (!hasMore || storeScreen.pageLoading) return false;
    if (storeScreen.onlinePaging && storeScreen.networkLoadFailed && !force) {
        return false;
    }
    int visibleBottomIndex = (int)(storeScreen.scrollPosition + STORE_LIST_HEIGHT - 1) /
                             STORE_ROW_HEIGHT;
    int triggerIndex = (int)storeItems.size() -
                       STORE_PREFETCH_AHEAD_PAGE_COUNT * STORE_NETWORK_PAGE_SIZE;
    if (triggerIndex < 0) triggerIndex = 0;
    if (!force && visibleBottomIndex < triggerIndex &&
        storeScreen.selectedIndex < triggerIndex) {
        return false;
    }
    if (!storeScreen.onlinePaging) {
        return storeAppendCachedBatch(STORE_NETWORK_PAGE_SIZE, false);
    }
    return storeStartPageTask(storeScreen.nextNetworkPage, 1);
}

static bool storeEnsureWifiConnected() {
    if (storeWifiConnected && wirelessManager.wifiActive()) {
        return true;
    }
    storeWifiConnected.store(false);

    if (!wifiProvisioningHasSavedConfig()) {
        runWifiProvisioningMenu();
        if (!wifiProvisioningHasSavedConfig()) {
            drawStoreScreen("未保存 WiFi");
            delay(1500);
            return false;
        }
    }

    String ssid;
    String error;
    if (!wirelessManager.wifiActive()) {
        drawStoreScreen("正在连接 WiFi...");
    }
    if (!wirelessManager.connectSavedWifi(ssid, error)) {
        drawStoreScreen(error);
        delay(1600);
        wirelessManager.releaseNetworkTask();
        return false;
    }

    storeWifiConnected = true;
    return true;
}

static void storeReleaseNetworkTask() {
    storeMenuActive.store(false);
    storePageTaskCancel.store(true);
    if (storePageTaskRunning.load()) {
        return;
    }
    if (!storeWifiConnected.exchange(false)) {
        return;
    }
    wirelessManager.releaseNetworkTask();
}

struct StoreProgressContext {
    String title;
    const char* label;
    uint32_t lastDrawMs = 0;
};

static void drawStoreProgress(size_t downloaded, size_t total, void* userData) {
    StoreProgressContext* ctx = (StoreProgressContext*)userData;
    uint32_t now = millis();
    if (ctx && (uint32_t)(now - ctx->lastDrawMs) < 200 && (total == 0 || downloaded < total)) {
        return;
    }
    if (ctx) {
        ctx->lastDrawMs = now;
    }

    tft.fillScreen(MENU_BG_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
    tft.setCursor(102, 58);
    tft.print(ctx ? ctx->label : "下载中");
    if (ctx) {
        drawClippedText(ctx->title, 38, 86, 244);
    }

    int progressW = 240;
    int filled = 0;
    if (total > 0) {
        filled = (int)((downloaded * progressW) / total);
        if (filled > progressW) filled = progressW;
    }
    tft.drawRect(40, 124, progressW, 18, MENU_BORDER_COLOR);
    tft.fillRect(42, 126, filled > 4 ? filled - 4 : 0, 14, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(84, 154);
    tft.print(formatStoreSize(downloaded));
    if (total > 0) {
        tft.print(" / ");
        tft.print(formatStoreSize(total));
    }
    tft.setFont(&fonts::Font0);
}

static bool storeDownloadSelected() {
    if (storeScreen.selectedIndex < 0 || storeScreen.selectedIndex >= (int)storeItems.size()) {
        return false;
    }
    const GameStoreItem& item = storeItems[storeScreen.selectedIndex];

    char romPath[128];
    if (!romStorageMakeDownloadRomPath(item.filename.c_str(), romPath, sizeof(romPath))) {
        drawStoreScreen("文件名无效");
        delay(1200);
        return false;
    }
    if (dijiLittleFsPathExistsQuiet(romPath)) {
        drawStoreScreen("这个游戏已经下载");
        delay(1000);
        return true;
    }
    if (storeScreen.pageLoading || storePageTaskRunning.load()) {
        storeQueuedDownloadId = item.id;
        storePageTaskCancel.store(true);
        drawStoreScreen("已排队，停止预加载后下载");
        return true;
    }

    String error;
    if (!storeEnsureWifiConnected()) {
        return false;
    }

    StoreProgressContext ctx;
    ctx.title = item.title.length() ? item.title : item.filename;
    ctx.label = "下载游戏";
    drawStoreProgress(0, item.size, &ctx);
    if (!gameStoreDownloadFile(item.downloadUrl, romPath, item.size, item.sha256,
                               drawStoreProgress, &ctx, error)) {
        drawStoreScreen(error);
        delay(1600);
        return false;
    }
    char coverPath[128];
    if (romStorageMakeDownloadCoverPath(romPath, coverPath, sizeof(coverPath)) &&
        item.coverUrl.length()) {
        ctx.label = "下载封面";
        ctx.lastDrawMs = 0;
        drawStoreProgress(0, 0, &ctx);
        String coverError;
        gameStoreDownloadFile(item.coverUrl, coverPath, 0, "",
                              drawStoreProgress, &ctx, coverError);
    }
    usageReportEvent("game_download", "game_id", item.id.c_str());

    scanROMFiles();
    if (storeScreen.selectedIndex >= 0 && storeScreen.selectedIndex < (int)storeDownloadedCache.size()) {
        storeDownloadedCache[storeScreen.selectedIndex] = 1;
    }
    drawStoreScreen("下载完成");
    delay(1200);
    return true;
}

static bool storeProcessQueuedDownload() {
    if (!storeQueuedDownloadId.length() || storeScreen.pageLoading ||
        storePageTaskRunning.load()) {
        return false;
    }

    String queuedId = storeQueuedDownloadId;
    storeQueuedDownloadId = "";
    for (size_t i = 0; i < storeItems.size(); i++) {
        if (storeItems[i].id == queuedId) {
            storeScreen.selectedIndex = (int)i;
            bool ok = storeDownloadSelected();
            drawStoreScreen();
            storeScreen.scrollLastAnimationMs = millis();
            return ok;
        }
    }
    drawStoreScreen("排队下载的游戏已不在列表中");
    return false;
}

static bool storeRefreshCachedIndex() {
    if (storePageTaskRunning.load() || storeScreen.pageLoading) {
        drawStoreScreen("商店列表正在后台加载");
        return true;
    }
    storeScreen.selectedIndex = 0;
    storeScreen.scrollPosition = 0.0f;
    storeScreen.scrollVelocity = 0.0f;
    storeScreen.nextNetworkPage = 1;
    storeScreen.onlinePaging = false;
    storeScreen.networkLoadFailed = false;
    storeScreen.networkHasMore = true;
    if (storeItems.size() >
        (size_t)(STORE_INITIAL_PAGE_COUNT * STORE_NETWORK_PAGE_SIZE)) {
        storeItems.resize(STORE_INITIAL_PAGE_COUNT * STORE_NETWORK_PAGE_SIZE);
        storeDownloadedCache.resize(storeItems.size());
    }
    storeShowingCachedList = !storeItems.empty();
    bool started = storeStartPageTask(1, 1);
    if (started) {
        drawStoreScreen("正在后台刷新前 3 页...");
    } else {
        drawStoreScreen("无法启动商店后台刷新");
    }
    return started;
}

static void drawStoreListForScroll(bool force = false) {
    uint32_t now = millis();
    if (!force && (uint32_t)(now - storeScreen.scrollLastDrawMs) < 33) return;
    storeScreen.scrollLastDrawMs = now;
    drawStoreList();
}

static void updateStoreScrollAnimation() {
    uint32_t now = millis();
    if (storeScreen.scrollLastAnimationMs == 0) storeScreen.scrollLastAnimationMs = now;
    float dt = (now - storeScreen.scrollLastAnimationMs) / 1000.0f;
    if (dt > 0.05f) dt = 0.05f;
    storeScreen.scrollLastAnimationMs = now;
    if (storeScreen.touchTracking || dt <= 0.0f) return;
    bool moved = false;
    if (fabsf(storeScreen.scrollVelocity) > 4.0f) {
        storeScreen.scrollPosition += storeScreen.scrollVelocity * dt;
        storeScreen.scrollVelocity *= powf(0.90f, dt * 60.0f);
        moved = true;
    } else {
        storeScreen.scrollVelocity = 0.0f;
    }
    if (storeMaybeLoadMore()) moved = true;
    int maxScrollY = storeMaxScrollY();
    if (storeScreen.scrollPosition < 0.0f) {
        storeScreen.scrollPosition += (0.0f - storeScreen.scrollPosition) * fminf(1.0f, dt * 18.0f);
        storeScreen.scrollVelocity *= 0.55f;
        if (fabsf(storeScreen.scrollPosition) < 0.5f && fabsf(storeScreen.scrollVelocity) < 5.0f) {
            storeScreen.scrollPosition = 0.0f;
            storeScreen.scrollVelocity = 0.0f;
        }
        moved = true;
    } else if (storeScreen.scrollPosition > maxScrollY) {
        storeScreen.scrollPosition += (maxScrollY - storeScreen.scrollPosition) * fminf(1.0f, dt * 18.0f);
        storeScreen.scrollVelocity *= 0.55f;
        if (fabsf(storeScreen.scrollPosition - maxScrollY) < 0.5f &&
            fabsf(storeScreen.scrollVelocity) < 5.0f) {
            storeScreen.scrollPosition = maxScrollY;
            storeScreen.scrollVelocity = 0.0f;
        }
        moved = true;
    }
    if (moved) drawStoreListForScroll();
}

static bool moveStoreSelection(int delta) {
    if (delta > 0 &&
        storeScreen.selectedIndex >= (int)storeItems.size() - STORE_LAZY_LOAD_AHEAD_ROWS) {
        storeMaybeLoadMore();
    }
    int next = storeScreen.selectedIndex + delta;
    if (next < 0) next = 0;
    if (next >= (int)storeItems.size()) next = (int)storeItems.size() - 1;
    if (next == storeScreen.selectedIndex || next < 0) return false;
    storeScreen.selectedIndex = next;
    float itemTop = next * STORE_ROW_HEIGHT;
    float itemBottom = itemTop + STORE_ROW_HEIGHT;
    if (itemTop < storeScreen.scrollPosition) storeScreen.scrollPosition = itemTop;
    else if (itemBottom > storeScreen.scrollPosition + STORE_LIST_HEIGHT) {
        storeScreen.scrollPosition = itemBottom - STORE_LIST_HEIGHT;
    }
    storeScreen.scrollVelocity = 0.0f;
    return true;
}

static bool activateStoreSoftbarItem() {
    switch (storeScreen.softbarIndex) {
        case 0:
            return storeRefreshCachedIndex();
        case 1:
            return storeDownloadSelected();
        case 2:
            return false;
        case 3:
            storeScreen.scrollPosition = 0.0f;
            storeScreen.selectedIndex = 0;
            drawStoreListForScroll(true);
            return true;
        case 4:
            storeMaybeLoadMore(true);
            storeScreen.scrollPosition = storeMaxScrollY();
            storeScreen.selectedIndex =
                storeItems.empty() ? 0 : (int)storeItems.size() - 1;
            drawStoreListForScroll(true);
            return true;
        default:
            return true;
    }
}

static void runGameStoreMenu() {
    CloudSaveBackgroundPauseGuard cloudSyncPause;
    if (!remoteConfigStoreEnabled()) {
        drawStoreScreen("游戏商店暂时关闭");
        delay(1500);
        return;
    }
    storeItems.clear();
    storeDownloadedCache.clear();
    drawStoreScreen("正在打开游戏商店...");

    if (!builtinStorageAvailable) {
        drawStoreScreen("内置存储不可用");
        delay(1500);
        return;
    }

    storeMenuSession.fetch_add(1);
    storeMenuActive.store(true);
    if (!storePageTaskRunning.load()) {
        storeWifiConnected = false;
    }
    storeQueuedDownloadId = "";
    storeShowingCachedList = false;
    storeScreen.selectedIndex = 0;
    storeTotal = 0;
    storeScreen.scrollPosition = 0.0f;
    storeScreen.scrollVelocity = 0.0f;
    storeScreen.softbarFocused = false;
    storeScreen.softbarIndex = 1;
    storeScreen.nextNetworkPage = 1;
    storeScreen.onlinePaging = false;
    storeScreen.networkHasMore = false;
    storeScreen.pageLoading = false;
    storeScreen.networkLoadFailed = false;

    bool loadedCache = gameStoreHasCachedIndex() &&
                       storeLoadInitialCached(false,
                                              STORE_INITIAL_PAGE_COUNT *
                                                  STORE_NETWORK_PAGE_SIZE);
    if (loadedCache) {
        storeShowingCachedList = true;
        storeTotal = (int)storeItems.size();
        drawStoreScreen("已显示缓存，正在后台加载前 3 页...");
    } else {
        drawStoreScreen("正在后台加载前 3 页...");
    }

    if (!wifiProvisioningHasSavedConfig()) {
        runWifiProvisioningMenu();
    }
    if (wifiProvisioningHasSavedConfig()) {
        storeStartPageTask(1, 1);
    } else {
        storeScreen.networkLoadFailed = true;
        drawStoreScreen(loadedCache ? "未配置 WiFi，正在使用缓存"
                                    : "未配置 WiFi");
    }

    uint32_t lastInputMs = 0;
    storeScreen.scrollLastAnimationMs = millis();
    storeScreen.scrollLastDrawMs = millis();
    while (true) {
        updateButtons();
        touchInputUpdate();
        storeProcessPageTask();
        storeProcessQueuedDownload();
        if (!storeScreen.pageLoading && !storeScreen.networkLoadFailed &&
            !storeQueuedDownloadId.length() && wifiProvisioningHasSavedConfig()) {
            if (!storeScreen.onlinePaging) {
                storeStartPageTask(1, 1);
            } else if (storeScreen.networkHasMore &&
                       storeScreen.nextNetworkPage <= STORE_INITIAL_PAGE_COUNT) {
                storeStartPageTask(storeScreen.nextNetworkPage, 1);
            }
        }
        uint32_t now = millis();

        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B) {
                if (storeScreen.softbarFocused) {
                    storeScreen.softbarFocused = false;
                    drawStoreSoftbar();
                    lastInputMs = now;
                } else {
                    waitForButtonsReleased(DIJI_BTN_B);
                    break;
                }
            } else if (buttons.SELECT) {
                waitForButtonsReleased(DIJI_BTN_SELECT);
                storeScreen.softbarFocused = !storeScreen.softbarFocused;
                drawStoreSoftbar();
                lastInputMs = millis();
            } else if (storeScreen.softbarFocused && buttons.LEFT &&
                       storeScreen.softbarIndex > 0) {
                storeScreen.softbarIndex--;
                drawStoreSoftbar();
                lastInputMs = now;
            } else if (storeScreen.softbarFocused && buttons.RIGHT &&
                       storeScreen.softbarIndex < 4) {
                storeScreen.softbarIndex++;
                drawStoreSoftbar();
                lastInputMs = now;
            } else if (storeScreen.softbarFocused && buttons.UP) {
                storeScreen.softbarFocused = false;
                drawStoreSoftbar();
                lastInputMs = now;
            } else if (storeScreen.softbarFocused &&
                       (buttons.A || buttons.START)) {
                waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
                if (storeScreen.softbarIndex == 2) break;
                activateStoreSoftbarItem();
                drawStoreScreen();
                lastInputMs = millis();
            } else if (!storeScreen.softbarFocused && buttons.UP) {
                if (moveStoreSelection(-1)) drawStoreListForScroll(true);
                lastInputMs = now;
            } else if (!storeScreen.softbarFocused && buttons.DOWN) {
                if (moveStoreSelection(1)) drawStoreListForScroll(true);
                lastInputMs = now;
            } else if (!storeScreen.softbarFocused &&
                       (buttons.A || buttons.START)) {
                waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
                storeDownloadSelected();
                drawStoreScreen();
                storeScreen.scrollLastAnimationMs = millis();
                lastInputMs = millis();
            }
        }

        if (touchInputJustPressed()) {
            TouchPoint point = touchInputPoint();
            storeScreen.touchTracking = touchPointInRect(point, STORE_LIST_X, STORE_LIST_Y,
                                                      STORE_LIST_WIDTH, STORE_LIST_HEIGHT);
            if (storeScreen.touchTracking) {
                storeScreen.touchStart = point;
                storeScreen.touchMoved = fabsf(storeScreen.scrollVelocity) > 30.0f ||
                                      storeScreen.scrollPosition < 0.0f ||
                                      storeScreen.scrollPosition > storeMaxScrollY();
                storeScreen.touchLastY = point.y;
                storeScreen.touchStartScroll = storeScreen.scrollPosition;
                storeScreen.touchLastSampleMs = now;
                storeScreen.scrollVelocity = 0.0f;
            }
        }

        if (storeScreen.touchTracking && touchInputTouched()) {
            TouchPoint point = touchInputPoint();
            int dy = point.y - storeScreen.touchStart.y;
            int dx = point.x - storeScreen.touchStart.x;
            if (abs(dy) > 5 || abs(dx) > 8) storeScreen.touchMoved = true;
            storeMaybeLoadMore();
            int maxScrollY = storeMaxScrollY();
            float target = storeScreen.touchStartScroll - dy;
            if (target < 0.0f) target *= 0.35f;
            else if (target > maxScrollY) target = maxScrollY + (target - maxScrollY) * 0.35f;
            uint32_t sampleDt = now - storeScreen.touchLastSampleMs;
            if (sampleDt > 0) {
                float instant = (float)(storeScreen.touchLastY - point.y) * 1000.0f / sampleDt;
                storeScreen.scrollVelocity = storeScreen.scrollVelocity * 0.55f + instant * 0.45f;
                if (storeScreen.scrollVelocity > 1200.0f) storeScreen.scrollVelocity = 1200.0f;
                if (storeScreen.scrollVelocity < -1200.0f) storeScreen.scrollVelocity = -1200.0f;
                storeScreen.touchLastY = point.y;
                storeScreen.touchLastSampleMs = now;
            }
            if (fabsf(target - storeScreen.scrollPosition) >= 0.5f) {
                storeScreen.scrollPosition = target;
                drawStoreListForScroll();
            }
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            bool handled = false;
            if (storeScreen.touchTracking) {
                storeScreen.touchTracking = false;
                storeScreen.scrollLastAnimationMs = millis();
                if (!storeScreen.touchMoved) {
                    int bounded = (int)storeScreen.scrollPosition;
                    if (bounded < 0) bounded = 0;
                    int maxScrollY = storeMaxScrollY();
                    if (bounded > maxScrollY) bounded = maxScrollY;
                    int index = (bounded + point.y - STORE_LIST_Y) / STORE_ROW_HEIGHT;
                    if (index >= 0 && index < (int)storeItems.size()) {
                        if (index == storeScreen.selectedIndex) {
                            storeDownloadSelected();
                            drawStoreScreen();
                        } else {
                            storeScreen.selectedIndex = index;
                            storeMaybeLoadMore();
                            drawStoreListForScroll(true);
                        }
                    }
                }
                handled = true;
            }

            if (!handled && touchPointInRect(point, 0, MENU_SOFTBAR_Y, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
                storeRefreshCachedIndex();
                handled = true;
            } else if (!handled && touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_Y, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
                storeDownloadSelected();
                drawStoreScreen();
                handled = true;
            } else if (!handled && touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 2, MENU_SOFTBAR_Y, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
                break;
            } else if (!handled && touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 3, MENU_SOFTBAR_Y, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
                storeScreen.scrollPosition = 0.0f;
                storeScreen.selectedIndex = 0;
                drawStoreListForScroll(true);
                handled = true;
            } else if (!handled && touchPointInRect(point, MENU_SOFTBAR_ITEM_WIDTH * 4, MENU_SOFTBAR_Y, MENU_SOFTBAR_ITEM_WIDTH, MENU_SOFTBAR_HEIGHT)) {
                storeMaybeLoadMore(true);
                storeScreen.scrollPosition = storeMaxScrollY();
                storeScreen.selectedIndex = storeItems.empty() ? 0 : (int)storeItems.size() - 1;
                drawStoreListForScroll(true);
                handled = true;
            }
        }

        updateStoreScrollAnimation();
        delay(16);
    }

    storeReleaseNetworkTask();
}
