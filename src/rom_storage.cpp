#include "rom_storage.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static constexpr size_t kMaxLittleFsNameBytes = 31;
// The mounted Arduino LittleFS accepts the 32-byte transactional name seen on
// existing saves, but rejects 33 bytes. Derived save paths must also reserve
// four bytes for the atomic .tmp/.bak suffix.
static constexpr size_t kMaxLittleFsTransactionalNameBytes = 32;

static bool startsWith(const char* text, const char* prefix) {
    return text && prefix && strncmp(text, prefix, strlen(prefix)) == 0;
}

static bool endsWithIgnoreCase(const char* text, const char* suffix) {
    if (!text || !suffix) {
        return false;
    }
    size_t textLen = strlen(text);
    size_t suffixLen = strlen(suffix);
    if (textLen < suffixLen) {
        return false;
    }
    const char* tail = text + textLen - suffixLen;
    for (size_t i = 0; i < suffixLen; i++) {
        char a = tail[i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) {
            return false;
        }
    }
    return true;
}

static const char* basenameOf(const char* path) {
    if (!path) {
        return "";
    }
    const char* local = romStorageLocalPath(path);
    const char* slash = strrchr(local, '/');
    return slash ? slash + 1 : local;
}

static void sanitizeFileName(const char* input, char* out, size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }
    out[0] = '\0';
    if (!input) {
        return;
    }

    const char* slash = strrchr(input, '/');
    const char* backslash = strrchr(input, '\\');
    const char* base = input;
    if (slash && slash + 1 > base) {
        base = slash + 1;
    }
    if (backslash && backslash + 1 > base) {
        base = backslash + 1;
    }

    size_t j = 0;
    for (size_t i = 0; base[i] != '\0' && j + 1 < outSize; i++) {
        unsigned char c = (unsigned char)base[i];
        if (c < 0x20 || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            out[j++] = '_';
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
}

static uint32_t fnv1a32(const char* text) {
    uint32_t hash = 2166136261u;
    if (!text) {
        return hash;
    }
    while (*text) {
        hash ^= (uint8_t)*text++;
        hash *= 16777619u;
    }
    return hash;
}

static size_t utf8CodepointBytes(unsigned char c) {
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

static void copyUtf8Prefix(const char* input, char* out, size_t maxBytes) {
    if (!out || maxBytes == 0) {
        return;
    }
    out[0] = '\0';
    if (!input) {
        return;
    }

    size_t used = 0;
    size_t pos = 0;
    while (input[pos] != '\0') {
        size_t cpBytes = utf8CodepointBytes((unsigned char)input[pos]);
        if (used + cpBytes > maxBytes) {
            break;
        }
        for (size_t i = 0; i < cpBytes && input[pos] != '\0'; i++) {
            out[used++] = input[pos++];
        }
    }
    out[used] = '\0';
}

static void makeShortStorageName(const char* input, const char* extension,
                                 char* out, size_t outSize) {
    if (!out || outSize == 0) {
        return;
    }
    out[0] = '\0';

    char safeName[160];
    sanitizeFileName(input, safeName, sizeof(safeName));
    if (safeName[0] == '\0') {
        snprintf(safeName, sizeof(safeName), "download");
    }

    char stem[144];
    snprintf(stem, sizeof(stem), "%s", safeName);
    char* dot = strrchr(stem, '.');
    if (dot) {
        *dot = '\0';
    }
    if (stem[0] == '\0') {
        snprintf(stem, sizeof(stem), "download");
    }

    const char* ext = extension ? extension : "";
    size_t extLen = strlen(ext);
    size_t stemLen = strlen(stem);
    if (stemLen + extLen <= kMaxLittleFsNameBytes) {
        snprintf(out, outSize, "%s%s", stem, ext);
        return;
    }

    char hashSuffix[12];
    snprintf(hashSuffix, sizeof(hashSuffix), "_%08lx", (unsigned long)fnv1a32(input));
    size_t suffixLen = strlen(hashSuffix);
    size_t maxStemBytes = 1;
    if (kMaxLittleFsNameBytes > extLen + suffixLen + 1) {
        maxStemBytes = kMaxLittleFsNameBytes - extLen - suffixLen;
    }

    char shortStem[96];
    copyUtf8Prefix(stem, shortStem, maxStemBytes);
    if (shortStem[0] == '\0') {
        snprintf(shortStem, sizeof(shortStem), "rom");
    }
    snprintf(out, outSize, "%s%s%s", shortStem, hashSuffix, ext);
}

RomStorageSource romStorageSourceForPath(const char* path) {
    if (startsWith(path, DIJI_ROM_SD_PREFIX)) {
        return RomStorageSource::Sd;
    }
    if (startsWith(path, DIJI_ROM_BUILTIN_PREFIX)) {
        return RomStorageSource::Builtin;
    }
    if (startsWith(path, DIJI_ROM_CACHE_PREFIX)) {
        return RomStorageSource::Cache;
    }
    if (path && path[0] == '/') {
        return RomStorageSource::Sd;
    }
    return RomStorageSource::Unknown;
}

const char* romStorageLocalPath(const char* path) {
    if (!path) {
        return "";
    }
    if (startsWith(path, DIJI_ROM_SD_PREFIX)) {
        return path + strlen(DIJI_ROM_SD_PREFIX);
    }
    if (startsWith(path, DIJI_ROM_BUILTIN_PREFIX)) {
        return path + strlen(DIJI_ROM_BUILTIN_PREFIX);
    }
    if (startsWith(path, DIJI_ROM_CACHE_PREFIX)) {
        return path + strlen(DIJI_ROM_CACHE_PREFIX);
    }
    return path;
}

bool romStorageIsNesPath(const char* path) {
    return endsWithIgnoreCase(path, ".nes");
}

bool romStorageShouldSkipName(const char* path) {
    const char* name = basenameOf(path);
    return name[0] == '\0' ||
           name[0] == '.' ||
           (name[0] == '_' && name[1] == '.');
}

bool romStorageMakeEntry(const char* prefix, const char* path, char* out, size_t outSize) {
    if (!prefix || !path || !out || outSize == 0) {
        return false;
    }
    const char* local = romStorageLocalPath(path);
    int written = snprintf(out, outSize, "%s%s%s",
                           prefix,
                           local[0] == '/' ? "" : "/",
                           local);
    return written > 0 && (size_t)written < outSize;
}

bool romStorageMakeDisplayName(const char* path, char* out, size_t outSize) {
    if (!path || !out || outSize == 0) {
        return false;
    }

    char name[96];
    snprintf(name, sizeof(name), "%s", basenameOf(path));
    char* dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }

    int written = snprintf(out, outSize, "%s", name);
    return written >= 0 && (size_t)written < outSize;
}

bool romStorageMakeSavePath(const char* romPath, char* out, size_t outSize) {
    if (!romPath || !out || outSize == 0) {
        return false;
    }

    const char* prefix = DIJI_ROM_SD_PREFIX;
    const char* sourceName = "sd";
    RomStorageSource source = romStorageSourceForPath(romPath);
    switch (source) {
        case RomStorageSource::Builtin:
            prefix = DIJI_ROM_BUILTIN_PREFIX;
            sourceName = "builtin";
            break;
        case RomStorageSource::Cache:
            prefix = DIJI_ROM_CACHE_PREFIX;
            sourceName = "cache";
            break;
        case RomStorageSource::Sd:
        case RomStorageSource::Unknown:
        default:
            prefix = DIJI_ROM_SD_PREFIX;
            sourceName = "sd";
            break;
    }

    char name[96];
    snprintf(name, sizeof(name), "%s", basenameOf(romPath));
    char* dot = strrchr(name, '.');
    if (dot) {
        *dot = '\0';
    }

    const char* localPath = romStorageLocalPath(romPath);
    const char* relativePath = localPath;
    if (source == RomStorageSource::Builtin && startsWith(localPath, "/rom/")) {
        relativePath = localPath + strlen("/rom/");
    } else if (source == RomStorageSource::Cache && startsWith(localPath, "/rom/downloads/")) {
        relativePath = localPath + strlen("/rom/downloads/");
    } else if (relativePath[0] == '/') {
        relativePath++;
    }

    // Preserve the legacy path for ordinary root-level ROMs, but hash nested
    // paths so e.g. /set-a/game.nes and /set-b/game.nes cannot share a save.
    bool nestedPath = strchr(relativePath, '/') != nullptr;
    char shortName[16];
    const char* saveName = name;
    if (strlen(name) > 18 || nestedPath) {
        snprintf(shortName, sizeof(shortName), "%08lx", (unsigned long)fnv1a32(romPath));
        saveName = shortName;
    }

    int written = snprintf(out, outSize, "%s/saves/%s_%s.sav",
                           prefix, sourceName, saveName);
    return written >= 0 && (size_t)written < outSize;
}

bool romStorageMakeBatterySavePath(const char* romPath, char* out, size_t outSize) {
    char statePath[160];
    if (!romStorageMakeSavePath(romPath, statePath, sizeof(statePath))) {
        return false;
    }
    char* extension = strrchr(statePath, '.');
    if (!extension) {
        return false;
    }
    *extension = '\0';
    int written = snprintf(out, outSize, "%s.srm", statePath);
    return written >= 0 && (size_t)written < outSize;
}

static bool replaceSaveExtension(const char* romPath, const char* suffix,
                                 char* out, size_t outSize) {
    char base[160];
    if (!romStorageMakeSavePath(romPath, base, sizeof(base))) return false;
    char* extension = strrchr(base, '.');
    if (!extension) return false;
    *extension = '\0';
    int written = snprintf(out, outSize, "%s%s", base, suffix);
    if (written < 0 || (size_t)written >= outSize) return false;

    RomStorageSource source = romStorageSourceForPath(romPath);
    if (source != RomStorageSource::Builtin && source != RomStorageSource::Cache) {
        return true;
    }
    const char* fileName = basenameOf(out);
    if (strlen(fileName) + strlen(".tmp") <= kMaxLittleFsTransactionalNameBytes) {
        return true;
    }

    const char* prefix = source == RomStorageSource::Builtin ?
                         DIJI_ROM_BUILTIN_PREFIX : DIJI_ROM_CACHE_PREFIX;
    char sourceTag = source == RomStorageSource::Builtin ? 'b' : 'c';
    written = snprintf(out, outSize, "%s/saves/%c_%08lx%s",
                       prefix,
                       sourceTag,
                       (unsigned long)fnv1a32(romPath),
                       suffix);
    return written >= 0 && (size_t)written < outSize;
}

bool romStorageMakeSlotSavePath(const char* romPath, int slot, char* out, size_t outSize) {
    if (slot < 0 || slot > 2) return false;
    char suffix[16];
    snprintf(suffix, sizeof(suffix), ".slot%d.sav", slot + 1);
    return replaceSaveExtension(romPath, suffix, out, outSize);
}

bool romStorageMakeAutoSavePath(const char* romPath, char* out, size_t outSize) {
    return replaceSaveExtension(romPath, ".auto.sav", out, outSize);
}

bool romStorageMakeSlotThumbnailPath(const char* romPath, int slot, char* out, size_t outSize) {
    if (slot < 0 || slot > 2) return false;
    char suffix[20];
    snprintf(suffix, sizeof(suffix), ".slot%d.thumb", slot + 1);
    return replaceSaveExtension(romPath, suffix, out, outSize);
}

bool romStorageMakeAutoThumbnailPath(const char* romPath, char* out, size_t outSize) {
    return replaceSaveExtension(romPath, ".auto.thumb", out, outSize);
}

bool romStorageMakeDownloadRomPath(const char* filename, char* out, size_t outSize) {
    if (!filename || !out || outSize == 0) {
        return false;
    }

    char safeName[96];
    makeShortStorageName(filename, ".nes", safeName, sizeof(safeName));

    int written = snprintf(out, outSize, "/rom/downloads/%s", safeName);
    return written >= 0 && (size_t)written < outSize;
}

bool romStorageMakeDownloadCoverPath(const char* romPath, char* out, size_t outSize) {
    if (!romPath || !out || outSize == 0) {
        return false;
    }

    char name[96];
    makeShortStorageName(basenameOf(romPath), ".png", name, sizeof(name));

    int written = snprintf(out, outSize, "/covers/%s", name);
    return written >= 0 && (size_t)written < outSize;
}

bool romStorageIsDeletableLocalRom(const char* romPath) {
    if (!romPath) {
        return false;
    }
    RomStorageSource source = romStorageSourceForPath(romPath);
    return (source == RomStorageSource::Builtin ||
            source == RomStorageSource::Cache ||
            source == RomStorageSource::Sd) &&
           romStorageIsNesPath(romPath);
}
