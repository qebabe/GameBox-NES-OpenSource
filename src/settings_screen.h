#pragma once

#include <Arduino.h>
#include "lgfx_conf.h"

class SettingsScreen {
public:
    static constexpr int ItemCount = 15;
    explicit SettingsScreen(LGFX& display) : listCanvas(&display) {}

    LGFX_Sprite listCanvas;
    bool listCanvasReady = false;
    String valueCache[ItemCount];
};
