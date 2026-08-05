#pragma once

#include <Arduino.h>

void powerManagerHandleEarlyWake();
void powerManagerBegin();
void powerManagerNotifyActivity();
bool powerManagerAutoShutdownDue();
uint16_t powerManagerAutoOffMinutes();
bool powerManagerSetAutoOffMinutes(uint16_t minutes);
bool powerManagerCanWakeFromSleep();
void powerManagerStartDeepSleep();
