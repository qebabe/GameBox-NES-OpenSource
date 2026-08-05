#pragma once

#include <Arduino.h>
#include "lgfx_conf.h"

class HomeScreen {
public:
    explicit HomeScreen(LGFX& display);
    void invalidate();
    void draw(bool force, bool canResume, uint8_t selectedAction);
    static bool timeValid(time_t now);

private:
    LGFX& display_;
    LGFX_Sprite canvas_;
    bool canvasReady_ = false;
    int lastSecond_ = -2;
    String wallpaperPath_;
};
