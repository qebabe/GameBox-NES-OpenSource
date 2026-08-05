#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

bool audioOutputBegin(int sampleRate, int mclkPin, int bclkPin, int lrclkPin, int dataPin);
esp_err_t audioOutputWrite(const void* data, size_t size, size_t* bytesWritten);
void audioOutputZero();
void audioOutputEnd();
void audioOutputDumpInfo();
