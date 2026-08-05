#include <unity.h>

#include "espnow_gamepad_protocol.h"

void test_protocol_v4_carries_discovery_and_time_sync_packets() {
    TEST_ASSERT_EQUAL_UINT8(4, DIJI_ESPNOW_GAMEPAD_VERSION);
    TEST_ASSERT_EQUAL_UINT8(6, (uint8_t)DijiEspNowPacketType::ChannelProbe);
    TEST_ASSERT_EQUAL_UINT8(7, (uint8_t)DijiEspNowPacketType::ChannelAck);
    TEST_ASSERT_EQUAL_UINT8(10, (uint8_t)DijiEspNowPacketType::TimeSyncRequest);
    TEST_ASSERT_EQUAL_UINT8(11, (uint8_t)DijiEspNowPacketType::TimeSyncResponse);

    DijiEspNowGamepadPacket packet;
    packet.type = (uint8_t)DijiEspNowPacketType::ChannelAck;
    packet.wifiChannel = 11;
    packet.sampleTimeMs = 1234;
    packet.syncT1 = 100;
    packet.syncT2 = 200;
    packet.syncT3 = 300;
    TEST_ASSERT_EQUAL_UINT8(11, packet.wifiChannel);
    TEST_ASSERT_EQUAL_UINT32(1234, packet.sampleTimeMs);
    TEST_ASSERT_EQUAL_UINT32(100, packet.syncT1);
    TEST_ASSERT_EQUAL_UINT32(200, packet.syncT2);
    TEST_ASSERT_EQUAL_UINT32(300, packet.syncT3);
}

void test_management_protocol_remains_independently_versioned() {
    TEST_ASSERT_EQUAL_UINT8(1, DIJI_ESPNOW_MANAGEMENT_VERSION);
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)DijiEspNowManagementType::UpdateOffer);
    TEST_ASSERT_TRUE(sizeof(DijiEspNowGamepadPacket) <= 250);
    TEST_ASSERT_TRUE(sizeof(DijiEspNowManagementPacket) <= 250);
    TEST_ASSERT_TRUE(sizeof(DijiEspNowUpdateOfferPacket) <= 250);
}

void test_channel_scan_prioritizes_common_non_overlapping_channels() {
    TEST_ASSERT_EQUAL_UINT8(13, sizeof(DIJI_ESPNOW_CHANNEL_SCAN_ORDER));
    TEST_ASSERT_EQUAL_UINT8(1, DIJI_ESPNOW_CHANNEL_SCAN_ORDER[0]);
    TEST_ASSERT_EQUAL_UINT8(6, DIJI_ESPNOW_CHANNEL_SCAN_ORDER[1]);
    TEST_ASSERT_EQUAL_UINT8(11, DIJI_ESPNOW_CHANNEL_SCAN_ORDER[2]);

    bool seen[14] = {false};
    for (uint8_t channel : DIJI_ESPNOW_CHANNEL_SCAN_ORDER) {
        TEST_ASSERT_TRUE(channel >= 1 && channel <= 13);
        TEST_ASSERT_FALSE(seen[channel]);
        seen[channel] = true;
    }
    for (uint8_t channel = 1; channel <= 13; channel++) TEST_ASSERT_TRUE(seen[channel]);
}

int main(int argc, char** argv) {
    UNITY_BEGIN();
    RUN_TEST(test_protocol_v4_carries_discovery_and_time_sync_packets);
    RUN_TEST(test_management_protocol_remains_independently_versioned);
    RUN_TEST(test_channel_scan_prioritizes_common_non_overlapping_channels);
    return UNITY_END();
}
