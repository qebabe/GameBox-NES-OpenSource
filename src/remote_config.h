#pragma once

#include <Arduino.h>

// Loads the cached configuration immediately and refreshes it in a background
// task whenever Wi-Fi is available. Safe defaults keep local features enabled.
void remoteConfigUpdate();

bool remoteConfigStoreEnabled();
bool remoteConfigCloudSavesEnabled();
bool remoteConfigUsageReportingEnabled();
uint32_t remoteConfigRevision();
