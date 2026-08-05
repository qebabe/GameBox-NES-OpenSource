#include "touch_input.h"

#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include "board_config.h"
#include "power_manager.h"

static constexpr uint8_t FT6336_ADDR = 0x38;
static constexpr uint8_t FT6336_REG_TD_STATUS = 0x02;
static constexpr uint8_t FT6336_REG_P1_XH = 0x03;
static constexpr uint8_t FT6336_REG_G_MODE = 0xA4;
static constexpr uint8_t FT6336_G_MODE_INTERRUPT_TRIGGER = 0x01;
static constexpr uint32_t TOUCH_CAL_MAGIC = 0x54434C31; // TCL1

static bool touch_available = false;
static bool touch_touched = false;
static bool touch_prev_touched = false;
static bool touch_just_pressed = false;
static bool touch_just_released = false;
static TouchPoint touch_point;
static TouchPoint touch_release_point;
static TouchPoint touch_points[TOUCH_INPUT_MAX_POINTS];
static uint8_t touch_point_count = 0;
static TouchCalibration touch_calibration;
static uint8_t touch_display_rotation = DIJI_TFT_ROTATION;

static bool readFt6336Bytes(uint8_t reg, uint8_t* data, size_t len) {
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    if (Wire.requestFrom(FT6336_ADDR, (uint8_t)len) != len) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        data[i] = Wire.read();
    }
    return true;
}

static bool writeFt6336Byte(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(FT6336_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

static bool loadTouchCalibration() {
    Preferences preferences;
    if (!preferences.begin("touch", true)) {
        return false;
    }

    uint32_t magic = preferences.getUInt("magic", 0);
    if (magic != TOUCH_CAL_MAGIC) {
        preferences.end();
        touch_calibration = TouchCalibration{};
        return false;
    }

    TouchCalibration loaded;
    loaded.enabled = true;
    loaded.ax = preferences.getFloat("ax", 0.0f);
    loaded.bx = preferences.getFloat("bx", 0.0f);
    loaded.cx = preferences.getFloat("cx", 0.0f);
    loaded.ay = preferences.getFloat("ay", 0.0f);
    loaded.by = preferences.getFloat("by", 0.0f);
    loaded.cy = preferences.getFloat("cy", 0.0f);
    preferences.end();

    touch_calibration = loaded;
    return true;
}

static bool saveTouchCalibration(const TouchCalibration& calibration) {
    Preferences preferences;
    if (!preferences.begin("touch", false)) {
        return false;
    }

    bool ok = true;
    ok &= preferences.putUInt("magic", TOUCH_CAL_MAGIC) == sizeof(uint32_t);
    ok &= preferences.putFloat("ax", calibration.ax) == sizeof(float);
    ok &= preferences.putFloat("bx", calibration.bx) == sizeof(float);
    ok &= preferences.putFloat("cx", calibration.cx) == sizeof(float);
    ok &= preferences.putFloat("ay", calibration.ay) == sizeof(float);
    ok &= preferences.putFloat("by", calibration.by) == sizeof(float);
    ok &= preferences.putFloat("cy", calibration.cy) == sizeof(float);
    preferences.end();
    return ok;
}

bool touchInputBegin(bool resetController) {
    touch_available = false;
    touch_touched = false;
    touch_prev_touched = false;
    touch_just_pressed = false;
    touch_just_released = false;
    touch_point_count = 0;
    touch_calibration = TouchCalibration{};

    if (!DIJI_TOUCH_FT6336) {
        return false;
    }

    if (resetController && DIJI_TOUCH_RST_PIN >= 0) {
        pinMode(DIJI_TOUCH_RST_PIN, OUTPUT);
        digitalWrite(DIJI_TOUCH_RST_PIN, LOW);
        delay(5);
        digitalWrite(DIJI_TOUCH_RST_PIN, HIGH);
        delay(80);
    } else if (DIJI_TOUCH_RST_PIN >= 0) {
        pinMode(DIJI_TOUCH_RST_PIN, OUTPUT);
        digitalWrite(DIJI_TOUCH_RST_PIN, HIGH);
    }

    if (DIJI_TOUCH_INT_PIN >= 0) {
        pinMode(DIJI_TOUCH_INT_PIN, INPUT_PULLUP);
    }

    Wire.begin(DIJI_TOUCH_I2C_SDA_PIN, DIJI_TOUCH_I2C_SCL_PIN);
    Wire.setClock(400000);

    uint8_t status = 0;
    touch_available = readFt6336Bytes(FT6336_REG_TD_STATUS, &status, 1);
    bool calibrated = false;
    if (touch_available) {
        calibrated = loadTouchCalibration();
    }
    Serial.printf("FT6336 touch %s at I2C 0x%02X\n",
                  touch_available ? "ready" : "not found", FT6336_ADDR);
    if (touch_available) {
        Serial.printf("Touch calibration %s\n", calibrated ? "loaded" : "not set");
    }

    return touch_available;
}

void touchInputSetDisplayRotation(uint8_t rotation) {
    touch_display_rotation = rotation & 3;
}

bool touchInputPrepareForWake() {
    if (!DIJI_TOUCH_FT6336 || !touch_available) {
        return false;
    }

    bool ok = writeFt6336Byte(FT6336_REG_G_MODE,
                             FT6336_G_MODE_INTERRUPT_TRIGGER);
    uint8_t mode = 0xFF;
    bool verified = ok && readFt6336Bytes(FT6336_REG_G_MODE, &mode, 1) &&
                    mode == FT6336_G_MODE_INTERRUPT_TRIGGER;
    Serial.printf("FT6336 wake interrupt mode %s (0x%02X)\n",
                  verified ? "ready" : "failed", mode);
    return verified;
}

bool touchInputReadRaw(TouchRawPoint* point) {
    if (!point) {
        return false;
    }

    TouchRawPoint points[1];
    uint8_t count = 0;
    if (!touchInputReadRawPoints(points, 1, &count) || count == 0) {
        return false;
    }

    *point = points[0];
    return true;
}

bool touchInputReadRawPoints(TouchRawPoint* points, uint8_t maxPoints, uint8_t* count) {
    if (count) {
        *count = 0;
    }
    if (!touch_available || !points || maxPoints == 0) {
        return false;
    }

    uint8_t status = 0;
    if (!readFt6336Bytes(FT6336_REG_TD_STATUS, &status, 1)) {
        return false;
    }

    uint8_t touchCount = status & 0x0F;
    if (touchCount == 0) {
        return false;
    }
    if (touchCount > TOUCH_INPUT_MAX_POINTS) {
        touchCount = TOUCH_INPUT_MAX_POINTS;
    }
    if (touchCount > maxPoints) {
        touchCount = maxPoints;
    }

    uint8_t data[TOUCH_INPUT_MAX_POINTS * 6] = {0};
    if (!readFt6336Bytes(FT6336_REG_P1_XH, data, touchCount * 6)) {
        return false;
    }

    for (uint8_t i = 0; i < touchCount; i++) {
        uint8_t* pointData = data + i * 6;
        points[i].x = ((uint16_t)(pointData[0] & 0x0F) << 8) | pointData[1];
        points[i].y = ((uint16_t)(pointData[2] & 0x0F) << 8) | pointData[3];
    }
    if (count) {
        *count = touchCount;
    }
    return true;
}

void touchInputUpdate() {
    touch_just_pressed = false;
    touch_just_released = false;

    if (!touch_available) {
        return;
    }

    touch_prev_touched = touch_touched;
    touch_touched = false;
    touch_point_count = 0;

    TouchRawPoint rawPoints[TOUCH_INPUT_MAX_POINTS];
    uint8_t rawCount = 0;
    if (touchInputReadRawPoints(rawPoints, TOUCH_INPUT_MAX_POINTS, &rawCount)) {
        for (uint8_t i = 0; i < rawCount; i++) {
            if (touch_calibration.enabled) {
                touch_points[i] = applyTouchCalibration(rawPoints[i].x, rawPoints[i].y,
                                                        touch_calibration, 320, 240);
                if (((touch_display_rotation + 4 - DIJI_TFT_ROTATION) & 3) == 2) {
                    touch_points[i].x = 319 - touch_points[i].x;
                    touch_points[i].y = 239 - touch_points[i].y;
                }
            } else {
                touch_points[i] = mapFt6336PointToScreen(rawPoints[i].x, rawPoints[i].y,
                                                         touch_display_rotation, 320, 240);
            }
        }
        touch_point_count = rawCount;
        touch_point = touch_points[0];
        touch_release_point = touch_point;
        touch_touched = true;
    }

    touch_just_pressed = touch_touched && !touch_prev_touched;
    touch_just_released = !touch_touched && touch_prev_touched;
    if (touch_touched) {
        powerManagerNotifyActivity();
    }
}

bool touchInputAvailable() {
    return touch_available;
}

bool touchInputTouched() {
    return touch_touched;
}

bool touchInputHasCalibration() {
    return touch_calibration.enabled;
}

bool touchInputSetCalibration(const TouchCalibration& calibration) {
    if (!calibration.enabled) {
        return false;
    }
    touch_calibration = calibration;
    return saveTouchCalibration(calibration);
}

void touchInputClearCalibration() {
    touch_calibration = TouchCalibration{};
    Preferences preferences;
    if (preferences.begin("touch", false)) {
        preferences.clear();
        preferences.end();
    }
}

bool touchInputJustPressed() {
    return touch_just_pressed;
}

bool touchInputJustReleased() {
    return touch_just_released;
}

TouchPoint touchInputPoint() {
    return touch_point;
}

TouchPoint touchInputReleasePoint() {
    return touch_release_point;
}

uint8_t touchInputPointCount() {
    return touch_point_count;
}

TouchPoint touchInputPointAt(uint8_t index) {
    if (index >= touch_point_count) {
        return TouchPoint{};
    }
    return touch_points[index];
}
