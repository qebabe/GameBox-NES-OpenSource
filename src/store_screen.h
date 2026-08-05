#pragma once

#include <Arduino.h>
#include "lgfx_conf.h"
#include "touch_layout.h"

class StoreScreen {
public:
    explicit StoreScreen(LGFX& display) : listCanvas(&display) {}

    int selectedIndex = 0;
    LGFX_Sprite listCanvas;
    bool listCanvasReady = false;
    bool touchTracking = false;
    TouchPoint touchStart{};
    bool touchMoved = false;
    int touchLastY = 0;
    float touchStartScroll = 0.0f;
    uint32_t touchLastSampleMs = 0;
    float scrollPosition = 0.0f;
    float scrollVelocity = 0.0f;
    uint32_t scrollLastAnimationMs = 0;
    uint32_t scrollLastDrawMs = 0;
    size_t cacheNextOffset = 0;
    bool cacheHasMore = false;
    int nextNetworkPage = 1;
    bool onlinePaging = false;
    bool networkHasMore = false;
    bool pageLoading = false;
    bool networkLoadFailed = false;
    bool softbarFocused = false;
    uint8_t softbarIndex = 1;
};
