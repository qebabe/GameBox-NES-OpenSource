#include "sd_card_manager.h"

#include "storage.h"

#if !DIJI_DISABLE_SD
#include <ff.h>
#endif

#if !DIJI_DISABLE_SD
static uint64_t fatFsVolumeBytes(const FATFS* fs) {
    if (!fs || fs->n_fatent < 2) return 0;
#if FF_MAX_SS != FF_MIN_SS
    const uint32_t sectorBytes = fs->ssize;
#else
    const uint32_t sectorBytes = FF_MAX_SS;
#endif
    return (uint64_t)(fs->n_fatent - 2) * fs->csize * sectorBytes;
}

static String fatFsTypeName(const FATFS* fs) {
    if (!fs) return "FAT";
    switch (fs->fs_type) {
        case FS_FAT12: return "FAT12";
        case FS_FAT16: return "FAT16";
        case FS_FAT32: return "FAT32";
#ifdef FS_EXFAT
        case FS_EXFAT: return "exFAT";
#endif
        default: return "FAT";
    }
}

static bool findSdFatFs(uint64_t expectedBytes, char drive[4], FATFS** result) {
    FATFS* best = nullptr;
    uint64_t bestDifference = UINT64_MAX;
    int bestDrive = -1;

    for (int index = 0; index < FF_VOLUMES; index++) {
        char candidateDrive[4];
        snprintf(candidateDrive, sizeof(candidateDrive), "%d:", index);
        DWORD freeClusters = 0;
        FATFS* candidate = nullptr;
        if (f_getfree(candidateDrive, &freeClusters, &candidate) != FR_OK || !candidate) continue;

        const uint64_t volumeBytes = fatFsVolumeBytes(candidate);
        const uint64_t difference = expectedBytes > volumeBytes
                                        ? expectedBytes - volumeBytes
                                        : volumeBytes - expectedBytes;
        if (!best || difference < bestDifference) {
            best = candidate;
            bestDifference = difference;
            bestDrive = index;
        }
    }

    if (!best || bestDrive < 0) return false;
    // Do not ever format an unrelated FatFs volume if another one is added to
    // the firmware later. The mounted SD filesystem should be within 5% (or
    // 8 MiB for small cards) of the size reported by Arduino's SD wrapper.
    const uint64_t allowedDifference = expectedBytes / 20 > 8ULL * 1024ULL * 1024ULL
                                           ? expectedBytes / 20
                                           : 8ULL * 1024ULL * 1024ULL;
    if (expectedBytes == 0 || bestDifference > allowedDifference) return false;
    snprintf(drive, 4, "%d:", bestDrive);
    *result = best;
    return true;
}
#endif

SdCardStatus sdCardReadStatus(bool mounted) {
    SdCardStatus status;
    status.mounted = mounted;
    if (!mounted) {
        status.fileSystem = "--";
        return status;
    }

    status.cardType = (uint8_t)DIJI_SD.cardType();
    status.cardBytes = DIJI_SD.cardSize();
    status.totalBytes = DIJI_SD.totalBytes();
    status.usedBytes = DIJI_SD.usedBytes();
    status.fileSystem = "FAT";

#if !DIJI_DISABLE_SD
    char drive[4];
    FATFS* fs = nullptr;
    if (findSdFatFs(status.totalBytes, drive, &fs)) {
        status.fileSystem = fatFsTypeName(fs);
    }
#endif
    return status;
}

bool sdCardFormatMounted(String& error) {
    error = "";
#if DIJI_DISABLE_SD
    error = "当前固件未启用 SD 卡";
    return false;
#else
    if (DIJI_SD.cardType() == CARD_NONE) {
        error = "未检测到 SD 卡";
        return false;
    }

    char drive[4];
    FATFS* fs = nullptr;
    if (!findSdFatFs(DIJI_SD.totalBytes(), drive, &fs)) {
        error = "未找到 SD 卡文件系统";
        return false;
    }

    FRESULT unmountResult = f_mount(nullptr, drive, 0);
    if (unmountResult != FR_OK) {
        error = String("卸载文件系统失败 (") + (int)unmountResult + ")";
        return false;
    }

    void* work = ff_memalloc(FF_MAX_SS);
    if (!work) {
        f_mount(fs, drive, 1);
        error = "格式化工作内存不足";
        return false;
    }

    FRESULT formatResult = f_mkfs(drive, FM_ANY, 0, work, FF_MAX_SS);
    ff_memfree(work);
    FRESULT remountResult = f_mount(fs, drive, 1);

    if (formatResult != FR_OK) {
        error = String("格式化失败 (") + (int)formatResult + ")";
        if (remountResult != FR_OK) error += "，重新挂载失败";
        return false;
    }
    if (remountResult != FR_OK) {
        error = String("格式化完成，但重新挂载失败 (") + (int)remountResult + ")";
        return false;
    }

    DIJI_SD.mkdir("/rom");
    DIJI_SD.mkdir("/saves");
    DIJI_SD.mkdir("/wallpapers");
    return true;
#endif
}
