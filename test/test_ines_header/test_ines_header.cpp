#include <unity.h>
#include <array>
#include "ines_header.h"

static std::array<uint8_t, 16> makeHeader(uint8_t prgBanks, uint8_t chrBanks,
                                          uint8_t flags6, uint8_t flags7) {
    std::array<uint8_t, 16> header = {};
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'S';
    header[3] = 0x1A;
    header[4] = prgBanks;
    header[5] = chrBanks;
    header[6] = flags6;
    header[7] = flags7;
    return header;
}

void test_parse_supported_mapper4_header() {
    auto header = makeHeader(16, 16, 0x40, 0x00);
    InesHeaderInfo info;

    TEST_ASSERT_TRUE(parseInesHeader(header.data(), header.size(), info));

    TEST_ASSERT_EQUAL_UINT8(16, info.prgBanks);
    TEST_ASSERT_EQUAL_UINT8(16, info.chrBanks);
    TEST_ASSERT_EQUAL_UINT8(4, info.mapper);
    TEST_ASSERT_FALSE(info.mirrorVertical);
    TEST_ASSERT_FALSE(info.hasBattery);
    TEST_ASSERT_TRUE(info.supportedMapper);
}

void test_dirty_header_ignores_flags7_mapper_bits() {
    auto header = makeHeader(2, 1, 0x20, 0xF0);
    header[12] = 0x42;
    InesHeaderInfo info;

    TEST_ASSERT_TRUE(parseInesHeader(header.data(), header.size(), info));

    TEST_ASSERT_EQUAL_UINT8(2, info.mapper);
    TEST_ASSERT_TRUE(info.dirtyHeader);
    TEST_ASSERT_TRUE(info.supportedMapper);
}

void test_valid_ines1_metadata_bytes_do_not_mark_header_dirty() {
    auto header = makeHeader(2, 1, 0x20, 0x10);
    header[8] = 8;   // PRG RAM size
    header[9] = 1;   // TV system
    InesHeaderInfo info;
    TEST_ASSERT_TRUE(parseInesHeader(header.data(), header.size(), info));
    TEST_ASSERT_FALSE(info.dirtyHeader);
    TEST_ASSERT_EQUAL_UINT8(0x12, info.mapper);
}

void test_nes2_is_identified_and_rejected_as_unsupported_format() {
    auto header = makeHeader(2, 1, 0x00, 0x08);
    InesHeaderInfo info;
    TEST_ASSERT_TRUE(parseInesHeader(header.data(), header.size(), info));
    TEST_ASSERT_EQUAL_INT((int)InesFormat::Nes2, (int)info.format);
    TEST_ASSERT_FALSE(info.supportedFormat);
    TEST_ASSERT_FALSE(info.supportedMapper);
}

void test_four_screen_flag_is_preserved() {
    auto header = makeHeader(2, 1, 0x08, 0x00);
    InesHeaderInfo info;
    TEST_ASSERT_TRUE(parseInesHeader(header.data(), header.size(), info));
    TEST_ASSERT_TRUE(info.fourScreen);
}

void test_invalid_magic_is_rejected() {
    auto header = makeHeader(1, 1, 0x00, 0x00);
    header[0] = 'B';
    InesHeaderInfo info;

    TEST_ASSERT_FALSE(parseInesHeader(header.data(), header.size(), info));
}

void test_mapper_above_four_is_unsupported() {
    auto header = makeHeader(1, 1, 0x50, 0x00);
    InesHeaderInfo info;

    TEST_ASSERT_TRUE(parseInesHeader(header.data(), header.size(), info));

    TEST_ASSERT_EQUAL_UINT8(5, info.mapper);
    TEST_ASSERT_FALSE(info.supportedMapper);
}

void test_mapper7_and_66_are_supported_explicitly() {
    auto mapper7 = makeHeader(8, 0, 0x70, 0x00);
    InesHeaderInfo info;
    TEST_ASSERT_TRUE(parseInesHeader(mapper7.data(), mapper7.size(), info));
    TEST_ASSERT_EQUAL_UINT8(7, info.mapper);
    TEST_ASSERT_TRUE(info.supportedMapper);

    auto mapper66 = makeHeader(8, 4, 0x20, 0x40);
    TEST_ASSERT_TRUE(parseInesHeader(mapper66.data(), mapper66.size(), info));
    TEST_ASSERT_EQUAL_UINT8(66, info.mapper);
    TEST_ASSERT_TRUE(info.supportedMapper);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_supported_mapper4_header);
    RUN_TEST(test_dirty_header_ignores_flags7_mapper_bits);
    RUN_TEST(test_valid_ines1_metadata_bytes_do_not_mark_header_dirty);
    RUN_TEST(test_nes2_is_identified_and_rejected_as_unsupported_format);
    RUN_TEST(test_four_screen_flag_is_preserved);
    RUN_TEST(test_invalid_magic_is_rejected);
    RUN_TEST(test_mapper_above_four_is_unsupported);
    RUN_TEST(test_mapper7_and_66_are_supported_explicitly);
    return UNITY_END();
}
