#include <unity.h>
#include "serial_controller.h"

void test_parses_uppercase_state_frame() {
    uint8_t state = 0;
    TEST_ASSERT_TRUE(parseSerialControllerLine("K:9A", &state));
    TEST_ASSERT_EQUAL_UINT8(0x9A, state);
}

void test_parses_lowercase_state_frame_with_newline() {
    uint8_t state = 0;
    TEST_ASSERT_TRUE(parseSerialControllerLine("K:af\r\n", &state));
    TEST_ASSERT_EQUAL_UINT8(0xAF, state);
}

void test_rejects_wrong_prefix() {
    uint8_t state = 0x55;
    TEST_ASSERT_FALSE(parseSerialControllerLine("B:01", &state));
    TEST_ASSERT_EQUAL_UINT8(0x55, state);
}

void test_rejects_trailing_garbage() {
    uint8_t state = 0x55;
    TEST_ASSERT_FALSE(parseSerialControllerLine("K:01x", &state));
    TEST_ASSERT_EQUAL_UINT8(0x55, state);
}

void test_parses_audio_command() {
    TEST_ASSERT_EQUAL(
        (int)SerialControllerCommand::AudioTest,
        (int)parseSerialControllerCommand("C:AUDIO\n")
    );
}

void test_parses_display_command() {
    TEST_ASSERT_EQUAL(
        (int)SerialControllerCommand::ToggleDisplayMode,
        (int)parseSerialControllerCommand("C:DISPLAY")
    );
}

void test_parses_touch_calibration_command() {
    TEST_ASSERT_EQUAL(
        (int)SerialControllerCommand::TouchCalibration,
        (int)parseSerialControllerCommand("C:TOUCHCAL\n")
    );
    TEST_ASSERT_EQUAL(
        (int)SerialControllerCommand::TouchCalibration,
        (int)parseSerialControllerCommand("C:TCAL")
    );
}

void test_rejects_unknown_command() {
    TEST_ASSERT_EQUAL(
        (int)SerialControllerCommand::None,
        (int)parseSerialControllerCommand("C:UNKNOWN")
    );
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_parses_uppercase_state_frame);
    RUN_TEST(test_parses_lowercase_state_frame_with_newline);
    RUN_TEST(test_rejects_wrong_prefix);
    RUN_TEST(test_rejects_trailing_garbage);
    RUN_TEST(test_parses_audio_command);
    RUN_TEST(test_parses_display_command);
    RUN_TEST(test_parses_touch_calibration_command);
    RUN_TEST(test_rejects_unknown_command);
    return UNITY_END();
}
