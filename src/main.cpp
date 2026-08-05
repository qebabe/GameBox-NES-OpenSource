#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <vector>
#include <algorithm>
#include <atomic>
#include <String.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nes.h"
#include "lgfx_conf.h"
#include "logo_bitmap.h"
#include "storage.h"
#include "serial_controller.h"
#include "audio_output.h"
#include "touch_input.h"
#include "touch_controls.h"
#include "espnow_host.h"
#include "wifi_provisioning.h"
#include "game_store.h"
#include "cloud_save_client.h"
#include "device_identity.h"
#include "ota_service.h"
#include "network_security.h"
#include "power_manager.h"
#include "display_pipeline.h"
#include "game_session.h"
#include "wireless_manager.h"
#include "usage_client.h"
#include "remote_config.h"
#include "rewind_buffer.h"
#include "screen_controller.h"
#include "home_screen.h"
#include "library_screen.h"
#include "store_screen.h"
#include "settings_screen.h"
#include "wallpaper_manager.h"
#include "wallpaper_web_server.h"
#include "rom_web_server.h"
#include "settings_web_server.h"
#include "esp_err.h"
#include "esp_timer.h"

// 串口调试开关
#ifndef ENABLE_DEBUG_SERIAL
#define ENABLE_DEBUG_SERIAL false
#endif

#ifndef DIJI_ENABLE_SERIAL_CONTROLLER
#define DIJI_ENABLE_SERIAL_CONTROLLER true
#endif

// ================ 菜单颜色配置 (掌机菜单暗色调) ================
#define MENU_BG_COLOR       0x1082  // 深黑蓝背景
#define MENU_HEADER_COLOR   0x2945  // 顶/底栏
#define MENU_PANEL_COLOR    0x18E3  // 内容面板
#define MENU_ROW_COLOR      0x2124  // 普通列表行
#define MENU_TEXT_COLOR     0xC638  // 浅灰文字
#define MENU_HIGHLIGHT_BG   0x043F  // 选中项蓝色
#define MENU_ACCENT_COLOR   0x07FF  // 青色点缀
#define MENU_ARROW_COLOR    0x867F  // 选中箭头
#define MENU_HINT_COLOR     0x8410  // 提示文字
#define MENU_TITLE_COLOR    0xEF7D  // 标题文字
#define MENU_BORDER_COLOR   0x4A69  // 边框颜色
#define PAUSE_OVERLAY_COLOR 0x18C3  // 暂停遮罩 (深色半透明效果)

// ================ 顶层页面状态 ================
static ScreenController screenController;
static std::vector<String> romList;       // ROM 文件列表
static std::vector<GameStoreItem> storeItems;
static std::vector<uint8_t> storeDownloadedCache;
static int storeTotal = 0;
static std::atomic<bool> storeWifiConnected{false};
static const int MENU_VISIBLE_ROWS = 4;   // 列表视口约显示 4 行，内容本身连续滚动
static int pauseMenuIndex = 0;            // 暂停菜单选项索引
static uint8_t pauseSaveSlot = 0;

// 按键防抖
static unsigned long lastButtonTime = 0;
static const unsigned long BUTTON_DEBOUNCE = 200;  // 200ms防抖
static constexpr int PAUSE_MENU_WIDTH = 270;
static constexpr int PAUSE_MENU_HEIGHT = 224;
static constexpr int PAUSE_OPTION_COUNT = 4;
static constexpr int PAUSE_OPTION_START_Y = 46;
static constexpr int PAUSE_OPTION_HEIGHT = 30;
static constexpr int PAUSE_OPTION_GAP = 4;
static constexpr int PAUSE_VOLUME_Y = 184;
static constexpr int PAUSE_VOLUME_BUTTON_W = 52;
static constexpr int PAUSE_VOLUME_BUTTON_H = 28;

#if ENABLE_DEBUG_SERIAL
#define FPS_PRINT(...) Serial.printf(__VA_ARGS__)
#else
#define FPS_PRINT(...) ((void)0)
#endif

// 游戏控制器按键
#define A_BUTTON      DIJI_A_BUTTON_PIN
#define B_BUTTON      DIJI_B_BUTTON_PIN
#define LEFT_BUTTON   DIJI_LEFT_BUTTON_PIN
#define RIGHT_BUTTON  DIJI_RIGHT_BUTTON_PIN
#define UP_BUTTON     DIJI_UP_BUTTON_PIN
#define DOWN_BUTTON   DIJI_DOWN_BUTTON_PIN
#define START_BUTTON  DIJI_START_BUTTON_PIN
#define SELECT_BUTTON DIJI_SELECT_BUTTON_PIN

// I2S / APU audio output
#define I2S_MCLK_PIN DIJI_I2S_MCLK_PIN
#define I2S_BCLK_PIN DIJI_I2S_BCLK_PIN
#define I2S_LRCLK_PIN DIJI_I2S_LRCLK_PIN
#define I2S_DATA_PIN DIJI_I2S_DATA_PIN

// 音频参数
constexpr int AUDIO_SAMPLE_RATE = 44100;

// ================ 全局变量 ================
NES nes;
LGFX tft;
static DisplayPipeline displayPipeline;
static GameSession gameSession(nes);
static RewindBuffer rewindBuffer(nes);
static WirelessManager wirelessManager;
static HomeScreen homeScreen(tft);
static SettingsScreen settingsScreen(tft);
static LibraryScreen libraryScreen(tft);
static StoreScreen storeScreen(tft);

// 屏幕参数
constexpr int SCREEN_WIDTH  = 256;
constexpr int SCREEN_HEIGHT = 240;
constexpr int TFT_OFFSET_X  = (320 - SCREEN_WIDTH) / 2; // 横向居中
constexpr int OVERSCAN_CROP_X = 4;  // 隐藏 LCD 上可见的横向卷轴边缘接缝
constexpr int DISPLAY_WIDTH = SCREEN_WIDTH - OVERSCAN_CROP_X * 2;
constexpr int MENU_COVER_X = 8;
constexpr int MENU_COVER_Y = 50;
constexpr int MENU_COVER_W = 112;
constexpr int MENU_COVER_H = 156;
constexpr int MENU_COVER_ASSET_W = 88;
constexpr int MENU_COVER_ASSET_H = 122;
constexpr int MENU_LIST_X = 126;
constexpr int MENU_LIST_Y = 50;
constexpr int MENU_LIST_WIDTH = 182;
constexpr int MENU_ITEM_HEIGHT = 39;
constexpr int MENU_LIST_HEIGHT = MENU_VISIBLE_ROWS * MENU_ITEM_HEIGHT;
constexpr int MENU_SOFTBAR_Y = 210;
constexpr int MENU_SOFTBAR_HEIGHT = 30;
constexpr int MENU_SOFTBAR_ITEM_WIDTH = 64;
constexpr int MENU_SOFTBAR_BUTTON_Y = MENU_SOFTBAR_Y + 2;
constexpr int MENU_SOFTBAR_BUTTON_H = 26;
constexpr int MENU_P1_BADGE_HIT_X = 0;
constexpr int MENU_P2_BADGE_HIT_X = 236;
constexpr int MENU_GAMEPAD_BADGE_HIT_Y = 0;
constexpr int MENU_GAMEPAD_BADGE_HIT_W = 84;
constexpr int MENU_GAMEPAD_BADGE_HIT_H = 36;
constexpr uint32_t MENU_DELETE_LONG_PRESS_MS = 650;
constexpr int MENU_DELETE_MOVE_TOLERANCE = 12;
constexpr int STORE_LIST_X = 10;
constexpr int STORE_LIST_Y = 58;
constexpr int STORE_LIST_WIDTH = 300;
constexpr int STORE_LIST_HEIGHT = 144;
constexpr int STORE_ROW_HEIGHT = 36;
constexpr int STORE_LAZY_BATCH_SIZE = 20;
constexpr int STORE_LAZY_LOAD_AHEAD_ROWS = 5;
constexpr int STORE_NETWORK_PAGE_SIZE = 25;
constexpr int STORE_INITIAL_PAGE_COUNT = 3;
constexpr int STORE_PREFETCH_AHEAD_PAGE_COUNT = 2;
// 每个块的行数（用 DMA 一次推多行以减少 setAddrWindow/wait 开销）
// 8 行 = 30 次 DMA/帧，60 行 = 4 次 DMA/帧，120 行 = 2 次 DMA/帧
constexpr int BLOCK_LINES = 60;  // 增大到 60 行，240/60=4 次 DMA 每帧
constexpr int DISPLAY_BLOCK_LINES = (OVERSCAN_CROP_X > 0) ? 16 : BLOCK_LINES;

// FPS 统计变量
static uint32_t last_emulation_us = 0;  // 最近一次仿真帧耗时（微秒）
static uint32_t fps_count = 0;          // 已完成的仿真帧计数
static uint32_t fps_last_ms = 0;        // 上次打印 FPS 的时间戳
static bool displayStretchFullscreen = true;
static bool displayRotate180 = false;
static bool displayColorInverted = DIJI_LCD_INVERT;
static volatile bool pendingAudioTest = false;
static volatile bool pendingDisplayToggle = false;
static volatile bool pendingTouchCalibration = false;
static uint8_t audioVolumePercent = 80;
static bool suppressNextPauseTouchRelease = false;
static bool bootToWallpaperHome = true;
static uint8_t rewindSeconds = 10;
static uint8_t rewindCapturePhase = 0;
static String lastResumeRomPath;
static uint8_t homeSelectedAction = 1;

static uint8_t effectiveDisplayRotation() {
    return displayRotate180 ? (uint8_t)((DIJI_TFT_ROTATION + 2) & 3)
                            : DIJI_TFT_ROTATION;
}

// Separate SPI bus for SD so it cannot reconfigure/conflict with the TFT SPI bus.
// If your TFT_eSPI setup uses HSPI, keep SD on FSPI.
SPIClass sdSPI(FSPI);

extern const uint8_t gamebox_clock_wallpaper_start[]
    asm("_binary_assets_gamebox_clock_wallpaper_jpg_start");
extern const uint8_t gamebox_clock_wallpaper_end[]
    asm("_binary_assets_gamebox_clock_wallpaper_jpg_end");

static TaskHandle_t apuTaskHandle = nullptr;

static void initializeAudio();
static void apu_task(void* arg);
static void muteAudio();
static void playAudioTestTone();
static void setAmplifierEnableLevel(int level);
static bool es8311WriteReg(uint8_t reg, uint8_t value);
static bool initializeEs8311Codec();
static void setEs8311Mute(bool mute);
static void dumpEs8311AudioRegisters();
static void drawAudioTestStatus(const char* message);
static void loadAudioVolumeSetting();
static void saveAudioVolumeSetting();
static void setAudioVolumePercent(uint8_t percent, bool persist);
static void applyAudioVolumePercent();
static void initializeBuiltinStorage();
static void initializeTouch();
static bool handleMenuTouch();
static bool handlePauseTouch();
static void activatePauseMenuSelection();
static void enterPauseMenu();
static bool runTouchCalibration();
static void runInitialTouchCalibrationIfNeeded();
static void initializeEspNowGamepads();
static void runGamepadPairingMenu();
static void runWifiProvisioningMenu();
static void runGameStoreMenu();
static void runSettingsMenu();
static void runVolumeSettingsMenu();
static void runSystemInfoScreen();
static void runCloudSaveMenu();
static void runOtaUpdateMenu();
static void runPowerSettingsMenu();
static void runWallpaperSettingsMenu();
static void runRomManagerMenu();
static void enterSoftwarePowerOff(bool automatic);
static void runGamepadPairingSlot(uint8_t player);
static void drawHomeClock(bool redrawWallpaper = true);
static void handleHomeInput();
static void syncHomeClockTime();
static void returnToHomeClock();
static void loadBootPageSetting();
static void saveBootPageSetting();

static bool gameJustEntered = false;
static bool audioOutputReady = false;
static bool mainUiReady = false;
static bool otaHealthObservationActive = false;
static uint32_t otaHealthObservationStartedMs = 0;
static constexpr uint32_t OTA_HEALTH_OBSERVATION_MS = 10000;
static bool sdCardAvailable = false;  // SD 卡是否可用
static bool builtinStorageAvailable = false;

#if DIJI_ENABLE_SERIAL_CONTROLLER
static uint8_t serialControllerState = 0;
static uint32_t serialControllerLastMs = 0;
static char serialControllerLine[16] = {0};
static uint8_t serialControllerLineLen = 0;
static constexpr uint32_t SERIAL_CONTROLLER_TIMEOUT_MS = 250;
#endif

// 抽帧开关: true=启用抽帧(性能优先), false=每帧都渲染(画面优先)
// SMB1 等游戏会用隔帧闪烁表现受伤/无敌，使用奇数周期跳帧避免锁相。
static bool ENABLE_FRAMESKIP = true;

struct ButtonState {
    uint8_t A = 0;
    uint8_t B = 0;
    uint8_t LEFT = 0;
    uint8_t RIGHT = 0;
    uint8_t UP = 0;
    uint8_t DOWN = 0;
    uint8_t START = 0;
    uint8_t SELECT = 0;
} buttons;

// ================ 函数前向声明 ================
void updateButtons();
void runFrame();
void scanROMFiles();
void playBootAnimation();
void drawBootLogo(int y);
static void drawMenuHeaderLogo();
void drawMainMenu();
void drawMenuList();
void drawPauseMenu();
static void drawAudioVolumeBlocks(int x, int y, uint8_t percent);
void handleMenuInput();
void handlePauseInput();
bool loadSelectedROM();
void returnToMainMenu();
void clearScreenForGame();
bool tryInitSD(bool formatIfMountFailed = false);  // 尝试初始化 SD 卡
static uint8_t buttonsToMask();
static void waitForButtonsReleased(uint8_t mask);
static String makeRomBaseName(const String& romPath);
static String makeRomCoverPath(const String& romPath);
static String makeClippedText(String text, int maxWidth);
static String makeRomDisplayName(const String& romPath, int maxWidth);
static void drawRomDisplayName(const String& romPath, int x, int y, int maxWidth);
static void drawClippedText(const String& text, int x, int y, int maxWidth);
static void drawMenuSoftbar();
static void drawStoreScreen(const String& status = "");
static bool storeDownloadSelected();
static bool storeRefreshCachedIndex();
static bool storeEnsureWifiConnected();
static void storeReleaseNetworkTask();
static void drawSettingsMenu(int selected, int scrollY,
                             bool redrawChrome = false,
                             bool backFocused = false);
static void drawVolumeSettingsScreen();
static void drawSystemInfoScreen(int selected = 0);
static bool confirmDeleteSelectedRom();
static bool deleteSelectedRom();
static void drawSelectedRomCover();
static void drawMenuRows();
static bool moveMenuSelection(int delta);
static void handleSerialControllerCommand(SerialControllerCommand command);
static void handlePendingCommands();
static void pauseFrameDisplayForUi();
static void resumeFrameDisplayForGame();
static void drawGamepadStatusBadges(bool force = false);
static bool touchControlsVisibleForGame();
static uint8_t currentTouchControlsMask();
static bool currentTouchCanOpenPause();
static void mergeTouchControlsIntoButtons();
static void drawTouchControlsOverlay(uint16_t* fb, uint8_t pressedMask);

#include "ui_helpers_impl.inc"

// Board bring-up, audio/touch/power, pairing, provisioning, settings and store
// modal implementations. They remain on the main UI task.
#include "device_runtime_impl.inc"

// ROM discovery, Home/Library navigation, pause/session UI and game launch.
#include "app_flow_impl.inc"

enum class BootInitStage : uint8_t {
    Waiting,
    Storage,
    RomScan,
    Audio,
    Touch,
    Complete,
};

static std::atomic<BootInitStage> bootInitStage{BootInitStage::Waiting};
static std::atomic<bool> bootInitComplete{false};
static TaskHandle_t bootInitTaskHandle = nullptr;
static uint32_t bootInitStartedMs = 0;

static const char* bootInitStageLabel(BootInitStage stage) {
    switch (stage) {
        case BootInitStage::Storage: return "正在初始化存储";
        case BootInitStage::RomScan: return "正在扫描游戏";
        case BootInitStage::Audio: return "正在初始化音频";
        case BootInitStage::Touch: return "正在初始化触摸";
        case BootInitStage::Complete: return "初始化完成";
        case BootInitStage::Waiting:
        default: return "正在准备系统";
    }
}

static void runBootHardwareInitialization() {
    uint32_t stageStartedMs = millis();
    bootInitStage.store(BootInitStage::Storage);
    Serial.println("开机初始化 存储开始");
    initializeSD();
    initializeBuiltinStorage();
    if (lastResumeRomPath.length()) {
        char autoPath[160];
        if (!romStorageMakeAutoSavePath(lastResumeRomPath.c_str(), autoPath, sizeof(autoPath)) ||
            !dijiStoragePathExists(autoPath)) {
            lastResumeRomPath = "";
        }
    }
    Serial.printf("开机初始化 存储完成：耗时=%u毫秒 SD=%d 内置存储=%d\n",
                  (unsigned)(millis() - stageStartedMs), sdCardAvailable ? 1 : 0,
                  builtinStorageAvailable ? 1 : 0);

    stageStartedMs = millis();
    bootInitStage.store(BootInitStage::RomScan);
    Serial.println("开机初始化 游戏扫描开始");
    loadROM();
    Serial.printf("开机初始化 游戏扫描完成：数量=%u 耗时=%u毫秒\n",
                  (unsigned)romList.size(), (unsigned)(millis() - stageStartedMs));

    stageStartedMs = millis();
    bootInitStage.store(BootInitStage::Audio);
    Serial.println("开机初始化 音频开始");
    initializeAudio();
    Serial.printf("开机初始化 音频完成：可用=%d 耗时=%u毫秒\n",
                  audioOutputReady ? 1 : 0, (unsigned)(millis() - stageStartedMs));

    stageStartedMs = millis();
    bootInitStage.store(BootInitStage::Touch);
    Serial.println("开机初始化 触摸开始");
    initializeTouch();
    Serial.printf("开机初始化 触摸完成：可用=%d 耗时=%u毫秒\n",
                  touchInputAvailable() ? 1 : 0, (unsigned)(millis() - stageStartedMs));

    bootInitStage.store(BootInitStage::Complete);
    bootInitComplete.store(true);
    Serial.printf("开机初始化 后台任务全部完成：总耗时=%u毫秒\n",
                  (unsigned)(millis() - bootInitStartedMs));
}

static void bootInitTaskEntry(void*) {
    runBootHardwareInitialization();
    bootInitTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

static bool startBootInitialization() {
    bootInitStartedMs = millis();
    bootInitStage.store(BootInitStage::Waiting);
    bootInitComplete.store(false);
    BaseType_t result = xTaskCreatePinnedToCore(
        bootInitTaskEntry, "BootInit", 8192, nullptr, 1, &bootInitTaskHandle, 0);
    if (result == pdPASS) {
        Serial.println("开机初始化 后台任务已启动：核心=0");
        return true;
    }
    bootInitTaskHandle = nullptr;
    Serial.println("开机初始化 后台任务创建失败，将在主线程执行");
    return false;
}

static void drawBootWaitStatus(BootInitStage stage) {
    tft.fillRect(0, 210, 320, 30, TFT_BLACK);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_HINT_COLOR, TFT_BLACK);
    const char* label = bootInitStageLabel(stage);
    int width = tft.textWidth(label);
    tft.setCursor((320 - width) / 2, 218);
    tft.print(label);
    tft.setFont(&fonts::Font0);
}

static void waitForBootInitialization() {
    BootInitStage lastStage = BootInitStage::Waiting;
    uint32_t lastProgressLogMs = 0;
    while (!bootInitComplete.load()) {
        wirelessManager.updateBootConnectivity();
        BootInitStage stage = bootInitStage.load();
        if (stage != lastStage) {
            lastStage = stage;
            drawBootWaitStatus(stage);
        }
        uint32_t now = millis();
        if ((uint32_t)(now - lastProgressLogMs) >= 2000) {
            lastProgressLogMs = now;
            Serial.printf("开机初始化 等待后台任务：阶段=%s 总耗时=%u毫秒\n",
                          bootInitStageLabel(stage),
                          (unsigned)(now - bootInitStartedMs));
        }
        delay(20);
    }
    wirelessManager.updateBootConnectivity();
}

// ================ 主程序 ================
void setup() {
    initializeSerial();
    powerManagerHandleEarlyWake();
    powerManagerBegin();
    loadBootPageSetting();
    Preferences sessionPrefs;
    if (sessionPrefs.begin("session", true)) {
        lastResumeRomPath = sessionPrefs.getString("last_rom", "");
        rewindSeconds = sessionPrefs.getUChar("rewind_sec", 10);
        if (rewindSeconds != 0 && rewindSeconds != 5 &&
            rewindSeconds != 10 && rewindSeconds != 20) rewindSeconds = 10;
        sessionPrefs.end();
    }
    initializeScreen();
    displayPipeline.begin(tft, &displayStretchFullscreen);
    initializeButtons();

    // Start radio work before the animation. Fast association (or its fallback
    // scan) and SNTP run in the WiFi driver while the boot worker initializes
    // storage/audio/touch on Core 0.
    if (wifiProvisioningHasSavedConfig()) {
        String bootWifiSsid;
        String bootWifiError;
        if (wirelessManager.beginSavedWifi(bootWifiSsid, bootWifiError)) {
            Serial.printf("开机联网 已优先尝试上次成功网络：SSID=%s\n",
                          bootWifiSsid.c_str());
        } else {
            Serial.printf("开机联网 启动失败：%s\n", bootWifiError.c_str());
        }
    }
    initializeEspNowGamepads();
    syncHomeClockTime();

    const bool bootTaskStarted = startBootInitialization();
    if (!bootTaskStarted) runBootHardwareInitialization();
    playBootAnimation();
    waitForBootInitialization();

    runInitialTouchCalibrationIfNeeded();
    if (bootToWallpaperHome) {
        screenController.show(AppScreen::Home);
        drawHomeClock(true);
    } else {
        screenController.show(AppScreen::Library);
        drawMainMenu();
    }
    // LittleFS is guaranteed mounted beyond this point, so normal wireless
    // maintenance may now inspect and schedule the cloud-save queue.
    wirelessManager.update();
    mainUiReady = true;
    if (otaCurrentFirmwarePendingVerify()) {
        if (otaBootHealthReady()) {
            otaHealthObservationStartedMs = millis();
            otaHealthObservationActive = true;
            Serial.println("OTA health observation started (10 seconds)");
        } else {
            Serial.println("OTA boot prerequisites failed; rollback remains enabled");
        }
    }
}

void loop() {
    updateOtaHealthObservation();
    otaUpdateReporting();
    // 根据当前状态处理不同逻辑
    switch (screenController.current()) {
        case AppScreen::Home:
            handleHomeInput();
            delay(50);
            return;

        case AppScreen::Library:
            handleMenuInput();
            drawGamepadStatusBadges();
            delay(libraryScreen.touchTracking || fabsf(libraryScreen.scrollVelocity) > 4.0f ? 16 : 50);
            return;

        case AppScreen::Paused:
            handlePauseInput();
            delay(50);
            return;

        case AppScreen::Playing:
            // 正常游戏逻辑
            if (gameJustEntered) {
                pauseFrameDisplayForUi();
                clearScreenForGame();   // ⭐ 强制清左右黑边
                resumeFrameDisplayForGame();
                gameJustEntered = false;
            }
            break;
    }

    // ===== Anemoia 风格游戏运行逻辑 =====
    // 帧级别调度：目标 60Hz 仿真 (16639µs/帧)
    static bool pauseKeyReleased = true;  // 暂停组合键是否已释放
    static bool touchPauseArmed = false;
    static uint32_t touchPauseStartMs = 0;

    // 更新按键输入
    updateButtons();
    touchInputUpdate();
    mergeTouchControlsIntoButtons();
    handlePendingCommands();

    if (touchInputJustPressed()) {
        touchPauseArmed = currentTouchControlsMask() == 0 && currentTouchCanOpenPause();
        touchPauseStartMs = millis();
    }

    if (touchPauseArmed && touchInputTouched() && !currentTouchCanOpenPause()) {
        touchPauseArmed = false;
    }

    if (touchPauseArmed && touchInputTouched() &&
        (uint32_t)(millis() - touchPauseStartMs) >= 650) {
        touchPauseArmed = false;
        enterPauseMenu();
        suppressNextPauseTouchRelease = true;
        return;
    }

    if (touchInputJustReleased()) {
        touchPauseArmed = false;
    }

    // 检测 START + SELECT 组合键进入暂停菜单
    if (buttons.START && buttons.SELECT) {
        if (pauseKeyReleased) {
            pauseKeyReleased = false;
            enterPauseMenu();
            waitForButtonsReleased(DIJI_BTN_START | DIJI_BTN_SELECT);
            delay(50);
            return;
        }
    } else {
        pauseKeyReleased = true;
    }

    uint8_t controllerState = 0;
    if (buttons.A)      controllerState |= 0x01;
    if (buttons.B)      controllerState |= 0x02;
    if (buttons.SELECT) controllerState |= 0x04;
    if (buttons.START)  controllerState |= 0x08;
    if (buttons.UP)     controllerState |= 0x10;
    if (buttons.DOWN)   controllerState |= 0x20;
    if (buttons.LEFT)   controllerState |= 0x40;
    if (buttons.RIGHT)  controllerState |= 0x80;
    nes.setController(0, controllerState);
    nes.setController(1, espNowHostGetControllerState(1));

    // SELECT + LEFT 持续倒带；每次恢复一个 4 帧采样点。
    if (rewindSeconds > 0 && buttons.SELECT && buttons.LEFT && rewindBuffer.available()) {
        if (rewindBuffer.rewindStep()) {
            nes.requestFrameSkip(false);
            nes.clock();
            tryEnqueueFrame();
            gameSession.resetFrameScheduler(3);
        }
        delay(45);
        return;
    }

    // 自适应抽帧：只有在主循环已经落后于目标帧节奏时才跳过本帧渲染。
    bool shouldSkipFrame = gameSession.shouldSkipFrame(
        ENABLE_FRAMESKIP, (int64_t)esp_timer_get_time());
    nes.requestFrameSkip(shouldSkipFrame);

    gameSession.serviceBatteryAutosave();

    // 执行一帧
    uint32_t emu0 = micros();
    //runFrame(); // 方法1: 行级调度
    nes.clock(); // 方法2: 帧级调度
    last_emulation_us = micros() - emu0;
    if (rewindSeconds > 0 && ++rewindCapturePhase >= 4) {
        rewindCapturePhase = 0;
        rewindBuffer.capture();
    }

    // 入队帧缓冲用于 DMA 显示
    tryEnqueueFrame();

    if (gameSession.launchTimedOut()) {
        gameSession.pause();
        pauseFrameDisplayForUi();
        muteAudio();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(0xF800);
        tft.setTextSize(2);
        tft.setFont(&fonts::efontCN_14);
        tft.setCursor(88, 95);
        tft.print("游戏启动失败");
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setTextSize(1);
        tft.setCursor(70, 130);
        tft.print("ROM 不支持或不稳定");
        tft.setCursor(96, 154);
        tft.print("正在返回菜单...");
        delay(3000);
        tft.setFont(&fonts::Font0);
        tft.fillScreen(MENU_BG_COLOR);
        drawMainMenu();
        screenController.show(AppScreen::Library);
        return;
    }

    gameSession.finishFrame(shouldSkipFrame);

    // FPS 统计
    fps_count++;
    uint32_t curMs = millis();
    if (fps_last_ms == 0) fps_last_ms = curMs;
    if (curMs - fps_last_ms >= 1000) {
        FPS_PRINT("FPS:%u  EMU:%uus  DMA:%uus\n",
            fps_count, last_emulation_us, displayPipeline.lastDmaUs());
        fps_count = 0;
        fps_last_ms = curMs;
    }

    // 帧限制
    gameSession.waitForFrameDeadline();
}
