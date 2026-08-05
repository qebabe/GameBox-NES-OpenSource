#include "display_pipeline.h"

#include <esp_heap_caps.h>
#include "lgfx_conf.h"

static constexpr int kScreenWidth = 256;
static constexpr int kScreenHeight = 240;
static constexpr int kTftOffsetX = (320 - kScreenWidth) / 2;
static constexpr int kOverscanCropX = 4;
static constexpr int kDisplayWidth = kScreenWidth - kOverscanCropX * 2;
static constexpr int kBlockLines = 16;

uint16_t* DisplayPipeline::allocateFrameBuffer(size_t pixels) {
    size_t bytes = pixels * sizeof(uint16_t);
    uint16_t* buffer = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return buffer ? buffer : (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
}

bool DisplayPipeline::begin(LGFX& display, bool* stretchFullscreen) {
    display_ = &display;
    stretchFullscreen_ = stretchFullscreen;
    for (uint8_t i = 0; i < 2; i++) {
        frameBuffers_[i] = allocateFrameBuffer(kScreenWidth * kScreenHeight);
        if (frameBuffers_[i]) memset(frameBuffers_[i], 0, kScreenWidth * kScreenHeight * sizeof(uint16_t));
        Serial.printf("Frame buffer %u %s at %p\n", i,
                      frameBuffers_[i] ? "allocated" : "failed", frameBuffers_[i]);
    }
    lineBuffer_ = (uint16_t*)heap_caps_malloc(
        DIJI_LCD_PANEL_HEIGHT * kBlockLines * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    Serial.printf("Display DMA line buffer %s at %p\n",
                  lineBuffer_ ? "allocated" : "failed", lineBuffer_);
    queue_ = xQueueCreate(1, sizeof(uint8_t));
    if (queue_) {
        if (xTaskCreatePinnedToCore(taskEntry, "Display", 6144, this, 1, &taskHandle_, 0) != pdPASS) {
            taskHandle_ = nullptr;
        }
    }
    return ready();
}

bool DisplayPipeline::ready() const {
    return display_ && frameBuffers_[0] && frameBuffers_[1] && lineBuffer_ && queue_ && taskHandle_;
}

bool DisplayPipeline::enqueueCurrentFrame() {
    if (paused_ || !queue_) return false;
    uint8_t index = renderBufferIndex_;
    if (xQueueSend(queue_, &index, 0) != pdTRUE) return false;
    renderBufferIndex_ = 1 - renderBufferIndex_;
    return true;
}

void DisplayPipeline::pause() {
    paused_ = true;
    if (queue_) {
        uint8_t discarded;
        while (xQueueReceive(queue_, &discarded, 0) == pdTRUE) {}
    }
    uint32_t started = millis();
    while (taskActive_ && (uint32_t)(millis() - started) < 150) delay(1);
    if (display_) display_->waitDMA();
}

void DisplayPipeline::resume() { paused_ = false; }

void DisplayPipeline::clear() {
    for (auto* buffer : frameBuffers_) {
        if (buffer) memset(buffer, 0, kScreenWidth * kScreenHeight * sizeof(uint16_t));
    }
    if (queue_) {
        uint8_t discarded;
        while (xQueueReceive(queue_, &discarded, 0) == pdTRUE) {}
    }
}

void DisplayPipeline::taskEntry(void* argument) {
    static_cast<DisplayPipeline*>(argument)->taskLoop();
}

void DisplayPipeline::taskLoop() {
    taskStarted_ = true;
    uint8_t index;
    for (;;) {
        if (xQueueReceive(queue_, &index, portMAX_DELAY) != pdTRUE) continue;
        if (paused_) continue;
        uint32_t started = micros();
        uint16_t* buffer = frameBuffers_[index];
        taskActive_ = true;
        display_->startWrite();
        bool stretch = stretchFullscreen_ && *stretchFullscreen_;
        if (pendingNativeBorderClear_ && !stretch) {
            pendingNativeBorderClear_ = false;
            display_->fillRect(0, 0, kTftOffsetX + kOverscanCropX, kScreenHeight, TFT_BLACK);
            display_->fillRect(kTftOffsetX + kScreenWidth - kOverscanCropX, 0,
                               kTftOffsetX + kOverscanCropX, kScreenHeight, TFT_BLACK);
        }
        for (int baseY = 0; baseY < kScreenHeight; baseY += kBlockLines) {
            int height = min(kBlockLines, kScreenHeight - baseY);
            if (stretch) {
                for (int row = 0; row < height; row++) {
                    uint16_t* dst = lineBuffer_ + row * DIJI_LCD_PANEL_HEIGHT;
                    const uint16_t* src = buffer + (baseY + row) * kScreenWidth;
                    for (int x = 0; x < DIJI_LCD_PANEL_HEIGHT; x++) dst[x] = src[(x * kScreenWidth) / DIJI_LCD_PANEL_HEIGHT];
                }
                display_->setAddrWindow(0, baseY, DIJI_LCD_PANEL_HEIGHT, height);
                display_->pushPixelsDMA(lineBuffer_, DIJI_LCD_PANEL_HEIGHT * height);
            } else {
                for (int row = 0; row < height; row++) {
                    memcpy(lineBuffer_ + row * kDisplayWidth,
                           buffer + (baseY + row) * kScreenWidth + kOverscanCropX,
                           kDisplayWidth * sizeof(uint16_t));
                }
                display_->setAddrWindow(kTftOffsetX + kOverscanCropX, baseY, kDisplayWidth, height);
                display_->pushPixelsDMA(lineBuffer_, kDisplayWidth * height);
            }
            display_->waitDMA();
        }
        display_->endWrite();
        lastDisplayedIndex_ = index;
        taskActive_ = false;
        lastDmaUs_ = micros() - started;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
