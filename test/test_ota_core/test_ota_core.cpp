#include <unity.h>

#include "device_identity_format.h"
#include "ota_version.h"

void test_formats_stable_device_identity_from_full_mac() {
    const uint8_t mac[6] = {0x10, 0x20, 0x3A, 0x4B, 0x5C, 0x6D};
    char deviceId[15];
    char macAddress[18];

    TEST_ASSERT_TRUE(formatGameBoxDeviceId(mac, deviceId, sizeof(deviceId)));
    TEST_ASSERT_EQUAL_STRING("GB10203A4B5C6D", deviceId);
    TEST_ASSERT_TRUE(formatGameBoxMacAddress(mac, macAddress, sizeof(macAddress)));
    TEST_ASSERT_EQUAL_STRING("10:20:3A:4B:5C:6D", macAddress);
}

void test_rejects_small_identity_buffers() {
    const uint8_t mac[6] = {0};
    char small[8];
    TEST_ASSERT_FALSE(formatGameBoxDeviceId(mac, small, sizeof(small)));
    TEST_ASSERT_FALSE(formatGameBoxMacAddress(mac, small, sizeof(small)));
}

void test_compares_semantic_ota_versions() {
    TEST_ASSERT_TRUE(compareOtaVersions("0.5.9", "0.6.0") < 0);
    TEST_ASSERT_TRUE(compareOtaVersions("v1.2.0", "1.1.9") > 0);
    TEST_ASSERT_EQUAL_INT(0, compareOtaVersions("1.2.3", "1.2.3"));
    TEST_ASSERT_TRUE(compareOtaVersions("0.6.0-dev", "0.6.0") < 0);
}

void test_validates_ota_version_format() {
    TEST_ASSERT_TRUE(isValidOtaVersion("0.6.0"));
    TEST_ASSERT_TRUE(isValidOtaVersion("v1.2.3-beta.1"));
    TEST_ASSERT_FALSE(isValidOtaVersion("0.6"));
    TEST_ASSERT_FALSE(isValidOtaVersion("latest"));
    TEST_ASSERT_FALSE(isValidOtaVersion("1.2.3-"));
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_formats_stable_device_identity_from_full_mac);
    RUN_TEST(test_rejects_small_identity_buffers);
    RUN_TEST(test_compares_semantic_ota_versions);
    RUN_TEST(test_validates_ota_version_format);
    return UNITY_END();
}
