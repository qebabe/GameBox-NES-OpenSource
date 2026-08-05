#pragma once

#include <Arduino.h>
#include <WebServer.h>

void wallpaperWebServerRegisterRoutes(WebServer& sharedServer);
void wallpaperWebServerPrepare(bool sdCardAvailable);
void wallpaperWebServerCleanup();
