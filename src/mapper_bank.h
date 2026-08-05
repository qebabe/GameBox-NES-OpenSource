#pragma once

#include <stddef.h>
#include <stdint.h>

struct AxromSelection {
    uint8_t prgBank;
    bool upperNameTable;
};

struct GxromSelection {
    uint8_t prgBank;
    uint8_t chrBank;
};

AxromSelection decodeAxromSelection(uint8_t value);
GxromSelection decodeGxromSelection(uint8_t value);
size_t mapperWindowOffset(uint8_t bank, size_t bankSize, size_t romSize);

struct MapperExtendedState {
    uint8_t prgBank = 0;
    uint8_t chrBank = 0;
    bool upperNameTable = false;
};

size_t mapperExtendedStateSize(uint8_t mapper);
size_t saveMapperExtendedState(uint8_t mapper, const MapperExtendedState& state,
                               uint8_t* output, size_t outputSize);
bool loadMapperExtendedState(uint8_t mapper, const uint8_t* input, size_t inputSize,
                             MapperExtendedState& state);
