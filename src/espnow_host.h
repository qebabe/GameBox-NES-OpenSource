#pragma once

#include <stdint.h>
#include <stddef.h>

bool espNowHostBegin();
void espNowHostEnd();
void espNowHostUpdate();
uint8_t espNowHostGetControllerState(uint8_t player);
bool espNowHostHasPairedController(uint8_t player);
bool espNowHostControllerConnected(uint8_t player);
bool espNowHostGetPairedMacString(uint8_t player, char* buffer, int bufferSize);
void espNowHostStartPairing(uint8_t player, uint32_t durationMs);
void espNowHostCancelPairing();
bool espNowHostPairingActive();
bool espNowHostPairingSucceeded();
uint8_t espNowHostPairingPlayer();
bool espNowHostGetBatteryPercent(uint8_t player, uint8_t& percent);
bool espNowHostSendRumble(uint8_t player, uint8_t strength, uint16_t durationMs);
uint8_t espNowHostCurrentChannel();
bool espNowHostGetFirmwareVersion(uint8_t player, char* buffer, int bufferSize);
bool espNowHostGetUpdateStatus(uint8_t player, uint8_t& status, uint8_t& progress);
bool espNowHostStartUpdate(uint8_t player, const char* version, const char* url,
                           const char* sha256, size_t size);

struct EspNowLatencyTestResult {
    bool running = false;
    bool complete = false;
    uint8_t sent = 0;
    uint8_t received = 0;
    uint16_t currentMs = 0;
    uint16_t minMs = 0;
    uint16_t averageMs = 0;
    uint16_t maxMs = 0;
};

bool espNowHostStartLatencyTest(uint8_t player, uint8_t sampleCount = 20);
void espNowHostCancelLatencyTest(uint8_t player);
bool espNowHostGetLatencyTestResult(uint8_t player, EspNowLatencyTestResult& result);
bool espNowHostGetRealtimeLatency(uint8_t player, uint16_t& latencyMs);
