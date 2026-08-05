// ================ 初始化函数 ================
void initializeSerial() {
#if ENABLE_DEBUG_SERIAL || DIJI_ENABLE_SERIAL_CONTROLLER
    Serial.begin(115200);
    delay(500);

#else
    (void)0;
#endif

}

void initializeScreen() {
    tft.init();
    tft.setRotation(effectiveDisplayRotation());
#if DIJI_LCD_DRIVER_ST7789
    tft.invertDisplay(displayColorInverted);
#endif
    touchInputSetDisplayRotation(effectiveDisplayRotation());
    if (DIJI_AMP_ENABLE_PIN >= 0) {
        pinMode(DIJI_AMP_ENABLE_PIN, OUTPUT);
        setAmplifierEnableLevel(DIJI_AMP_ENABLE_LEVEL);
    }
    if (DIJI_LCD_BL_PIN >= 0) {
        pinMode(DIJI_LCD_BL_PIN, OUTPUT);
        digitalWrite(DIJI_LCD_BL_PIN, HIGH);
    }
#if defined(DIJI_BOARD_WOKWI_LCDWIKI)
    tft.fillScreen(TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.setTextSize(2);
    tft.setFont(&fonts::efontCN_14);
    tft.setCursor(84, 100);
    tft.print("屏幕正常");
    tft.setFont(&fonts::Font0);
    delay(500);
#endif
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(0, 0);

}

static bool bootLogoPixelOn(int x, int y);

static uint16_t bootLogoPixelColor(int x, int y, uint16_t fallback = TFT_WHITE) {
    if (x < 0 || x >= DIJI_LOGO_W || y < 0 || y >= DIJI_LOGO_H) {
        return fallback;
    }
    int index = y * DIJI_LOGO_W + x;
    return pgm_read_word(DIJI_LOGO_RGB565 + index);
}

void drawBootLogo(int y) {
    const int logoX = (320 - DIJI_LOGO_W) / 2;
    for (int yy = 0; yy < DIJI_LOGO_H; yy++) {
        for (int xx = 0; xx < DIJI_LOGO_W; xx++) {
            if (bootLogoPixelOn(xx, yy)) {
                tft.drawPixel(logoX + xx, y + yy, bootLogoPixelColor(xx, yy));
            }
        }
    }

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setFont(&fonts::Font0);
    tft.setCursor(128, y + DIJI_LOGO_H + 12);
    tft.print("ESP32-S3 NES");
}

static bool bootLogoPixelOn(int x, int y) {
    if (x < 0 || x >= DIJI_LOGO_W || y < 0 || y >= DIJI_LOGO_H) {
        return false;
    }
    int byteIndex = y * ((DIJI_LOGO_W + 7) / 8) + (x >> 3);
    uint8_t mask = 0x80 >> (x & 7);
    return (pgm_read_byte(DIJI_LOGO_MASK + byteIndex) & mask) != 0;
}

static bool bootLogoBlockOn(int x, int y, int blockSize) {
    for (int yy = 0; yy < blockSize; yy++) {
        for (int xx = 0; xx < blockSize; xx++) {
            if (bootLogoPixelOn(x + xx, y + yy)) {
                return true;
            }
        }
    }
    return false;
}

static uint16_t bootLogoBlockColor(int x, int y, int blockSize) {
    for (int yy = 0; yy < blockSize; yy++) {
        for (int xx = 0; xx < blockSize; xx++) {
            int px = x + xx;
            int py = y + yy;
            if (bootLogoPixelOn(px, py)) {
                uint16_t color = bootLogoPixelColor(px, py);
                if (color != TFT_BLACK) {
                    return color;
                }
            }
        }
    }
    return TFT_WHITE;
}

static uint32_t bootHash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return value;
}

void playBootAnimation() {
    tft.fillScreen(TFT_BLACK);

    const int logoY = 74;
    const int logoX = (320 - DIJI_LOGO_W) / 2;
    const int blockSize = 3;
    const int frames = 30;

    for (int frame = 0; frame <= frames; frame++) {
        tft.fillScreen(TFT_BLACK);

        for (int by = 0; by < DIJI_LOGO_H; by += blockSize) {
            for (int bx = 0; bx < DIJI_LOGO_W; bx += blockSize) {
                if (!bootLogoBlockOn(bx, by, blockSize)) {
                    continue;
                }

                int targetX = logoX + bx;
                int targetY = logoY + by;
                uint32_t h = bootHash((uint32_t)bx * 131u + (uint32_t)by * 17u);
                int delayFrames = h % 9;
                int travelFrames = 14 + ((h >> 8) % 12);
                int localFrame = frame - delayFrames;

                if (localFrame < 0) {
                    if ((h & 0x03) != 0) {
                        continue;
                    }
                    localFrame = 0;
                }
                if (localFrame > travelFrames) {
                    localFrame = travelFrames;
                }

                int startX = (int)((h >> 16) % 380) - 30;
                int startY = (int)((h >> 1) % 300) - 30;
                int jitterX = (int)((h >> 24) & 0x0F) - 8;
                int jitterY = (int)((h >> 20) & 0x0F) - 8;

                int eased = localFrame * localFrame * (3 * travelFrames - 2 * localFrame);
                int denom = travelFrames * travelFrames * travelFrames;
                int x = startX + ((targetX - startX) * eased) / denom;
                int y = startY + ((targetY - startY) * eased) / denom;

                int remaining = travelFrames - localFrame;
                if (remaining > 4) {
                    x += (jitterX * remaining) / travelFrames;
                    y += (jitterY * remaining) / travelFrames;
                }

                int particleSize = 2 + ((h >> 28) & 0x03);
                if (localFrame == travelFrames) {
                    particleSize = blockSize;
                }
                tft.fillRect(x, y, particleSize, particleSize,
                             bootLogoBlockColor(bx, by, blockSize));
            }
        }

        wirelessManager.updateBootConnectivity();
        delay(24);
    }

    tft.fillScreen(TFT_BLACK);
    drawBootLogo(logoY);
    const uint32_t holdStartedMs = millis();
    while ((uint32_t)(millis() - holdStartedMs) < 1200) {
        wirelessManager.updateBootConnectivity();
        delay(20);
    }
}

static void drawMenuHeaderLogo() {
    constexpr int logoW = 98;
    constexpr int logoH = 32;
    constexpr int logoX = (320 - logoW) / 2;
    constexpr int logoY = 2;

    for (int y = 0; y < logoH; y++) {
        int sourceY = (y * DIJI_LOGO_H) / logoH;
        for (int x = 0; x < logoW; x++) {
            int sourceX = (x * DIJI_LOGO_W) / logoW;
            if (bootLogoPixelOn(sourceX, sourceY)) {
                tft.drawPixel(logoX + x, logoY + y,
                              bootLogoPixelColor(sourceX, sourceY, MENU_TITLE_COLOR));
            }
        }
    }
}

static void handleSerialControllerCommand(SerialControllerCommand command) {
    switch (command) {
        case SerialControllerCommand::AudioTest:
            pendingAudioTest = true;
            break;
        case SerialControllerCommand::ToggleDisplayMode:
            pendingDisplayToggle = true;
            break;
        case SerialControllerCommand::TouchCalibration:
            pendingTouchCalibration = true;
            break;
        case SerialControllerCommand::None:
        default:
            break;
    }
}

static void handlePendingCommands() {
    if (pendingDisplayToggle) {
        pendingDisplayToggle = false;
        displayStretchFullscreen = !displayStretchFullscreen;
        if (screenController.is(AppScreen::Playing)) {
            if (!displayStretchFullscreen) {
                displayPipeline.requestNativeBorderClear();
            }
            resetFrameScheduler(3);
        } else if (screenController.is(AppScreen::Home)) {
            drawHomeClock(true);
        } else {
            drawMainMenu();
        }
        Serial.printf("Display mode: %s\n", displayStretchFullscreen ? "fullscreen" : "native");
    }

    if (pendingAudioTest) {
        pendingAudioTest = false;
        playAudioTestTone();
    }

    if (pendingTouchCalibration) {
        pendingTouchCalibration = false;
        if (screenController.is(AppScreen::Playing)) {
            Serial.println("Touch calibration is only available from menu or pause");
        } else {
            runTouchCalibration();
            if (screenController.is(AppScreen::Paused)) {
                drawPauseMenu();
            } else if (screenController.is(AppScreen::Home)) {
                drawHomeClock(true);
            } else {
                drawMainMenu();
            }
        }
    }
}

// 在 loop() 中调用：检查帧完成并入队
static bool tryEnqueueFrame() {
    PPU& ppu = nes.getPPU();
    if (!ppu.frameReady) return false;

    if (displayPipeline.paused()) {
        ppu.frameReady = false;
        return false;
    }

    // 如果本帧没有渲染（跳帧），不入队，直接清除标志
    if (!ppu.renderedThisFrame) {
        ppu.frameReady = false;
        return false;
    }

    drawTouchControlsOverlay(displayPipeline.renderBuffer(), buttonsToMask());
    // 如果本帧有实际渲染，发送当前渲染缓冲并切换到另一个用于下一帧渲染
    if (displayPipeline.enqueueCurrentFrame()) {
        ppu.frameBuffer = displayPipeline.renderBuffer();
        ppu.frameReady = false;
        gameSession.noteRenderedFrame();
        return true;
    }
    return false;
}

static void pauseFrameDisplayForUi() {
    if (nes.getPPU().frameReady) {
        nes.getPPU().frameReady = false;
    }
    displayPipeline.pause();
}

static void resumeFrameDisplayForGame() {
    displayPipeline.resume();
}

// 行级帧调度（方法1使用）
void runFrame() {
    const int SCANLINES_PER_FRAME = 262;
    for (int i = 0; i < SCANLINES_PER_FRAME; ++i) {
        nes.stepScanline();
    }
}

void initializeButtons() {
    auto setupButton = [](int pin) {
        if (pin >= 0) {
            pinMode(pin, INPUT_PULLUP);
        }
    };

    setupButton(A_BUTTON);
    setupButton(B_BUTTON);
    setupButton(LEFT_BUTTON);
    setupButton(RIGHT_BUTTON);
    setupButton(UP_BUTTON);
    setupButton(DOWN_BUTTON);
    setupButton(START_BUTTON);
    setupButton(SELECT_BUTTON);
}

static void initializeBuiltinStorage() {
    Serial.println("ROMFS mounting...");
    // The complete first-flash image installs the partition table and app but
    // does not carry a multi-megabyte LittleFS payload. A new romfs partition
    // therefore has to be formatted on its first mount before the store can
    // create its index and download directories.
    builtinStorageAvailable = DIJI_ROMFS.begin(
        true, DIJI_ROMFS_BASE_PATH, 10, DIJI_ROMFS_PARTITION_LABEL);
    Serial.printf("ROMFS %s\n", builtinStorageAvailable ? "ready" : "not available");
    if (builtinStorageAvailable) {
        size_t total = DIJI_ROMFS.totalBytes();
        size_t used = DIJI_ROMFS.usedBytes();
        Serial.printf("ROMFS space: total=%u used=%u free=%u\n",
                      (unsigned)total,
                      (unsigned)used,
                      (unsigned)(total > used ? total - used : 0));
    }
}

static void initializeTouch() {
    if (touchInputBegin()) {
        Serial.println("Touch input enabled");
    } else {
        Serial.println("Touch input disabled");
    }
}

static void initializeEspNowGamepads() {
    wirelessManager.startGamepads();
}

static void drawTouchCalibrationTarget(const TouchPoint& target, int index, int count) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(MENU_TITLE_COLOR, TFT_BLACK);
    tft.setTextSize(1);
    tft.setFont(&fonts::efontCN_14);
    tft.setCursor(92, 16);
    tft.print("触摸校准");
    tft.setCursor(86, 36);
    tft.print("请点击十字中心");
    tft.setFont(&fonts::Font0);

    tft.drawCircle(target.x, target.y, 12, 0xFFFF);
    tft.drawCircle(target.x, target.y, 6, 0xFFFF);
    tft.drawFastHLine(target.x - 18, target.y, 36, 0xF800);
    tft.drawFastVLine(target.x, target.y - 18, 36, 0xF800);

    tft.setTextColor(MENU_HINT_COLOR, TFT_BLACK);
    tft.setCursor(132, 222);
    tft.printf("%d/%d", index + 1, count);
}

static bool waitForRawTouchSample(TouchPoint* sample) {
    if (!sample || !touchInputAvailable()) {
        return false;
    }

    TouchRawPoint raw;
    uint32_t start = millis();
    while (touchInputReadRaw(&raw)) {
        settingsWebServerLoop();
        if ((uint32_t)(millis() - start) > 1500) {
            break;
        }
        delay(10);
    }

    start = millis();
    while ((uint32_t)(millis() - start) < 15000) {
        settingsWebServerLoop();
        if (touchInputReadRaw(&raw)) {
            sample->x = (int16_t)raw.x;
            sample->y = (int16_t)raw.y;
            Serial.printf("Touch calibration raw: %u,%u\n", raw.x, raw.y);
            while (touchInputReadRaw(&raw)) {
                settingsWebServerLoop();
                delay(10);
            }
            delay(160);
            return true;
        }
        delay(10);
    }

    return false;
}

static bool runTouchCalibration() {
    if (!touchInputAvailable()) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(0xF800, TFT_BLACK);
        tft.setTextSize(2);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(92, 102);
        tft.print("未检测到触摸");
        tft.setFont(&fonts::Font0);
        delay(1200);
        return false;
    }

    bool wasRunning = gameSession.running();
    gameSession.pause();
    muteAudio();

    constexpr int targetCount = 4;
    const TouchPoint screenTargets[targetCount] = {
        {24, 24},
        {295, 24},
        {24, 215},
        {295, 215},
    };
    TouchPoint rawSamples[targetCount];

    for (int i = 0; i < targetCount; i++) {
        drawTouchCalibrationTarget(screenTargets[i], i, targetCount);
        if (!waitForRawTouchSample(&rawSamples[i])) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(0xF800, TFT_BLACK);
            tft.setTextSize(2);
            tft.setFont(&fonts::efontCN_14);
            tft.setCursor(96, 102);
            tft.print("校准超时");
            tft.setFont(&fonts::Font0);
            delay(1200);
            if (wasRunning && screenController.is(AppScreen::Playing)) gameSession.resume(); else gameSession.pause();
            return false;
        }
    }

    TouchCalibration calibration;
    TouchPoint calibrationTargets[targetCount];
    for (int i = 0; i < targetCount; i++) {
        calibrationTargets[i] = screenTargets[i];
        if (displayRotate180) {
            calibrationTargets[i].x = 319 - calibrationTargets[i].x;
            calibrationTargets[i].y = 239 - calibrationTargets[i].y;
        }
    }
    bool ok = buildTouchCalibration(rawSamples[0], rawSamples[1], rawSamples[2],
                                    calibrationTargets[0], calibrationTargets[1],
                                    calibrationTargets[2],
                                    &calibration);
    ok = ok && touchInputSetCalibration(calibration);

    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);
    if (ok) {
        TouchPoint check = applyTouchCalibration((uint16_t)rawSamples[3].x,
                                                (uint16_t)rawSamples[3].y,
                                                calibration, 320, 240);
        if (displayRotate180) {
            check.x = 319 - check.x;
            check.y = 239 - check.y;
        }
        int errorX = check.x - screenTargets[3].x;
        int errorY = check.y - screenTargets[3].y;
        if (errorX < 0) errorX = -errorX;
        if (errorY < 0) errorY = -errorY;
        Serial.printf("Touch calibration saved, BR error=%d,%d\n", errorX, errorY);
        tft.setTextColor(0x07E0, TFT_BLACK);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(96, 92);
        tft.print("校准已保存");
        tft.setTextColor(MENU_HINT_COLOR, TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(98, 126);
        tft.printf("检查误差: %d,%d", errorX, errorY);
        tft.setFont(&fonts::Font0);
    } else {
        Serial.println("Touch calibration failed");
        tft.setTextColor(0xF800, TFT_BLACK);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(96, 102);
        tft.print("校准失败");
        tft.setFont(&fonts::Font0);
    }
    delay(1500);

    if (wasRunning && screenController.is(AppScreen::Playing)) gameSession.resume(); else gameSession.pause();
    return ok;
}

static void runInitialTouchCalibrationIfNeeded() {
    bool available = touchInputAvailable();
    bool calibrated = touchInputHasCalibration();
    Serial.printf("Boot touch check: available=%d calibrated=%d\n",
                  available ? 1 : 0, calibrated ? 1 : 0);

    if (calibrated) {
        return;
    }

    if (!available) {
        Serial.println("Touch not ready at boot, retrying before calibration");
        for (int i = 0; i < 3 && !touchInputAvailable(); i++) {
            delay(250);
            initializeTouch();
        }
        available = touchInputAvailable();
        Serial.printf("Boot touch retry result: available=%d\n", available ? 1 : 0);
    }

    if (!available) {
        Serial.println("No touch controller detected; skipping calibration");
        return;
    }

    Serial.println("No saved touch calibration, starting calibration");
    runTouchCalibration();
}

static void drawGamepadPairingMenu(int selected) {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(116, 10);
    tft.print("手柄配对");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    char mac[24];
    const int cardX = 32;
    const int cardW = 256;
    const int cardH = 52;
    for (int i = 0; i < 2; i++) {
        int y = 56 + i * 64;
        if (i == selected) {
            tft.fillRect(cardX, y, cardW, cardH, MENU_HIGHLIGHT_BG);
        } else {
            tft.fillRect(cardX, y, cardW, cardH, MENU_PANEL_COLOR);
        }
        uint16_t fill = i == selected ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
        tft.drawRect(cardX, y, cardW, cardH, i == selected ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR);
        tft.drawRect(cardX + 1, y + 1, cardW - 2, cardH - 2, i == selected ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR);
        tft.setTextColor(i == selected ? MENU_TITLE_COLOR : MENU_TEXT_COLOR, fill);
        tft.setCursor(cardX + 18, y + 10);
        tft.printf("配对 P%d", i + 1);
        tft.setTextColor(MENU_HINT_COLOR, fill);
        tft.setCursor(cardX + 18, y + 30);
        if (espNowHostGetPairedMacString((uint8_t)i, mac, sizeof(mac))) {
            tft.print(mac);
        } else {
            tft.print("未配对");
        }
    }

    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(48, 176);
    tft.printf("信道 CH%u  WiFi:%s", (unsigned)espNowHostCurrentChannel(),
               wirelessManager.wifiActive() ? "ON" : "OFF");
    tft.setCursor(48, 194);
    tft.print("A 配对 SELECT震动 START通信");

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(144, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void drawGamepadLatencyTest(uint8_t player,
                                   const EspNowLatencyTestResult& result) {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(102, 10);
    tft.printf("P%d 手柄通信测试", player + 1);
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(42, 54);
    tft.printf("进度：%u / 20", (unsigned)result.sent);
    tft.setCursor(42, 78);
    tft.printf("收到：%u  丢包：%u",
               (unsigned)result.received,
               (unsigned)(result.sent - result.received -
                          (result.running && result.sent > result.received ? 1 : 0)));
    tft.setCursor(42, 102);
    tft.printf("当前：%u ms", (unsigned)result.currentMs);
    tft.setCursor(42, 126);
    tft.printf("最低：%u  平均：%u  最高：%u ms",
               (unsigned)result.minMs, (unsigned)result.averageMs,
               (unsigned)result.maxMs);

    tft.setCursor(42, 158);
    if (result.running) {
        tft.setTextColor(MENU_ACCENT_COLOR, MENU_BG_COLOR);
        tft.print("正在测试，请保持手柄在线");
    } else if (!result.complete || result.received == 0) {
        tft.setTextColor(0xF800, MENU_BG_COLOR);
        tft.print("测试失败：手柄无响应");
    } else {
        uint8_t lost = result.sent - result.received;
        bool good = result.averageMs <= 30 && result.maxMs <= 80 && lost <= 1;
        tft.setTextColor(good ? 0x07E0 : 0xFD20, MENU_BG_COLOR);
        tft.print(good ? "通信正常" : "延迟或丢包偏高");
    }

    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(42, 184);
    tft.print("参考：平均≤30ms，最高≤80ms");

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H,
                 MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(144, MENU_SOFTBAR_Y + 9);
    tft.print(result.running ? "取消" : "返回");
    tft.setFont(&fonts::Font0);
}

static void runGamepadLatencyTest(uint8_t player) {
    if (!espNowHostStartLatencyTest(player, 20)) {
        EspNowLatencyTestResult unavailable;
        drawGamepadLatencyTest(player, unavailable);
        delay(1500);
        return;
    }

    uint32_t lastDrawMs = 0;
    bool resultLogged = false;
    while (true) {
        settingsWebServerLoop();
        espNowHostUpdate();
        updateButtons();
        touchInputUpdate();

        EspNowLatencyTestResult result;
        espNowHostGetLatencyTestResult(player, result);
        uint32_t now = millis();
        if (lastDrawMs == 0 || (uint32_t)(now - lastDrawMs) >= 100) {
            lastDrawMs = now;
            drawGamepadLatencyTest(player, result);
        }

        if (!result.running && result.complete && !resultLogged) {
            resultLogged = true;
            Serial.printf("手柄 P%d 通信测试完成：发送=%u 收到=%u "
                          "最低=%ums 平均=%ums 最高=%ums\n",
                          player + 1, (unsigned)result.sent,
                          (unsigned)result.received, (unsigned)result.minMs,
                          (unsigned)result.averageMs, (unsigned)result.maxMs);
        }
        if (buttons.B || (!result.running && result.complete &&
                          (buttons.A || buttons.START || buttons.SELECT))) {
            if (result.running) espNowHostCancelLatencyTest(player);
            return;
        }
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y,
                                 320, MENU_SOFTBAR_HEIGHT)) {
                if (result.running) espNowHostCancelLatencyTest(player);
                return;
            }
        }
        delay(10);
    }
}

static void runGamepadPairingSlot(uint8_t player) {
    constexpr uint32_t PAIRING_MS = 15000;
    espNowHostStartPairing(player, PAIRING_MS);
    uint32_t start = millis();
    uint32_t lastDraw = 0;

    while (espNowHostPairingActive()) {
        settingsWebServerLoop();
        uint32_t now = millis();
        if (lastDraw == 0 || (uint32_t)(now - lastDraw) >= 250) {
            lastDraw = now;
            int remaining = (int)((PAIRING_MS - (uint32_t)(now - start) + 999) / 1000);
            if (remaining < 0) remaining = 0;
            tft.fillScreen(MENU_BG_COLOR);
            tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
            tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
            tft.setFont(&fonts::efontCN_14);
            tft.setTextSize(1);
            tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
            tft.setCursor(116, 10);
            tft.print("手柄配对");
            tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);
            tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
            tft.setCursor(102, 58);
            tft.printf("正在配对 P%d", player + 1);
            tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
            tft.setCursor(42, 96);
            tft.print("请长按手柄配对键");
            tft.setCursor(106, 126);
            tft.printf("剩余 %d 秒", remaining);
            tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
            tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
            tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
            tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
            tft.setCursor(144, MENU_SOFTBAR_Y + 9);
            tft.print("取消");
            tft.setFont(&fonts::Font0);
        }

        espNowHostUpdate();
        updateButtons();
        touchInputUpdate();
        if (buttons.B) {
            waitForButtonsReleased(DIJI_BTN_B);
            espNowHostCancelPairing();
            break;
        }
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT)) {
                espNowHostCancelPairing();
                break;
            }
        }
        delay(20);
    }

    tft.fillScreen(MENU_BG_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    if (espNowHostPairingSucceeded()) {
        char mac[24];
        tft.setTextColor(0x07E0, MENU_BG_COLOR);
        tft.setCursor(104, 78);
        tft.printf("P%d 已配对", player + 1);
        if (espNowHostGetPairedMacString(player, mac, sizeof(mac))) {
            tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
            tft.setCursor(42, 112);
            tft.print(mac);
        }
        espNowHostSendRumble(player, 140, 220);
    } else {
        tft.setTextColor(0xF800, MENU_BG_COLOR);
        tft.setCursor(110, 88);
        tft.print("配对超时");
    }
    tft.setFont(&fonts::Font0);
    delay(1200);
}

static void runGamepadPairingMenu() {
    int selected = 0;
    uint32_t lastInputMs = 0;
    drawGamepadPairingMenu(selected);

    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();

        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.UP && selected > 0) {
                selected--;
                lastInputMs = now;
                drawGamepadPairingMenu(selected);
            } else if (buttons.DOWN && selected < 1) {
                selected++;
                lastInputMs = now;
                drawGamepadPairingMenu(selected);
            } else if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                return;
            } else if (buttons.SELECT) {
                espNowHostSendRumble((uint8_t)selected, 160, 300);
                lastInputMs = now;
            } else if (buttons.START) {
                runGamepadLatencyTest((uint8_t)selected);
                drawGamepadPairingMenu(selected);
                lastInputMs = now;
            } else if (buttons.A) {
                runGamepadPairingSlot((uint8_t)selected);
                drawGamepadPairingMenu(selected);
                lastInputMs = now;
            }
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT)) {
                return;
            }
            for (int i = 0; i < 2; i++) {
                int y = 56 + i * 64;
                if (touchPointInRect(point, 32, y, 256, 52)) {
                    selected = i;
                    runGamepadPairingSlot((uint8_t)i);
                    drawGamepadPairingMenu(selected);
                    break;
                }
            }
        }
        delay(30);
    }
}

static void drawWifiProvisioningScreen() {
    WifiProvisioningStatus wifiStatus = wifiProvisioningStatus();

    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(112, 10);
    tft.print("网络配置");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(26, 54);
    tft.print("请用手机连接热点");

    tft.fillRect(24, 78, 272, 34, MENU_PANEL_COLOR);
    tft.drawRect(24, 78, 272, 34, MENU_ACCENT_COLOR);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_PANEL_COLOR);
    tft.setCursor(78, 88);
    tft.print(wifiStatus.apSsid.length() ? wifiStatus.apSsid : "GameBox-XXXXXX");

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(26, 128);
    tft.print("然后打开浏览器访问");

    tft.fillRect(24, 150, 272, 28, MENU_PANEL_COLOR);
    tft.drawRect(24, 150, 272, 28, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_ACCENT_COLOR, MENU_PANEL_COLOR);
    tft.setCursor(88, 158);
    tft.print("http://");
    tft.print(wifiStatus.ip.length() ? wifiStatus.ip : "192.168.4.1");

    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(26, 190);
    tft.print(wifiStatus.message.length() ? wifiStatus.message : "等待手机提交 WiFi 信息");

    if (wifiStatus.state == WifiProvisioningState::Connecting) {
        tft.setTextColor(MENU_ACCENT_COLOR, MENU_BG_COLOR);
        tft.setCursor(26, 204);
        tft.print("SSID: ");
        drawClippedText(wifiStatus.selectedSsid, 72, 204, 210);
    } else if (wifiStatus.state == WifiProvisioningState::Connected) {
        tft.setTextColor(0x07E0, MENU_BG_COLOR);
        tft.setCursor(26, 204);
        tft.print("已连接，点返回结束配网");
    } else if (wifiStatus.state == WifiProvisioningState::Failed) {
        tft.setTextColor(0xF800, MENU_BG_COLOR);
        tft.setCursor(26, 204);
        tft.print("连接失败，可在手机上重试");
    }

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(144, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void runWifiProvisioningMenu() {
    wirelessManager.prepareForExternalWifiChange();
    wifiProvisioningBegin();
    drawWifiProvisioningScreen();

    uint32_t lastDrawMs = 0;
    uint32_t lastInputMs = 0;
    while (true) {
        wifiProvisioningLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();

        if ((uint32_t)(now - lastDrawMs) >= 500) {
            lastDrawMs = now;
            drawWifiProvisioningScreen();
        }

        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                break;
            }
            lastInputMs = now;
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT)) {
                break;
            }
        }

        delay(20);
    }

    wifiProvisioningStop();
    wirelessManager.reconcileAfterExternalWifiChange();
}

// Modal settings and store screens share this translation unit so they can use
// the existing hardware/UI callbacks, while their implementation lives beside
// the corresponding Screen state classes.
#include "settings_screen_impl.inc"
#include "store_screen_impl.inc"

static uint8_t gamepadStatusCode(uint8_t player) {
    bool paired = espNowHostHasPairedController(player);
    bool connected = espNowHostControllerConnected(player);
    if (connected) return 2;
    if (paired) return 1;
    return 0;
}

static void drawGamepadStatusBadges(bool force) {
    static uint8_t lastStatus[2] = {255, 255};
    static uint8_t lastBattery[2] = {255, 255};
    static uint16_t lastLatency[2] = {UINT16_MAX, UINT16_MAX};
    static bool lastLatencyValid[2] = {false, false};
    static bool lowBatteryRumbled[2] = {false, false};
    static uint32_t lastCheckMs = 0;

    espNowHostUpdate();

    uint32_t now = millis();
    if (!force && (uint32_t)(now - lastCheckMs) < 250) {
        return;
    }
    lastCheckMs = now;

    uint8_t status[2] = {
        gamepadStatusCode(0),
        gamepadStatusCode(1),
    };
    uint8_t battery[2] = {255, 255};
    uint16_t latency[2] = {0, 0};
    bool latencyValid[2] = {
        espNowHostGetRealtimeLatency(0, latency[0]),
        espNowHostGetRealtimeLatency(1, latency[1]),
    };
    espNowHostGetBatteryPercent(0, battery[0]);
    espNowHostGetBatteryPercent(1, battery[1]);
    if (!force && status[0] == lastStatus[0] && status[1] == lastStatus[1] &&
        battery[0] == lastBattery[0] && battery[1] == lastBattery[1] &&
        latencyValid[0] == lastLatencyValid[0] &&
        latencyValid[1] == lastLatencyValid[1] &&
        (!latencyValid[0] || latency[0] == lastLatency[0]) &&
        (!latencyValid[1] || latency[1] == lastLatency[1])) {
        return;
    }

    tft.setFont(&fonts::Font0);
    tft.setTextSize(1);

    auto drawBadge = [](int x, int y, const char* label, uint8_t statusCode,
                        bool latencyIsValid, uint16_t latencyMs) {
        uint16_t color = statusCode == 2 ? 0x07E0 : (statusCode == 1 ? 0xFD20 : MENU_HINT_COLOR);

        tft.fillRect(x, y, 92, 12, MENU_HEADER_COLOR);
        tft.drawRect(x, y, 92, 12, color);
        tft.setTextColor(color, MENU_HEADER_COLOR);
        tft.setCursor(x + 4, y + 2);
        tft.print(label);
        tft.print(statusCode == 2 ? ":ON " : (statusCode == 1 ? ":--" : ":NO"));
        if (statusCode == 2) {
            if (latencyIsValid) {
                uint16_t latencyColor =
                    latencyMs <= 30 ? 0x07E0 :
                    (latencyMs <= 60 ? 0xFFE0 :
                     (latencyMs <= 100 ? 0xFD20 : 0xF800));
                tft.setTextColor(latencyColor, MENU_HEADER_COLOR);
                tft.printf("%ums", (unsigned)latencyMs);
            } else {
                tft.setTextColor(MENU_HINT_COLOR, MENU_HEADER_COLOR);
                tft.print("--ms");
            }
        }
    };

    drawBadge(4, 4, "P1", status[0], latencyValid[0], latency[0]);
    drawBadge(224, 4, "P2", status[1], latencyValid[1], latency[1]);
    for (uint8_t player = 0; player < 2; player++) {
        if (battery[player] <= 15 && !lowBatteryRumbled[player]) {
            espNowHostSendRumble(player, 80, 250);
            lowBatteryRumbled[player] = true;
        } else if (battery[player] > 20) {
            lowBatteryRumbled[player] = false;
        }
        lastBattery[player] = battery[player];
        lastLatency[player] = latency[player];
        lastLatencyValid[player] = latencyValid[player];
    }
    lastStatus[0] = status[0];
    lastStatus[1] = status[1];
}

void updateButtons() {
#if DIJI_ENABLE_SERIAL_CONTROLLER
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r') {
            continue;
        }
        if (c == '\n') {
            serialControllerLine[serialControllerLineLen] = '\0';
            uint8_t parsedState = 0;
            if (parseSerialControllerLine(serialControllerLine, &parsedState)) {
                serialControllerState = parsedState;
                serialControllerLastMs = millis();
            } else {
                handleSerialControllerCommand(parseSerialControllerCommand(serialControllerLine));
            }
            serialControllerLineLen = 0;
            continue;
        }
        if (serialControllerLineLen < sizeof(serialControllerLine) - 1) {
            serialControllerLine[serialControllerLineLen++] = c;
        } else {
            serialControllerLineLen = 0;
        }
    }
#endif

    auto readButton = [](int pin) -> uint8_t {
        return (pin >= 0) ? !digitalRead(pin) : 0;
    };

    // 直接读取按键状态 (INPUT_PULLUP: 按下=0, 松开=1)
    buttons.A      = readButton(A_BUTTON);
    buttons.B      = readButton(B_BUTTON);
    buttons.LEFT   = readButton(LEFT_BUTTON);
    buttons.RIGHT  = readButton(RIGHT_BUTTON);
    buttons.UP     = readButton(UP_BUTTON);
    buttons.DOWN   = readButton(DOWN_BUTTON);
    buttons.START  = readButton(START_BUTTON);
    buttons.SELECT = readButton(SELECT_BUTTON);

#if DIJI_ENABLE_SERIAL_CONTROLLER
    if (serialControllerLastMs != 0 &&
        (uint32_t)(millis() - serialControllerLastMs) <= SERIAL_CONTROLLER_TIMEOUT_MS) {
        buttons.A      |= (serialControllerState & DIJI_BTN_A) ? 1 : 0;
        buttons.B      |= (serialControllerState & DIJI_BTN_B) ? 1 : 0;
        buttons.SELECT |= (serialControllerState & DIJI_BTN_SELECT) ? 1 : 0;
        buttons.START  |= (serialControllerState & DIJI_BTN_START) ? 1 : 0;
        buttons.UP     |= (serialControllerState & DIJI_BTN_UP) ? 1 : 0;
        buttons.DOWN   |= (serialControllerState & DIJI_BTN_DOWN) ? 1 : 0;
        buttons.LEFT   |= (serialControllerState & DIJI_BTN_LEFT) ? 1 : 0;
        buttons.RIGHT  |= (serialControllerState & DIJI_BTN_RIGHT) ? 1 : 0;
    }
#endif

    wirelessManager.update();
    espNowHostUpdate();
    uint8_t espP1 = espNowHostGetControllerState(0);
    uint8_t espUi = espP1;
    if (!screenController.is(AppScreen::Playing)) {
        espUi |= espNowHostGetControllerState(1);
    }
    buttons.A      |= (espUi & DIJI_BTN_A) ? 1 : 0;
    buttons.B      |= (espUi & DIJI_BTN_B) ? 1 : 0;
    buttons.SELECT |= (espUi & DIJI_BTN_SELECT) ? 1 : 0;
    buttons.START  |= (espUi & DIJI_BTN_START) ? 1 : 0;
    buttons.UP     |= (espUi & DIJI_BTN_UP) ? 1 : 0;
    buttons.DOWN   |= (espUi & DIJI_BTN_DOWN) ? 1 : 0;
    buttons.LEFT   |= (espUi & DIJI_BTN_LEFT) ? 1 : 0;
    buttons.RIGHT  |= (espUi & DIJI_BTN_RIGHT) ? 1 : 0;

    if (buttonsToMask() != 0) {
        powerManagerNotifyActivity();
    }
    if (powerManagerAutoShutdownDue()) {
        enterSoftwarePowerOff(true);
    }
}

static uint8_t buttonsToMask() {
    uint8_t state = 0;
    if (buttons.A) state |= DIJI_BTN_A;
    if (buttons.B) state |= DIJI_BTN_B;
    if (buttons.SELECT) state |= DIJI_BTN_SELECT;
    if (buttons.START) state |= DIJI_BTN_START;
    if (buttons.UP) state |= DIJI_BTN_UP;
    if (buttons.DOWN) state |= DIJI_BTN_DOWN;
    if (buttons.LEFT) state |= DIJI_BTN_LEFT;
    if (buttons.RIGHT) state |= DIJI_BTN_RIGHT;
    return state;
}

static void waitForButtonsReleased(uint8_t mask) {
    delay(100);
    do {
        updateButtons();
        delay(10);
    } while ((buttonsToMask() & mask) != 0);
    delay(50);
}

static uint16_t blendRgb565(uint16_t base, uint16_t overlay, uint8_t alpha) {
    uint8_t inv = 255 - alpha;
    uint8_t br = (uint8_t)(((base >> 11) & 0x1F) << 3);
    uint8_t bg = (uint8_t)(((base >> 5) & 0x3F) << 2);
    uint8_t bb = (uint8_t)((base & 0x1F) << 3);
    uint8_t orr = (uint8_t)(((overlay >> 11) & 0x1F) << 3);
    uint8_t og = (uint8_t)(((overlay >> 5) & 0x3F) << 2);
    uint8_t ob = (uint8_t)((overlay & 0x1F) << 3);
    uint8_t r = (uint8_t)((br * inv + orr * alpha) / 255);
    uint8_t g = (uint8_t)((bg * inv + og * alpha) / 255);
    uint8_t b = (uint8_t)((bb * inv + ob * alpha) / 255);
    return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static bool screenToFramePoint(int sx, int sy, int* fx, int* fy) {
    if (!fx || !fy || sy < 0 || sy >= SCREEN_HEIGHT) {
        return false;
    }

    int mappedX = (sx * SCREEN_WIDTH) / 320;
    if (!displayStretchFullscreen) {
        mappedX = sx - TFT_OFFSET_X;
    }
    if (mappedX < 0 || mappedX >= SCREEN_WIDTH) {
        return false;
    }

    *fx = mappedX;
    *fy = sy;
    return true;
}

static void blendFbPixel(uint16_t* fb, int fx, int fy, uint16_t color, uint8_t alpha) {
    if (!fb || fx < 0 || fx >= SCREEN_WIDTH || fy < 0 || fy >= SCREEN_HEIGHT) {
        return;
    }
    uint16_t* pixel = fb + fy * SCREEN_WIDTH + fx;
    *pixel = blendRgb565(*pixel, color, alpha);
}

static void blendFbPointFromScreen(uint16_t* fb, int sx, int sy,
                                   uint16_t color, uint8_t alpha) {
    if (!fb) {
        return;
    }

    int fx = 0;
    int fy = 0;
    if (!screenToFramePoint(sx, sy, &fx, &fy)) {
        return;
    }
    blendFbPixel(fb, fx, fy, color, alpha);
    blendFbPixel(fb, fx + 1, fy, color, alpha > 24 ? alpha - 18 : alpha);
    blendFbPixel(fb, fx, fy + 1, color, alpha > 24 ? alpha - 18 : alpha);
}

static void blendFbRectFromScreen(uint16_t* fb, int sx, int sy, int sw, int sh,
                                  uint16_t color, uint8_t alpha) {
    if (!fb) {
        return;
    }

    int x0 = (sx * SCREEN_WIDTH) / 320;
    int x1 = ((sx + sw) * SCREEN_WIDTH + 319) / 320;
    if (!displayStretchFullscreen) {
        x0 = sx - TFT_OFFSET_X;
        x1 = sx + sw - TFT_OFFSET_X;
    }
    if (x0 < 0) x0 = 0;
    if (x1 > SCREEN_WIDTH) x1 = SCREEN_WIDTH;
    if (sy < 0) sy = 0;
    int y1 = sy + sh;
    if (y1 > SCREEN_HEIGHT) y1 = SCREEN_HEIGHT;
    if (x0 >= x1 || sy >= y1) {
        return;
    }

    for (int y = sy; y < y1; y++) {
        uint16_t* row = fb + y * SCREEN_WIDTH;
        for (int x = x0; x < x1; x++) {
            row[x] = blendRgb565(row[x], color, alpha);
        }
    }
}

static void drawFbLineFromScreen(uint16_t* fb, int x0, int y0, int x1, int y1,
                                 uint16_t color, uint8_t alpha) {
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        blendFbPointFromScreen(fb, x0, y0, color, alpha);
        int e2 = 2 * err;
        if (x0 == x1 && y0 == y1) {
            break;
        }
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static void drawFbCircleFromScreen(uint16_t* fb, int cx, int cy, int radius,
                                   uint16_t color, uint8_t alpha) {
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        blendFbPointFromScreen(fb, cx + x, cy + y, color, alpha);
        blendFbPointFromScreen(fb, cx + y, cy + x, color, alpha);
        blendFbPointFromScreen(fb, cx - y, cy + x, color, alpha);
        blendFbPointFromScreen(fb, cx - x, cy + y, color, alpha);
        blendFbPointFromScreen(fb, cx - x, cy - y, color, alpha);
        blendFbPointFromScreen(fb, cx - y, cy - x, color, alpha);
        blendFbPointFromScreen(fb, cx + y, cy - x, color, alpha);
        blendFbPointFromScreen(fb, cx + x, cy - y, color, alpha);

        if (err <= 0) {
            y++;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

static void drawFbSoftCircleFromScreen(uint16_t* fb, int cx, int cy, int radius,
                                       uint16_t color, uint8_t alpha) {
    drawFbCircleFromScreen(fb, cx, cy, radius, color, alpha);
    drawFbCircleFromScreen(fb, cx, cy, radius - 1, color, alpha > 20 ? alpha - 18 : alpha);
}

static void drawFbPillOutlineFromScreen(uint16_t* fb, int x, int y, int w, int h,
                                        uint16_t color, uint8_t alpha) {
    int r = h / 2;
    drawFbLineFromScreen(fb, x + r, y, x + w - r, y, color, alpha);
    drawFbLineFromScreen(fb, x + r, y + h, x + w - r, y + h, color, alpha);
    drawFbCircleFromScreen(fb, x + r, y + r, r, color, alpha);
    drawFbCircleFromScreen(fb, x + w - r, y + r, r, color, alpha);
}

static void drawTouchControlsOverlay(uint16_t* fb, uint8_t pressedMask) {
    if (!touchControlsVisibleForGame() || !fb) {
        return;
    }

    constexpr uint16_t lineColor = 0xFFFF;
    constexpr uint16_t pressedColor = 0xC618;
    uint8_t idleAlpha = 58;
    uint8_t pressedAlpha = 95;

    bool dpadPressed = (pressedMask & (DIJI_BTN_UP | DIJI_BTN_DOWN |
                                       DIJI_BTN_LEFT | DIJI_BTN_RIGHT)) != 0;
    uint8_t dpadAlpha = dpadPressed ? pressedAlpha : idleAlpha;
    uint16_t dpadColor = dpadPressed ? pressedColor : lineColor;
    drawFbSoftCircleFromScreen(fb, 66, 180, 46, dpadColor, dpadAlpha);
    drawFbSoftCircleFromScreen(fb, 66, 180, 24, dpadColor, dpadAlpha > 22 ? dpadAlpha - 18 : dpadAlpha);
    drawFbLineFromScreen(fb, 66, 138, 66, 154, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 66, 206, 66, 222, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 24, 180, 40, 180, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 92, 180, 108, 180, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 53, 151, 66, 138, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 79, 151, 66, 138, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 53, 209, 66, 222, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 79, 209, 66, 222, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 37, 167, 24, 180, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 37, 193, 24, 180, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 95, 167, 108, 180, dpadColor, dpadAlpha);
    drawFbLineFromScreen(fb, 95, 193, 108, 180, dpadColor, dpadAlpha);

    auto drawAction = [&](int cx, int cy, uint8_t mask) {
        bool pressed = (pressedMask & mask) != 0;
        uint8_t alpha = pressed ? pressedAlpha : idleAlpha;
        uint16_t color = pressed ? pressedColor : lineColor;
        drawFbSoftCircleFromScreen(fb, cx, cy, 25, color, alpha);
        drawFbCircleFromScreen(fb, cx, cy, 11, color, alpha > 18 ? alpha - 16 : alpha);
    };
    drawAction(230, 190, DIJI_BTN_B);
    drawAction(288, 174, DIJI_BTN_A);

    uint8_t selectAlpha = (pressedMask & DIJI_BTN_SELECT) ? pressedAlpha : idleAlpha;
    uint8_t startAlpha = (pressedMask & DIJI_BTN_START) ? pressedAlpha : idleAlpha;
    drawFbPillOutlineFromScreen(fb, 112, 220, 48, 13, lineColor, selectAlpha);
    drawFbPillOutlineFromScreen(fb, 166, 220, 48, 13, lineColor, startAlpha);
}

static bool touchControlsVisibleForGame() {
    return touchInputAvailable() && !espNowHostControllerConnected(0);
}

static uint8_t currentTouchControlsMask() {
    if (!touchControlsVisibleForGame() || !touchInputTouched()) {
        return 0;
    }

    TouchPoint points[DIJI_TOUCH_CONTROLS_MAX_POINTS];
    uint8_t count = touchInputPointCount();
    if (count > DIJI_TOUCH_CONTROLS_MAX_POINTS) {
        count = DIJI_TOUCH_CONTROLS_MAX_POINTS;
    }
    for (uint8_t i = 0; i < count; i++) {
        points[i] = touchInputPointAt(i);
    }

    return touchControlsMaskForPoints(points, count);
}

static bool currentTouchCanOpenPause() {
    if (!touchInputAvailable() || !touchInputTouched()) {
        return false;
    }

    uint8_t count = touchInputPointCount();
    if (count > DIJI_TOUCH_CONTROLS_MAX_POINTS) {
        count = DIJI_TOUCH_CONTROLS_MAX_POINTS;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (!touchControlsPointCanOpenPause(touchInputPointAt(i))) {
            return false;
        }
    }
    return count > 0;
}

static void mergeTouchControlsIntoButtons() {
    uint8_t mask = currentTouchControlsMask();
    if (mask == 0) {
        return;
    }

    buttons.A      |= (mask & DIJI_BTN_A) ? 1 : 0;
    buttons.B      |= (mask & DIJI_BTN_B) ? 1 : 0;
    buttons.SELECT |= (mask & DIJI_BTN_SELECT) ? 1 : 0;
    buttons.START  |= (mask & DIJI_BTN_START) ? 1 : 0;
    buttons.UP     |= (mask & DIJI_BTN_UP) ? 1 : 0;
    buttons.DOWN   |= (mask & DIJI_BTN_DOWN) ? 1 : 0;
    buttons.LEFT   |= (mask & DIJI_BTN_LEFT) ? 1 : 0;
    buttons.RIGHT  |= (mask & DIJI_BTN_RIGHT) ? 1 : 0;
}
