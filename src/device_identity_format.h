#pragma once

#include <stddef.h>
#include <stdint.h>

bool formatGameBoxDeviceId(const uint8_t mac[6], char* out, size_t outSize);
bool formatGameBoxMacAddress(const uint8_t mac[6], char* out, size_t outSize);
