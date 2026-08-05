static void resetFrameScheduler(uint8_t forceRenderFrames = 2) {
    gameSession.resetFrameScheduler(forceRenderFrames);
    nes.requestFrameSkip(false);
}

static void removeLastUtf8Codepoint(String& text) {
    int index = text.length() - 1;
    if (index < 0) return;

    while (index > 0 && (((uint8_t)text[index] & 0xC0) == 0x80)) {
        index--;
    }
    text.remove(index);
}

static String makeRomBaseName(const String& romPath) {
    const char* local = romStorageLocalPath(romPath.c_str());
    const char* slash = strrchr(local, '/');
    String name = slash ? slash + 1 : local;
    int dot = name.lastIndexOf('.');
    if (dot > 0) {
        name.remove(dot);
    }
    return name;
}

static String makeRomCoverPath(const String& romPath) {
    return String("/covers/") + makeRomBaseName(romPath) + ".png";
}

static String makeClippedText(String text, int maxWidth) {
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    if (tft.textWidth(text.c_str()) <= maxWidth) {
        return text;
    }

    const char* suffix = "...";
    while (text.length() > 0) {
        removeLastUtf8Codepoint(text);
        String candidate = text + suffix;
        if (tft.textWidth(candidate.c_str()) <= maxWidth) {
            return candidate;
        }
    }

    return suffix;
}

static String makeRomDisplayName(const String& romPath, int maxWidth) {
    char displayBuffer[128];
    if (!romStorageMakeDisplayName(romPath.c_str(), displayBuffer, sizeof(displayBuffer))) {
        snprintf(displayBuffer, sizeof(displayBuffer), "%s", romPath.c_str());
    }
    return makeClippedText(String(displayBuffer), maxWidth);
}

static void drawRomDisplayName(const String& romPath, int x, int y, int maxWidth) {
    String displayName = makeRomDisplayName(romPath, maxWidth);
    drawClippedText(displayName, x, y, maxWidth);
}

static void drawClippedText(const String& text, int x, int y, int maxWidth) {
    String displayName = makeClippedText(text, maxWidth);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setCursor(x, y);
    tft.print(displayName);
    tft.setFont(&fonts::Font0);
}

static void drawCoverPlaceholder(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, 0x0841);
    tft.drawRect(x, y, w, h, MENU_BORDER_COLOR);
    tft.drawFastHLine(x + 10, y + 34, w - 20, MENU_BORDER_COLOR);
    tft.drawFastHLine(x + 10, y + 64, w - 20, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_HINT_COLOR, 0x0841);
    tft.setTextSize(1);
    tft.setFont(&fonts::efontCN_14);
    tft.setCursor(x + 22, y + 43);
    tft.print("NO");
    tft.setCursor(x + 14, y + 58);
    tft.print("COVER");
    tft.setFont(&fonts::Font0);
}

static bool drawCoverFromStorage(const String& coverPath, int x, int y) {
    float scaleX = (float)MENU_COVER_W / (float)MENU_COVER_ASSET_W;
    float scaleY = (float)MENU_COVER_H / (float)MENU_COVER_ASSET_H;
    if (builtinStorageAvailable && dijiLittleFsPathExistsQuiet(coverPath.c_str())) {
        File coverFile = DIJI_ROMFS.open(coverPath.c_str(), FILE_READ);
        if (coverFile) {
            lgfx::v1::DataWrapperT<fs::File> data(&coverFile);
            bool result = tft.drawPng(&data, x, y, MENU_COVER_W, MENU_COVER_H,
                                      0, 0, scaleX, scaleY);
            data.close();
            return result;
        }
    }
    if (sdCardAvailable && DIJI_SD.exists(coverPath.c_str())) {
        File coverFile = DIJI_SD.open(coverPath.c_str(), FILE_READ);
        if (coverFile) {
            lgfx::v1::DataWrapperT<fs::File> data(&coverFile);
            bool result = tft.drawPng(&data, x, y, MENU_COVER_W, MENU_COVER_H,
                                      0, 0, scaleX, scaleY);
            data.close();
            return result;
        }
    }
    return false;
}

struct GameThumbnailHeader {
    char magic[4];
    uint16_t width;
    uint16_t height;
    uint32_t timestamp;
};

static bool readGameThumbnailHeader(const char* path, GameThumbnailHeader& header) {
    if (!dijiStoragePathExists(path)) return false;
    File file = dijiOpenStoragePath(path, FILE_READ);
    if (!file) return false;
    const size_t expectedSize = sizeof(GameThumbnailHeader) + 160u * 120u * sizeof(uint16_t);
    const bool valid = file.size() == expectedSize &&
                       file.read((uint8_t*)&header, sizeof(header)) == sizeof(header) &&
                       memcmp(header.magic, "DTHM", 4) == 0 &&
                       header.width == 160 && header.height == 120;
    file.close();
    return valid;
}

static bool drawGameThumbnail(const char* path, int panelX, int panelY, int panelW, int panelH) {
    if (!dijiStoragePathExists(path)) return false;
    File file = dijiOpenStoragePath(path, FILE_READ);
    if (!file) return false;
    GameThumbnailHeader header;
    const size_t expectedSize = sizeof(header) + 160u * 120u * sizeof(uint16_t);
    if (file.size() != expectedSize ||
        file.read((uint8_t*)&header, sizeof(header)) != sizeof(header) ||
        memcmp(header.magic, "DTHM", 4) != 0 || header.width != 160 || header.height != 120) {
        file.close();
        return false;
    }

    // Keep the 4:3 game image undistorted inside the portrait cover panel.
    int drawW = panelW;
    int drawH = drawW * 3 / 4;
    if (drawH > panelH) {
        drawH = panelH;
        drawW = drawH * 4 / 3;
    }
    const int drawX = panelX + (panelW - drawW) / 2;
    const int drawY = panelY + (panelH - drawH) / 2;
    uint16_t source[160];
    uint16_t row[MENU_COVER_W];
    for (int dy = 0; dy < drawH; dy++) {
        const int sourceY = dy * 120 / drawH;
        if (!file.seek(sizeof(header) + sourceY * sizeof(source)) ||
            file.read((uint8_t*)source, sizeof(source)) != sizeof(source)) {
            file.close();
            return false;
        }
        for (int dx = 0; dx < drawW; dx++) source[dx] = source[dx * 160 / drawW];
        memcpy(row, source, drawW * sizeof(uint16_t));
        tft.pushImage(drawX, drawY + dy, drawW, 1, row);
    }
    file.close();
    tft.drawRect(drawX, drawY, drawW, drawH, MENU_BORDER_COLOR);
    return true;
}

static bool drawLatestGameThumbnail(const String& romPath) {
    char path[160];
    GameThumbnailHeader header;
    if (romStorageMakeAutoThumbnailPath(romPath.c_str(), path, sizeof(path)) &&
        readGameThumbnailHeader(path, header)) {
        return drawGameThumbnail(path, MENU_COVER_X, MENU_COVER_Y,
                                 MENU_COVER_W, MENU_COVER_H);
    }

    char newestPath[160] = {0};
    uint32_t newestTimestamp = 0;
    for (uint8_t slot = 0; slot < 3; slot++) {
        if (!romStorageMakeSlotThumbnailPath(romPath.c_str(), slot, path, sizeof(path)) ||
            !readGameThumbnailHeader(path, header)) continue;
        if (!newestPath[0] || header.timestamp >= newestTimestamp) {
            newestTimestamp = header.timestamp;
            snprintf(newestPath, sizeof(newestPath), "%s", path);
        }
    }
    return newestPath[0] && drawGameThumbnail(newestPath, MENU_COVER_X, MENU_COVER_Y,
                                               MENU_COVER_W, MENU_COVER_H);
}

static void drawSelectedRomCover() {
    tft.fillRect(4, 42, 120, 164, MENU_BG_COLOR);
    tft.fillRect(MENU_COVER_X, MENU_COVER_Y, MENU_COVER_W, MENU_COVER_H, MENU_PANEL_COLOR);

    bool coverDrawn = false;
    if (!romList.empty() && libraryScreen.selectedIndex >= 0 && libraryScreen.selectedIndex < (int)romList.size()) {
        const String& romPath = romList[libraryScreen.selectedIndex];
        coverDrawn = drawLatestGameThumbnail(romPath);
        if (!coverDrawn) {
            String coverPath = makeRomCoverPath(romPath);
            coverDrawn = drawCoverFromStorage(coverPath, MENU_COVER_X, MENU_COVER_Y);
        }
    }
    if (!coverDrawn) {
        drawCoverPlaceholder(MENU_COVER_X, MENU_COVER_Y, MENU_COVER_W, MENU_COVER_H);
    }
    tft.drawRect(MENU_COVER_X, MENU_COVER_Y, MENU_COVER_W, MENU_COVER_H, MENU_BORDER_COLOR);
}

static int menuMaxScrollY() {
    int contentHeight = (int)romList.size() * MENU_ITEM_HEIGHT;
    return contentHeight > MENU_LIST_HEIGHT ? contentHeight - MENU_LIST_HEIGHT : 0;
}

template <typename Surface>
static void renderMenuRows(Surface& surface, int scrollY, int leftX, int topY) {
    surface.fillRect(leftX, topY, MENU_LIST_WIDTH, MENU_LIST_HEIGHT, MENU_BG_COLOR);
    surface.setClipRect(leftX, topY, MENU_LIST_WIDTH, MENU_LIST_HEIGHT);
    for (int romIndex = 0; romIndex < (int)romList.size(); romIndex++) {
        int itemY = topY + romIndex * MENU_ITEM_HEIGHT - scrollY;
        if (itemY + MENU_ITEM_HEIGHT <= topY || itemY >= topY + MENU_LIST_HEIGHT) continue;
        bool selected = romIndex == libraryScreen.selectedIndex;
        uint16_t rowColor = selected ? MENU_HIGHLIGHT_BG : MENU_ROW_COLOR;
        surface.fillRoundRect(leftX, itemY + 1, MENU_LIST_WIDTH - 5,
                              MENU_ITEM_HEIGHT - 4, 6, rowColor);
        surface.drawFastVLine(leftX, itemY + 4, MENU_ITEM_HEIGHT - 10,
                              selected ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR);
        surface.setFont(&fonts::efontCN_14);
        surface.setTextSize(1);
        if (selected) {
            surface.setTextColor(MENU_ACCENT_COLOR, rowColor);
            surface.setCursor(leftX + 6, itemY + 9);
            surface.print(">");
            surface.setTextColor(MENU_TITLE_COLOR, rowColor);
        } else {
            surface.setTextColor(MENU_TEXT_COLOR, rowColor);
        }
        String displayName = makeRomDisplayName(romList[romIndex], MENU_LIST_WIDTH - 32);
        surface.setCursor(leftX + 20, itemY + 7);
        surface.print(displayName);
    }
    surface.clearClipRect();

    int maxScrollY = menuMaxScrollY();
    if (maxScrollY > 0) {
        constexpr int trackH = MENU_LIST_HEIGHT - 8;
        int thumbH = trackH * MENU_LIST_HEIGHT /
                     ((int)romList.size() * MENU_ITEM_HEIGHT);
        if (thumbH < 20) thumbH = 20;
        int bounded = scrollY;
        if (bounded < 0) bounded = 0;
        if (bounded > maxScrollY) bounded = maxScrollY;
        int thumbY = topY + 4 + (trackH - thumbH) * bounded / maxScrollY;
        surface.fillRoundRect(leftX + MENU_LIST_WIDTH - 3, topY + 4, 3, trackH, 2, MENU_BORDER_COLOR);
        surface.fillRoundRect(leftX + MENU_LIST_WIDTH - 3, thumbY, 3, thumbH, 2, MENU_ACCENT_COLOR);
    }
    surface.setFont(&fonts::Font0);
}

static void drawMenuRows() {
    int scrollY = (int)roundf(libraryScreen.scrollPosition);
    if (!libraryScreen.listCanvasReady) {
        libraryScreen.listCanvas.setColorDepth(16);
        libraryScreen.listCanvas.setPsram(true);
        libraryScreen.listCanvasReady = libraryScreen.listCanvas.createSprite(MENU_LIST_WIDTH, MENU_LIST_HEIGHT) != nullptr;
    }
    if (libraryScreen.listCanvasReady) {
        renderMenuRows(libraryScreen.listCanvas, scrollY, 0, 0);
        libraryScreen.listCanvas.pushSprite(MENU_LIST_X, MENU_LIST_Y);
    } else {
        tft.startWrite();
        renderMenuRows(tft, scrollY, MENU_LIST_X, MENU_LIST_Y);
        tft.endWrite();
    }
}

static void drawMenuSoftbar() {
    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setTextSize(1);
    tft.setFont(&fonts::efontCN_14);
    const char* softbarLabels[] = {"主页", "设置", "商店", "开始", "关机"};
    const int labelX[] = {18, 18, 18, 18, 18};
    for (int i = 0; i < 5; i++) {
        int buttonX = i * MENU_SOFTBAR_ITEM_WIDTH;
        bool focused = libraryScreen.softbarFocused &&
                       libraryScreen.softbarIndex == i;
        if (focused) {
            tft.fillRect(buttonX + 2, MENU_SOFTBAR_BUTTON_Y,
                         MENU_SOFTBAR_ITEM_WIDTH - 4, MENU_SOFTBAR_BUTTON_H,
                         MENU_HIGHLIGHT_BG);
        }
        tft.drawRect(buttonX + 2, MENU_SOFTBAR_BUTTON_Y,
                     MENU_SOFTBAR_ITEM_WIDTH - 4, MENU_SOFTBAR_BUTTON_H,
                     focused ? MENU_TITLE_COLOR :
                     (i == 3 ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR));
        tft.setTextColor(focused ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                         focused ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
        tft.setCursor(buttonX + labelX[i], MENU_SOFTBAR_Y + 9);
        tft.print(softbarLabels[i]);
    }
    tft.setFont(&fonts::Font0);
}

static bool moveMenuSelection(int delta) {
    if (romList.empty() || delta == 0) {
        return false;
    }

    int nextIndex = libraryScreen.selectedIndex + delta;
    if (nextIndex < 0) {
        nextIndex = 0;
    }
    if (nextIndex >= (int)romList.size()) {
        nextIndex = (int)romList.size() - 1;
    }
    if (nextIndex == libraryScreen.selectedIndex) {
        return false;
    }

    libraryScreen.selectedIndex = nextIndex;
    float itemTop = libraryScreen.selectedIndex * MENU_ITEM_HEIGHT;
    float itemBottom = itemTop + MENU_ITEM_HEIGHT;
    if (itemTop < libraryScreen.scrollPosition) {
        libraryScreen.scrollPosition = itemTop;
    } else if (itemBottom > libraryScreen.scrollPosition + MENU_LIST_HEIGHT) {
        libraryScreen.scrollPosition = itemBottom - MENU_LIST_HEIGHT;
    }
    libraryScreen.scrollVelocity = 0.0f;
    return true;
}

static bool confirmDeleteSelectedRom() {
    if (libraryScreen.selectedIndex < 0 || libraryScreen.selectedIndex >= (int)romList.size()) {
        return false;
    }

    const int boxX = 34;
    const int boxY = 54;
    const int boxW = 252;
    const int boxH = 132;
    const int cancelX = boxX + 18;
    const int confirmX = boxX + 136;
    const int buttonY = boxY + 92;
    const int buttonW = 98;
    const int buttonH = 28;

    tft.fillRect(boxX, boxY, boxW, boxH, MENU_BG_COLOR);
    tft.drawRect(boxX, boxY, boxW, boxH, MENU_BORDER_COLOR);
    tft.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
    tft.setCursor(boxX + 78, boxY + 14);
    tft.print("删除游戏");
    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    drawClippedText(makeRomDisplayName(romList[libraryScreen.selectedIndex], 200), boxX + 26, boxY + 42, 200);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(boxX + 34, boxY + 66);
    tft.print("只删除本地游戏文件");

    bool confirmFocused = false;
    auto drawButtons = [&]() {
        const uint16_t cancelFill =
            confirmFocused ? MENU_PANEL_COLOR : MENU_HIGHLIGHT_BG;
        const uint16_t deleteFill =
            confirmFocused ? 0xA800 : MENU_PANEL_COLOR;
        tft.fillRect(cancelX, buttonY, buttonW, buttonH, cancelFill);
        tft.drawRect(cancelX, buttonY, buttonW, buttonH,
                     confirmFocused ? MENU_BORDER_COLOR : MENU_TITLE_COLOR);
        tft.fillRect(confirmX, buttonY, buttonW, buttonH, deleteFill);
        tft.drawRect(confirmX, buttonY, buttonW, buttonH,
                     confirmFocused ? 0xF800 : MENU_BORDER_COLOR);
        tft.setTextColor(MENU_TITLE_COLOR, cancelFill);
        tft.setCursor(cancelX + 32, buttonY + 7);
        tft.print("取消");
        tft.setTextColor(confirmFocused ? 0xFFFF : MENU_TEXT_COLOR, deleteFill);
        tft.setCursor(confirmX + 32, buttonY + 7);
        tft.print("确认");
        tft.setFont(&fonts::Font0);
    };
    drawButtons();

    uint32_t lastInputMs = 0;
    bool waitForFreshTouch = touchInputTouched();
    while (true) {
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B || buttons.SELECT) {
                return false;
            }
            if (buttons.LEFT || buttons.RIGHT) {
                confirmFocused = !confirmFocused;
                drawButtons();
            } else if (buttons.A || buttons.START) {
                waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
                return confirmFocused;
            }
            lastInputMs = now;
        }

        if (waitForFreshTouch) {
            if (!touchInputTouched()) {
                waitForFreshTouch = false;
            }
            delay(10);
            continue;
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, cancelX, buttonY, buttonW, buttonH)) {
                return false;
            }
            if (touchPointInRect(point, confirmX, buttonY, buttonW, buttonH)) {
                return true;
            }
            if (!touchPointInRect(point, boxX, boxY, boxW, boxH)) {
                return false;
            }
        }
        delay(20);
    }
}

static bool deleteSelectedRom() {
    if (libraryScreen.selectedIndex < 0 || libraryScreen.selectedIndex >= (int)romList.size()) {
        return false;
    }

    String romPath = romList[libraryScreen.selectedIndex];
    if (!romStorageIsDeletableLocalRom(romPath.c_str())) {
        tft.fillRect(0, 188, 320, 22, MENU_BG_COLOR);
        tft.setFont(&fonts::efontCN_14);
        tft.setTextSize(1);
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        tft.setCursor(76, 192);
        tft.print("这个条目不能删除");
        tft.setFont(&fonts::Font0);
        delay(1200);
        drawMainMenu();
        return false;
    }

    if (!confirmDeleteSelectedRom()) {
        drawMainMenu();
        return false;
    }

    const char* localPath = romStorageLocalPath(romPath.c_str());
    RomStorageSource source = romStorageSourceForPath(romPath.c_str());
    bool removedRom = false;
    if (source == RomStorageSource::Builtin || source == RomStorageSource::Cache) {
        removedRom = DIJI_ROMFS.remove(localPath);
    } else if (source == RomStorageSource::Sd) {
        removedRom = DIJI_SD.remove(localPath);
    }

    char coverPath[128];
    if (romStorageMakeDownloadCoverPath(romPath.c_str(), coverPath, sizeof(coverPath)) &&
        dijiLittleFsPathExistsQuiet(coverPath)) {
        DIJI_ROMFS.remove(coverPath);
    }
    if (romStorageMakeDownloadCoverPath(romPath.c_str(), coverPath, sizeof(coverPath)) &&
        sdCardAvailable && DIJI_SD.exists(coverPath)) {
        DIJI_SD.remove(coverPath);
    }

    tft.fillScreen(MENU_BG_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(removedRom ? 0x07E0 : 0xF800, MENU_BG_COLOR);
    tft.setCursor(104, 108);
    tft.print(removedRom ? "删除成功" : "删除失败");
    tft.setFont(&fonts::Font0);
    delay(900);

    scanROMFiles();
    if (libraryScreen.selectedIndex >= (int)romList.size()) {
        libraryScreen.selectedIndex = (int)romList.size() - 1;
    }
    if (libraryScreen.selectedIndex < 0) {
        libraryScreen.selectedIndex = 0;
    }
    int maxScrollY = menuMaxScrollY();
    if (libraryScreen.scrollPosition > maxScrollY) libraryScreen.scrollPosition = maxScrollY;
    if (libraryScreen.scrollPosition < 0.0f) libraryScreen.scrollPosition = 0.0f;
    libraryScreen.scrollVelocity = 0.0f;
    drawMainMenu();
    return removedRom;
}
