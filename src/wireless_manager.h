#pragma once

#include <Arduino.h>
#include <atomic>

class WirelessManager {
public:
    bool startGamepads();
    void stopGamepads();
    bool beginSavedWifi(String& ssid, String& error);
    bool connectSavedWifi(String& ssid, String& error);
    void releaseNetworkTask();
    void prepareForExternalWifiChange();
    void reconcileAfterExternalWifiChange();
    void updateBootConnectivity();
    void update();
    void shutdown();

    bool gamepadsActive() const { return gamepadsActive_.load(); }
    bool wifiActive() const { return wifiActive_.load(); }

private:
    void serviceBootFastConnect();
    bool startBootWifiScan(const char* reason, uint32_t maxMsPerChannel);
    void serviceBootWifiScan();
    void cancelBootWifiScan(const char* reason);

    std::atomic<bool> gamepadsActive_{false};
    std::atomic<bool> wifiActive_{false};
    std::atomic<bool> externalWifiChange_{false};
    std::atomic<uint32_t> wifiAttemptStartedMs_{0};
    std::atomic<bool> bootWifiScanActive_{false};
    std::atomic<uint32_t> bootWifiScanLastLogMs_{0};
    std::atomic<uint32_t> bootWifiScanFailureSeenMs_{0};
    std::atomic<uint8_t> bootWifiScanAttempt_{0};
    std::atomic<bool> bootFastConnectActive_{false};
    std::atomic<uint32_t> bootFastConnectStartedMs_{0};
};
