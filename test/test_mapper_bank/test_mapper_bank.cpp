#include <unity.h>

#include "mapper_bank.h"

static void test_axrom_register_decodes_bank_and_single_screen() {
    AxromSelection lower = decodeAxromSelection(0x07);
    TEST_ASSERT_EQUAL_UINT8(7, lower.prgBank);
    TEST_ASSERT_FALSE(lower.upperNameTable);

    AxromSelection upper = decodeAxromSelection(0x15);
    TEST_ASSERT_EQUAL_UINT8(5, upper.prgBank);
    TEST_ASSERT_TRUE(upper.upperNameTable);
}

static void test_gxrom_register_decodes_prg_and_chr_banks() {
    GxromSelection selection = decodeGxromSelection(0x32);
    TEST_ASSERT_EQUAL_UINT8(3, selection.prgBank);
    TEST_ASSERT_EQUAL_UINT8(2, selection.chrBank);
}

static void test_bank_offset_wraps_to_available_complete_windows() {
    TEST_ASSERT_EQUAL_UINT32(0x18000, mapperWindowOffset(3, 0x8000, 0x20000));
    TEST_ASSERT_EQUAL_UINT32(0x00000, mapperWindowOffset(4, 0x8000, 0x20000));
    TEST_ASSERT_EQUAL_UINT32(0x06000, mapperWindowOffset(3, 0x2000, 0x08000));
    TEST_ASSERT_EQUAL_UINT32(0, mapperWindowOffset(3, 0x8000, 0x4000));
}

static void test_axrom_and_gxrom_save_state_round_trip() {
    uint8_t data[2] = {};
    MapperExtendedState source;
    source.prgBank = 5;
    source.upperNameTable = true;
    TEST_ASSERT_EQUAL_UINT32(2, saveMapperExtendedState(7, source, data, sizeof(data)));
    MapperExtendedState restored;
    TEST_ASSERT_TRUE(loadMapperExtendedState(7, data, sizeof(data), restored));
    TEST_ASSERT_EQUAL_UINT8(5, restored.prgBank);
    TEST_ASSERT_TRUE(restored.upperNameTable);

    source.prgBank = 3;
    source.chrBank = 2;
    TEST_ASSERT_EQUAL_UINT32(2, saveMapperExtendedState(66, source, data, sizeof(data)));
    restored = MapperExtendedState{};
    TEST_ASSERT_TRUE(loadMapperExtendedState(66, data, sizeof(data), restored));
    TEST_ASSERT_EQUAL_UINT8(3, restored.prgBank);
    TEST_ASSERT_EQUAL_UINT8(2, restored.chrBank);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_axrom_register_decodes_bank_and_single_screen);
    RUN_TEST(test_gxrom_register_decodes_prg_and_chr_banks);
    RUN_TEST(test_bank_offset_wraps_to_available_complete_windows);
    RUN_TEST(test_axrom_and_gxrom_save_state_round_trip);
    return UNITY_END();
}
