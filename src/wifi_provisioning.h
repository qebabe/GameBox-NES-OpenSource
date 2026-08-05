#pragma once

#include <Arduino.h>

enum class WifiProvisioningState {
    Idle,
    PortalRunning,
    Connecting,
    Connected,
    Failed,
};

struct WifiProvisioningStatus {
    WifiProvisioningState state = WifiProvisioningState::Idle;
    String apSsid;
    String selectedSsid;
    String ip;
    String message;
};

void wifiProvisioningBegin();
void wifiProvisioningLoop();
void wifiProvisioningStop();
bool wifiProvisioningActive();
bool wifiProvisioningFinished();
bool wifiProvisioningConnected();
WifiProvisioningStatus wifiProvisioningStatus();
bool wifiProvisioningHasSavedConfig();
int wifiProvisioningSavedNetworkCount();
bool wifiProvisioningGetSavedSsid(char* out, size_t outSize);
bool wifiProvisioningGetSavedCredentials(char* ssidOut, size_t ssidSize,
                                         char* passOut, size_t passSize);
bool wifiProvisioningGetLastSuccessfulCredentials(char* ssidOut, size_t ssidSize,
                                                  char* passOut, size_t passSize);
bool wifiProvisioningGetCredentialsForSsid(const char* ssid,
                                           char* passOut, size_t passSize);
void wifiProvisioningRememberSuccessfulSsid(const char* ssid);
bool wifiProvisioningGetMatchingSavedCredentials(char* ssidOut, size_t ssidSize,
                                                char* passOut, size_t passSize);
bool wifiProvisioningGetBestSavedCredentialsFromScan(int scanCount,
                                                     char* ssidOut, size_t ssidSize,
                                                     char* passOut, size_t passSize,
                                                     int* rssiOut = nullptr);
