#pragma once

#include <stdint.h>

constexpr uint32_t DIJI_ESPNOW_GAMEPAD_MAGIC = 0x444A4750; // DJGP
constexpr uint8_t DIJI_ESPNOW_GAMEPAD_VERSION = 4;
constexpr uint32_t DIJI_ESPNOW_MANAGEMENT_MAGIC = 0x444A4D47; // DJMG
constexpr uint8_t DIJI_ESPNOW_MANAGEMENT_VERSION = 1;
constexpr uint8_t DIJI_ESPNOW_GAMEPAD_BROADCAST_ID = 0xFF;
// 信道切换和广播发送均为异步射频操作，留出更宽松的驻留时间可提高配对可靠性。
constexpr uint32_t DIJI_ESPNOW_CHANNEL_SCAN_INTERVAL_MS = 120;
constexpr uint8_t DIJI_ESPNOW_CHANNEL_SCAN_ORDER[] = {
    1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13
};

enum class DijiEspNowPacketType : uint8_t {
    State = 1,
    PairRequest = 2,
    PairAck = 3,
    HostHeartbeat = 4,
    Rumble = 5,
    ChannelProbe = 6,
    ChannelAck = 7,
    LatencyPing = 8,
    LatencyPong = 9,
    TimeSyncRequest = 10,
    TimeSyncResponse = 11,
};

enum class DijiEspNowManagementType : uint8_t {
    Hello = 1,
    UpdateOffer = 2,
    UpdateStatus = 3,
    WifiCredentials = 4,
};

enum class DijiGamepadUpdateStatus : uint8_t {
    Idle = 0,
    OfferReceived = 1,
    Accepted = 2,
    ConnectingWifi = 3,
    Downloading = 4,
    Installed = 5,
    Failed = 6,
    LowBattery = 7,
};

struct DijiEspNowGamepadPacket {
    uint32_t magic = DIJI_ESPNOW_GAMEPAD_MAGIC;
    uint8_t version = DIJI_ESPNOW_GAMEPAD_VERSION;
    uint8_t type = (uint8_t)DijiEspNowPacketType::State;
    uint8_t controllerId = DIJI_ESPNOW_GAMEPAD_BROADCAST_ID;
    uint8_t buttons = 0;
    uint32_t sequence = 0;
    uint8_t batteryPercent = 0;
    uint8_t flags = 0;
    uint8_t wifiChannel = 0;
    uint8_t rumbleStrength = 0;
    uint16_t rumbleDurationMs = 0;
    uint32_t sampleTimeMs = 0;
    uint32_t syncT1 = 0;
    uint32_t syncT2 = 0;
    uint32_t syncT3 = 0;
};

static_assert(sizeof(DijiEspNowGamepadPacket) <= 250,
              "ESP-NOW controller packet exceeds the v1 payload limit");

constexpr uint8_t DIJI_GAMEPAD_FLAG_BATTERY_VALID = 0x01;

// This management frame deliberately has its own stable magic/version instead
// of changing the latency-sensitive controller protocol. Future controller
// packet revisions can therefore still ask an older OTA-capable gamepad to
// update itself.
struct DijiEspNowManagementPacket {
    uint32_t magic = DIJI_ESPNOW_MANAGEMENT_MAGIC;
    uint8_t version = DIJI_ESPNOW_MANAGEMENT_VERSION;
    uint8_t type = (uint8_t)DijiEspNowManagementType::Hello;
    uint8_t controllerId = DIJI_ESPNOW_GAMEPAD_BROADCAST_ID;
    uint8_t status = (uint8_t)DijiGamepadUpdateStatus::Idle;
    uint32_t sessionId = 0;
    uint8_t progress = 0;
    uint8_t batteryPercent = 0;
    uint8_t flags = 0;
    uint8_t reserved = 0;
    char firmwareVersion[24] = {0};
    char ssid[33] = {0};
    char password[65] = {0};
};

static_assert(sizeof(DijiEspNowManagementPacket) <= 250,
              "ESP-NOW management packet exceeds the v1 payload limit");

struct DijiEspNowUpdateOfferPacket {
    uint32_t magic = DIJI_ESPNOW_MANAGEMENT_MAGIC;
    uint8_t version = DIJI_ESPNOW_MANAGEMENT_VERSION;
    uint8_t type = (uint8_t)DijiEspNowManagementType::UpdateOffer;
    uint8_t controllerId = DIJI_ESPNOW_GAMEPAD_BROADCAST_ID;
    uint8_t reserved = 0;
    uint32_t sessionId = 0;
    uint32_t firmwareSize = 0;
    char firmwareVersion[24] = {0};
    char sha256[65] = {0};
    char url[129] = {0};
};

static_assert(sizeof(DijiEspNowUpdateOfferPacket) <= 250,
              "ESP-NOW update offer exceeds the v1 payload limit");
