#pragma once

#include <Arduino.h>

bool cloudSaveUploadRomSaves(const char* romPath, int& uploaded, String& error);
bool cloudSaveRestoreRomSaves(const char* romPath, int& restored, String& error);
bool cloudSaveMarkPending(const char* romPath, const char* slot = nullptr);
bool cloudSaveHasPending();
bool cloudSaveSyncPending(int& uploaded, String& error);
void cloudSaveRequestBackgroundSync(uint32_t delayMs = 2000);
void cloudSaveUpdate();
bool cloudSaveSyncInProgress();
void cloudSavePauseBackgroundSync();
void cloudSaveResumeBackgroundSync();

class CloudSaveBackgroundPauseGuard {
public:
    CloudSaveBackgroundPauseGuard() { cloudSavePauseBackgroundSync(); }
    ~CloudSaveBackgroundPauseGuard() { cloudSaveResumeBackgroundSync(); }
    CloudSaveBackgroundPauseGuard(const CloudSaveBackgroundPauseGuard&) = delete;
    CloudSaveBackgroundPauseGuard& operator=(const CloudSaveBackgroundPauseGuard&) = delete;
};
