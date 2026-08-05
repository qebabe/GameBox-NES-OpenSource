// ================ 清除屏幕边缘，进入游戏前调用 ================
void clearScreenForGame() {
    // 屏幕 320x240, 游戏区域居中显示，左右 overscan 裁边保持黑色

    // 先停止 DMA，确保不会覆盖我们的清屏操作
    tft.waitDMA();

    // 直接清除左右边缘区域（这些区域 DMA 不会触碰）
    tft.startWrite();
    if (displayStretchFullscreen) {
        tft.fillRect(0, 0, 320, 240, TFT_BLACK);
    } else {
        tft.fillRect(0, 0, TFT_OFFSET_X + OVERSCAN_CROP_X, 240, TFT_BLACK);
        tft.fillRect(TFT_OFFSET_X + OVERSCAN_CROP_X, 0, DISPLAY_WIDTH, 240, TFT_BLACK);
        tft.fillRect(TFT_OFFSET_X + SCREEN_WIDTH - OVERSCAN_CROP_X, 0,
                     TFT_OFFSET_X + OVERSCAN_CROP_X, 240, TFT_BLACK);
    }
    tft.endWrite();

    displayPipeline.clear();
}

bool tryInitSD(bool formatIfMountFailed) {
#if DIJI_DISABLE_SD
    Serial.println("SD disabled for this build");
    sdCardAvailable = false;
    return false;
#elif DIJI_USE_SD_MMC
    if (!SD_MMC.setPins(DIJI_SDMMC_CLK_PIN, DIJI_SDMMC_CMD_PIN, DIJI_SDMMC_D0_PIN,
                        DIJI_SDMMC_D1_PIN, DIJI_SDMMC_D2_PIN, DIJI_SDMMC_D3_PIN)) {
        Serial.println("SD_MMC setPins failed");
        sdCardAvailable = false;
        return false;
    }

    if (!DIJI_SD.begin("/sdcard", DIJI_SDMMC_1BIT_MODE, formatIfMountFailed)) {
        Serial.println("SD_MMC card init failed or not inserted");
        sdCardAvailable = false;
        return false;
    }
#else
    // 使用独立 SPI 总线初始化 SD
    sdSPI.begin(DIJI_SD_SCLK_PIN, DIJI_SD_MISO_PIN, DIJI_SD_MOSI_PIN, DIJI_SD_CS_PIN);

    if (!DIJI_SD.begin(DIJI_SD_CS_PIN, sdSPI, DIJI_SD_FREQ,
                       "/sd", 5, formatIfMountFailed)) {
        Serial.println("SD card init failed or not inserted");
        sdCardAvailable = false;
        return false;
    }
#endif

    Serial.println("SD card initialized");
    sdCardAvailable = true;
    return true;
}

void initializeSD() {
    tryInitSD();
}

// ================ ROM 文件扫描 ================
static void scanROMFilesFromRoot(fs::FS& fs, const char* prefix,
                                 const char* rootPath, bool requireRomDir) {
    File root = fs.open(rootPath);
    if (!root) {
        Serial.printf("Failed to open ROM root %s\n", rootPath);
        return;
    }

    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;

        String entryPath = entry.name();
        if (!entryPath.startsWith("/")) {
            entryPath = String(rootPath);
            if (!entryPath.endsWith("/")) {
                entryPath += "/";
            }
            entryPath += entry.name();
        }

        if (entry.isDirectory()) {
            scanROMFilesFromRoot(fs, prefix, entryPath.c_str(), requireRomDir);
        } else {
            const char* localPath = romStorageLocalPath(entryPath.c_str());
            if (strcmp(prefix, DIJI_ROM_BUILTIN_PREFIX) == 0 &&
                strncmp(localPath, "/rom/downloads/", 15) == 0) {
                entry.close();
                continue;
            }
            bool inRequiredRomDir = !requireRomDir ||
                                    strncmp(localPath, "/rom/", 5) == 0 ||
                                    strncmp(rootPath, "/rom", 4) == 0;
            if (inRequiredRomDir &&
                !romStorageShouldSkipName(entryPath.c_str()) &&
                romStorageIsNesPath(entryPath.c_str())) {
                char romEntry[160];
                if (romStorageMakeEntry(prefix, entryPath.c_str(), romEntry, sizeof(romEntry))) {
                    romList.push_back(String(romEntry));
                    Serial.printf("Found ROM: %s\n", romEntry);
                }
            }
        }
        entry.close();
    }
    root.close();
}

void scanROMFiles() {
    romList.clear();

    if (builtinStorageAvailable) {
        scanROMFilesFromRoot(DIJI_ROMFS, DIJI_ROM_BUILTIN_PREFIX, "/rom", true);
        scanROMFilesFromRoot(DIJI_ROMFS, DIJI_ROM_CACHE_PREFIX, "/rom/downloads", false);
    }

    if (sdCardAvailable) {
        scanROMFilesFromRoot(DIJI_SD, DIJI_ROM_SD_PREFIX, "/", false);
    }

    Serial.printf("Total ROMs found: %d\n", romList.size());

    // 排序 ROM 列表
    std::sort(romList.begin(), romList.end());
}

// ================ 壁纸时钟主页 ================
static void drawHomeClock(bool redrawWallpaper) {
    const bool canResume = lastResumeRomPath.length() != 0;
    if (!canResume && homeSelectedAction == 0) homeSelectedAction = 1;
    homeScreen.draw(redrawWallpaper, canResume, homeSelectedAction);
}

static void syncHomeClockTime() {
    static bool syncRequested = false;
    applyGameBoxTimezone();

    // configTime only starts the SNTP service; it does not wait for a reply.
    // HomeScreen keeps showing its placeholder until the system clock becomes
    // valid, so input remains responsive during WiFi association/time sync.
    const time_t currentTime = time(nullptr);
    if (!HomeScreen::timeValid(currentTime) && wifiProvisioningHasSavedConfig() &&
        !syncRequested) {
        syncRequested = true;
        Serial.printf("桌面时钟 后台校时开始：当前时间戳=%lld WiFi状态码=%d 运行时间=%u毫秒\n",
                      (long long)currentTime, (int)WiFi.status(), (unsigned)millis());
        startGameBoxNetworkTimeSync();
    } else if (!HomeScreen::timeValid(currentTime) && syncRequested) {
        Serial.printf("桌面时钟 后台校时已在运行：当前时间戳=%lld WiFi状态码=%d 运行时间=%u毫秒\n",
                      (long long)currentTime, (int)WiFi.status(), (unsigned)millis());
    } else if (HomeScreen::timeValid(currentTime)) {
        Serial.printf("桌面时钟 时间已经有效：时间戳=%lld 运行时间=%u毫秒\n",
                      (long long)currentTime, (unsigned)millis());
    }

    homeScreen.invalidate();
}

static void openGameLibraryFromHome() {
    waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
    libraryScreen.softbarFocused = false;
    screenController.show(AppScreen::Library);
    drawMainMenu();
}

static bool continueLastGameFromHome() {
    if (lastResumeRomPath.isEmpty()) return false;
    for (size_t i = 0; i < romList.size(); i++) {
        if (romList[i] != lastResumeRomPath) continue;
        libraryScreen.selectedIndex = (int)i;
        if (!loadSelectedROM()) return false;
        if (!gameSession.loadAutoSlot()) {
            returnToMainMenu();
            return false;
        }
        rewindBuffer.clear();
        screenController.show(AppScreen::Playing);
        return true;
    }
    lastResumeRomPath = "";
    return false;
}

static void returnToHomeClock() {
    screenController.show(AppScreen::Home);
    homeSelectedAction = lastResumeRomPath.length() ? 0 : 1;
    homeScreen.invalidate();
    drawHomeClock(true);
    if (!HomeScreen::timeValid(time(nullptr)) && wifiProvisioningHasSavedConfig()) {
        syncHomeClockTime();
        drawHomeClock(true);
    }
}

static void handleHomeInput() {
    static uint32_t lastInputMs = 0;
    touchInputUpdate();
    updateButtons();
    handlePendingCommands();
    drawHomeClock(false);

    uint32_t now = millis();
    if ((uint32_t)(now - lastInputMs) < BUTTON_DEBOUNCE) return;

    const bool canResume = lastResumeRomPath.length() != 0;
    if (buttons.LEFT) {
        if (homeSelectedAction > (canResume ? 0 : 1)) homeSelectedAction--;
        homeScreen.invalidate();
        drawHomeClock(true);
        lastInputMs = now;
        return;
    }
    if (buttons.RIGHT) {
        if (homeSelectedAction < 2) homeSelectedAction++;
        homeScreen.invalidate();
        drawHomeClock(true);
        lastInputMs = now;
        return;
    }
    if (buttons.START) {
        openGameLibraryFromHome();
        return;
    }
    if (buttons.A) {
        waitForButtonsReleased(DIJI_BTN_A);
        if (homeSelectedAction == 0 && canResume) {
            continueLastGameFromHome();
        } else if (homeSelectedAction == 2) {
            runSettingsMenu();
            homeScreen.invalidate();
            drawHomeClock(true);
        } else {
            openGameLibraryFromHome();
        }
        return;
    }
    if (buttons.B) {
        waitForButtonsReleased(DIJI_BTN_B);
        if (canResume) continueLastGameFromHome();
        return;
    }
    if (touchInputJustReleased()) {
        TouchPoint point = touchInputReleasePoint();
        if (touchPointInRect(point, 130, 176, 64, 54) && lastResumeRomPath.length()) {
            homeSelectedAction = 0;
            continueLastGameFromHome();
        } else if (touchPointInRect(point, 190, 176, 64, 54)) {
            homeSelectedAction = 1;
            openGameLibraryFromHome();
        } else if (touchPointInRect(point, 250, 176, 64, 54)) {
            homeSelectedAction = 2;
            runSettingsMenu();
            drawHomeClock(true);
        }
    }
}

// ROM library rendering and interaction are kept in dedicated screen modules.
#include "library_screen_render_impl.inc"
#include "library_screen_input_impl.inc"
#include "game_session_screen_impl.inc"

// ================ 加载选中的ROM ================
bool loadSelectedROM() {
    if (libraryScreen.selectedIndex < 0 || libraryScreen.selectedIndex >= (int)romList.size()) {
        return false;
    }

    if (!displayPipeline.ready()) {
        Serial.println("Cannot load ROM: frame buffers are not allocated");
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(0xF800);
        tft.setTextSize(1);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(72, 96);
        tft.print("帧缓冲内存不足");
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setCursor(70, 126);
        tft.print("请重启或关闭部分功能");
        tft.setFont(&fonts::Font0);
        delay(2000);
        drawMainMenu();
        return false;
    }

    const char* romPath = romList[libraryScreen.selectedIndex].c_str();
    Serial.printf("Loading ROM: %s\n", romPath);

    // 显示加载中
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(MENU_TITLE_COLOR);
    tft.setTextSize(2);
    tft.setFont(&fonts::efontCN_14);
    tft.setCursor(104, 110);
    tft.print("载入中...");

    if (!gameSession.loadRom(romPath, displayPipeline.renderBuffer(), ENABLE_FRAMESKIP)) {
        Serial.printf("Failed to load ROM: %s\n", romPath);
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(0xF800);  // 红色错误提示
        tft.setTextSize(2);
        tft.setCursor(92, 96);
        tft.print("载入失败");
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setTextSize(1);
        tft.setCursor(66, 128);
        tft.print("Mapper 不支持或 ROM 错误");
        tft.setCursor(88, 150);
        tft.print("支持 Mapper 0-4");
        tft.setCursor(96, 174);
        tft.print("正在返回菜单...");
        delay(3000);
        tft.setFont(&fonts::Font0);
        tft.fillScreen(MENU_BG_COLOR);
        drawMainMenu();
        return false;
    }
    tft.setFont(&fonts::Font0);

    // 彻底清屏为黑色 (清除菜单残留)
    clearScreenForGame();

    // 开始运行游戏并启用音频
    gameSession.start(3);
    if (rewindSeconds > 0) rewindBuffer.begin(rewindSeconds);
    rewindCapturePhase = 0;
    Preferences sessionPrefs;
    if (sessionPrefs.begin("session", false)) {
        sessionPrefs.putString("last_rom", romPath);
        sessionPrefs.end();
        lastResumeRomPath = romPath;
    }
    resumeFrameDisplayForGame();
    Serial.println("ROM loaded successfully");

    GameStoreItem usageGame;
    if (gameStoreFindCachedByRomPath(romPath, usageGame) && usageGame.id.length()) {
        usageReportEvent("game_started", "game_id", usageGame.id.c_str());
    } else {
        usageReportEvent("game_started");
    }

    gameJustEntered = true;
    return true;
}

// ================ 返回主菜单 ================
void returnToMainMenu() {
    // 停止游戏并静音
    gameSession.pause();
    pauseFrameDisplayForUi();
    muteAudio();
    if (gameSession.hasLoadedRom()) {
        gameSession.saveAutoSlot(displayPipeline.lastDisplayedBuffer());
    }
    nes.flushBatterySave(true);

    screenController.show(AppScreen::Library);
    libraryScreen.selectedIndex = 0;
    libraryScreen.softbarFocused = false;
    libraryScreen.scrollPosition = 0.0f;
    libraryScreen.scrollVelocity = 0.0f;
    pauseMenuIndex = 0;

    // 清屏并重绘菜单
    tft.fillScreen(MENU_BG_COLOR);
    drawMainMenu();
}

void loadROM() {
    // 现在使用菜单选择，这里只是扫描ROM列表
    if (sdCardAvailable || builtinStorageAvailable) {
        scanROMFiles();
    }
}

// ---------------- Audio (I2S) ----------------
static void loadAudioVolumeSetting() {
    Preferences preferences;
    if (preferences.begin("audio", true)) {
        audioVolumePercent = preferences.getUChar("vol", audioVolumePercent);
        preferences.end();
    }
    if (audioVolumePercent > 100) {
        audioVolumePercent = 80;
    }
}

static void saveAudioVolumeSetting() {
    Preferences preferences;
    if (preferences.begin("audio", false)) {
        preferences.putUChar("vol", audioVolumePercent);
        preferences.end();
    }
}

static void applyAudioVolumePercent() {
    if (!DIJI_AUDIO_CODEC_ES8311) {
        return;
    }

    uint8_t dacVolume = (uint8_t)((audioVolumePercent * 0xF0) / 100);
    es8311WriteReg(0x32, dacVolume);
    setEs8311Mute(audioVolumePercent == 0);
    Serial.printf("Audio volume: %u%% reg32=0x%02X\n", audioVolumePercent, dacVolume);
}

static void setAudioVolumePercent(uint8_t percent, bool persist) {
    if (percent > 100) {
        percent = 100;
    }
    audioVolumePercent = percent;
    applyAudioVolumePercent();
    if (persist) {
        saveAudioVolumeSetting();
    }
}

static void initializeAudio() {
    loadAudioVolumeSetting();

    if (DIJI_AMP_ENABLE_PIN >= 0) {
        pinMode(DIJI_AMP_ENABLE_PIN, OUTPUT);
        setAmplifierEnableLevel(DIJI_AMP_ENABLE_LEVEL);
        delay(20);
    }

    audioOutputReady = audioOutputBegin(AUDIO_SAMPLE_RATE, I2S_MCLK_PIN, I2S_BCLK_PIN,
                                        I2S_LRCLK_PIN, I2S_DATA_PIN);
    if (!audioOutputReady) {
        Serial.println("I2S output failed to start");
    }
    audioOutputZero();
    initializeEs8311Codec();

    // Let APU know sample rate (if needed)
    nes.apu.setSampleRate(AUDIO_SAMPLE_RATE);

    // APU 独占 Core 0，仿真+渲染+推屏全在 Core 1 (loop)
    // APU::clock() 内部会在缓冲区满时阻塞写入 I2S
    // I2S DMA 的消费速度 (44100 Hz) 自然控制 APU 时钟速度
    BaseType_t taskResult = xTaskCreatePinnedToCore(
        apu_task, "APU", 3072, &nes.apu, 1, &apuTaskHandle, 0);
    if (taskResult != pdPASS) {
        apuTaskHandle = nullptr;
        Serial.println("APU task failed to start");
    }
}

static bool otaBootHealthReady() {
    bool storageReady = sdCardAvailable || builtinStorageAvailable;
    return displayPipeline.ready() && displayPipeline.taskStarted() && storageReady &&
           audioOutputReady && apuTaskHandle && touchInputAvailable() && mainUiReady;
}

static void updateOtaHealthObservation() {
    if (!otaHealthObservationActive) return;
    if (!otaBootHealthReady()) {
        otaHealthObservationActive = false;
        Serial.println("OTA health check failed; rollback remains enabled");
        return;
    }
    if ((uint32_t)(millis() - otaHealthObservationStartedMs) < OTA_HEALTH_OBSERVATION_MS) {
        return;
    }
    otaMarkCurrentFirmwareValid();
    otaReportCurrentFirmwareBootOk();
    otaHealthObservationActive = false;
    Serial.println("OTA health check passed; firmware marked valid");
}

// APU task: 像 Anemoia 一样无限循环调用 clock()
// 每 2 次循环调用一次 clock()，匹配 NES APU 时序（APU 时钟 = CPU 时钟 / 2）
// I2S 写入阻塞会自然控制速度
static void apu_task(void* arg) {
    APU* apu = (APU*)arg;
    while (1) {
        if (gameSession.running()) {
            apu->clock();
        } else {
            // 游戏未运行时，输出静音并稍作等待
            vTaskDelay(1);
        }
    }
}

// 静音音频输出
static void muteAudio() {
    setEs8311Mute(true);
    audioOutputZero();
    delay(50);
    audioOutputZero();
    setEs8311Mute(false);
}

static void writeSquareTone(uint16_t frequency, uint16_t durationMs) {
    constexpr size_t FRAMES_PER_CHUNK = 128;
    int16_t samples[FRAMES_PER_CHUNK * 2];
    uint32_t totalFrames = (AUDIO_SAMPLE_RATE * durationMs) / 1000;
    uint32_t periodFrames = AUDIO_SAMPLE_RATE / frequency;
    if (periodFrames < 2) periodFrames = 2;

    uint32_t frame = 0;
    size_t totalBytesWritten = 0;
    while (frame < totalFrames) {
        size_t framesThisChunk = totalFrames - frame;
        if (framesThisChunk > FRAMES_PER_CHUNK) {
            framesThisChunk = FRAMES_PER_CHUNK;
        }

        for (size_t i = 0; i < framesThisChunk; i++) {
            int16_t sample = (((frame + i) % periodFrames) < (periodFrames / 2)) ? 9000 : -9000;
            samples[i * 2] = sample;
            samples[i * 2 + 1] = sample;
        }

        size_t bytesWritten = 0;
        esp_err_t res = audioOutputWrite(samples, framesThisChunk * 2 * sizeof(int16_t), &bytesWritten);
        if (res != ESP_OK) {
            Serial.printf("Tone I2S write failed: %d\n", res);
            break;
        }
        totalBytesWritten += bytesWritten;
        frame += framesThisChunk;
    }
    Serial.printf("Tone %uHz %ums wrote %u/%u bytes\n",
                  frequency, durationMs, (unsigned)totalBytesWritten,
                  (unsigned)(totalFrames * 2 * sizeof(int16_t)));
}

static void setAmplifierEnableLevel(int level) {
    if (DIJI_AMP_ENABLE_PIN >= 0) {
        digitalWrite(DIJI_AMP_ENABLE_PIN, level ? HIGH : LOW);
    }
}

static bool es8311WriteReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(DIJI_AUDIO_CODEC_ES8311_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool es8311ReadReg(uint8_t reg, uint8_t* value) {
    if (!value) return false;
    Wire.beginTransmission(DIJI_AUDIO_CODEC_ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom((uint8_t)DIJI_AUDIO_CODEC_ES8311_ADDR, (uint8_t)1) != 1) {
        return false;
    }
    *value = Wire.read();
    return true;
}

static bool es8311UpdateReg(uint8_t reg, uint8_t clearMask, uint8_t setMask) {
    uint8_t value = 0;
    if (!es8311ReadReg(reg, &value)) {
        return false;
    }
    value = (value & ~clearMask) | setMask;
    return es8311WriteReg(reg, value);
}

static void setEs8311Mute(bool mute) {
    if (!DIJI_AUDIO_CODEC_ES8311) {
        return;
    }
    uint8_t reg31 = 0;
    if (!es8311ReadReg(0x31, &reg31)) {
        return;
    }
    reg31 &= 0x9F;
    if (mute) {
        reg31 |= 0x60;
    }
    es8311WriteReg(0x31, reg31);
}

static bool initializeEs8311Codec() {
    if (!DIJI_AUDIO_CODEC_ES8311) {
        return true;
    }
    if (DIJI_AUDIO_CODEC_I2C_SDA_PIN < 0 || DIJI_AUDIO_CODEC_I2C_SCL_PIN < 0) {
        Serial.println("ES8311 I2C pins are not configured");
        return false;
    }

    Wire.begin(DIJI_AUDIO_CODEC_I2C_SDA_PIN, DIJI_AUDIO_CODEC_I2C_SCL_PIN);
    Wire.setClock(400000);
    delay(10);

    uint8_t chipId1 = 0;
    uint8_t chipId2 = 0;
    if (!es8311ReadReg(0xFD, &chipId1) || !es8311ReadReg(0xFE, &chipId2)) {
        Serial.printf("ES8311 not found at I2C 0x%02X\n", DIJI_AUDIO_CODEC_ES8311_ADDR);
        return false;
    }
    Serial.printf("ES8311 detected: id=%02X %02X\n", chipId1, chipId2);

    bool ok = true;
    uint8_t reg = 0;
    if (es8311ReadReg(0x0D, &reg) && reg != 0xFA) {
        ok &= es8311WriteReg(0x0D, 0xFA);
    }

    ok &= es8311WriteReg(0x44, 0x08);
    ok &= es8311WriteReg(0x44, 0x08);
    ok &= es8311WriteReg(0x01, 0x30);
    ok &= es8311WriteReg(0x02, 0x00);
    ok &= es8311WriteReg(0x03, 0x10);
    ok &= es8311WriteReg(0x16, 0x24);
    ok &= es8311WriteReg(0x04, 0x10);
    ok &= es8311WriteReg(0x05, 0x00);
    ok &= es8311WriteReg(0x0B, 0x00);
    ok &= es8311WriteReg(0x0C, 0x00);
    ok &= es8311WriteReg(0x10, 0x1F);
    ok &= es8311WriteReg(0x11, 0x7F);
    ok &= es8311WriteReg(0x00, 0x80);
    ok &= es8311UpdateReg(0x00, 0x40, 0x00); // I2S slave mode.
    ok &= es8311WriteReg(0x01, 0x3F);        // Use external MCLK, non-inverted.
    ok &= es8311UpdateReg(0x06, 0x20, 0x00); // Non-inverted BCLK.
    ok &= es8311WriteReg(0x13, 0x10);
    ok &= es8311WriteReg(0x1B, 0x0A);
    ok &= es8311WriteReg(0x1C, 0x6A);
    ok &= es8311WriteReg(0x44, 0x58);

    // 16-bit I2S normal format.
    ok &= es8311UpdateReg(0x09, 0x00, 0x0C);
    ok &= es8311UpdateReg(0x0A, 0x00, 0x0C);
    ok &= es8311UpdateReg(0x09, 0x03, 0x00);
    ok &= es8311UpdateReg(0x0A, 0x03, 0x00);

    // 44.1 kHz with 256 * Fs MCLK = 11.2896 MHz.
    ok &= es8311WriteReg(0x02, 0x00);
    ok &= es8311WriteReg(0x05, 0x00);
    ok &= es8311UpdateReg(0x03, 0x7F, 0x10);
    ok &= es8311UpdateReg(0x04, 0x7F, 0x10);
    ok &= es8311UpdateReg(0x07, 0x3F, 0x00);
    ok &= es8311WriteReg(0x08, 0xFF);
    ok &= es8311UpdateReg(0x06, 0x1F, 0x03);

    // Start DAC path and unmute.
    ok &= es8311WriteReg(0x00, 0x80);
    ok &= es8311WriteReg(0x01, 0x3F);
    ok &= es8311UpdateReg(0x09, 0x40, 0x00);
    ok &= es8311WriteReg(0x17, 0xBF);
    ok &= es8311WriteReg(0x0E, 0x02);
    ok &= es8311WriteReg(0x12, 0x00);
    ok &= es8311WriteReg(0x14, 0x1A);
    ok &= es8311UpdateReg(0x14, 0x40, 0x00);
    ok &= es8311WriteReg(0x0D, 0x01);
    ok &= es8311WriteReg(0x15, 0x40);
    ok &= es8311WriteReg(0x37, 0x08);
    ok &= es8311WriteReg(0x45, 0x00);
    applyAudioVolumePercent();

    Serial.printf("ES8311 init %s\n", ok ? "ok" : "failed");
    return ok;
}

static void dumpEs8311AudioRegisters() {
    if (!DIJI_AUDIO_CODEC_ES8311) {
        return;
    }
    constexpr uint8_t regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0D, 0x0E, 0x12, 0x14, 0x31, 0x32,
        0x37, 0x44, 0x45
    };
    Serial.print("ES8311 regs:");
    for (uint8_t reg : regs) {
        uint8_t value = 0;
        if (es8311ReadReg(reg, &value)) {
            Serial.printf(" %02X=%02X", reg, value);
        } else {
            Serial.printf(" %02X=??", reg);
        }
    }
    Serial.println();
}

static void drawAudioTestStatus(const char* message) {
    tft.fillRect(0, 216, 320, 24, MENU_BG_COLOR);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
    tft.setTextSize(1);
    tft.setFont(&fonts::efontCN_14);
    tft.setCursor(8, 221);
    tft.print(message);
    tft.setFont(&fonts::Font0);
}

static void playAudioTestTone() {
    bool wasRunning = gameSession.running();
    gameSession.pause();
    delay(20);
    audioOutputZero();

    Serial.printf("Audio test: amp level %d\n", DIJI_AMP_ENABLE_LEVEL);
    audioOutputDumpInfo();
    initializeEs8311Codec();
    dumpEs8311AudioRegisters();
    drawAudioTestStatus(DIJI_AUDIO_CODEC_ES8311 ? "音频测试: ES8311" : "音频测试: I2S");
    delay(300);
    drawAudioTestStatus("音频测试: 440Hz");
    setAmplifierEnableLevel(DIJI_AMP_ENABLE_LEVEL);
    writeSquareTone(440, 1200);
    muteAudio();
    delay(120);

    drawAudioTestStatus("音频测试: 880Hz");
    setAmplifierEnableLevel(DIJI_AMP_ENABLE_LEVEL);
    writeSquareTone(880, 1200);
    muteAudio();

    setAmplifierEnableLevel(DIJI_AMP_ENABLE_LEVEL);
    if (!screenController.is(AppScreen::Playing)) {
        delay(250);
        if (screenController.is(AppScreen::Home)) {
            drawHomeClock(true);
        } else {
            drawMainMenu();
        }
    }

    if (wasRunning) gameSession.resume(); else gameSession.pause();
}
