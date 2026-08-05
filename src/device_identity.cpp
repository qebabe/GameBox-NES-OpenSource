#include "device_identity.h"

#include <esp_mac.h>

#include "device_identity_format.h"

static bool readFactoryStaMac(uint8_t mac[6]) {
    return mac && esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
}

String gameBoxDeviceId() {
    static const String deviceId = []() {
        uint8_t mac[6] = {0};
        char value[15] = {0};
        if (!readFactoryStaMac(mac) || !formatGameBoxDeviceId(mac, value, sizeof(value))) {
            return String("GB000000000000");
        }
        return String(value);
    }();
    return deviceId;
}

String gameBoxMacAddress() {
    uint8_t mac[6] = {0};
    char value[18] = {0};
    if (!readFactoryStaMac(mac) || !formatGameBoxMacAddress(mac, value, sizeof(value))) {
        return String("00:00:00:00:00:00");
    }
    return String(value);
}
