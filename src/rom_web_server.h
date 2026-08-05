#pragma once

#include <Arduino.h>
#include <WebServer.h>

void romWebServerRegisterRoutes(WebServer& sharedServer);
void romWebServerPrepare(bool sdCardAvailable);
void romWebServerCleanup();
bool romWebServerConsumeFilesChanged();
