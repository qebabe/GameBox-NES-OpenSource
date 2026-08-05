#include "ota_version.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char* skipVersionPrefix(const char* version) {
    if (!version) return "";
    return (*version == 'v' || *version == 'V') ? version + 1 : version;
}

static long readVersionNumber(const char*& cursor) {
    long value = 0;
    while (isdigit((unsigned char)*cursor)) {
        value = value * 10 + (*cursor - '0');
        cursor++;
    }
    return value;
}

bool isValidOtaVersion(const char* version) {
    const char* cursor = skipVersionPrefix(version);
    for (int component = 0; component < 3; component++) {
        if (!isdigit((unsigned char)*cursor)) return false;
        while (isdigit((unsigned char)*cursor)) cursor++;
        if (component < 2) {
            if (*cursor != '.') return false;
            cursor++;
        }
    }
    if (*cursor == '\0') return true;
    if (*cursor++ != '-' || *cursor == '\0') return false;
    while (*cursor) {
        unsigned char c = (unsigned char)*cursor++;
        if (!(isalnum(c) || c == '.' || c == '-')) return false;
    }
    return true;
}

int compareOtaVersions(const char* left, const char* right) {
    const char* a = skipVersionPrefix(left);
    const char* b = skipVersionPrefix(right);

    for (int component = 0; component < 3; component++) {
        long av = readVersionNumber(a);
        long bv = readVersionNumber(b);
        if (av != bv) return av < bv ? -1 : 1;
        if (*a == '.') a++;
        if (*b == '.') b++;
    }

    bool aPrerelease = *a == '-';
    bool bPrerelease = *b == '-';
    if (aPrerelease != bPrerelease) return aPrerelease ? -1 : 1;
    if (!aPrerelease) return 0;

    int suffix = strcmp(a + 1, b + 1);
    return suffix < 0 ? -1 : (suffix > 0 ? 1 : 0);
}
