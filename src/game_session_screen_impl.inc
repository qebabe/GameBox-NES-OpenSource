// ================ 暂停菜单绘制 ================
void drawPauseMenu() {
    // 在游戏画面上绘制半透明遮罩
    // 由于硬件限制，我们用条纹效果模拟半透明
    for (int y = 0; y < 240; y += 2) {
        tft.drawFastHLine(TFT_OFFSET_X, y, SCREEN_WIDTH, PAUSE_OVERLAY_COLOR);
    }

    // 暂停菜单框
    int menuWidth = PAUSE_MENU_WIDTH;
    int menuHeight = PAUSE_MENU_HEIGHT;
    int menuX = (320 - menuWidth) / 2;
    int menuY = (240 - menuHeight) / 2;

    // 背景
    tft.fillRect(menuX, menuY, menuWidth, menuHeight, MENU_BG_COLOR);
    tft.drawRect(menuX, menuY, menuWidth, menuHeight, MENU_BORDER_COLOR);
    tft.drawRect(menuX + 1, menuY + 1, menuWidth - 2, menuHeight - 2, MENU_BORDER_COLOR);

    // 标题
    tft.setTextColor(MENU_TITLE_COLOR);
    tft.setTextSize(2);
    tft.setFont(&fonts::efontCN_14);
    tft.setCursor(menuX + 106, menuY + 10);
    tft.print("暂停");

    // 分隔线
    tft.drawFastHLine(menuX + 12, menuY + 44, menuWidth - 24, MENU_BORDER_COLOR);

    const char* options[] = {"继续游戏", "保存进度", "读取进度", "返回菜单"};
    tft.setTextSize(1);

    for (int i = 0; i < PAUSE_OPTION_COUNT; i++) {
        int optY = menuY + PAUSE_OPTION_START_Y + i * (PAUSE_OPTION_HEIGHT + PAUSE_OPTION_GAP);

        if (i == pauseMenuIndex) {
            tft.fillRect(menuX + 18, optY, menuWidth - 36, PAUSE_OPTION_HEIGHT, MENU_HIGHLIGHT_BG);
            tft.drawRect(menuX + 18, optY, menuWidth - 36, PAUSE_OPTION_HEIGHT, MENU_TITLE_COLOR);
            tft.setTextColor(MENU_TITLE_COLOR);
        } else {
            tft.drawRect(menuX + 18, optY, menuWidth - 36, PAUSE_OPTION_HEIGHT, MENU_BORDER_COLOR);
            tft.setTextColor(MENU_TEXT_COLOR);
        }

        tft.setCursor(menuX + 94, optY + 8);
        tft.print(options[i]);
        if (i == 1 || i == 2) {
            tft.setCursor(menuX + 205, optY + 8);
            tft.printf("槽%u", (unsigned)(pauseSaveSlot + 1));
        }
    }

    int volumeY = menuY + PAUSE_VOLUME_Y;
    tft.drawFastHLine(menuX + 12, volumeY - 8, menuWidth - 24, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_HINT_COLOR);
    tft.setCursor(menuX + 20, volumeY + 7);
    tft.print("音量");
    tft.drawRect(menuX + 72, volumeY, PAUSE_VOLUME_BUTTON_W, PAUSE_VOLUME_BUTTON_H, MENU_BORDER_COLOR);
    tft.drawRect(menuX + 198, volumeY, PAUSE_VOLUME_BUTTON_W, PAUSE_VOLUME_BUTTON_H, MENU_BORDER_COLOR);
    tft.setCursor(menuX + 96, volumeY + 8);
    tft.print("-");
    tft.setCursor(menuX + 222, volumeY + 8);
    tft.print("+");
    tft.setTextColor(MENU_TITLE_COLOR);
    tft.setCursor(menuX + 138, volumeY + 2);
    tft.printf("%d%%", audioVolumePercent);
    drawAudioVolumeBlocks(menuX + 138, volumeY + 18, audioVolumePercent);

    // 操作提示
    tft.setTextColor(MENU_HINT_COLOR);
    tft.setCursor(menuX + 38, menuY + menuHeight - 10);
    tft.print("方向键选择 A确认 B返回");
    tft.setFont(&fonts::Font0);
}

static void drawAudioVolumeBlocks(int x, int y, uint8_t percent) {
    const int blockW = 8;
    const int blockH = 5;
    const int gap = 3;
    uint8_t level = (percent + 19) / 20;
    if (level > 5) {
        level = 5;
    }

    for (int i = 0; i < 5; i++) {
        int bx = x + i * (blockW + gap);
        tft.drawRect(bx, y, blockW, blockH, MENU_BORDER_COLOR);
        tft.fillRect(bx + 1, y + 1, blockW - 2, blockH - 2,
                     i < level ? MENU_TITLE_COLOR : MENU_BG_COLOR);
    }
}

// ================ 暂停输入处理 ================
static uint32_t drawSlotThumbnail(uint8_t slot, int x, int y) {
    char path[160];
    if (!romStorageMakeSlotThumbnailPath(gameSession.currentRomPath(), slot, path, sizeof(path))) return 0;
    if (!dijiStoragePathExists(path)) return 0;
    File file = dijiOpenStoragePath(path, FILE_READ);
    if (!file) return 0;
    struct Header { char magic[4]; uint16_t width; uint16_t height; uint32_t timestamp; } header;
    if (file.read((uint8_t*)&header, sizeof(header)) != sizeof(header) ||
        memcmp(header.magic, "DTHM", 4) != 0 || header.width != 160 || header.height != 120) {
        file.close();
        return 0;
    }
    uint16_t source[160];
    uint16_t row[80];
    for (int dy = 0; dy < 60; dy++) {
        file.seek(sizeof(header) + (dy * 2) * sizeof(source));
        if (file.read((uint8_t*)source, sizeof(source)) != sizeof(source)) break;
        for (int dx = 0; dx < 80; dx++) row[dx] = source[dx * 2];
        tft.pushImage(x, y + dy, 80, 1, row);
    }
    file.close();
    return header.timestamp;
}

static int runSaveSlotPicker(bool saving) {
    int selected = pauseSaveSlot;
    uint32_t lastInput = 0;
    auto draw = [&]() {
        tft.fillScreen(MENU_BG_COLOR);
        tft.setFont(&fonts::efontCN_14);
        tft.setTextSize(1);
        tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
        tft.setCursor(118, 18);
        tft.print(saving ? "选择保存槽" : "选择读取槽");
        for (int slot = 0; slot < 3; slot++) {
            int x = 14 + slot * 102;
            uint16_t border = slot == selected ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR;
            tft.drawRoundRect(x, 52, 90, 116, 6, border);
            tft.fillRect(x + 5, 58, 80, 60, TFT_BLACK);
            uint32_t savedAt = drawSlotThumbnail((uint8_t)slot, x + 5, 58);
            tft.setTextColor(slot == selected ? MENU_TITLE_COLOR : MENU_TEXT_COLOR, MENU_BG_COLOR);
            tft.setCursor(x + 30, 128);
            tft.printf("槽%d", slot + 1);
            char savePath[160];
            bool exists = romStorageMakeSlotSavePath(gameSession.currentRomPath(), slot,
                                                     savePath, sizeof(savePath)) &&
                          dijiStoragePathExists(savePath);
            tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
            tft.setCursor(x + 12, 148);
            if (exists && savedAt > 0) {
                time_t timestamp = savedAt;
                struct tm savedTime = {};
                if (localtime_r(&timestamp, &savedTime)) {
                    char text[16];
                    strftime(text, sizeof(text), "%m-%d %H:%M", &savedTime);
                    tft.print(text);
                } else tft.print("已有存档");
            } else tft.print(exists ? "已有存档" : "空槽");
        }
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        tft.setCursor(70, 202);
        tft.print("左右选择  A确认  B返回");
        tft.setFont(&fonts::Font0);
    };
    draw();
    while (true) {
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInput) >= BUTTON_DEBOUNCE) {
            if (buttons.LEFT) { selected = (selected + 2) % 3; draw(); lastInput = now; }
            else if (buttons.RIGHT) { selected = (selected + 1) % 3; draw(); lastInput = now; }
            else if (buttons.A || buttons.START) {
                waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
                return selected;
            } else if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                return -1;
            }
        }
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            for (int slot = 0; slot < 3; slot++) {
                if (touchPointInRect(point, 14 + slot * 102, 52, 90, 116)) return slot;
            }
        }
        delay(20);
    }
}

static void activatePauseMenuSelection() {
    if (pauseMenuIndex == 0) {
        clearScreenForGame();
        resetFrameScheduler(3);
        gameSession.resume();
        resumeFrameDisplayForGame();
        screenController.show(AppScreen::Playing);
    } else if (pauseMenuIndex == 1) {
        int chosenSlot = runSaveSlotPicker(true);
        if (chosenSlot < 0) { drawPauseMenu(); return; }
        pauseSaveSlot = (uint8_t)chosenSlot;
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(MENU_TITLE_COLOR);
        tft.setTextSize(2);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(104, 110);
        tft.print("保存中...");

        if (gameSession.saveSlot(pauseSaveSlot, displayPipeline.lastDisplayedBuffer())) {
            espNowHostSendRumble(0, 110, 120);
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(0x07E0);
            tft.setTextSize(2);
            tft.setCursor(92, 110);
            tft.print("保存成功");
            delay(1000);
        } else {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(0xF800);
            tft.setTextSize(2);
            tft.setCursor(92, 110);
            tft.print("保存失败");
            delay(1500);
        }
        tft.setFont(&fonts::Font0);

        clearScreenForGame();
        resetFrameScheduler(3);
        gameSession.resume();
        resumeFrameDisplayForGame();
        screenController.show(AppScreen::Playing);
    } else if (pauseMenuIndex == 2) {
        int chosenSlot = runSaveSlotPicker(false);
        if (chosenSlot < 0) { drawPauseMenu(); return; }
        pauseSaveSlot = (uint8_t)chosenSlot;
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(MENU_TITLE_COLOR);
        tft.setTextSize(2);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(104, 110);
        tft.print("读取中...");

        if (gameSession.loadSlot(pauseSaveSlot)) {
            rewindBuffer.clear();
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(0x07E0);
            tft.setTextSize(2);
            tft.setCursor(92, 110);
            tft.print("读取成功");
            delay(1000);
        } else {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(0xF800);
            tft.setTextSize(2);
            tft.setCursor(92, 110);
            tft.print("读取失败");
            tft.setTextColor(MENU_HINT_COLOR);
            tft.setTextSize(1);
            tft.setCursor(90, 140);
            tft.print("未找到存档");
            delay(1500);
        }
        tft.setFont(&fonts::Font0);

        clearScreenForGame();
        resetFrameScheduler(3);
        gameSession.resume();
        resumeFrameDisplayForGame();
        screenController.show(AppScreen::Playing);
    } else {
        returnToMainMenu();
    }
}

static bool handlePauseTouch() {
    if (!touchInputJustReleased()) {
        return false;
    }

    if (suppressNextPauseTouchRelease) {
        suppressNextPauseTouchRelease = false;
        return true;
    }

    TouchPoint point = touchInputReleasePoint();
    int menuWidth = PAUSE_MENU_WIDTH;
    int menuHeight = PAUSE_MENU_HEIGHT;
    int menuX = (320 - menuWidth) / 2;
    int menuY = (240 - menuHeight) / 2;
    int volumeY = menuY + PAUSE_VOLUME_Y;

    if (touchPointInRect(point, menuX + 72, volumeY,
                         PAUSE_VOLUME_BUTTON_W, PAUSE_VOLUME_BUTTON_H)) {
        setAudioVolumePercent(audioVolumePercent >= 10 ? audioVolumePercent - 10 : 0, true);
        drawPauseMenu();
        return true;
    }

    if (touchPointInRect(point, menuX + 198, volumeY,
                         PAUSE_VOLUME_BUTTON_W, PAUSE_VOLUME_BUTTON_H)) {
        setAudioVolumePercent(audioVolumePercent <= 90 ? audioVolumePercent + 10 : 100, true);
        drawPauseMenu();
        return true;
    }

    for (int i = 0; i < PAUSE_OPTION_COUNT; i++) {
        int optY = menuY + PAUSE_OPTION_START_Y + i * (PAUSE_OPTION_HEIGHT + PAUSE_OPTION_GAP);
        if (touchPointInRect(point, menuX + 18, optY, menuWidth - 36, PAUSE_OPTION_HEIGHT)) {
            pauseMenuIndex = i;
            drawPauseMenu();
            activatePauseMenuSelection();
            return true;
        }
    }

    return false;
}

static void enterPauseMenu() {
    screenController.show(AppScreen::Paused);
    pauseMenuIndex = 0;
    gameSession.pause();
    pauseFrameDisplayForUi();
    muteAudio();
    nes.flushBatterySave(true);
    suppressNextPauseTouchRelease = false;
    drawPauseMenu();
}

void handlePauseInput() {
    touchInputUpdate();
    unsigned long now = millis();
    updateButtons();
    handlePendingCommands();
    if (handlePauseTouch()) {
        lastButtonTime = now;
        return;
    }
    if (now - lastButtonTime < BUTTON_DEBOUNCE) return;

    bool buttonPressed = false;

    if (buttons.UP) {
        if (pauseMenuIndex > 0) {
            pauseMenuIndex--;
            buttonPressed = true;
        }
    }

    if (buttons.DOWN) {
        if (pauseMenuIndex < PAUSE_OPTION_COUNT - 1) {
            pauseMenuIndex++;
            buttonPressed = true;
        }
    }

    if (buttons.LEFT) {
        if (pauseMenuIndex == 1 || pauseMenuIndex == 2) {
            pauseSaveSlot = pauseSaveSlot == 0 ? 2 : pauseSaveSlot - 1;
        } else {
            setAudioVolumePercent(audioVolumePercent >= 10 ? audioVolumePercent - 10 : 0, true);
        }
        buttonPressed = true;
    }

    if (buttons.RIGHT) {
        if (pauseMenuIndex == 1 || pauseMenuIndex == 2) {
            pauseSaveSlot = (pauseSaveSlot + 1) % 3;
        } else {
            setAudioVolumePercent(audioVolumePercent <= 90 ? audioVolumePercent + 10 : 100, true);
        }
        buttonPressed = true;
    }

    if (buttons.A || buttons.START) {
        waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
        activatePauseMenuSelection();
        return;
    }

    // B 按钮也返回游戏
    if (buttons.B) {
        waitForButtonsReleased(DIJI_BTN_B);
        clearScreenForGame();
        resetFrameScheduler(3);
        gameSession.resume();  // 恢复音频
        resumeFrameDisplayForGame();
        screenController.show(AppScreen::Playing);
        return;
    }

    if (buttonPressed) {
        lastButtonTime = now;
        if (screenController.is(AppScreen::Paused)) {
            drawPauseMenu();
        }
    }
}
