#pragma once

#include <stdint.h>

enum class AppScreen : uint8_t {
    Home,
    Library,
    Playing,
    Paused,
};

// Top-level UI state only. Background tasks must post events to the main loop
// instead of mutating this controller directly.
class ScreenController {
public:
    AppScreen current() const { return current_; }
    AppScreen previous() const { return previous_; }
    bool is(AppScreen screen) const { return current_ == screen; }

    void show(AppScreen screen) {
        if (screen == current_) return;
        previous_ = current_;
        current_ = screen;
        generation_++;
    }

    uint32_t generation() const { return generation_; }

private:
    AppScreen current_ = AppScreen::Home;
    AppScreen previous_ = AppScreen::Home;
    uint32_t generation_ = 0;
};
