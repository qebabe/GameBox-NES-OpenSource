#include "ines_header.h"

bool parseInesHeader(const uint8_t* header, size_t size, InesHeaderInfo& info) {
    info = InesHeaderInfo{};

    if (!header || size < 16) {
        return false;
    }

    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        return false;
    }

    uint8_t flags6 = header[6];
    uint8_t flags7 = header[7];

    info.prgBanks = header[4];
    info.chrBanks = header[5];
    info.mirrorVertical = (flags6 & 0x01) != 0;
    info.hasBattery = (flags6 & 0x02) != 0;
    info.hasTrainer = (flags6 & 0x04) != 0;
    info.fourScreen = (flags6 & 0x08) != 0;

    if ((flags7 & 0x0C) == 0x08) {
        info.format = InesFormat::Nes2;
        info.supportedFormat = false;
        info.mapper = ((flags6 >> 4) & 0x0F) | (flags7 & 0xF0);
        info.supportedMapper = false;
        return true;
    }

    // iNES 1.0 bytes 8-11 have defined uses. Only the normally reserved tail
    // indicates a classic polluted/"DiskDude" header.
    for (int i = 12; i < 16; i++) {
        if (header[i] != 0) {
            info.dirtyHeader = true;
            break;
        }
    }

    if (info.dirtyHeader) {
        info.mapper = (flags6 >> 4) & 0x0F;
    } else {
        info.mapper = ((flags6 >> 4) & 0x0F) | (flags7 & 0xF0);
    }

    info.supportedMapper = info.mapper == 0 || info.mapper == 1 ||
                           info.mapper == 2 || info.mapper == 3 ||
                           info.mapper == 4 || info.mapper == 7 ||
                           info.mapper == 66;
    return true;
}
