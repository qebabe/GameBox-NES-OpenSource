#include "power_manager.h"

#include <Preferences.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_err.h>
#include <esp_sleep.h>

#include "board_config.h"
#include "touch_input.h"

static constexpr const char* kPowerPrefsNamespace = "power";
static constexpr const char* kAutoOffKey = "auto_off";
static constexpr uint32_t kWakeHoldMs = 1500;
static constexpr uint32_t kWakeTouchGraceMs = 1000;

static uint16_t autoOffMinutes = 0;
static uint32_t lastActivityMs = 0;

static bool validAutoOffMinutes(uint16_t minutes) {
    return minutes == 0 || minutes == 5 || minutes == 10 || minutes == 20;
}

static int amplifierDisabledLevel() {
    return DIJI_AMP_ENABLE_LEVEL == HIGH ? LOW : HIGH;
}

static void setPowerOutputsOff() {
    if (DIJI_LCD_BL_PIN >= 0) {
        pinMode(DIJI_LCD_BL_PIN, OUTPUT);
        digitalWrite(DIJI_LCD_BL_PIN, LOW);
    }
    if (DIJI_AMP_ENABLE_PIN >= 0) {
        pinMode(DIJI_AMP_ENABLE_PIN, OUTPUT);
        digitalWrite(DIJI_AMP_ENABLE_PIN, amplifierDisabledLevel());
    }
}

static void releaseDeepSleepPinHolds() {
    gpio_deep_sleep_hold_dis();
    if (DIJI_LCD_BL_PIN >= 0) {
        gpio_hold_dis((gpio_num_t)DIJI_LCD_BL_PIN);
    }
    if (DIJI_AMP_ENABLE_PIN >= 0) {
        gpio_hold_dis((gpio_num_t)DIJI_AMP_ENABLE_PIN);
    }
    if (DIJI_TOUCH_RST_PIN >= 0) {
        gpio_hold_dis((gpio_num_t)DIJI_TOUCH_RST_PIN);
    }
}

static bool waitForLongTouchAfterWake() {
    // Do not reset FT6336 here. Resetting it while the user's finger is still
    // on the panel can discard the touch which generated the wake interrupt.
    if (!DIJI_TOUCH_FT6336 || !touchInputBegin(false)) {
        return false;
    }

    uint32_t wakeStarted = millis();
    uint32_t holdStarted = 0;
    while ((uint32_t)(millis() - wakeStarted) < kWakeTouchGraceMs + kWakeHoldMs + 1500) {
        touchInputUpdate();
        uint32_t now = millis();
        if (touchInputTouched()) {
            if (holdStarted == 0) holdStarted = now;
            if ((uint32_t)(now - holdStarted) >= kWakeHoldMs) {
                while (touchInputTouched()) {
                    touchInputUpdate();
                    delay(20);
                }
                delay(80);
                return true;
            }
        } else {
            if (holdStarted != 0 || (uint32_t)(now - wakeStarted) >= kWakeTouchGraceMs) {
                return false;
            }
        }
        delay(20);
    }
    return false;
}

void powerManagerHandleEarlyWake() {
    releaseDeepSleepPinHolds();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    Serial.printf("Power wake cause=%d, touch INT=%d\n",
                  (int)cause,
                  DIJI_TOUCH_INT_PIN >= 0 ? digitalRead(DIJI_TOUCH_INT_PIN) : -1);
    if (cause != ESP_SLEEP_WAKEUP_EXT0) {
        return;
    }

    setPowerOutputsOff();
    if (!waitForLongTouchAfterWake()) {
        powerManagerStartDeepSleep();
    }
}

void powerManagerBegin() {
    Preferences preferences;
    if (preferences.begin(kPowerPrefsNamespace, true)) {
        uint16_t loaded = preferences.getUShort(kAutoOffKey, 0);
        if (validAutoOffMinutes(loaded)) {
            autoOffMinutes = loaded;
        }
        preferences.end();
    }
    if (!powerManagerCanWakeFromSleep()) {
        autoOffMinutes = 0;
    }
    lastActivityMs = millis();
}

void powerManagerNotifyActivity() {
    lastActivityMs = millis();
}

bool powerManagerAutoShutdownDue() {
    if (autoOffMinutes == 0) return false;
    if (DIJI_TOUCH_INT_PIN >= 0 && digitalRead(DIJI_TOUCH_INT_PIN) == LOW) {
        powerManagerNotifyActivity();
        return false;
    }
    uint32_t timeoutMs = (uint32_t)autoOffMinutes * 60UL * 1000UL;
    return (uint32_t)(millis() - lastActivityMs) >= timeoutMs;
}

uint16_t powerManagerAutoOffMinutes() {
    return autoOffMinutes;
}

bool powerManagerSetAutoOffMinutes(uint16_t minutes) {
    if (!validAutoOffMinutes(minutes)) return false;
    if (minutes != 0 && !powerManagerCanWakeFromSleep()) return false;
    autoOffMinutes = minutes;
    powerManagerNotifyActivity();

    Preferences preferences;
    if (!preferences.begin(kPowerPrefsNamespace, false)) return false;
    bool ok = preferences.putUShort(kAutoOffKey, minutes) == sizeof(uint16_t);
    preferences.end();
    return ok;
}

bool powerManagerCanWakeFromSleep() {
    return DIJI_TOUCH_INT_PIN >= 0;
}

void powerManagerStartDeepSleep() {
    if (!powerManagerCanWakeFromSleep()) {
        Serial.println("软件关机已取消：当前板型没有可用的深度睡眠唤醒引脚");
        return;
    }
    setPowerOutputsOff();
    if (!touchInputPrepareForWake()) {
        // FT6336 normally powers up in interrupt-trigger mode. Keep the
        // existing wake path available even if this verification read fails.
        Serial.println("FT6336 唤醒中断配置未确认，继续使用当前中断模式");
    }

    if (DIJI_LCD_BL_PIN >= 0) {
        gpio_hold_en((gpio_num_t)DIJI_LCD_BL_PIN);
    }
    if (DIJI_AMP_ENABLE_PIN >= 0) {
        gpio_hold_en((gpio_num_t)DIJI_AMP_ENABLE_PIN);
    }
    if (DIJI_TOUCH_RST_PIN >= 0) {
        pinMode(DIJI_TOUCH_RST_PIN, OUTPUT);
        digitalWrite(DIJI_TOUCH_RST_PIN, HIGH);
        gpio_hold_en((gpio_num_t)DIJI_TOUCH_RST_PIN);
    }
    gpio_deep_sleep_hold_en();

    if (DIJI_TOUCH_INT_PIN >= 0) {
        pinMode(DIJI_TOUCH_INT_PIN, INPUT_PULLUP);
        uint32_t waitStarted = millis();
        while (digitalRead(DIJI_TOUCH_INT_PIN) == LOW &&
               (uint32_t)(millis() - waitStarted) < 3000) {
            delay(20);
        }
        rtc_gpio_pullup_en((gpio_num_t)DIJI_TOUCH_INT_PIN);
        rtc_gpio_pulldown_dis((gpio_num_t)DIJI_TOUCH_INT_PIN);
        esp_err_t wakeResult =
            esp_sleep_enable_ext0_wakeup((gpio_num_t)DIJI_TOUCH_INT_PIN, 0);
        Serial.printf("EXT0 touch wake GPIO%d: %s, level=%d\n",
                      DIJI_TOUCH_INT_PIN, esp_err_to_name(wakeResult),
                      digitalRead(DIJI_TOUCH_INT_PIN));
        if (wakeResult != ESP_OK) {
            releaseDeepSleepPinHolds();
            Serial.println("软件关机已取消：无法启用触摸深睡唤醒");
            return;
        }
    }

    Serial.flush();
    esp_deep_sleep_start();
}
