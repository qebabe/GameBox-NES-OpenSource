#include "touch_controls.h"

#include "serial_controller.h"

static constexpr int16_t kDpadCenterX = 66;
static constexpr int16_t kDpadCenterY = 180;
static constexpr int16_t kDpadRadius = 54;
static constexpr int16_t kDpadDeadzone = 12;
static constexpr int16_t kPauseHotspotX = 160;
static constexpr int16_t kPauseHotspotY = 0;
static constexpr int16_t kPauseHotspotWidth = 160;
static constexpr int16_t kPauseHotspotHeight = 120;

static constexpr TouchControlButton kTouchButtons[] = {
    {DIJI_BTN_B,     206, 178, 48, 48},
    {DIJI_BTN_A,     264, 158, 48, 48},
    {DIJI_BTN_SELECT, 112, 216, 52, 22},
    {DIJI_BTN_START, 166, 216, 52, 22},
};

static uint8_t dpadMaskForPoint(TouchPoint point) {
    int16_t dx = point.x - kDpadCenterX;
    int16_t dy = point.y - kDpadCenterY;
    int32_t distanceSq = (int32_t)dx * dx + (int32_t)dy * dy;
    if (distanceSq > (int32_t)kDpadRadius * kDpadRadius) {
        return 0;
    }

    uint8_t mask = 0;
    if (dx > kDpadDeadzone) {
        mask |= DIJI_BTN_RIGHT;
    } else if (dx < -kDpadDeadzone) {
        mask |= DIJI_BTN_LEFT;
    }

    if (dy > kDpadDeadzone) {
        mask |= DIJI_BTN_DOWN;
    } else if (dy < -kDpadDeadzone) {
        mask |= DIJI_BTN_UP;
    }
    return mask;
}

const TouchControlButton* touchControlsButtons(uint8_t* count) {
    if (count) {
        *count = (uint8_t)(sizeof(kTouchButtons) / sizeof(kTouchButtons[0]));
    }
    return kTouchButtons;
}

uint8_t touchControlsMaskForPoints(const TouchPoint* points, uint8_t count) {
    if (!points || count == 0) {
        return 0;
    }

    uint8_t buttonCount = 0;
    const TouchControlButton* buttons = touchControlsButtons(&buttonCount);
    uint8_t mask = 0;

    for (uint8_t pointIndex = 0; pointIndex < count; pointIndex++) {
        mask |= dpadMaskForPoint(points[pointIndex]);

        for (uint8_t buttonIndex = 0; buttonIndex < buttonCount; buttonIndex++) {
            const TouchControlButton& button = buttons[buttonIndex];
            if (touchPointInRect(points[pointIndex], button.x, button.y,
                                 button.width, button.height)) {
                mask |= button.mask;
            }
        }
    }

    if ((mask & DIJI_BTN_LEFT) && (mask & DIJI_BTN_RIGHT)) {
        mask &= (uint8_t)~(DIJI_BTN_LEFT | DIJI_BTN_RIGHT);
    }
    if ((mask & DIJI_BTN_UP) && (mask & DIJI_BTN_DOWN)) {
        mask &= (uint8_t)~(DIJI_BTN_UP | DIJI_BTN_DOWN);
    }

    return mask;
}

bool touchControlsPointCanOpenPause(TouchPoint point) {
    if (!touchPointInRect(point, kPauseHotspotX, kPauseHotspotY,
                          kPauseHotspotWidth, kPauseHotspotHeight)) {
        return false;
    }

    return touchControlsMaskForPoints(&point, 1) == 0;
}
