#pragma once

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

class LGFX;

class DisplayPipeline {
public:
    bool begin(LGFX& display, bool* stretchFullscreen);
    bool enqueueCurrentFrame();
    void pause();
    void resume();
    void clear();
    void requestNativeBorderClear() { pendingNativeBorderClear_ = true; }

    uint16_t* renderBuffer() const { return frameBuffers_[renderBufferIndex_]; }
    uint16_t* frameBuffer(uint8_t index) const { return index < 2 ? frameBuffers_[index] : nullptr; }
    const uint16_t* lastDisplayedBuffer() const { return frameBuffers_[lastDisplayedIndex_]; }
    uint8_t renderBufferIndex() const { return renderBufferIndex_; }
    uint32_t lastDmaUs() const { return lastDmaUs_; }
    bool paused() const { return paused_; }
    bool ready() const;
    bool taskStarted() const { return taskStarted_; }

private:
    static void taskEntry(void* argument);
    void taskLoop();
    uint16_t* allocateFrameBuffer(size_t pixels);

    LGFX* display_ = nullptr;
    bool* stretchFullscreen_ = nullptr;
    uint16_t* frameBuffers_[2] = {nullptr, nullptr};
    uint16_t* lineBuffer_ = nullptr;
    QueueHandle_t queue_ = nullptr;
    TaskHandle_t taskHandle_ = nullptr;
    volatile uint8_t renderBufferIndex_ = 0;
    volatile uint8_t lastDisplayedIndex_ = 0;
    volatile bool paused_ = true;
    volatile bool taskActive_ = false;
    volatile bool taskStarted_ = false;
    volatile bool pendingNativeBorderClear_ = false;
    volatile uint32_t lastDmaUs_ = 0;
};
