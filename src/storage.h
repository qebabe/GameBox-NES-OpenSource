#pragma once

#include "board_config.h"
#include "rom_storage.h"

#if DIJI_USE_SD_MMC
#include <SD_MMC.h>
#define DIJI_SD SD_MMC
#else
#include <SD.h>
#define DIJI_SD SD
#endif

#include <LittleFS.h>
#include <stdio.h>
#include <sys/stat.h>
#define DIJI_ROMFS LittleFS
#define DIJI_ROMFS_BASE_PATH "/romfs"
#define DIJI_ROMFS_PARTITION_LABEL "romfs"

// Arduino-ESP32 FS::exists() opens the path internally and emits an error for
// every normal cache miss. Use VFS stat for read-only existence checks so the
// serial log remains useful.
static inline bool dijiLittleFsPathExistsQuiet(const char* localPath) {
    if (!localPath || localPath[0] != '/') return false;
    char fullPath[256];
    int written = snprintf(fullPath, sizeof(fullPath), "%s%s",
                           DIJI_ROMFS_BASE_PATH, localPath);
    if (written <= 0 || (size_t)written >= sizeof(fullPath)) return false;
    struct stat info;
    return stat(fullPath, &info) == 0;
}

static inline bool dijiLittleFsRemoveIfPresent(const char* localPath) {
    return !dijiLittleFsPathExistsQuiet(localPath) || DIJI_ROMFS.remove(localPath);
}

static inline File dijiOpenStoragePath(const char* path, const char* mode = FILE_READ) {
    RomStorageSource source = romStorageSourceForPath(path);
    const char* localPath = romStorageLocalPath(path);
    switch (source) {
        case RomStorageSource::Builtin:
        case RomStorageSource::Cache:
            return DIJI_ROMFS.open(localPath, mode);
        case RomStorageSource::Sd:
        case RomStorageSource::Unknown:
        default:
            return DIJI_SD.open(localPath, mode);
    }
}

static inline bool dijiStoragePathExists(const char* path) {
    RomStorageSource source = romStorageSourceForPath(path);
    const char* localPath = romStorageLocalPath(path);
    switch (source) {
        case RomStorageSource::Builtin:
        case RomStorageSource::Cache:
            return dijiLittleFsPathExistsQuiet(localPath);
        case RomStorageSource::Sd:
        case RomStorageSource::Unknown:
        default:
            return DIJI_SD.exists(localPath);
    }
}

static inline bool dijiRemoveStoragePath(const char* path) {
    RomStorageSource source = romStorageSourceForPath(path);
    const char* localPath = romStorageLocalPath(path);
    switch (source) {
        case RomStorageSource::Builtin:
        case RomStorageSource::Cache:
            return dijiLittleFsRemoveIfPresent(localPath);
        case RomStorageSource::Sd:
        case RomStorageSource::Unknown:
        default:
            return DIJI_SD.remove(localPath);
    }
}

static inline bool dijiRenameStoragePath(const char* from, const char* to) {
    RomStorageSource fromSource = romStorageSourceForPath(from);
    RomStorageSource toSource = romStorageSourceForPath(to);
    if (fromSource != toSource) {
        return false;
    }

    const char* fromLocal = romStorageLocalPath(from);
    const char* toLocal = romStorageLocalPath(to);
    switch (fromSource) {
        case RomStorageSource::Builtin:
        case RomStorageSource::Cache:
            return DIJI_ROMFS.rename(fromLocal, toLocal);
        case RomStorageSource::Sd:
        case RomStorageSource::Unknown:
        default:
            return DIJI_SD.rename(fromLocal, toLocal);
    }
}

static inline void dijiEnsureSaveDirectory(const char* path) {
    RomStorageSource source = romStorageSourceForPath(path);
    switch (source) {
        case RomStorageSource::Builtin:
        case RomStorageSource::Cache:
            DIJI_ROMFS.mkdir("/saves");
            break;
        case RomStorageSource::Sd:
        case RomStorageSource::Unknown:
        default:
            DIJI_SD.mkdir("/saves");
            break;
    }
}
