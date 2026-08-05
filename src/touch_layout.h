#pragma once

#include <stdint.h>

struct TouchPoint {
    int16_t x = 0;
    int16_t y = 0;

    TouchPoint() = default;
    constexpr TouchPoint(int16_t pointX, int16_t pointY) : x(pointX), y(pointY) {}
};

struct TouchCalibration {
    bool enabled = false;
    float ax = 0.0f;
    float bx = 0.0f;
    float cx = 0.0f;
    float ay = 0.0f;
    float by = 0.0f;
    float cy = 0.0f;
};

TouchPoint mapFt6336PointToScreen(uint16_t rawX, uint16_t rawY,
                                  uint8_t rotation,
                                  int16_t screenWidth,
                                  int16_t screenHeight);

bool buildTouchCalibration(const TouchPoint& rawTopLeft,
                           const TouchPoint& rawTopRight,
                           const TouchPoint& rawBottomLeft,
                           const TouchPoint& screenTopLeft,
                           const TouchPoint& screenTopRight,
                           const TouchPoint& screenBottomLeft,
                           TouchCalibration* calibration);

TouchPoint applyTouchCalibration(uint16_t rawX, uint16_t rawY,
                                 const TouchCalibration& calibration,
                                 int16_t screenWidth,
                                 int16_t screenHeight);

bool touchPointInRect(const TouchPoint& point, int16_t x, int16_t y,
                      int16_t width, int16_t height);
