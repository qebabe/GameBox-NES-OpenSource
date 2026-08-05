#pragma once

#include <Arduino.h>

#include "service_config.h"

#ifndef DIJI_FIRMWARE_VERSION
#define DIJI_FIRMWARE_VERSION "0.6.0-dev"
#endif

#ifndef DIJI_OTA_CHANNEL
#define DIJI_OTA_CHANNEL "lcdwiki-es3c28p"
#endif

struct OtaUpdateInfo {
    bool available = false;
    bool forced = false;
    String version;
    String url;
    String sha256;
    String message;
    size_t size = 0;
    struct GamepadUpdate {
        bool present = false;
        bool available = false;
        uint8_t player = 0;
        String currentVersion;
        String version;
        String url;
        String sha256;
        String message;
        size_t size = 0;
    } gamepads[2];
};

using OtaProgressCallback = void (*)(size_t downloaded, size_t total,
                                     size_t bytesPerSecond, void* userData);

const char* otaCurrentVersion();
const char* otaCurrentChannel();
bool otaCheckForUpdate(OtaUpdateInfo& info, String& error);
bool otaInstallUpdate(const OtaUpdateInfo& info, OtaProgressCallback progress,
                      void* userData, String& error);
bool otaCurrentFirmwarePendingVerify();
void otaMarkCurrentFirmwareValid();
void otaReportCurrentFirmwareBootOk();
void otaUpdateReporting();
