#pragma once

#include <Arduino.h>

using SettingsWebJsonProvider = String (*)();
using SettingsWebApplyHandler = bool (*)(const String& key, const String& value,
                                         String& error);

struct SettingsWebServerStatus {
    bool active = false;
    bool accessPoint = false;
    String ssid;
    String url;
    String message;
};

bool settingsWebServerBegin(bool sdCardAvailable,
                            SettingsWebJsonProvider jsonProvider,
                            SettingsWebApplyHandler applyHandler);
void settingsWebServerLoop();
void settingsWebServerStop();
SettingsWebServerStatus settingsWebServerStatus();
