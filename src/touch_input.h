#pragma once

#include <stdint.h>
#include "touch_layout.h"

struct TouchRawPoint {
    uint16_t x = 0;
    uint16_t y = 0;
};

constexpr uint8_t TOUCH_INPUT_MAX_POINTS = 2;

bool touchInputBegin(bool resetController = true);
void touchInputSetDisplayRotation(uint8_t rotation);
bool touchInputPrepareForWake();
void touchInputUpdate();
bool touchInputAvailable();

bool touchInputReadRaw(TouchRawPoint* point);
bool touchInputReadRawPoints(TouchRawPoint* points, uint8_t maxPoints, uint8_t* count);
bool touchInputHasCalibration();
bool touchInputSetCalibration(const TouchCalibration& calibration);
void touchInputClearCalibration();
bool touchInputTouched();
bool touchInputJustPressed();
bool touchInputJustReleased();
TouchPoint touchInputPoint();
TouchPoint touchInputReleasePoint();
uint8_t touchInputPointCount();
TouchPoint touchInputPointAt(uint8_t index);
