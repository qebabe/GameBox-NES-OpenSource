#include "home_screen.h"

#include <time.h>
#include <stdlib.h>

#include "storage.h"
#include "wallpaper_manager.h"

extern const uint8_t gamebox_clock_wallpaper_start[]
    asm("_binary_assets_gamebox_clock_wallpaper_jpg_start");
extern const uint8_t gamebox_clock_wallpaper_end[]
    asm("_binary_assets_gamebox_clock_wallpaper_jpg_end");

namespace {
const char* weekdayName(int weekday) {
    static const char* weekdays[] = {
        "星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"
    };
    return weekday >= 0 && weekday < 7 ? weekdays[weekday] : "--";
}

template <typename Surface>
void render(Surface& surface, const struct tm& timeinfo, bool valid, bool canResume,
            const String& wallpaperPath, uint8_t selectedAction) {
    bool wallpaperDrawn = false;
    if (!wallpaperPath.isEmpty() && DIJI_SD.exists(wallpaperPath)) {
        File wallpaperFile = DIJI_SD.open(wallpaperPath, FILE_READ);
        if (wallpaperFile) {
            lgfx::v1::DataWrapperT<fs::File> data(&wallpaperFile);
            wallpaperDrawn = surface.drawJpg(&data, 0, 0, 320, 240);
            data.close();
        }
    }
    const size_t wallpaperSize =
        (size_t)(gamebox_clock_wallpaper_end - gamebox_clock_wallpaper_start);
    if (!wallpaperDrawn && wallpaperSize > 0) {
        wallpaperDrawn = surface.drawJpg(gamebox_clock_wallpaper_start,
                                         wallpaperSize, 0, 0, 320, 240);
    }
    if (!wallpaperDrawn) {
        for (int y = 0; y < 240; y += 12) {
            uint8_t blue = (uint8_t)(42 + y / 8);
            uint16_t color = (uint16_t)(((4 + y / 40) << 11) |
                                        ((18 + y / 16) << 5) | (blue >> 3));
            surface.fillRect(0, y, 320, 12, color);
        }
    }

    char hourText[4] = "--", minuteText[4] = "--", secondText[4] = "--";
    char dateText[24] = "----年--月--日";
    const char* weekday = "等待校时";
    if (valid) {
        snprintf(hourText, sizeof(hourText), "%02d", timeinfo.tm_hour);
        snprintf(minuteText, sizeof(minuteText), "%02d", timeinfo.tm_min);
        snprintf(secondText, sizeof(secondText), "%02d", timeinfo.tm_sec);
        snprintf(dateText, sizeof(dateText), "%04d年%02d月%02d日",
                 timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
        weekday = weekdayName(timeinfo.tm_wday);
    }

    surface.setFont(&fonts::Font7);
    surface.setTextSize(1.22f);
    int x = 16;
    constexpr int timeY = 17;
    auto shadowText = [&](const char* text, uint16_t color, int y) {
        surface.setTextColor(0x18C3);
        surface.setCursor(x + 2, y + 2);
        surface.print(text);
        surface.setTextColor(color);
        surface.setCursor(x, y);
        surface.print(text);
    };
    shadowText(hourText, 0xFFFF, timeY);
    x += surface.textWidth(hourText) + 3;
    shadowText(":", 0xFFFF, timeY);
    x += surface.textWidth(":") + 3;
    shadowText(minuteText, 0xFD20, timeY);
    x += surface.textWidth(minuteText) + 5;

    surface.setFont(&fonts::Font4);
    surface.setTextSize(0.78f);
    shadowText(secondText, 0xF9B2, 56);

    surface.setFont(&fonts::efontCN_14);
    surface.setTextSize(1.15f);
    surface.setTextColor(0x18C3);
    surface.setCursor(20, 101);
    surface.print(dateText);
    surface.setTextColor(0xFFFF);
    surface.setCursor(19, 100);
    surface.print(dateText);
    surface.setTextColor(0x18C3);
    surface.setCursor(184, 101);
    surface.print(weekday);
    surface.setTextColor(0xFFFF);
    surface.setCursor(183, 100);
    surface.print(weekday);

    auto button = [&](int x0, uint16_t border, const char* label, uint8_t action) {
        const bool focused = selectedAction == action;
        if (focused) {
            surface.fillRoundRect(x0, 183, 52, 38, 10, 0x2945);
        }
        surface.drawRoundRect(x0 + 1, 184, 52, 38, 10, 0x18C3);
        surface.drawRoundRect(x0, 183, 52, 38, 10,
                              focused ? 0xFFE0 : border);
        if (focused) {
            surface.drawRoundRect(x0 + 2, 185, 48, 34, 8, 0xFFFF);
        }
        surface.setTextSize(1);
        surface.setTextColor(0x18C3);
        surface.setCursor(x0 + 12 + 1, 197);
        surface.print(label);
        surface.setTextColor(0xFFFF);
        surface.setCursor(x0 + 12, 196);
        surface.print(label);
    };
    if (canResume) button(136, 0x07E0, "继续", 0);
    button(196, 0x07FF, "游戏", 1);
    button(256, 0xFFFF, "设置", 2);
    surface.setFont(&fonts::Font0);
    surface.setTextSize(1);
}
}  // namespace

HomeScreen::HomeScreen(LGFX& display) : display_(display), canvas_(&display) {}

bool HomeScreen::timeValid(time_t now) {
    return now >= 1704067200;  // 2024-01-01 UTC
}

void HomeScreen::invalidate() {
    lastSecond_ = -2;
}

void HomeScreen::draw(bool force, bool canResume, uint8_t selectedAction) {
    if (force) {
        invalidate();
        wallpaperPath_ = wallpaperSelectedPath();
    }
    time_t now = time(nullptr);
    struct tm timeinfo = {};
    const bool valid = timeValid(now) && localtime_r(&now, &timeinfo) != nullptr;
    static bool previousTimeValid = false;
    if (valid && !previousTimeValid) {
        struct tm utcInfo = {};
        gmtime_r(&now, &utcInfo);
        Serial.printf("桌面时钟 校时完成并刷新：时间戳=%lld 时区=%s "
                      "UTC=%04d-%02d-%02d %02d:%02d:%02d "
                      "东八区=%04d-%02d-%02d %02d:%02d:%02d\n",
                      (long long)now, getenv("TZ") ? getenv("TZ") : "未设置",
                      utcInfo.tm_year + 1900, utcInfo.tm_mon + 1, utcInfo.tm_mday,
                      utcInfo.tm_hour, utcInfo.tm_min, utcInfo.tm_sec,
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
    previousTimeValid = valid;
    const int second = valid ? timeinfo.tm_sec : -1;
    if (!force && second == lastSecond_) return;
    lastSecond_ = second;

    if (!canvasReady_) {
        canvas_.setColorDepth(16);
        canvas_.setPsram(true);
        canvasReady_ = canvas_.createSprite(320, 240) != nullptr;
    }
    if (canvasReady_) {
        render(canvas_, timeinfo, valid, canResume, wallpaperPath_, selectedAction);
        canvas_.pushSprite(0, 0);
    } else {
        render(display_, timeinfo, valid, canResume, wallpaperPath_, selectedAction);
    }
}
