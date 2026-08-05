#pragma once

#include <stddef.h>
#include <stdint.h>

enum class InesFormat : uint8_t {
    Ines1,
    Nes2,
};

struct InesHeaderInfo {
    uint8_t prgBanks = 0;
    uint8_t chrBanks = 0;
    uint8_t mapper = 0;
    bool mirrorVertical = false;
    bool hasBattery = false;
    bool hasTrainer = false;
    bool fourScreen = false;
    bool dirtyHeader = false;
    InesFormat format = InesFormat::Ines1;
    bool supportedFormat = true;
    bool supportedMapper = false;
};

bool parseInesHeader(const uint8_t* header, size_t size, InesHeaderInfo& info);
