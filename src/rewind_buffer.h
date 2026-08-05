#pragma once

#include <Arduino.h>

class NES;

class RewindBuffer {
public:
    explicit RewindBuffer(NES& nes) : nes_(nes) {}
    ~RewindBuffer();

    bool begin(uint8_t seconds);
    void clear();
    bool capture();
    bool rewindStep();
    bool available() const { return count_ > 0; }
    uint8_t configuredSeconds() const { return configuredSeconds_; }
    uint8_t actualSeconds() const;

private:
    NES& nes_;
    uint8_t* records_ = nullptr;
    uint8_t* recordTypes_ = nullptr; // 1 = previous full state, 0 = XOR delta
    uint8_t* current_ = nullptr;
    uint8_t* scratch_ = nullptr;
    size_t stateSize_ = 0;
    size_t capacity_ = 0;
    size_t head_ = 0;
    size_t count_ = 0;
    uint32_t captureCount_ = 0;
    uint8_t configuredSeconds_ = 0;
};
