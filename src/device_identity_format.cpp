#include "device_identity_format.h"

#include <stdio.h>

bool formatGameBoxDeviceId(const uint8_t mac[6], char* out, size_t outSize) {
    if (!mac || !out || outSize < 15) {
        return false;
    }
    int written = snprintf(out, outSize, "GB%02X%02X%02X%02X%02X%02X",
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return written == 14;
}

bool formatGameBoxMacAddress(const uint8_t mac[6], char* out, size_t outSize) {
    if (!mac || !out || outSize < 18) {
        return false;
    }
    int written = snprintf(out, outSize, "%02X:%02X:%02X:%02X:%02X:%02X",
                           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return written == 17;
}
