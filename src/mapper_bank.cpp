#include "mapper_bank.h"

AxromSelection decodeAxromSelection(uint8_t value) {
    return {(uint8_t)(value & 0x07u), (value & 0x10u) != 0};
}

GxromSelection decodeGxromSelection(uint8_t value) {
    return {(uint8_t)((value >> 4) & 0x03u), (uint8_t)(value & 0x03u)};
}

size_t mapperWindowOffset(uint8_t bank, size_t bankSize, size_t romSize) {
    if (romSize == 0 || bankSize == 0 || romSize < bankSize) return 0;
    const size_t bankCount = romSize / bankSize;
    return ((size_t)bank % bankCount) * bankSize;
}

size_t mapperExtendedStateSize(uint8_t mapper) {
    return mapper == 7 || mapper == 66 ? 2u : 0u;
}

size_t saveMapperExtendedState(uint8_t mapper, const MapperExtendedState& state,
                               uint8_t* output, size_t outputSize) {
    const size_t required = mapperExtendedStateSize(mapper);
    if (required == 0) return 0;
    if (!output || outputSize < required) return 0;
    output[0] = state.prgBank;
    output[1] = mapper == 7 ? (state.upperNameTable ? 1u : 0u) : state.chrBank;
    return required;
}

bool loadMapperExtendedState(uint8_t mapper, const uint8_t* input, size_t inputSize,
                             MapperExtendedState& state) {
    const size_t required = mapperExtendedStateSize(mapper);
    if (required == 0) return true;
    if (!input || inputSize < required) return false;
    state.prgBank = input[0];
    if (mapper == 7) state.upperNameTable = input[1] != 0;
    else state.chrBank = input[1];
    return true;
}
