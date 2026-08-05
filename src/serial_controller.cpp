#include "serial_controller.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool parseSerialControllerLine(const char* line, uint8_t* state) {
    if (!line || !state) return false;

    while (*line && isspace((unsigned char)*line)) {
        line++;
    }

    if (line[0] != 'K' || line[1] != ':') {
        return false;
    }

    int hi = hexValue(line[2]);
    int lo = hexValue(line[3]);
    if (hi < 0 || lo < 0) {
        return false;
    }

    const char* rest = line + 4;
    while (*rest) {
        if (!isspace((unsigned char)*rest)) {
            return false;
        }
        rest++;
    }

    *state = (uint8_t)((hi << 4) | lo);
    return true;
}

SerialControllerCommand parseSerialControllerCommand(const char* line) {
    if (!line) return SerialControllerCommand::None;

    while (*line && isspace((unsigned char)*line)) {
        line++;
    }

    if (line[0] != 'C' || line[1] != ':') {
        return SerialControllerCommand::None;
    }

    const char* command = line + 2;
    size_t length = strlen(command);
    while (length > 0 && isspace((unsigned char)command[length - 1])) {
        length--;
    }

    if (length == 5 && strncmp(command, "AUDIO", length) == 0) {
        return SerialControllerCommand::AudioTest;
    }
    if (length == 7 && strncmp(command, "DISPLAY", length) == 0) {
        return SerialControllerCommand::ToggleDisplayMode;
    }
    if ((length == 8 && strncmp(command, "TOUCHCAL", length) == 0) ||
        (length == 4 && strncmp(command, "TCAL", length) == 0)) {
        return SerialControllerCommand::TouchCalibration;
    }

    return SerialControllerCommand::None;
}
