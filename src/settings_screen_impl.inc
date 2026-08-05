#include "sd_card_manager.h"

static void saveDisplayHardwareSettings();
static void applyDisplayHardwareSettings(bool persist);

static String buildSettingsWebJson() {
    String body = "{\"ok\":true,\"volume\":";
    body += String(audioVolumePercent);
    body += ",\"display\":";
    body += displayStretchFullscreen ? "1" : "0";
    body += ",\"rotation\":";
    body += displayRotate180 ? "1" : "0";
    body += ",\"invert\":";
    body += displayColorInverted ? "1" : "0";
    body += ",\"st7789\":";
    body += DIJI_LCD_DRIVER_ST7789 ? "true" : "false";
    body += ",\"original_s3\":";
#if defined(DIJI_BOARD_ORIGINAL_ESP32S3)
    body += "true";
#else
    body += "false";
#endif
    body += ",\"boot\":";
    body += bootToWallpaperHome ? "1" : "0";
    body += ",\"rewind\":";
    body += String(rewindSeconds);
    body += ",\"autooff\":";
    body += String(powerManagerAutoOffMinutes());
    body += "}";
    return body;
}

static bool applySettingsWebValue(const String& key, const String& value,
                                  String& error) {
    error = "";
    if (key == "volume") {
        const bool validVolume = value == "0" || value == "10" || value == "20" ||
                                 value == "30" || value == "40" || value == "50" ||
                                 value == "60" || value == "70" || value == "80" ||
                                 value == "90" || value == "100";
        const int volume = value.toInt();
        if (!validVolume) {
            error = "音量参数无效";
            return false;
        }
        setAudioVolumePercent((uint8_t)volume, true);
        return true;
    }
    if (key == "display") {
        if (value != "0" && value != "1") {
            error = "显示模式参数无效";
            return false;
        }
        displayStretchFullscreen = value == "1";
        if (!displayStretchFullscreen) displayPipeline.requestNativeBorderClear();
        Preferences preferences;
        if (preferences.begin("ui", false)) {
            preferences.putBool("stretch", displayStretchFullscreen);
            preferences.end();
        }
        return true;
    }
    if (key == "rotation") {
        if (value != "0" && value != "1") {
            error = "屏幕旋转参数无效";
            return false;
        }
        displayRotate180 = value == "1";
        applyDisplayHardwareSettings(true);
        return true;
    }
    if (key == "invert") {
#if DIJI_LCD_DRIVER_ST7789
        if (value != "0" && value != "1") {
            error = "屏幕反色参数无效";
            return false;
        }
        displayColorInverted = value == "1";
        applyDisplayHardwareSettings(true);
        return true;
#else
        error = "当前屏幕不支持颜色反转设置";
        return false;
#endif
    }
    if (key == "boot") {
        if (value != "0" && value != "1") {
            error = "开机主页参数无效";
            return false;
        }
        bootToWallpaperHome = value == "1";
        saveBootPageSetting();
        return true;
    }
    if (key == "rewind") {
        const int seconds = value.toInt();
        if (seconds != 0 && seconds != 5 && seconds != 10 && seconds != 20) {
            error = "倒带时长参数无效";
            return false;
        }
        rewindSeconds = (uint8_t)seconds;
        Preferences preferences;
        if (preferences.begin("session", false)) {
            preferences.putUChar("rewind_sec", rewindSeconds);
            preferences.end();
        }
        if (gameSession.hasLoadedRom()) rewindBuffer.begin(rewindSeconds);
        return true;
    }
    if (key == "autooff") {
        const int minutes = value.toInt();
        if (minutes != 0 && minutes != 5 && minutes != 10 && minutes != 20) {
            error = "自动关机参数无效";
            return false;
        }
        if (!powerManagerSetAutoOffMinutes((uint16_t)minutes)) {
            error = "保存自动关机设置失败";
            return false;
        }
        return true;
    }
    error = "未知设置项";
    return false;
}

enum class SettingsAction : uint8_t {
    Wifi, Gamepad, DisplayMode, Rotation, ColorInvert, Volume, TouchCalibration,
    BootPage, Wallpaper, WebManager, Rewind, Power, SdCard, CloudSave, SystemUpdate,
};

struct SettingsItemDefinition {
    const char* label;
    const char* icon;
    uint16_t color;
    SettingsAction action;
};

static const SettingsItemDefinition SETTINGS_ITEMS[] = {
    {"WiFi 配网", "W", 0x057F, SettingsAction::Wifi},
    {"游戏手柄", "P", 0x7A2F, SettingsAction::Gamepad},
#if defined(DIJI_BOARD_ORIGINAL_ESP32S3)
    {"游戏分辨率", "D", 0xFD20, SettingsAction::DisplayMode},
#else
    {"显示模式", "D", 0xFD20, SettingsAction::DisplayMode},
#endif
    {"屏幕旋转", "O", 0x867F, SettingsAction::Rotation},
#if DIJI_LCD_DRIVER_ST7789
    {"屏幕反色", "I", 0xF81F, SettingsAction::ColorInvert},
#endif
    {"音量", "V", 0xF9A6, SettingsAction::Volume},
    {"触摸校准", "+", 0x07E0, SettingsAction::TouchCalibration},
    {"开机主页", "H", 0x5C9F, SettingsAction::BootPage},
    {"壁纸", "B", 0x7C1F, SettingsAction::Wallpaper},
    {"网页管理", "G", 0x05BF, SettingsAction::WebManager},
    {"倒带时长", "R", 0xA81F, SettingsAction::Rewind},
    {"关机设置", "P", 0xE8E4, SettingsAction::Power},
    {"SD 卡", "S", 0x3D8E, SettingsAction::SdCard},
    {"云端存档", "C", 0x4C9F, SettingsAction::CloudSave},
    {"系统更新", "U", 0x043F, SettingsAction::SystemUpdate},
};

static constexpr int SETTINGS_ITEM_COUNT =
    sizeof(SETTINGS_ITEMS) / sizeof(SETTINGS_ITEMS[0]);
static_assert(SETTINGS_ITEM_COUNT <= SettingsScreen::ItemCount,
              "SettingsScreen value cache is too small");
static constexpr int SETTINGS_VISIBLE_ROWS = 4;
static constexpr int SETTINGS_LIST_Y = 38;
static constexpr int SETTINGS_ROW_HEIGHT = 43;
static constexpr int SETTINGS_LIST_HEIGHT = SETTINGS_VISIBLE_ROWS * SETTINGS_ROW_HEIGHT;
static constexpr int SETTINGS_MAX_SCROLL_Y =
    SETTINGS_ITEM_COUNT * SETTINGS_ROW_HEIGHT - SETTINGS_LIST_HEIGHT;
static String settingsItemValue(int index) {
    if (index < 0 || index >= SETTINGS_ITEM_COUNT) return "";
    switch (SETTINGS_ITEMS[index].action) {
        case SettingsAction::Wifi: {
            int saved = wifiProvisioningSavedNetworkCount();
            if (WiFi.status() == WL_CONNECTED) return "已连接";
            return saved > 0 ? "已断开" : "未配置";
        }
        case SettingsAction::Gamepad: {
            int paired = (espNowHostHasPairedController(0) ? 1 : 0) +
                         (espNowHostHasPairedController(1) ? 1 : 0);
            return paired > 0 ? String(paired) + " 个手柄" : "未配对";
        }
#if defined(DIJI_BOARD_ORIGINAL_ESP32S3)
        case SettingsAction::DisplayMode:
            return displayStretchFullscreen ? "320x240" : "256x240";
#else
        case SettingsAction::DisplayMode:
            return displayStretchFullscreen ? "全屏" : "原始比例";
#endif
        case SettingsAction::Rotation:
            return displayRotate180 ? "旋转 180°" : "默认方向";
        case SettingsAction::ColorInvert:
            return displayColorInverted ? "已开启" : "已关闭";
        case SettingsAction::Volume:
            return String(audioVolumePercent) + "%";
        case SettingsAction::TouchCalibration:
            if (!touchInputAvailable()) return "不可用";
            return touchInputHasCalibration() ? "已校准" : "未校准";
        case SettingsAction::BootPage:
            return bootToWallpaperHome ? "壁纸时钟" : "游戏列表";
        case SettingsAction::Wallpaper:
            return wallpaperSelectedDisplayName();
        case SettingsAction::WebManager:
            return settingsWebServerStatus().active ? "已启动" : "未启动";
        case SettingsAction::Rewind: {
            if (rewindSeconds == 0) return "关闭";
            uint8_t actual = rewindBuffer.actualSeconds();
            if (gameSession.hasLoadedRom() && actual < rewindSeconds) {
                return actual == 0 ? "内存不足" : String("可用 ") + actual + " 秒";
            }
            return String(rewindSeconds) + " 秒";
        }
        case SettingsAction::Power: {
            uint16_t autoOff = powerManagerAutoOffMinutes();
            return autoOff == 0 ? "自动关机关闭" : String(autoOff) + " 分钟";
        }
        case SettingsAction::SdCard:
            return sdCardAvailable ? "已挂载" : "未检测";
        case SettingsAction::CloudSave:
            if (cloudSaveSyncInProgress()) return "同步中";
            return cloudSaveHasPending() ? "待同步" : "已同步";
        case SettingsAction::SystemUpdate:
            return otaCurrentVersion();
        default:
            return "";
    }
}

static void refreshSettingsValueCache() {
    for (int i = 0; i < SETTINGS_ITEM_COUNT; i++) {
        settingsScreen.valueCache[i] = settingsItemValue(i);
    }
}

template <typename Surface>
static void renderSettingsList(Surface& surface, int selected, int scrollY, int topY) {
    surface.fillRect(0, topY, 320, SETTINGS_LIST_HEIGHT, MENU_BG_COLOR);
    surface.setClipRect(0, topY, 320, SETTINGS_LIST_HEIGHT);

    for (int index = 0; index < SETTINGS_ITEM_COUNT; index++) {
        int y = topY + index * SETTINGS_ROW_HEIGHT - scrollY;
        if (y + SETTINGS_ROW_HEIGHT <= topY ||
            y >= topY + SETTINGS_LIST_HEIGHT) {
            continue;
        }
        bool focused = index == selected;
        uint16_t fill = focused ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
        uint16_t border = focused ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR;
        surface.fillRoundRect(8, y + 2, 304, SETTINGS_ROW_HEIGHT - 4, 8, fill);
        surface.drawRoundRect(8, y + 2, 304, SETTINGS_ROW_HEIGHT - 4, 8, border);

        const SettingsItemDefinition& item = SETTINGS_ITEMS[index];
        surface.fillRoundRect(16, y + 7, 28, 28, 7, item.color);
        surface.setFont(&fonts::Font2);
        surface.setTextSize(1);
        surface.setTextColor(0xFFFF, item.color);
        int iconWidth = surface.textWidth(item.icon);
        surface.setCursor(16 + (28 - iconWidth) / 2, y + 13);
        surface.print(item.icon);

        surface.setFont(&fonts::efontCN_14);
        surface.setTextColor(focused ? MENU_TITLE_COLOR : MENU_TEXT_COLOR, fill);
        surface.setCursor(54, y + 14);
        surface.print(item.label);

        String value = makeClippedText(settingsScreen.valueCache[index], 110);
        surface.setTextColor(focused ? 0xBFFF : MENU_HINT_COLOR, fill);
        int valueWidth = surface.textWidth(value);
        surface.setCursor(288 - valueWidth, y + 14);
        surface.print(value);
        surface.setTextColor(focused ? MENU_TITLE_COLOR : MENU_HINT_COLOR, fill);
        surface.setCursor(298, y + 14);
        surface.print(">");
    }
    surface.clearClipRect();

    if (SETTINGS_MAX_SCROLL_Y > 0) {
        int trackY = topY + 4;
        constexpr int trackH = SETTINGS_LIST_HEIGHT - 8;
        int thumbH = trackH * SETTINGS_VISIBLE_ROWS / SETTINGS_ITEM_COUNT;
        int boundedScrollY = scrollY;
        if (boundedScrollY < 0) boundedScrollY = 0;
        if (boundedScrollY > SETTINGS_MAX_SCROLL_Y) boundedScrollY = SETTINGS_MAX_SCROLL_Y;
        int thumbY = trackY + (trackH - thumbH) * boundedScrollY / SETTINGS_MAX_SCROLL_Y;
        surface.fillRoundRect(316, trackY, 3, trackH, 2, MENU_BORDER_COLOR);
        surface.fillRoundRect(316, thumbY, 3, thumbH, 2, MENU_ACCENT_COLOR);
    }
    surface.setFont(&fonts::Font0);
    surface.setTextSize(1);
}

static void drawSettingsChrome(bool backFocused = false) {
    tft.startWrite();
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("系统设置");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);
    tft.fillRect(0, 36, 320, SETTINGS_LIST_Y - 36, MENU_BG_COLOR);

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    if (backFocused) {
        tft.fillRect(100, MENU_SOFTBAR_BUTTON_Y, 120,
                     MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
    }
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H,
                 backFocused ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
    tft.setTextColor(backFocused ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                     backFocused ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
    tft.setCursor(144, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
    tft.endWrite();
}

static void drawSettingsMenu(int selected, int scrollY, bool redrawChrome,
                             bool backFocused) {
    if (!settingsScreen.listCanvasReady) {
        settingsScreen.listCanvas.setColorDepth(16);
        settingsScreen.listCanvas.setPsram(true);
        settingsScreen.listCanvasReady =
            settingsScreen.listCanvas.createSprite(320, SETTINGS_LIST_HEIGHT) != nullptr;
    }

    if (settingsScreen.listCanvasReady) {
        renderSettingsList(settingsScreen.listCanvas, selected, scrollY, 0);
        settingsScreen.listCanvas.pushSprite(0, SETTINGS_LIST_Y);
    } else {
        tft.startWrite();
        renderSettingsList(tft, selected, scrollY, SETTINGS_LIST_Y);
        tft.endWrite();
    }
    if (redrawChrome) {
        drawSettingsChrome(backFocused);
    }
}

static void drawVolumeSettingsScreen() {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("音量设置");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
    tft.setCursor(136, 62);
    tft.printf("%u%%", audioVolumePercent);
    drawAudioVolumeBlocks(126, 88, audioVolumePercent);

    tft.fillRect(38, 124, 88, 46, MENU_PANEL_COLOR);
    tft.drawRect(38, 124, 88, 46, MENU_BORDER_COLOR);
    tft.fillRect(194, 124, 88, 46, MENU_PANEL_COLOR);
    tft.drawRect(194, 124, 88, 46, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_PANEL_COLOR);
    tft.setCursor(76, 140);
    tft.print("-");
    tft.setCursor(232, 140);
    tft.print("+");

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(144, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void runVolumeSettingsMenu() {
    uint32_t lastInputMs = 0;
    drawVolumeSettingsScreen();
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                return;
            } else if (buttons.LEFT || buttons.DOWN) {
                setAudioVolumePercent(audioVolumePercent >= 10 ? audioVolumePercent - 10 : 0, true);
                drawVolumeSettingsScreen();
                lastInputMs = now;
            } else if (buttons.RIGHT || buttons.UP || buttons.A || buttons.START) {
                setAudioVolumePercent(audioVolumePercent <= 90 ? audioVolumePercent + 10 : 100, true);
                drawVolumeSettingsScreen();
                lastInputMs = now;
            }
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 38, 124, 88, 46)) {
                setAudioVolumePercent(audioVolumePercent >= 10 ? audioVolumePercent - 10 : 0, true);
                drawVolumeSettingsScreen();
            } else if (touchPointInRect(point, 194, 124, 88, 46)) {
                setAudioVolumePercent(audioVolumePercent <= 90 ? audioVolumePercent + 10 : 100, true);
                drawVolumeSettingsScreen();
            } else if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT)) {
                return;
            }
        }
        delay(25);
    }
}

static void drawSystemInfoScreen(int selected) {
    char ssid[64] = {0};
    char password[64] = {0};
    int savedWifiCount = wifiProvisioningSavedNetworkCount();
    bool connectedWifi = WiFi.status() == WL_CONNECTED;
    bool hasWifi = connectedWifi ||
                   wifiProvisioningGetSavedCredentials(ssid, sizeof(ssid), password, sizeof(password));
    if (connectedWifi) {
        snprintf(ssid, sizeof(ssid), "%s", WiFi.SSID().c_str());
    }

    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("系统更新");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(24, 48);
    tft.printf("游戏数量: %d", (int)romList.size());
    tft.setCursor(24, 68);
    tft.printf("内置存储: %s", builtinStorageAvailable ? "可用" : "不可用");
    tft.setCursor(24, 88);
    tft.printf("SD 卡: %s", sdCardAvailable ? "可用" : "未检测");
    tft.setCursor(24, 108);
    if (connectedWifi) {
        tft.printf("WiFi: 已连接  %ddBm", WiFi.RSSI());
    } else if (savedWifiCount > 0) {
        tft.printf("WiFi: 已断开（已保存 %d 组）", savedWifiCount);
    } else {
        tft.print("WiFi: 未配置");
    }
    if (hasWifi) {
        tft.setCursor(24, 128);
        tft.print("SSID: ");
        drawClippedText(String(ssid), 74, 128, 218);
        tft.setFont(&fonts::efontCN_14);
        tft.setTextSize(1);
    }
    tft.setCursor(24, 148);
    tft.printf("版本: %s", otaCurrentVersion());
    tft.setCursor(24, 168);
    tft.print("设备ID: ");
    tft.print(gameBoxDeviceId());
    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(24, 188);
    tft.printf("可用内存: %uK", (unsigned)(ESP.getFreeHeap() / 1024));

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    if (selected == 0) {
        tft.fillRect(0, MENU_SOFTBAR_BUTTON_Y, 160,
                     MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
    } else {
        tft.fillRect(160, MENU_SOFTBAR_BUTTON_Y, 160,
                     MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
    }
    tft.drawRect(0, MENU_SOFTBAR_BUTTON_Y, 160, MENU_SOFTBAR_BUTTON_H,
                 selected == 0 ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
    tft.drawRect(160, MENU_SOFTBAR_BUTTON_Y, 160, MENU_SOFTBAR_BUTTON_H,
                 selected == 1 ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
    tft.setTextColor(selected == 0 ? MENU_TITLE_COLOR : MENU_ACCENT_COLOR,
                     selected == 0 ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
    tft.setCursor(54, MENU_SOFTBAR_Y + 9);
    tft.print("检查更新");
    tft.setTextColor(selected == 1 ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                     selected == 1 ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
    tft.setCursor(224, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void runSystemInfoScreen() {
    int selected = 0;
    uint32_t lastInputMs = 0;
    drawSystemInfoScreen(selected);
    wl_status_t lastWifiStatus = WiFi.status();
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        wl_status_t currentWifiStatus = WiFi.status();
        if (currentWifiStatus != lastWifiStatus) {
            lastWifiStatus = currentWifiStatus;
            drawSystemInfoScreen(selected);
        }
        uint32_t now = millis();
        if (buttons.B) {
            waitForButtonsReleased(DIJI_BTN_B);
            return;
        }
        if ((uint32_t)(now - lastInputMs) < BUTTON_DEBOUNCE) {
            delay(10);
            continue;
        }
        if (buttons.LEFT || buttons.RIGHT || buttons.SELECT) {
            selected = selected == 0 ? 1 : 0;
            if (buttons.SELECT) waitForButtonsReleased(DIJI_BTN_SELECT);
            drawSystemInfoScreen(selected);
            lastInputMs = millis();
        } else if (buttons.A || buttons.START) {
            waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
            if (selected == 1) return;
            runOtaUpdateMenu();
            drawSystemInfoScreen(selected);
            lastInputMs = millis();
        }
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 160, MENU_SOFTBAR_HEIGHT)) {
                runOtaUpdateMenu();
                drawSystemInfoScreen(selected);
            } else if (touchPointInRect(point, 160, MENU_SOFTBAR_Y, 160, MENU_SOFTBAR_HEIGHT)) {
                return;
            }
        }
        delay(30);
    }
}

static const uint16_t POWER_AUTO_OPTIONS[] = {0, 5, 10, 20};

static void drawPowerSettingsScreen(int selected) {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("关机设置");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(18, 46);
    tft.print("无操作自动关机");

    const char* labels[] = {"关闭", "5分", "10分", "20分"};
    uint16_t current = powerManagerAutoOffMinutes();
    for (int i = 0; i < 4; i++) {
        int x = 12 + i * 76;
        bool focused = selected == i;
        bool active = current == POWER_AUTO_OPTIONS[i];
        uint16_t fill = focused ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
        uint16_t border = focused || active ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR;
        tft.fillRect(x, 70, 68, 42, fill);
        tft.drawRect(x, 70, 68, 42, border);
        if (active) tft.drawRect(x + 2, 72, 64, 38, border);
        tft.setTextColor(focused ? MENU_TITLE_COLOR : MENU_TEXT_COLOR, fill);
        tft.setCursor(x + (68 - tft.textWidth(labels[i])) / 2, 84);
        tft.print(labels[i]);
    }

    uint16_t shutdownFill = selected == 4 ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
    tft.fillRect(54, 130, 212, 52, shutdownFill);
    tft.drawRect(54, 130, 212, 52, selected == 4 ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR);
    tft.setTextColor(selected == 4 ? MENU_TITLE_COLOR : 0xFBE0, shutdownFill);
    tft.setCursor(126, 149);
    tft.print("立即关机");

    tft.setTextColor(powerManagerCanWakeFromSleep() ? MENU_HINT_COLOR : 0xFBE0,
                     MENU_BG_COLOR);
    tft.setCursor(powerManagerCanWakeFromSleep() ? 34 : 22, 192);
    tft.print(powerManagerCanWakeFromSleep()
                  ? "关机后长按屏幕约 1.5 秒开机"
                  : "无唤醒按键：自动关机和立即关机不可用");

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(144, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static bool confirmSoftwarePowerOff() {
    tft.fillScreen(MENU_BG_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
    tft.setCursor(104, 58);
    tft.print("确认关机？");
    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(45, 92);
    tft.print("关机后长按屏幕约 1.5 秒开机");
    bool shutdownFocused = false;
    auto drawButtons = [&]() {
        uint16_t shutdownFill =
            shutdownFocused ? 0xA800 : MENU_PANEL_COLOR;
        uint16_t cancelFill =
            shutdownFocused ? MENU_PANEL_COLOR : MENU_HIGHLIGHT_BG;
        tft.fillRect(20, 138, 130, 50, shutdownFill);
        tft.drawRect(20, 138, 130, 50,
                     shutdownFocused ? 0xF800 : MENU_BORDER_COLOR);
        tft.fillRect(170, 138, 130, 50, cancelFill);
        tft.drawRect(170, 138, 130, 50,
                     shutdownFocused ? MENU_BORDER_COLOR : MENU_TITLE_COLOR);
        tft.setTextColor(shutdownFocused ? 0xFFFF : MENU_TEXT_COLOR,
                         shutdownFill);
        tft.setCursor(66, 157);
        tft.print("关机");
        tft.setTextColor(MENU_TITLE_COLOR, cancelFill);
        tft.setCursor(216, 157);
        tft.print("取消");
        tft.setFont(&fonts::Font0);
    };
    drawButtons();

    uint32_t lastInputMs = 0;
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) < BUTTON_DEBOUNCE) {
            delay(10);
            continue;
        }
        if (buttons.LEFT || buttons.RIGHT) {
            shutdownFocused = !shutdownFocused;
            drawButtons();
            lastInputMs = now;
        } else if (buttons.A || buttons.START) {
            waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
            return shutdownFocused;
        }
        if (buttons.B || buttons.SELECT) return false;
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 20, 138, 130, 50)) return true;
            if (touchPointInRect(point, 170, 138, 130, 50)) return false;
        }
        delay(25);
    }
}

static void activatePowerSelection(int selected) {
    if (!powerManagerCanWakeFromSleep() && selected != 0) {
        return;
    }
    if (selected >= 0 && selected < 4) {
        powerManagerSetAutoOffMinutes(POWER_AUTO_OPTIONS[selected]);
    } else if (selected == 4) {
        waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
        if (confirmSoftwarePowerOff()) {
            enterSoftwarePowerOff(false);
        }
    }
}

static void runPowerSettingsMenu() {
    int selected = 0;
    uint16_t current = powerManagerAutoOffMinutes();
    for (int i = 0; i < 4; i++) {
        if (POWER_AUTO_OPTIONS[i] == current) selected = i;
    }
    uint32_t lastInputMs = 0;
    drawPowerSettingsScreen(selected);
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                return;
            }
            if (buttons.LEFT) {
                if (selected < 4 && selected > 0) selected--;
                drawPowerSettingsScreen(selected);
                lastInputMs = now;
            } else if (buttons.RIGHT) {
                if (selected < 3) selected++;
                drawPowerSettingsScreen(selected);
                lastInputMs = now;
            } else if (buttons.DOWN && selected < 4) {
                selected = 4;
                drawPowerSettingsScreen(selected);
                lastInputMs = now;
            } else if (buttons.UP && selected >= 4) {
                selected = 0;
                drawPowerSettingsScreen(selected);
                lastInputMs = now;
            } else if (buttons.A || buttons.START) {
                activatePowerSelection(selected);
                drawPowerSettingsScreen(selected);
                lastInputMs = now;
            }
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT)) return;
            for (int i = 0; i < 4; i++) {
                if (touchPointInRect(point, 12 + i * 76, 70, 68, 42)) {
                    selected = i;
                    activatePowerSelection(selected);
                    drawPowerSettingsScreen(selected);
                }
            }
            if (touchPointInRect(point, 54, 130, 212, 52)) {
                selected = 4;
                activatePowerSelection(selected);
                drawPowerSettingsScreen(selected);
            }
        }
        delay(25);
    }
}

static void enterSoftwarePowerOff(bool automatic) {
    if (!powerManagerCanWakeFromSleep()) {
        tft.fillScreen(MENU_BG_COLOR);
        tft.setFont(&fonts::efontCN_14);
        tft.setTextSize(1);
        tft.setTextColor(0xFBE0, MENU_BG_COLOR);
        tft.setCursor(58, 90);
        tft.print("当前设备没有可用的唤醒按键");
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        tft.setCursor(52, 122);
        tft.print("已取消软件关机，避免无法开机");
        tft.setFont(&fonts::Font0);
        delay(1800);
        return;
    }
    settingsWebServerStop();
    gameSession.pause();
    pauseFrameDisplayForUi();
    if (gameSession.hasLoadedRom()) {
        gameSession.saveAutoSlot(displayPipeline.lastDisplayedBuffer());
    }
    nes.flushBatterySave(true);

    tft.fillScreen(MENU_BG_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_BG_COLOR);
    tft.setCursor(automatic ? 90 : 112, 76);
    tft.print(automatic ? "长时间无操作" : "正在关机");
    if (automatic) {
        tft.setCursor(112, 104);
        tft.print("自动关机");
    }
    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(45, 148);
    tft.print("长按屏幕约 1.5 秒重新开机");
    tft.setFont(&fonts::Font0);
    delay(700);

    setEs8311Mute(true);
    setAmplifierEnableLevel(DIJI_AMP_ENABLE_LEVEL == HIGH ? LOW : HIGH);
    delay(20);
    if (apuTaskHandle) {
        vTaskSuspend(apuTaskHandle);
    }
    audioOutputEnd();
    wirelessManager.shutdown();

    tft.sleep();
    if (DIJI_LCD_BL_PIN >= 0) {
        digitalWrite(DIJI_LCD_BL_PIN, LOW);
    }
    powerManagerStartDeepSleep();
}

static void drawOtaScreen(const String& status, int progress = -1, size_t speed = 0) {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("系统更新");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(24, 58);
    tft.printf("当前版本: %s", otaCurrentVersion());
    tft.setCursor(24, 82);
    tft.print("设备ID: ");
    tft.print(gameBoxDeviceId());

    tft.fillRect(20, 112, 280, 54, MENU_PANEL_COLOR);
    tft.drawRect(20, 112, 280, 54, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_ACCENT_COLOR, MENU_PANEL_COLOR);
    drawClippedText(status, 32, 126, 256);

    if (progress >= 0) {
        int clamped = progress < 0 ? 0 : (progress > 100 ? 100 : progress);
        tft.drawRect(24, 180, 272, 16, MENU_BORDER_COLOR);
        tft.fillRect(26, 182, (268 * clamped) / 100, 12, MENU_ACCENT_COLOR);
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        tft.setCursor(90, 200);
        tft.printf("%d%%  %u KB/s", clamped, (unsigned)(speed / 1024));
    }
    tft.setFont(&fonts::Font0);
}

static void otaProgressUi(size_t downloaded, size_t total, size_t speed, void*) {
    int percent = total > 0 ? (int)(downloaded * 100 / total) : 0;
    drawOtaScreen("正在下载并校验固件", percent, speed);
}

static bool waitForOtaConfirmation(const String& version) {
    drawOtaScreen(String("发现新版本 ") + version + "，请选择安装");
    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    bool installFocused = false;
    auto drawButtons = [&]() {
        tft.fillRect(0, MENU_SOFTBAR_BUTTON_Y, 320,
                     MENU_SOFTBAR_BUTTON_H, MENU_HEADER_COLOR);
        if (installFocused) {
            tft.fillRect(0, MENU_SOFTBAR_BUTTON_Y, 160,
                         MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
        } else {
            tft.fillRect(160, MENU_SOFTBAR_BUTTON_Y, 160,
                         MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
        }
        tft.drawRect(0, MENU_SOFTBAR_BUTTON_Y, 160, MENU_SOFTBAR_BUTTON_H,
                     installFocused ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
        tft.drawRect(160, MENU_SOFTBAR_BUTTON_Y, 160, MENU_SOFTBAR_BUTTON_H,
                     installFocused ? MENU_BORDER_COLOR : MENU_TITLE_COLOR);
        tft.setFont(&fonts::efontCN_14);
        tft.setTextColor(installFocused ? MENU_TITLE_COLOR : MENU_ACCENT_COLOR,
                         installFocused ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
        tft.setCursor(64, MENU_SOFTBAR_Y + 9);
        tft.print("安装");
        tft.setTextColor(MENU_TITLE_COLOR,
                         installFocused ? MENU_HEADER_COLOR : MENU_HIGHLIGHT_BG);
        tft.setCursor(224, MENU_SOFTBAR_Y + 9);
        tft.print("取消");
        tft.setFont(&fonts::Font0);
    };
    drawButtons();
    uint32_t lastInputMs = 0;
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B || buttons.SELECT) return false;
            if (buttons.LEFT || buttons.RIGHT) {
                installFocused = !installFocused;
                drawButtons();
                lastInputMs = now;
                continue;
            }
            if (buttons.A || buttons.START) {
                waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
                return installFocused;
            }
            lastInputMs = now;
        }
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (point.x < 160) return true;
            return false;
        }
        delay(25);
    }
}

static void runOtaUpdateMenu() {
    CloudSaveBackgroundPauseGuard cloudSyncPause;
    if (!wifiProvisioningHasSavedConfig()) {
        drawOtaScreen("请先在设置中完成 WiFi 配网");
        delay(1800);
        return;
    }

    String ssid;
    String error;
    if (!wirelessManager.wifiActive()) {
        drawOtaScreen("正在连接 WiFi...");
    }
    if (!wirelessManager.connectSavedWifi(ssid, error)) {
        drawOtaScreen(error);
        delay(1800);
        wirelessManager.releaseNetworkTask();
        return;
    }

    OtaUpdateInfo info;
    drawOtaScreen("正在检查新版本...");
    if (!otaCheckForUpdate(info, error)) {
        drawOtaScreen(error);
        delay(1800);
        wirelessManager.releaseNetworkTask();
        return;
    }

    auto startGamepadUpdates = [&info]() {
        int started = 0;
        for (uint8_t player = 0; player < 2; player++) {
            const OtaUpdateInfo::GamepadUpdate& update = info.gamepads[player];
            if (!update.present || !update.available) continue;
            if (espNowHostStartUpdate(player, update.version.c_str(),
                                      update.url.c_str(), update.sha256.c_str(),
                                      update.size)) {
                started++;
            }
        }
        return started;
    };

    if (!info.available) {
        int availableGamepads = 0;
        for (uint8_t player = 0; player < 2; player++) {
            if (info.gamepads[player].present &&
                info.gamepads[player].available) {
                availableGamepads++;
            }
        }
        int started = startGamepadUpdates();
        if (started > 0) {
            drawOtaScreen(String("主机已是最新，已通知 ") + started +
                          " 个手柄升级");
        } else if (availableGamepads > 0) {
            drawOtaScreen("发现手柄更新，但升级申请发送失败");
        } else {
            drawOtaScreen(info.message.length() ? info.message : "当前已是最新版本");
        }
        delay(1800);
        wirelessManager.releaseNetworkTask();
        return;
    }
    if (!info.forced && !waitForOtaConfirmation(info.version)) {
        wirelessManager.releaseNetworkTask();
        return;
    }

    gameSession.pause();
    muteAudio();
    if (gameSession.hasLoadedRom()) {
        gameSession.saveAutoSlot(displayPipeline.lastDisplayedBuffer());
    }
    nes.flushBatterySave(true);
    drawOtaScreen(String("准备安装 ") + info.version, 0, 0);
    if (!otaInstallUpdate(info, otaProgressUi, nullptr, error)) {
        drawOtaScreen(error);
        delay(2200);
        wirelessManager.releaseNetworkTask();
        return;
    }

    int startedGamepads = startGamepadUpdates();
    drawOtaScreen(startedGamepads > 0
                      ? String("主机完成，已通知 ") + startedGamepads + " 个手柄"
                      : "升级成功，正在重启...",
                  100, 0);
    delay(1000);
    ESP.restart();
}

static void loadBootPageSetting() {
    Preferences preferences;
    if (preferences.begin("ui", true)) {
        bootToWallpaperHome = preferences.getBool("boot_home", true);
        displayStretchFullscreen = preferences.getBool("stretch", true);
        displayRotate180 = preferences.getBool("rotate180", false);
#if DIJI_LCD_DRIVER_ST7789
        displayColorInverted = preferences.getBool("lcd_invert", DIJI_LCD_INVERT);
#else
        displayColorInverted = DIJI_LCD_INVERT;
#endif
        preferences.end();
    }
}

static void saveDisplayHardwareSettings() {
    Preferences preferences;
    if (preferences.begin("ui", false)) {
        preferences.putBool("rotate180", displayRotate180);
#if DIJI_LCD_DRIVER_ST7789
        preferences.putBool("lcd_invert", displayColorInverted);
#endif
        preferences.end();
    }
}

static void applyDisplayHardwareSettings(bool persist) {
    tft.waitDMA();
    tft.setRotation(effectiveDisplayRotation());
#if DIJI_LCD_DRIVER_ST7789
    tft.invertDisplay(displayColorInverted);
#endif
    touchInputSetDisplayRotation(effectiveDisplayRotation());
    tft.fillScreen(MENU_BG_COLOR);
    if (persist) saveDisplayHardwareSettings();
#if DIJI_LCD_DRIVER_ST7789
    Serial.printf("屏幕设置：旋转=%u 反色=%s\n",
                  (unsigned)effectiveDisplayRotation(),
                  displayColorInverted ? "开启" : "关闭");
#else
    Serial.printf("屏幕设置：旋转=%u\n", (unsigned)effectiveDisplayRotation());
#endif
}

static void saveBootPageSetting() {
    Preferences preferences;
    if (preferences.begin("ui", false)) {
        preferences.putBool("boot_home", bootToWallpaperHome);
        preferences.end();
    }
}

static constexpr int CLOUD_SAVE_SYNC_X = 12;
static constexpr int CLOUD_SAVE_RESTORE_X = 164;
static constexpr int CLOUD_SAVE_ACTION_Y = 92;
static constexpr int CLOUD_SAVE_ACTION_W = 144;
static constexpr int CLOUD_SAVE_ACTION_H = 62;

static void drawCloudSaveScreen(const String& message, int selected = 0) {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(126, 10);
    tft.print("云端存档");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(20, 58);
    tft.print(message);

    uint16_t syncFill = selected == 0 ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
    uint16_t restoreFill = selected == 1 ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
    tft.fillRoundRect(CLOUD_SAVE_SYNC_X, CLOUD_SAVE_ACTION_Y,
                      CLOUD_SAVE_ACTION_W, CLOUD_SAVE_ACTION_H, 8, syncFill);
    tft.drawRoundRect(CLOUD_SAVE_SYNC_X, CLOUD_SAVE_ACTION_Y,
                      CLOUD_SAVE_ACTION_W, CLOUD_SAVE_ACTION_H, 8,
                      selected == 0 ? MENU_TITLE_COLOR : MENU_ACCENT_COLOR);
    tft.fillRoundRect(CLOUD_SAVE_RESTORE_X, CLOUD_SAVE_ACTION_Y,
                      CLOUD_SAVE_ACTION_W, CLOUD_SAVE_ACTION_H, 8, restoreFill);
    tft.drawRoundRect(CLOUD_SAVE_RESTORE_X, CLOUD_SAVE_ACTION_Y,
                      CLOUD_SAVE_ACTION_W, CLOUD_SAVE_ACTION_H, 8,
                      selected == 1 ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
    tft.setTextColor(selected == 0 ? MENU_TITLE_COLOR : MENU_ACCENT_COLOR,
                     syncFill);
    tft.setCursor(CLOUD_SAVE_SYNC_X + 44, CLOUD_SAVE_ACTION_Y + 22);
    tft.print("立即同步");
    tft.setTextColor(selected == 1 ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                     restoreFill);
    tft.setCursor(CLOUD_SAVE_RESTORE_X + 37, CLOUD_SAVE_ACTION_Y + 22);
    tft.print("从云端恢复");

    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(20, 168);
    tft.print("存档先安全保存在本地，联网时自动同步");
    tft.setCursor(20, 190);
    tft.print("方向键选择，A / START 执行");
    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    if (selected == 2) {
        tft.fillRect(100, MENU_SOFTBAR_BUTTON_Y, 120,
                     MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
    }
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H,
                 selected == 2 ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
    tft.setTextColor(selected == 2 ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                     selected == 2 ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
    tft.setCursor(136, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void runCloudSaveMenu() {
    CloudSaveBackgroundPauseGuard cloudSyncPause;
    int selected = 0;
    uint32_t lastInputMs = 0;
    String pageMessage =
        cloudSaveHasPending() ? "存在尚未上传的本地存档" : "云存档自动同步已启用";
    drawCloudSaveScreen(pageMessage, selected);
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        if (buttons.B) {
            waitForButtonsReleased(DIJI_BTN_B);
            return;
        }

        bool actionRequested = false;
        bool restoreFromCloud = false;
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
            (buttons.LEFT || buttons.RIGHT)) {
            if (selected == 2) selected = 0;
            else selected = selected == 0 ? 1 : 0;
            drawCloudSaveScreen(pageMessage, selected);
            lastInputMs = now;
        } else if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
                   buttons.DOWN) {
            selected = 2;
            drawCloudSaveScreen(pageMessage, selected);
            lastInputMs = now;
        } else if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
                   (buttons.UP || buttons.SELECT)) {
            selected = selected == 2 ? 0 : 2;
            if (buttons.SELECT) waitForButtonsReleased(DIJI_BTN_SELECT);
            drawCloudSaveScreen(pageMessage, selected);
            lastInputMs = millis();
        } else if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
                   (buttons.A || buttons.START)) {
            if (selected == 2) return;
            actionRequested = true;
            restoreFromCloud = selected == 1;
            waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
        } else if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, CLOUD_SAVE_SYNC_X, CLOUD_SAVE_ACTION_Y,
                                 CLOUD_SAVE_ACTION_W, CLOUD_SAVE_ACTION_H)) {
                actionRequested = true;
            } else if (touchPointInRect(point, CLOUD_SAVE_RESTORE_X, CLOUD_SAVE_ACTION_Y,
                                        CLOUD_SAVE_ACTION_W, CLOUD_SAVE_ACTION_H)) {
                actionRequested = true;
                restoreFromCloud = true;
            } else if (touchPointInRect(point, 0, MENU_SOFTBAR_Y,
                                        320, MENU_SOFTBAR_HEIGHT)) {
                return;
            }
        }
        if (!actionRequested) { delay(30); continue; }

        if (!gameSession.hasLoadedRom() && restoreFromCloud) {
            drawCloudSaveScreen("请先启动一次要恢复的游戏");
            delay(1600);
            return;
        }
        String ssid;
        String error;
        if (!wirelessManager.wifiActive()) {
            drawCloudSaveScreen("正在连接 WiFi...");
        }
        if (!wirelessManager.connectSavedWifi(ssid, error)) {
            drawCloudSaveScreen(error);
            delay(1800);
            return;
        }
        int changed = 0;
        bool ok = false;
        if (restoreFromCloud) {
            drawCloudSaveScreen("正在校验并恢复云端存档...");
            ok = cloudSaveRestoreRomSaves(gameSession.currentRomPath(), changed, error);
            drawCloudSaveScreen(ok ? String("恢复完成：") + changed + " 个存档" : error);
        } else {
            if (gameSession.hasLoadedRom()) {
                gameSession.saveAutoSlot(displayPipeline.lastDisplayedBuffer());
                if (nes.getCart().hasSRAM() && nes.flushBatterySave(true)) {
                    cloudSaveMarkPending(gameSession.currentRomPath(), "battery");
                }
            }
            drawCloudSaveScreen("正在同步待上传存档...");
            ok = cloudSaveSyncPending(changed, error);
            drawCloudSaveScreen(ok ? String("同步完成：") + changed + " 个存档" : error);
        }
        delay(2000);
        wirelessManager.releaseNetworkTask();
        return;
    }
}

static constexpr int SD_CARD_REFRESH_X = 12;
static constexpr int SD_CARD_FORMAT_X = 164;
static constexpr int SD_CARD_ACTION_Y = 150;
static constexpr int SD_CARD_ACTION_W = 144;
static constexpr int SD_CARD_ACTION_H = 46;

static String formatSdCardBytes(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        return String((double)bytes / (1024.0 * 1024.0 * 1024.0), 1) + " GB";
    }
    if (bytes >= 1024ULL * 1024ULL) {
        return String((double)bytes / (1024.0 * 1024.0), 1) + " MB";
    }
    if (bytes >= 1024ULL) return String((double)bytes / 1024.0, 1) + " KB";
    return String((unsigned long)bytes) + " B";
}

static void drawSdCardScreen(const String& message = "", int selected = 0) {
    const SdCardStatus status = sdCardReadStatus(sdCardAvailable);
    const uint64_t freeBytes = status.totalBytes > status.usedBytes
                                   ? status.totalBytes - status.usedBytes
                                   : 0;

    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(132, 10);
    tft.print("SD 卡状态");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(20, 48);
    tft.printf("状态: %s", status.mounted ? "已挂载" : "未检测到 SD 卡");
    tft.setCursor(20, 68);
    if (status.mounted) {
        tft.printf("类型: %s    文件系统: %s",
                   status.cardType == CARD_MMC ? "MMC" :
                   status.cardType == CARD_SD ? "SDSC" :
                   status.cardType == CARD_SDHC ? "SDHC/SDXC" : "未知",
                   status.fileSystem.c_str());
        tft.setCursor(20, 88);
        tft.printf("容量: %s", formatSdCardBytes(status.totalBytes).c_str());
        tft.setCursor(20, 108);
        tft.printf("已用: %s    剩余: %s",
                   formatSdCardBytes(status.usedBytes).c_str(),
                   formatSdCardBytes(freeBytes).c_str());
    } else {
        tft.print("请插入 SD 卡后点击重新检测");
    }

    if (message.length()) {
        tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
        tft.setCursor(20, 128);
        drawClippedText(message, 20, 128, 280);
        tft.setFont(&fonts::efontCN_14);
        tft.setTextSize(1);
    }

    uint16_t refreshFill = selected == 0 ? MENU_HIGHLIGHT_BG : MENU_PANEL_COLOR;
    uint16_t formatFill = selected == 1 ? 0xA800 : MENU_PANEL_COLOR;
    tft.fillRoundRect(SD_CARD_REFRESH_X, SD_CARD_ACTION_Y,
                      SD_CARD_ACTION_W, SD_CARD_ACTION_H, 8, refreshFill);
    tft.drawRoundRect(SD_CARD_REFRESH_X, SD_CARD_ACTION_Y,
                      SD_CARD_ACTION_W, SD_CARD_ACTION_H, 8,
                      selected == 0 ? MENU_TITLE_COLOR : MENU_ACCENT_COLOR);
    tft.fillRoundRect(SD_CARD_FORMAT_X, SD_CARD_ACTION_Y,
                      SD_CARD_ACTION_W, SD_CARD_ACTION_H, 8, formatFill);
    tft.drawRoundRect(SD_CARD_FORMAT_X, SD_CARD_ACTION_Y,
                      SD_CARD_ACTION_W, SD_CARD_ACTION_H, 8,
                      selected == 1 ? 0xF800 : MENU_BORDER_COLOR);
    tft.setTextColor(selected == 0 ? MENU_TITLE_COLOR : MENU_ACCENT_COLOR,
                     refreshFill);
    tft.setCursor(SD_CARD_REFRESH_X + 39, SD_CARD_ACTION_Y + 15);
    tft.print("重新检测");
    tft.setTextColor(selected == 1 ? 0xFFFF : 0xFBE0, formatFill);
    tft.setCursor(SD_CARD_FORMAT_X + 34, SD_CARD_ACTION_Y + 15);
    tft.print("格式化 SD 卡");

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    if (selected == 2) {
        tft.fillRect(100, MENU_SOFTBAR_BUTTON_Y, 120,
                     MENU_SOFTBAR_BUTTON_H, MENU_HIGHLIGHT_BG);
    }
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H,
                 selected == 2 ? MENU_TITLE_COLOR : MENU_BORDER_COLOR);
    tft.setTextColor(selected == 2 ? MENU_TITLE_COLOR : MENU_TEXT_COLOR,
                     selected == 2 ? MENU_HIGHLIGHT_BG : MENU_HEADER_COLOR);
    tft.setCursor(136, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static bool confirmSdCardFormat() {
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(0xFBE0, MENU_HEADER_COLOR);
    tft.setCursor(112, 10);
    tft.print("确认格式化 SD 卡");
    tft.drawFastHLine(20, 34, 280, 0xF800);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    tft.setCursor(24, 58);
    tft.print("此操作会永久删除 SD 卡中的：");
    tft.setTextColor(0xFBE0, MENU_BG_COLOR);
    tft.setCursor(42, 82);
    tft.print("所有游戏 ROM、存档和截图");
    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(36, 108);
    tft.print("格式化过程中请勿拔卡或关闭电源");

    bool formatFocused = false;
    auto drawButtons = [&]() {
        uint16_t cancelFill =
            formatFocused ? MENU_PANEL_COLOR : MENU_HIGHLIGHT_BG;
        uint16_t formatFill =
            formatFocused ? 0xA800 : MENU_PANEL_COLOR;
        tft.fillRoundRect(12, 142, 144, 54, 8, cancelFill);
        tft.drawRoundRect(12, 142, 144, 54, 8,
                          formatFocused ? MENU_BORDER_COLOR : MENU_TITLE_COLOR);
        tft.fillRoundRect(164, 142, 144, 54, 8, formatFill);
        tft.drawRoundRect(164, 142, 144, 54, 8,
                          formatFocused ? 0xF800 : MENU_BORDER_COLOR);
        tft.setTextColor(MENU_TITLE_COLOR, cancelFill);
        tft.setCursor(68, 161);
        tft.print("取消");
        tft.setTextColor(formatFocused ? 0xFFFF : MENU_TEXT_COLOR, formatFill);
        tft.setCursor(196, 161);
        tft.print("确认格式化");
        tft.setFont(&fonts::Font0);
    };
    drawButtons();

    uint32_t lastInputMs = 0;
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) < BUTTON_DEBOUNCE) {
            delay(10);
            continue;
        }
        if (buttons.B || buttons.SELECT) return false;
        if (buttons.LEFT || buttons.RIGHT) {
            formatFocused = !formatFocused;
            drawButtons();
            lastInputMs = now;
        } else if (buttons.A || buttons.START) {
            waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
            return formatFocused;
        }
        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 12, 142, 144, 54)) return false;
            if (touchPointInRect(point, 164, 142, 144, 54)) return true;
        }
        delay(25);
    }
}

static bool refreshSdCard() {
    CloudSaveBackgroundPauseGuard cloudSyncPause;
    if (sdCardAvailable) {
        DIJI_SD.end();
        sdCardAvailable = false;
        delay(80);
    }
    const bool mounted = tryInitSD();
    scanROMFiles();
    Serial.printf("SD settings: refresh %s\n", mounted ? "ok" : "failed");
    return mounted;
}

static bool formatSdCard(String& error) {
    CloudSaveBackgroundPauseGuard cloudSyncPause;
    Serial.println("SD settings: format requested");
    if (!sdCardAvailable) {
        // A card with an unsupported/corrupt filesystem cannot reach the normal
        // mounted state. This user-confirmed path lets Arduino format it while
        // mounting; the explicit FatFs pass below then guarantees a clean card.
        if (!tryInitSD(true)) {
            error = "未检测到可格式化的 SD 卡";
            Serial.printf("SD settings: format mount failed: %s\n", error.c_str());
            return false;
        }
    }

    drawSdCardScreen("正在格式化，请勿断电或拔卡...");
    const bool formatted = sdCardFormatMounted(error);
    if (!formatted) {
        // A failed low-level remount must not leave the Arduino FS wrapper in a
        // half-mounted state. Reinitialize it before returning to the page.
        DIJI_SD.end();
        sdCardAvailable = false;
        delay(80);
        tryInitSD();
        scanROMFiles();
        Serial.printf("SD settings: format failed: %s\n", error.c_str());
        return false;
    }

    sdCardAvailable = true;
    rewindBuffer.clear();
    scanROMFiles();
    if (lastResumeRomPath.startsWith(DIJI_ROM_SD_PREFIX)) {
        lastResumeRomPath = "";
        Preferences preferences;
        if (preferences.begin("session", false)) {
            preferences.remove("last_rom");
            preferences.end();
        }
    }
    Serial.println("SD settings: format completed");
    return true;
}

static void runSdCardMenu() {
    int selected = 0;
    uint32_t lastInputMs = 0;
    String message;
    drawSdCardScreen(message, selected);
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        if (buttons.B) {
            waitForButtonsReleased(DIJI_BTN_B);
            return;
        }

        bool refreshRequested = false;
        bool formatRequested = false;
        uint32_t now = millis();
        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
            (buttons.LEFT || buttons.RIGHT)) {
            if (selected == 2) selected = 0;
            else selected = selected == 0 ? 1 : 0;
            drawSdCardScreen(message, selected);
            lastInputMs = now;
        } else if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
                   buttons.DOWN) {
            selected = 2;
            drawSdCardScreen(message, selected);
            lastInputMs = now;
        } else if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
                   (buttons.UP || buttons.SELECT)) {
            selected = selected == 2 ? 0 : 2;
            if (buttons.SELECT) waitForButtonsReleased(DIJI_BTN_SELECT);
            drawSdCardScreen(message, selected);
            lastInputMs = millis();
        } else if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE &&
                   (buttons.A || buttons.START)) {
            waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
            if (selected == 2) return;
            refreshRequested = selected == 0;
            formatRequested = selected == 1;
        } else if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, SD_CARD_REFRESH_X, SD_CARD_ACTION_Y,
                                 SD_CARD_ACTION_W, SD_CARD_ACTION_H)) {
                refreshRequested = true;
            } else if (touchPointInRect(point, SD_CARD_FORMAT_X, SD_CARD_ACTION_Y,
                                        SD_CARD_ACTION_W, SD_CARD_ACTION_H)) {
                formatRequested = true;
            } else if (touchPointInRect(point, 0, MENU_SOFTBAR_Y,
                                        320, MENU_SOFTBAR_HEIGHT)) {
                return;
            }
        }

        if (refreshRequested) {
            settingsWebServerStop();
            drawSdCardScreen("正在重新检测 SD 卡...");
            const bool mounted = refreshSdCard();
            settingsWebServerBegin(sdCardAvailable, buildSettingsWebJson,
                                   applySettingsWebValue);
            message = mounted ? "SD 卡已重新挂载" : "未检测到可用 SD 卡";
            drawSdCardScreen(message, selected);
        } else if (formatRequested) {
            if (confirmSdCardFormat()) {
                settingsWebServerStop();
                String error;
                bool ok = formatSdCard(error);
                settingsWebServerBegin(sdCardAvailable, buildSettingsWebJson,
                                       applySettingsWebValue);
                message = ok ? "格式化完成，已创建 rom 和 saves 目录" : error;
                drawSdCardScreen(message, selected);
            } else {
                message = "已取消格式化";
                drawSdCardScreen(message, selected);
            }
        }
        delay(25);
    }
}

static constexpr int WALLPAPER_GRID_X = 4;
static constexpr int WALLPAPER_GRID_Y = 82;
static constexpr int WALLPAPER_GRID_COLUMNS = 3;
static constexpr int WALLPAPER_GRID_ROWS = 2;
static constexpr int WALLPAPER_GRID_PAGE_SIZE =
    WALLPAPER_GRID_COLUMNS * WALLPAPER_GRID_ROWS;
static constexpr int WALLPAPER_CELL_WIDTH = 104;
static constexpr int WALLPAPER_CELL_HEIGHT = 62;
static constexpr int WALLPAPER_THUMB_WIDTH = 72;
static constexpr int WALLPAPER_THUMB_HEIGHT = 54;

static std::vector<String> wallpaperMenuEntries() {
    std::vector<String> names;
    names.push_back("");
    if (!sdCardAvailable) return names;
    const std::vector<WallpaperEntry> files = wallpaperListSd();
    for (const WallpaperEntry& file : files) names.push_back(file.name);
    return names;
}

static int selectedWallpaperMenuIndex(const std::vector<String>& names) {
    const String current = wallpaperSelectedName();
    for (size_t i = 0; i < names.size(); i++) {
        if (names[i] == current) return (int)i;
    }
    return 0;
}

static bool drawWallpaperThumbnail(const String& name, int x, int y) {
    bool drawn = false;
    if (name.isEmpty()) {
        const size_t wallpaperSize =
            (size_t)(gamebox_clock_wallpaper_end - gamebox_clock_wallpaper_start);
        if (wallpaperSize > 0) {
            drawn = tft.drawJpg(gamebox_clock_wallpaper_start, wallpaperSize,
                                x, y, WALLPAPER_THUMB_WIDTH,
                                WALLPAPER_THUMB_HEIGHT, 0, 0, 0.225f);
        }
    } else {
        const String path = wallpaperPathForName(name);
        File wallpaperFile = DIJI_SD.open(path, FILE_READ);
        if (wallpaperFile) {
            lgfx::v1::DataWrapperT<fs::File> data(&wallpaperFile);
            drawn = tft.drawJpg(&data, x, y, WALLPAPER_THUMB_WIDTH,
                                WALLPAPER_THUMB_HEIGHT, 0, 0, 0.225f);
            data.close();
        }
    }
    if (!drawn) {
        tft.fillRect(x, y, WALLPAPER_THUMB_WIDTH, WALLPAPER_THUMB_HEIGHT, 0x0841);
        tft.drawFastHLine(x + 8, y + 17, WALLPAPER_THUMB_WIDTH - 16,
                          MENU_BORDER_COLOR);
        tft.drawFastHLine(x + 8, y + 35, WALLPAPER_THUMB_WIDTH - 16,
                          MENU_BORDER_COLOR);
    }
    return drawn;
}

static void drawWallpaperSettingsScreen(const std::vector<String>& names,
                                        int selected, const String& message = "") {
    const SettingsWebServerStatus web = settingsWebServerStatus();
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(124, 10);
    tft.print("壁纸设置");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(web.active ? 0x07E0 : 0xFBE0, MENU_BG_COLOR);
    tft.setCursor(12, 43);
    if (web.active) {
        tft.print(web.accessPoint ? "热点: " : "网页服务: 已启动");
        if (web.accessPoint) drawClippedText(web.ssid, 58, 43, 250);
    } else {
        drawClippedText(web.message, 12, 43, 296);
    }
    tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
    if (!message.isEmpty()) {
        drawClippedText(message, 12, 62, 260);
    } else if (web.active) {
        drawClippedText(web.url, 12, 62, 260);
    } else {
        drawClippedText("插入 SD 卡后可上传 320x240 JPEG", 12, 62, 260);
    }

    const int first = (selected / WALLPAPER_GRID_PAGE_SIZE) * WALLPAPER_GRID_PAGE_SIZE;
    const int pageCount = std::max(1, ((int)names.size() + WALLPAPER_GRID_PAGE_SIZE - 1) /
                                      WALLPAPER_GRID_PAGE_SIZE);
    const int page = first / WALLPAPER_GRID_PAGE_SIZE + 1;
    tft.setFont(&fonts::Font0);
    tft.setTextSize(1);
    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(282, 64);
    tft.printf("%d/%d", page, pageCount);

    const String current = wallpaperSelectedName();
    for (int slot = 0; slot < WALLPAPER_GRID_PAGE_SIZE; slot++) {
        const int index = first + slot;
        const int column = slot % WALLPAPER_GRID_COLUMNS;
        const int row = slot / WALLPAPER_GRID_COLUMNS;
        const int cellX = WALLPAPER_GRID_X + column * WALLPAPER_CELL_WIDTH;
        const int cellY = WALLPAPER_GRID_Y + row * WALLPAPER_CELL_HEIGHT;
        const int thumbX = cellX + (WALLPAPER_CELL_WIDTH - WALLPAPER_THUMB_WIDTH) / 2;
        const int thumbY = cellY + (WALLPAPER_CELL_HEIGHT - WALLPAPER_THUMB_HEIGHT) / 2;
        tft.fillRoundRect(cellX + 1, cellY + 1, WALLPAPER_CELL_WIDTH - 4,
                          WALLPAPER_CELL_HEIGHT - 3, 6, MENU_PANEL_COLOR);
        if (index >= (int)names.size()) {
            tft.drawRoundRect(cellX + 1, cellY + 1, WALLPAPER_CELL_WIDTH - 4,
                              WALLPAPER_CELL_HEIGHT - 3, 6, MENU_BORDER_COLOR);
            continue;
        }
        const bool active = names[index] == current;
        drawWallpaperThumbnail(names[index], thumbX, thumbY);
        tft.drawRoundRect(cellX + 1, cellY + 1, WALLPAPER_CELL_WIDTH - 4,
                          WALLPAPER_CELL_HEIGHT - 3, 6,
                          index == selected ? MENU_ACCENT_COLOR : MENU_BORDER_COLOR);
        if (active) {
            tft.drawRoundRect(cellX + 3, cellY + 3, WALLPAPER_CELL_WIDTH - 8,
                              WALLPAPER_CELL_HEIGHT - 7, 5, 0x07E0);
            tft.fillCircle(cellX + WALLPAPER_CELL_WIDTH - 10, cellY + 10, 5, 0x07E0);
            tft.drawCircle(cellX + WALLPAPER_CELL_WIDTH - 10, cellY + 10, 5, 0xFFFF);
        }
    }
    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(0, MENU_SOFTBAR_BUTTON_Y, 160, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
    tft.drawRect(160, MENU_SOFTBAR_BUTTON_Y, 160, MENU_SOFTBAR_BUTTON_H, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_ACCENT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(54, MENU_SOFTBAR_Y + 9);
    tft.print("选择");
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(224, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void runWallpaperSettingsMenu() {
    std::vector<String> names = wallpaperMenuEntries();
    int selected = selectedWallpaperMenuIndex(names);
    String message;
    String lastKnownSelected = wallpaperSelectedName();
    uint32_t lastRefreshMs = millis();
    uint32_t lastInputMs = 0;
    drawWallpaperSettingsScreen(names, selected);

    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        const uint32_t now = millis();
        bool redraw = false;
        bool leave = false;

        if ((uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                leave = true;
            } else if (buttons.LEFT && selected > 0) {
                selected--;
                message = "";
                redraw = true;
                lastInputMs = now;
            } else if (buttons.RIGHT && selected + 1 < (int)names.size()) {
                selected++;
                message = "";
                redraw = true;
                lastInputMs = now;
            } else if (buttons.UP && selected >= WALLPAPER_GRID_COLUMNS) {
                selected -= WALLPAPER_GRID_COLUMNS;
                message = "";
                redraw = true;
                lastInputMs = now;
            } else if (buttons.DOWN &&
                       selected + WALLPAPER_GRID_COLUMNS < (int)names.size()) {
                selected += WALLPAPER_GRID_COLUMNS;
                message = "";
                redraw = true;
                lastInputMs = now;
            } else if (buttons.A || buttons.START) {
                String error;
                const bool ok = wallpaperSelect(names[selected], error);
                message = ok ? "壁纸已切换" : error;
                homeScreen.invalidate();
                redraw = true;
                lastInputMs = now;
            }
        }

        if (touchInputJustReleased()) {
            const TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 160, MENU_SOFTBAR_Y, 160, MENU_SOFTBAR_HEIGHT)) {
                leave = true;
            } else if (touchPointInRect(point, 0, MENU_SOFTBAR_Y, 160, MENU_SOFTBAR_HEIGHT)) {
                String error;
                const bool ok = wallpaperSelect(names[selected], error);
                message = ok ? "壁纸已切换" : error;
                homeScreen.invalidate();
                redraw = true;
            } else if (point.x >= WALLPAPER_GRID_X &&
                       point.x < WALLPAPER_GRID_X + WALLPAPER_GRID_COLUMNS * WALLPAPER_CELL_WIDTH &&
                       point.y >= WALLPAPER_GRID_Y &&
                       point.y < WALLPAPER_GRID_Y + WALLPAPER_GRID_ROWS * WALLPAPER_CELL_HEIGHT) {
                const int first = (selected / WALLPAPER_GRID_PAGE_SIZE) *
                                  WALLPAPER_GRID_PAGE_SIZE;
                const int column = (point.x - WALLPAPER_GRID_X) / WALLPAPER_CELL_WIDTH;
                const int row = (point.y - WALLPAPER_GRID_Y) / WALLPAPER_CELL_HEIGHT;
                const int index = first + row * WALLPAPER_GRID_COLUMNS + column;
                if (index >= 0 && index < (int)names.size()) {
                    selected = index;
                    String error;
                    const bool ok = wallpaperSelect(names[selected], error);
                    message = ok ? "壁纸已切换" : error;
                    homeScreen.invalidate();
                    redraw = true;
                }
            }
        }

        if (leave) {
            return;
        }

        if ((uint32_t)(now - lastRefreshMs) >= 800) {
            lastRefreshMs = now;
            const String currentSelection = wallpaperSelectedName();
            const String focusedName = selected >= 0 && selected < (int)names.size()
                                           ? names[selected] : String();
            std::vector<String> refreshed = wallpaperMenuEntries();
            bool changed = refreshed.size() != names.size();
            if (!changed) {
                for (size_t i = 0; i < names.size(); i++) {
                    if (names[i] != refreshed[i]) { changed = true; break; }
                }
            }
            names.swap(refreshed);
            const int currentIndex = selectedWallpaperMenuIndex(names);
            const bool selectionChanged = currentSelection != lastKnownSelected;
            if (selectionChanged) {
                selected = currentIndex;
                lastKnownSelected = currentSelection;
                homeScreen.invalidate();
                redraw = true;
            } else if (changed) {
                selected = 0;
                for (size_t i = 0; i < names.size(); i++) {
                    if (names[i] == focusedName) {
                        selected = (int)i;
                        break;
                    }
                }
                redraw = true;
            }
        }

        if (redraw) drawWallpaperSettingsScreen(names, selected, message);
        delay(10);
    }
}

static void drawRomManagerScreen() {
    const SettingsWebServerStatus web = settingsWebServerStatus();
    tft.fillScreen(MENU_BG_COLOR);
    tft.fillRect(0, 0, 320, 36, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, 320, 36, MENU_BORDER_COLOR);
    tft.setFont(&fonts::efontCN_14);
    tft.setTextSize(1);
    tft.setTextColor(MENU_TITLE_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(116, 10);
    tft.print("网页管理");
    tft.drawFastHLine(20, 34, 280, MENU_ACCENT_COLOR);

    tft.setTextColor(web.active ? 0x07E0 : 0xFBE0, MENU_BG_COLOR);
    tft.setCursor(18, 50);
    if (web.active) {
        tft.print(web.accessPoint ? "请先连接热点：" : "设置网页已启动");
        if (web.accessPoint) drawClippedText(web.ssid, 122, 50, 180);
        tft.setTextColor(MENU_TEXT_COLOR, MENU_BG_COLOR);
        tft.setCursor(18, 78);
        tft.print("浏览器打开：");
        drawClippedText(web.url, 18, 100, 284);
    } else {
        drawClippedText(web.message, 18, 50, 284);
    }

    tft.setTextColor(MENU_HINT_COLOR, MENU_BG_COLOR);
    tft.setCursor(18, 132);
    tft.print("首页可调整音量、显示和开机选项");
    tft.setCursor(18, 154);
    tft.print("也可管理壁纸和 SD 卡 ROM");
    tft.setCursor(18, 176);
    tft.print("退出系统设置后网页服务自动关闭");

    tft.fillRect(0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, MENU_SOFTBAR_Y, 320, MENU_BORDER_COLOR);
    tft.drawRect(100, MENU_SOFTBAR_BUTTON_Y, 120, MENU_SOFTBAR_BUTTON_H,
                 MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TEXT_COLOR, MENU_HEADER_COLOR);
    tft.setCursor(136, MENU_SOFTBAR_Y + 9);
    tft.print("返回");
    tft.setFont(&fonts::Font0);
}

static void runRomManagerMenu() {
    drawRomManagerScreen();
    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        bool leave = false;
        if (buttons.B) {
            waitForButtonsReleased(DIJI_BTN_B);
            leave = true;
        }
        if (touchInputJustReleased()) {
            const TouchPoint point = touchInputReleasePoint();
            if (touchPointInRect(point, 0, MENU_SOFTBAR_Y,
                                 320, MENU_SOFTBAR_HEIGHT)) {
                leave = true;
            }
        }
        if (leave) {
            return;
        }
        delay(10);
    }
}

static void activateSettingsItem(int selected) {
    waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
    if (selected < 0 || selected >= SETTINGS_ITEM_COUNT) return;
    switch (SETTINGS_ITEMS[selected].action) {
        case SettingsAction::Wifi:
            settingsWebServerStop();
            runWifiProvisioningMenu();
            settingsWebServerBegin(sdCardAvailable, buildSettingsWebJson,
                                   applySettingsWebValue);
            break;
        case SettingsAction::Gamepad:
            runGamepadPairingMenu();
            break;
        case SettingsAction::DisplayMode: {
            displayStretchFullscreen = !displayStretchFullscreen;
            if (!displayStretchFullscreen) displayPipeline.requestNativeBorderClear();
            Preferences preferences;
            if (preferences.begin("ui", false)) {
                preferences.putBool("stretch", displayStretchFullscreen);
                preferences.end();
            }
            break;
        }
        case SettingsAction::Rotation:
            displayRotate180 = !displayRotate180;
            applyDisplayHardwareSettings(true);
            break;
        case SettingsAction::ColorInvert:
#if DIJI_LCD_DRIVER_ST7789
            displayColorInverted = !displayColorInverted;
            applyDisplayHardwareSettings(true);
#endif
            break;
        case SettingsAction::Volume:
            runVolumeSettingsMenu();
            break;
        case SettingsAction::TouchCalibration:
            if (touchInputAvailable()) runTouchCalibration();
            break;
        case SettingsAction::BootPage:
            bootToWallpaperHome = !bootToWallpaperHome;
            saveBootPageSetting();
            break;
        case SettingsAction::Wallpaper:
            runWallpaperSettingsMenu();
            break;
        case SettingsAction::WebManager:
            runRomManagerMenu();
            break;
        case SettingsAction::Rewind: {
            const uint8_t options[] = {0, 5, 10, 20};
            int index = 0;
            while (index < 4 && options[index] != rewindSeconds) index++;
            rewindSeconds = options[(index + 1) % 4];
            Preferences preferences;
            if (preferences.begin("session", false)) {
                preferences.putUChar("rewind_sec", rewindSeconds);
                preferences.end();
            }
            if (gameSession.hasLoadedRom()) rewindBuffer.begin(rewindSeconds);
            break;
        }
        case SettingsAction::Power:
            runPowerSettingsMenu();
            break;
        case SettingsAction::SdCard:
            runSdCardMenu();
            break;
        case SettingsAction::CloudSave:
            runCloudSaveMenu();
            break;
        case SettingsAction::SystemUpdate:
            runSystemInfoScreen();
            break;
    }
}

static void runSettingsMenu() {
    settingsWebServerBegin(sdCardAvailable, buildSettingsWebJson,
                           applySettingsWebValue);
    struct SettingsWebSessionGuard {
        ~SettingsWebSessionGuard() {
            settingsWebServerStop();
            if (!romWebServerConsumeFilesChanged()) return;
            scanROMFiles();
            if (lastResumeRomPath.startsWith(DIJI_ROM_SD_PREFIX) &&
                !dijiStoragePathExists(lastResumeRomPath.c_str())) {
                lastResumeRomPath = "";
                Preferences preferences;
                if (preferences.begin("session", false)) {
                    preferences.remove("last_rom");
                    preferences.end();
                }
            }
        }
    } settingsWebSessionGuard;
    int selected = 0;
    float scrollPosition = 0.0f;
    float scrollVelocity = 0.0f;
    uint32_t lastInputMs = 0;
    bool touchTracking = false;
    bool touchMoved = false;
    int touchStartY = 0;
    int touchLastY = 0;
    float touchStartScroll = 0.0f;
    uint32_t touchLastSampleMs = 0;
    uint32_t lastAnimationMs = millis();
    int lastRenderedScrollY = 0;
    refreshSettingsValueCache();
    drawSettingsMenu(selected, lastRenderedScrollY, true);
    uint32_t lastListDrawMs = millis();
    bool listDirty = false;
    bool chromeDirty = false;
    bool backFocused = false;
    wl_status_t lastWifiStatus = WiFi.status();
    uint32_t lastSettingsStatusMs = 0;

    while (true) {
        settingsWebServerLoop();
        updateButtons();
        touchInputUpdate();
        uint32_t now = millis();
        bool redraw = false;
        bool redrawChrome = false;
        wl_status_t currentWifiStatus = WiFi.status();
        if (currentWifiStatus != lastWifiStatus) {
            lastWifiStatus = currentWifiStatus;
            settingsScreen.valueCache[0] = settingsItemValue(0);
            redraw = true;
        }
        if ((uint32_t)(now - lastSettingsStatusMs) >= 500) {
            lastSettingsStatusMs = now;
            for (int index = 0; index < SETTINGS_ITEM_COUNT; index++) {
                const String value = settingsItemValue(index);
                if (value != settingsScreen.valueCache[index]) {
                    settingsScreen.valueCache[index] = value;
                    redraw = true;
                }
            }
        }

        if (!touchTracking && (uint32_t)(now - lastInputMs) >= BUTTON_DEBOUNCE) {
            if (buttons.B) {
                waitForButtonsReleased(DIJI_BTN_B);
                return;
            } else if (buttons.SELECT) {
                waitForButtonsReleased(DIJI_BTN_SELECT);
                backFocused = !backFocused;
                redraw = true;
                redrawChrome = true;
                lastInputMs = millis();
            } else if (backFocused && buttons.UP) {
                backFocused = false;
                redraw = true;
                redrawChrome = true;
                lastInputMs = now;
            } else if (backFocused && (buttons.A || buttons.START)) {
                waitForButtonsReleased(DIJI_BTN_A | DIJI_BTN_START);
                return;
            } else if (!backFocused && buttons.UP && selected > 0) {
                selected--;
                float itemTop = selected * SETTINGS_ROW_HEIGHT;
                if (itemTop < scrollPosition) scrollPosition = itemTop;
                scrollVelocity = 0.0f;
                redraw = true;
                lastInputMs = now;
            } else if (!backFocused && buttons.DOWN &&
                       selected < SETTINGS_ITEM_COUNT - 1) {
                selected++;
                float itemBottom = (selected + 1) * SETTINGS_ROW_HEIGHT;
                if (itemBottom > scrollPosition + SETTINGS_LIST_HEIGHT) {
                    scrollPosition = itemBottom - SETTINGS_LIST_HEIGHT;
                }
                scrollVelocity = 0.0f;
                redraw = true;
                lastInputMs = now;
            } else if (!backFocused && (buttons.A || buttons.START)) {
                activateSettingsItem(selected);
                refreshSettingsValueCache();
                scrollVelocity = 0.0f;
                lastAnimationMs = millis();
                redraw = true;
                redrawChrome = true;
                lastInputMs = now;
            }
        }

        if (touchInputJustPressed()) {
            TouchPoint point = touchInputPoint();
            if (touchPointInRect(point, 0, SETTINGS_LIST_Y, 320,
                                 SETTINGS_LIST_HEIGHT)) {
                touchTracking = true;
                // 手机列表在惯性滚动时，第一次触摸只负责“刹车”，不误触条目。
                touchMoved = fabsf(scrollVelocity) > 30.0f ||
                             scrollPosition < 0.0f ||
                             scrollPosition > SETTINGS_MAX_SCROLL_Y;
                touchStartY = point.y;
                touchLastY = point.y;
                touchStartScroll = scrollPosition;
                touchLastSampleMs = now;
                scrollVelocity = 0.0f;
            }
        }

        if (touchTracking && touchInputTouched()) {
            TouchPoint point = touchInputPoint();
            int totalDy = point.y - touchStartY;
            if (abs(totalDy) > 5) touchMoved = true;

            float target = touchStartScroll - totalDy;
            if (target < 0.0f) {
                target *= 0.35f;
            } else if (target > SETTINGS_MAX_SCROLL_Y) {
                target = SETTINGS_MAX_SCROLL_Y +
                         (target - SETTINGS_MAX_SCROLL_Y) * 0.35f;
            }

            uint32_t sampleDtMs = now - touchLastSampleMs;
            if (sampleDtMs > 0) {
                float instantVelocity =
                    (float)(touchLastY - point.y) * 1000.0f / sampleDtMs;
                scrollVelocity = scrollVelocity * 0.55f + instantVelocity * 0.45f;
                if (scrollVelocity > 1200.0f) scrollVelocity = 1200.0f;
                if (scrollVelocity < -1200.0f) scrollVelocity = -1200.0f;
                touchLastY = point.y;
                touchLastSampleMs = now;
            }
            if (fabsf(target - scrollPosition) >= 0.5f) {
                scrollPosition = target;
                redraw = true;
            }
        }

        if (touchInputJustReleased()) {
            TouchPoint point = touchInputReleasePoint();
            if (!touchTracking &&
                touchPointInRect(point, 0, MENU_SOFTBAR_Y, 320, MENU_SOFTBAR_HEIGHT)) {
                return;
            }
            if (touchTracking) {
                touchTracking = false;
                if (!touchMoved && point.y >= SETTINGS_LIST_Y &&
                    point.y < SETTINGS_LIST_Y + SETTINGS_LIST_HEIGHT) {
                    float boundedScroll = scrollPosition;
                    if (boundedScroll < 0.0f) boundedScroll = 0.0f;
                    if (boundedScroll > SETTINGS_MAX_SCROLL_Y) {
                        boundedScroll = SETTINGS_MAX_SCROLL_Y;
                    }
                    int index = (int)(boundedScroll + point.y - SETTINGS_LIST_Y) /
                                SETTINGS_ROW_HEIGHT;
                    if (index >= 0 && index < SETTINGS_ITEM_COUNT) {
                        backFocused = false;
                        selected = index;
                        activateSettingsItem(selected);
                        refreshSettingsValueCache();
                        scrollVelocity = 0.0f;
                        lastAnimationMs = millis();
                        redraw = true;
                        redrawChrome = true;
                    }
                }
            }
        }

        uint32_t animationNow = millis();
        float dt = (animationNow - lastAnimationMs) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f;
        lastAnimationMs = animationNow;
        if (!touchTracking && dt > 0.0f) {
            if (fabsf(scrollVelocity) > 4.0f) {
                scrollPosition += scrollVelocity * dt;
                scrollVelocity *= powf(0.90f, dt * 60.0f);
                redraw = true;
            } else {
                scrollVelocity = 0.0f;
            }

            if (scrollPosition < 0.0f) {
                scrollPosition += (0.0f - scrollPosition) * fminf(1.0f, dt * 18.0f);
                scrollVelocity *= 0.55f;
                if (fabsf(scrollPosition) < 0.5f && fabsf(scrollVelocity) < 5.0f) {
                    scrollPosition = 0.0f;
                    scrollVelocity = 0.0f;
                }
                redraw = true;
            } else if (scrollPosition > SETTINGS_MAX_SCROLL_Y) {
                scrollPosition += (SETTINGS_MAX_SCROLL_Y - scrollPosition) *
                                  fminf(1.0f, dt * 18.0f);
                scrollVelocity *= 0.55f;
                if (fabsf(scrollPosition - SETTINGS_MAX_SCROLL_Y) < 0.5f &&
                    fabsf(scrollVelocity) < 5.0f) {
                    scrollPosition = SETTINGS_MAX_SCROLL_Y;
                    scrollVelocity = 0.0f;
                }
                redraw = true;
            }
        }

        int renderedScrollY = (int)roundf(scrollPosition);
        if (redraw) listDirty = true;
        if (redrawChrome) chromeDirty = true;
        bool frameDue = chromeDirty ||
                        (uint32_t)(animationNow - lastListDrawMs) >= 33;
        if ((listDirty || renderedScrollY != lastRenderedScrollY) && frameDue) {
            lastRenderedScrollY = renderedScrollY;
            drawSettingsMenu(backFocused ? -1 : selected, lastRenderedScrollY,
                             chromeDirty, backFocused);
            lastListDrawMs = millis();
            listDirty = false;
            chromeDirty = false;
        }
        delay(16);
    }
}
