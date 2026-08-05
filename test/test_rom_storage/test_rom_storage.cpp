#include <unity.h>
#include <string.h>
#include "rom_storage.h"

void test_detects_rom_sources_and_local_paths() {
    TEST_ASSERT_EQUAL_INT((int)RomStorageSource::Sd,
                          (int)romStorageSourceForPath("sd:/mario.nes"));
    TEST_ASSERT_EQUAL_STRING("/mario.nes", romStorageLocalPath("sd:/mario.nes"));
    TEST_ASSERT_EQUAL_INT((int)RomStorageSource::Builtin,
                          (int)romStorageSourceForPath("builtin:/rom/mario.nes"));
    TEST_ASSERT_EQUAL_STRING("/rom/mario.nes", romStorageLocalPath("builtin:/rom/mario.nes"));
    TEST_ASSERT_EQUAL_INT((int)RomStorageSource::Cache,
                          (int)romStorageSourceForPath("cache:/rom/mario.nes"));
}

void test_filters_rom_names() {
    TEST_ASSERT_TRUE(romStorageIsNesPath("/rom/Contra.NES"));
    TEST_ASSERT_FALSE(romStorageIsNesPath("/rom/readme.txt"));
    TEST_ASSERT_TRUE(romStorageShouldSkipName("/rom/._Contra.nes"));
    TEST_ASSERT_TRUE(romStorageShouldSkipName("/rom/.DS_Store"));
    TEST_ASSERT_FALSE(romStorageShouldSkipName("/rom/Contra.nes"));
}

void test_builds_entries_display_names_and_save_paths() {
    char buffer[128];

    TEST_ASSERT_TRUE(romStorageMakeEntry(DIJI_ROM_BUILTIN_PREFIX,
                                         "/rom/Contra.nes",
                                         buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("builtin:/rom/Contra.nes", buffer);

    TEST_ASSERT_TRUE(romStorageMakeEntry(DIJI_ROM_SD_PREFIX,
                                         "Contra.nes",
                                         buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("sd:/Contra.nes", buffer);

    TEST_ASSERT_TRUE(romStorageMakeDisplayName("builtin:/rom/Contra.nes",
                                               buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("Contra", buffer);

    TEST_ASSERT_TRUE(romStorageMakeSavePath("builtin:/rom/Contra.nes",
                                            buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("builtin:/saves/builtin_Contra.sav", buffer);

    TEST_ASSERT_TRUE(romStorageMakeSavePath("sd:/Contra.nes",
                                            buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("sd:/saves/sd_Contra.sav", buffer);

    TEST_ASSERT_TRUE(romStorageMakeDisplayName("cache:/rom/downloads/Contra.nes",
                                               buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("Contra", buffer);

    TEST_ASSERT_TRUE(romStorageMakeSavePath("cache:/rom/downloads/Contra.nes",
                                            buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("cache:/saves/cache_Contra.sav", buffer);
}

void test_shortens_long_builtin_save_paths() {
    char buffer[128];

    TEST_ASSERT_TRUE(romStorageMakeSavePath("builtin:/rom/魂斗罗S枪30条命版.nes",
                                            buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("builtin:/saves/builtin_5585ba73.sav", buffer);
}

void test_nested_same_name_roms_get_distinct_save_paths() {
    char first[128];
    char second[128];

    TEST_ASSERT_TRUE(romStorageMakeSavePath("sd:/set-a/mario.nes", first, sizeof(first)));
    TEST_ASSERT_TRUE(romStorageMakeSavePath("sd:/set-b/mario.nes", second, sizeof(second)));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(first, second));
    TEST_ASSERT_NOT_NULL(strstr(first, "sd:/saves/sd_"));
}

void test_battery_save_uses_srm_extension_and_same_identity() {
    char statePath[128];
    char batteryPath[128];
    TEST_ASSERT_TRUE(romStorageMakeSavePath("sd:/set-a/game.nes", statePath, sizeof(statePath)));
    TEST_ASSERT_TRUE(romStorageMakeBatterySavePath("sd:/set-a/game.nes", batteryPath, sizeof(batteryPath)));
    TEST_ASSERT_NOT_NULL(strstr(statePath, ".sav"));
    TEST_ASSERT_NOT_NULL(strstr(batteryPath, ".srm"));
    statePath[strlen(statePath) - 4] = '\0';
    batteryPath[strlen(batteryPath) - 4] = '\0';
    TEST_ASSERT_EQUAL_STRING(statePath, batteryPath);
}

void test_builds_three_manual_slots_and_auto_slot() {
    char path[128];
    TEST_ASSERT_TRUE(romStorageMakeSlotSavePath("sd:/game.nes", 0, path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("sd:/saves/sd_game.slot1.sav", path);
    TEST_ASSERT_TRUE(romStorageMakeSlotSavePath("sd:/game.nes", 2, path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("sd:/saves/sd_game.slot3.sav", path);
    TEST_ASSERT_FALSE(romStorageMakeSlotSavePath("sd:/game.nes", 3, path, sizeof(path)));
    TEST_ASSERT_TRUE(romStorageMakeAutoSavePath("sd:/game.nes", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("sd:/saves/sd_game.auto.sav", path);
    TEST_ASSERT_TRUE(romStorageMakeSlotThumbnailPath("sd:/game.nes", 1, path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("sd:/saves/sd_game.slot2.thumb", path);
    TEST_ASSERT_TRUE(romStorageMakeAutoThumbnailPath("sd:/game.nes", path, sizeof(path)));
    TEST_ASSERT_EQUAL_STRING("sd:/saves/sd_game.auto.thumb", path);
}

void test_compacts_utf8_transactional_save_names_for_littlefs() {
    const char* rom = "cache:/rom/downloads/4人麻将(J).nes";
    char slotSave[128];
    char autoSave[128];
    char slotThumb[128];
    char autoThumb[128];
    TEST_ASSERT_TRUE(romStorageMakeSlotSavePath(rom, 0, slotSave, sizeof(slotSave)));
    TEST_ASSERT_TRUE(romStorageMakeAutoSavePath(rom, autoSave, sizeof(autoSave)));
    TEST_ASSERT_TRUE(romStorageMakeSlotThumbnailPath(rom, 0, slotThumb, sizeof(slotThumb)));
    TEST_ASSERT_TRUE(romStorageMakeAutoThumbnailPath(rom, autoThumb, sizeof(autoThumb)));

    const char* paths[] = {slotSave, autoSave, slotThumb, autoThumb};
    for (const char* path : paths) {
        const char* name = strrchr(path, '/');
        TEST_ASSERT_NOT_NULL(name);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(32, strlen(name + 1) + strlen(".tmp"));
    }
    TEST_ASSERT_NOT_NULL(strstr(slotSave, "cache:/saves/c_"));
    TEST_ASSERT_EQUAL_STRING("cache:/saves/cache_4人麻将(J).auto.sav", autoSave);
    TEST_ASSERT_NOT_NULL(strstr(slotThumb, "cache:/saves/c_"));
    TEST_ASSERT_NOT_NULL(strstr(autoThumb, "cache:/saves/c_"));
}

void test_builds_download_cache_paths() {
    char buffer[128];

    TEST_ASSERT_TRUE(romStorageMakeDownloadRomPath("folder/Contra.nes",
                                                   buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("/rom/downloads/Contra.nes", buffer);

    TEST_ASSERT_TRUE(romStorageMakeDownloadRomPath("Contra",
                                                   buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("/rom/downloads/Contra.nes", buffer);

    TEST_ASSERT_TRUE(romStorageMakeDownloadCoverPath("cache:/rom/downloads/Contra.nes",
                                                     buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_STRING("/covers/Contra.png", buffer);
}

void test_shortens_long_download_paths_for_littlefs() {
    char buffer[128];

    const char* longName = "魂斗罗S枪30条命中文无敌加强最终收藏特别长文件名版本.nes";
    TEST_ASSERT_TRUE(romStorageMakeDownloadRomPath(longName, buffer, sizeof(buffer)));
    TEST_ASSERT_TRUE(strlen(buffer) < 64);
    TEST_ASSERT_TRUE(strlen(strrchr(buffer, '/') + 1) <= 31);
    TEST_ASSERT_TRUE(strstr(buffer, "/rom/downloads/魂斗罗") == buffer);
    TEST_ASSERT_TRUE(strstr(buffer, ".nes") != nullptr);

    TEST_ASSERT_TRUE(romStorageMakeDownloadCoverPath(buffer, buffer, sizeof(buffer)));
    TEST_ASSERT_TRUE(strlen(buffer) < 48);
    TEST_ASSERT_TRUE(strlen(strrchr(buffer, '/') + 1) <= 31);
    TEST_ASSERT_TRUE(strstr(buffer, "/covers/魂斗罗") == buffer);
    TEST_ASSERT_TRUE(strstr(buffer, ".png") != nullptr);
}

void test_all_visible_rom_sources_are_deletable() {
    TEST_ASSERT_TRUE(romStorageIsDeletableLocalRom("cache:/rom/downloads/Contra.nes"));
    TEST_ASSERT_TRUE(romStorageIsDeletableLocalRom("builtin:/rom/Contra.nes"));
    TEST_ASSERT_TRUE(romStorageIsDeletableLocalRom("sd:/Contra.nes"));
    TEST_ASSERT_TRUE(romStorageIsDeletableLocalRom("/Contra.nes"));
    TEST_ASSERT_FALSE(romStorageIsDeletableLocalRom("cache:/rom/downloads/readme.txt"));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_detects_rom_sources_and_local_paths);
    RUN_TEST(test_filters_rom_names);
    RUN_TEST(test_builds_entries_display_names_and_save_paths);
    RUN_TEST(test_shortens_long_builtin_save_paths);
    RUN_TEST(test_nested_same_name_roms_get_distinct_save_paths);
    RUN_TEST(test_battery_save_uses_srm_extension_and_same_identity);
    RUN_TEST(test_builds_three_manual_slots_and_auto_slot);
    RUN_TEST(test_compacts_utf8_transactional_save_names_for_littlefs);
    RUN_TEST(test_builds_download_cache_paths);
    RUN_TEST(test_shortens_long_download_paths_for_littlefs);
    RUN_TEST(test_all_visible_rom_sources_are_deletable);
    return UNITY_END();
}
