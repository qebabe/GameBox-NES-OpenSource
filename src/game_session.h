#pragma once

#include <Arduino.h>

class NES;

class GameSession {
public:
    explicit GameSession(NES& nes) : nes_(nes) {}

    bool running() const { return running_; }
    bool hasLoadedRom() const;
    const char* currentRomPath() const;
    bool loadRom(const char* path, uint16_t* frameBuffer, bool frameskipEnabled);
    bool saveCurrentState();
    bool loadCurrentState();
    bool saveSlot(uint8_t slot, const uint16_t* frameBuffer = nullptr);
    bool loadSlot(uint8_t slot);
    bool saveAutoSlot(const uint16_t* frameBuffer = nullptr);
    bool loadAutoSlot();
    void start(uint8_t forceRenderFrames = 3);
    void pause(bool flushBattery = false);
    void resume(uint8_t forceRenderFrames = 3);
    void stop(bool flushBattery = true);

    void resetFrameScheduler(uint8_t forceRenderFrames = 2);
    bool shouldSkipFrame(bool frameskipEnabled, int64_t nowUs);
    void finishFrame(bool skipped);
    void waitForFrameDeadline();

    void noteRenderedFrame();
    bool launchTimedOut(uint32_t timeoutMs = 3500) const;
    void serviceBatteryAutosave(uint32_t intervalMs = 30000);

private:
    bool makeSavePath(char* path, size_t size) const;
    bool saveToPath(const char* path);
    bool loadFromPath(const char* path);
    bool saveSlotThumbnail(uint8_t slot, const uint16_t* frameBuffer);
    bool saveThumbnailPath(const char* path, const uint16_t* frameBuffer);
    static constexpr uint32_t kFrameTimeUs = 16639;
    NES& nes_;
    volatile bool running_ = false;
    uint32_t gameStartedMs_ = 0;
    uint32_t lastRenderedMs_ = 0;
    uint32_t lastBatterySaveMs_ = 0;
    uint64_t nextFrameUs_ = 0;
    uint8_t forceRenderFrames_ = 0;
    uint8_t consecutiveSkippedFrames_ = 0;
    uint8_t frameskipPhase_ = 0;
};
