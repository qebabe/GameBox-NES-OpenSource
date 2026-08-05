#pragma once

#include <stdint.h>
#include "touch_layout.h"

constexpr uint8_t DIJI_TOUCH_CONTROLS_MAX_POINTS = 2;

struct TouchControlButton {
    uint8_t mask;
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;

    constexpr TouchControlButton(uint8_t buttonMask = 0,
                                 int16_t buttonX = 0,
                                 int16_t buttonY = 0,
                                 int16_t buttonWidth = 0,
                                 int16_t buttonHeight = 0)
        : mask(buttonMask),
          x(buttonX),
          y(buttonY),
          width(buttonWidth),
          height(buttonHeight) {}
};

const TouchControlButton* touchControlsButtons(uint8_t* count);
uint8_t touchControlsMaskForPoints(const TouchPoint* points, uint8_t count);
bool touchControlsPointCanOpenPause(TouchPoint point);
