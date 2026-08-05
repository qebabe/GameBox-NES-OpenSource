#pragma once

#include <Arduino.h>
#include "lgfx_conf.h"
#include "touch_layout.h"

// Persistent interaction/render state for the ROM library. Rendering and
// navigation callbacks remain on the UI thread.
class LibraryScreen {
public:
    explicit LibraryScreen(LGFX& display) : listCanvas(&display) {}

    int selectedIndex = 0;
    LGFX_Sprite listCanvas;
    bool listCanvasReady = false;
    bool touchTracking = false;
    TouchPoint touchStart{};
    uint32_t touchStartMs = 0;
    int touchStartIndex = -1;
    bool longPressCanceled = false;
    bool touchMoved = false;
    int touchLastY = 0;
    float touchStartScroll = 0.0f;
    uint32_t touchLastSampleMs = 0;
    float scrollPosition = 0.0f;
    float scrollVelocity = 0.0f;
    uint32_t scrollLastAnimationMs = 0;
    uint32_t scrollLastDrawMs = 0;
    bool softbarFocused = false;
    uint8_t softbarIndex = 3;
    uint32_t backPressedMs = 0;
    bool backLongHandled = false;
};
