#pragma once

#include <stdint.h>

constexpr uint8_t DIJI_BTN_A = 0x01;
constexpr uint8_t DIJI_BTN_B = 0x02;
constexpr uint8_t DIJI_BTN_SELECT = 0x04;
constexpr uint8_t DIJI_BTN_START = 0x08;
constexpr uint8_t DIJI_BTN_UP = 0x10;
constexpr uint8_t DIJI_BTN_DOWN = 0x20;
constexpr uint8_t DIJI_BTN_LEFT = 0x40;
constexpr uint8_t DIJI_BTN_RIGHT = 0x80;

enum class SerialControllerCommand : uint8_t {
    None = 0,
    AudioTest,
    ToggleDisplayMode,
    TouchCalibration,
};

bool parseSerialControllerLine(const char* line, uint8_t* state);
SerialControllerCommand parseSerialControllerCommand(const char* line);
