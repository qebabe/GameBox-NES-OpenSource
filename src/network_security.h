#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

void configureGameBoxTls(WiFiClientSecure& client);
// OTA bodies may be redirected to a private OSS hostname with a different CA.
// Only use this for downloads whose size and SHA-256 came from the authenticated
// GameBox API and are verified before the update is committed.
void configureHashedDownloadTls(WiFiClientSecure& client);
void applyGameBoxTimezone();
void startGameBoxNetworkTimeSync();
bool ensureGameBoxNetworkTime(String& error, uint32_t timeoutMs = 10000);
