#pragma once

#include <stddef.h>

enum class RomStorageSource {
    Sd,
    Builtin,
    Cache,
    Unknown,
};

constexpr const char* DIJI_ROM_SD_PREFIX = "sd:";
constexpr const char* DIJI_ROM_BUILTIN_PREFIX = "builtin:";
constexpr const char* DIJI_ROM_CACHE_PREFIX = "cache:";

RomStorageSource romStorageSourceForPath(const char* path);
const char* romStorageLocalPath(const char* path);
bool romStorageIsNesPath(const char* path);
bool romStorageShouldSkipName(const char* path);
bool romStorageMakeEntry(const char* prefix, const char* path, char* out, size_t outSize);
bool romStorageMakeDisplayName(const char* path, char* out, size_t outSize);
bool romStorageMakeSavePath(const char* romPath, char* out, size_t outSize);
bool romStorageMakeSlotSavePath(const char* romPath, int slot, char* out, size_t outSize);
bool romStorageMakeAutoSavePath(const char* romPath, char* out, size_t outSize);
bool romStorageMakeSlotThumbnailPath(const char* romPath, int slot, char* out, size_t outSize);
bool romStorageMakeAutoThumbnailPath(const char* romPath, char* out, size_t outSize);
bool romStorageMakeBatterySavePath(const char* romPath, char* out, size_t outSize);
bool romStorageMakeDownloadRomPath(const char* filename, char* out, size_t outSize);
bool romStorageMakeDownloadCoverPath(const char* romPath, char* out, size_t outSize);
bool romStorageIsDeletableLocalRom(const char* romPath);
