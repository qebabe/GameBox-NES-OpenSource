#include "touch_layout.h"

static int16_t clampTouchValue(int32_t value, int16_t maxValue) {
    if (value < 0) return 0;
    if (value > maxValue) return maxValue;
    return (int16_t)value;
}

static int32_t roundTouchValue(float value) {
    return (int32_t)(value >= 0.0f ? value + 0.5f : value - 0.5f);
}

TouchPoint mapFt6336PointToScreen(uint16_t rawX, uint16_t rawY,
                                  uint8_t rotation,
                                  int16_t screenWidth,
                                  int16_t screenHeight) {
    constexpr int16_t rawWidth = 240;
    constexpr int16_t rawHeight = 320;
    TouchPoint point;

    switch (rotation & 3) {
        case 1:
            point.x = (int16_t)rawY;
            point.y = (int16_t)(rawWidth - 1 - rawX);
            break;
        case 2:
            point.x = (int16_t)(rawWidth - 1 - rawX);
            point.y = (int16_t)(rawHeight - 1 - rawY);
            break;
        case 3:
            point.x = (int16_t)(rawHeight - 1 - rawY);
            point.y = (int16_t)rawX;
            break;
        case 0:
        default:
            point.x = (int16_t)rawX;
            point.y = (int16_t)rawY;
            break;
    }

    point.x = clampTouchValue(point.x, screenWidth - 1);
    point.y = clampTouchValue(point.y, screenHeight - 1);
    return point;
}

bool buildTouchCalibration(const TouchPoint& rawTopLeft,
                           const TouchPoint& rawTopRight,
                           const TouchPoint& rawBottomLeft,
                           const TouchPoint& screenTopLeft,
                           const TouchPoint& screenTopRight,
                           const TouchPoint& screenBottomLeft,
                           TouchCalibration* calibration) {
    if (!calibration) {
        return false;
    }

    const float x0 = (float)rawTopLeft.x;
    const float y0 = (float)rawTopLeft.y;
    const float x1 = (float)rawTopRight.x;
    const float y1 = (float)rawTopRight.y;
    const float x2 = (float)rawBottomLeft.x;
    const float y2 = (float)rawBottomLeft.y;
    const float denom = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);

    if (denom > -0.001f && denom < 0.001f) {
        *calibration = TouchCalibration{};
        return false;
    }

    auto solveAxis = [&](float s0, float s1, float s2,
                         float* a, float* b, float* c) {
        *a = ((s1 - s0) * (y2 - y0) - (s2 - s0) * (y1 - y0)) / denom;
        *b = ((x1 - x0) * (s2 - s0) - (x2 - x0) * (s1 - s0)) / denom;
        *c = s0 - (*a * x0) - (*b * y0);
    };

    solveAxis((float)screenTopLeft.x, (float)screenTopRight.x, (float)screenBottomLeft.x,
              &calibration->ax, &calibration->bx, &calibration->cx);
    solveAxis((float)screenTopLeft.y, (float)screenTopRight.y, (float)screenBottomLeft.y,
              &calibration->ay, &calibration->by, &calibration->cy);
    calibration->enabled = true;
    return true;
}

TouchPoint applyTouchCalibration(uint16_t rawX, uint16_t rawY,
                                 const TouchCalibration& calibration,
                                 int16_t screenWidth,
                                 int16_t screenHeight) {
    if (!calibration.enabled) {
        return mapFt6336PointToScreen(rawX, rawY, 3, screenWidth, screenHeight);
    }

    TouchPoint point;
    point.x = clampTouchValue(roundTouchValue(calibration.ax * rawX +
                                              calibration.bx * rawY +
                                              calibration.cx),
                              screenWidth - 1);
    point.y = clampTouchValue(roundTouchValue(calibration.ay * rawX +
                                              calibration.by * rawY +
                                              calibration.cy),
                              screenHeight - 1);
    return point;
}

bool touchPointInRect(const TouchPoint& point, int16_t x, int16_t y,
                      int16_t width, int16_t height) {
    return point.x >= x && point.x < x + width &&
           point.y >= y && point.y < y + height;
}
