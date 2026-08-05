#pragma once

#include <Arduino.h>
#include <stdint.h>

struct SdCardStatus {
    bool mounted = false;
    uint8_t cardType = 0;
    uint64_t cardBytes = 0;
    uint64_t totalBytes = 0;
    uint64_t usedBytes = 0;
    String fileSystem;
};

SdCardStatus sdCardReadStatus(bool mounted);

// Formats the currently mounted SD volume and restores its FatFs mount.
// All open files on the SD card must be closed before calling this function.
bool sdCardFormatMounted(String& error);
