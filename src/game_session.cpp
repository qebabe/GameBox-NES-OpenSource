#include "game_session.h"

#include "cloud_save_client.h"
#include <esp_timer.h>
#include <rom/ets_sys.h>
#include "nes.h"
#include "rom_storage.h"
#include "storage.h"
#include <time.h>

bool GameSession::hasLoadedRom() const { return nes_.getCurrentRomPath()[0] != '\0'; }
const char* GameSession::currentRomPath() const { return nes_.getCurrentRomPath(); }

bool GameSession::loadRom(const char* path, uint16_t* frameBuffer, bool frameskipEnabled) {
    if (!path || !frameBuffer || !nes_.loadROM(path)) return false;
    nes_.reset();
    nes_.setFrameskipEnabled(frameskipEnabled);
    nes_.getPPU().frameBuffer = frameBuffer;
    return true;
}

bool GameSession::makeSavePath(char* path, size_t size) const {
    return romStorageMakeSavePath(nes_.getCurrentRomPath(), path, size);
}

bool GameSession::saveCurrentState() {
    char path[160];
    return makeSavePath(path, sizeof(path)) && nes_.saveState(path);
}

bool GameSession::loadCurrentState() {
    char path[160];
    return makeSavePath(path, sizeof(path)) && nes_.loadState(path);
}

bool GameSession::saveToPath(const char* path) {
    return path && path[0] && nes_.saveState(path);
}

bool GameSession::loadFromPath(const char* path) {
    return path && path[0] && nes_.loadState(path);
}

bool GameSession::saveSlot(uint8_t slot, const uint16_t* frameBuffer) {
    char path[160];
    bool saved = romStorageMakeSlotSavePath(nes_.getCurrentRomPath(), slot, path, sizeof(path)) &&
                 saveToPath(path);
    if (saved && frameBuffer) saveSlotThumbnail(slot, frameBuffer);
    if (saved) {
        String cloudSlot = String("slot") + (slot + 1);
        cloudSaveMarkPending(nes_.getCurrentRomPath(), cloudSlot.c_str());
    }
    return saved;
}

bool GameSession::saveSlotThumbnail(uint8_t slot, const uint16_t* frameBuffer) {
    char path[160];
    if (!romStorageMakeSlotThumbnailPath(nes_.getCurrentRomPath(), slot, path, sizeof(path))) return false;
    return saveThumbnailPath(path, frameBuffer);
}

bool GameSession::saveThumbnailPath(const char* path, const uint16_t* frameBuffer) {
    if (!path || !path[0] || !frameBuffer) return false;
    char temp[168];
    char backup[168];
    int length = snprintf(temp, sizeof(temp), "%s.tmp", path);
    int backupLength = snprintf(backup, sizeof(backup), "%s.bak", path);
    if (length <= 0 || (size_t)length >= sizeof(temp) ||
        backupLength <= 0 || (size_t)backupLength >= sizeof(backup)) return false;
    dijiRemoveStoragePath(temp);
    File file = dijiOpenStoragePath(temp, FILE_WRITE);
    if (!file) return false;
    struct Header { char magic[4]; uint16_t width; uint16_t height; uint32_t timestamp; } header = {
        {'D','T','H','M'}, 160, 120, (uint32_t)time(nullptr)
    };
    bool ok = file.write((const uint8_t*)&header, sizeof(header)) == sizeof(header);
    uint16_t row[160];
    for (int y = 0; ok && y < 120; y++) {
        const uint16_t* source = frameBuffer + (y * 2) * 256;
        for (int x = 0; x < 160; x++) row[x] = source[(x * 256) / 160];
        ok = file.write((const uint8_t*)row, sizeof(row)) == sizeof(row);
    }
    file.close();
    if (!ok) { dijiRemoveStoragePath(temp); return false; }
    dijiRemoveStoragePath(backup);
    const bool hadPrevious = dijiStoragePathExists(path);
    if (hadPrevious && !dijiRenameStoragePath(path, backup)) {
        dijiRemoveStoragePath(temp);
        return false;
    }
    if (!dijiRenameStoragePath(temp, path)) {
        if (hadPrevious) dijiRenameStoragePath(backup, path);
        dijiRemoveStoragePath(temp);
        return false;
    }
    if (hadPrevious) dijiRemoveStoragePath(backup);
    return true;
}

bool GameSession::loadSlot(uint8_t slot) {
    char path[160];
    return romStorageMakeSlotSavePath(nes_.getCurrentRomPath(), slot, path, sizeof(path)) &&
           loadFromPath(path);
}

bool GameSession::saveAutoSlot(const uint16_t* frameBuffer) {
    char path[160];
    const bool saved = romStorageMakeAutoSavePath(nes_.getCurrentRomPath(), path, sizeof(path)) &&
                       saveToPath(path);
    if (saved && frameBuffer) {
        char thumbnailPath[160];
        if (romStorageMakeAutoThumbnailPath(nes_.getCurrentRomPath(), thumbnailPath,
                                            sizeof(thumbnailPath))) {
            saveThumbnailPath(thumbnailPath, frameBuffer);
        }
    }
    if (saved) cloudSaveMarkPending(nes_.getCurrentRomPath(), "auto");
    return saved;
}

bool GameSession::loadAutoSlot() {
    char path[160];
    return romStorageMakeAutoSavePath(nes_.getCurrentRomPath(), path, sizeof(path)) &&
           loadFromPath(path);
}

void GameSession::start(uint8_t forceRenderFrames) {
    resetFrameScheduler(forceRenderFrames);
    gameStartedMs_ = millis();
    lastRenderedMs_ = 0;
    lastBatterySaveMs_ = gameStartedMs_;
    running_ = true;
}

void GameSession::pause(bool flushBattery) {
    running_ = false;
    if (flushBattery && nes_.getCart().hasSRAM() && nes_.flushBatterySave(true)) {
        cloudSaveMarkPending(currentRomPath(), "battery");
    }
}

void GameSession::resume(uint8_t forceRenderFrames) {
    resetFrameScheduler(forceRenderFrames);
    running_ = true;
}

void GameSession::stop(bool flushBattery) {
    running_ = false;
    if (flushBattery && nes_.getCart().hasSRAM() && nes_.flushBatterySave(true)) {
        cloudSaveMarkPending(currentRomPath(), "battery");
    }
    resetFrameScheduler();
}

void GameSession::resetFrameScheduler(uint8_t forceRenderFrames) {
    nextFrameUs_ = 0;
    forceRenderFrames_ = forceRenderFrames;
    consecutiveSkippedFrames_ = 0;
    frameskipPhase_ = 0;
}

bool GameSession::shouldSkipFrame(bool frameskipEnabled, int64_t nowUs) {
    if (nextFrameUs_ == 0) nextFrameUs_ = (uint64_t)nowUs;
    int64_t lag = nowUs - (int64_t)nextFrameUs_;
    bool phaseAllows = frameskipPhase_ == 0 || frameskipPhase_ == 2 ||
                       frameskipPhase_ == 4 || frameskipPhase_ == 6 ||
                       frameskipPhase_ == 8;
    return frameskipEnabled && forceRenderFrames_ == 0 &&
           consecutiveSkippedFrames_ == 0 && phaseAllows &&
           lag > (kFrameTimeUs / 2);
}

void GameSession::finishFrame(bool skipped) {
    if (skipped) {
        consecutiveSkippedFrames_++;
    } else {
        consecutiveSkippedFrames_ = 0;
        if (forceRenderFrames_ > 0) forceRenderFrames_--;
    }
    frameskipPhase_ = (uint8_t)((frameskipPhase_ + 1) % 9);
}

void GameSession::waitForFrameDeadline() {
    uint64_t now = esp_timer_get_time();
    if (nextFrameUs_ == 0) nextFrameUs_ = now;
    if (now < nextFrameUs_) ets_delay_us(nextFrameUs_ - now);
    nextFrameUs_ += kFrameTimeUs;
}

void GameSession::noteRenderedFrame() { lastRenderedMs_ = millis(); }

bool GameSession::launchTimedOut(uint32_t timeoutMs) const {
    return lastRenderedMs_ == 0 && (uint32_t)(millis() - gameStartedMs_) > timeoutMs;
}

void GameSession::serviceBatteryAutosave(uint32_t intervalMs) {
    uint32_t now = millis();
    if ((uint32_t)(now - lastBatterySaveMs_) < intervalMs) return;
    if (nes_.batterySaveDirty() && nes_.flushBatterySave(false)) {
        cloudSaveMarkPending(currentRomPath(), "battery");
    }
    lastBatterySaveMs_ = now;
}
