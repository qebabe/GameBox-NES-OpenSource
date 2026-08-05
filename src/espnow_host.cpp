#include "espnow_host.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <string.h>
#include "espnow_gamepad_protocol.h"
#include "wifi_provisioning.h"

static constexpr uint32_t GAMEPAD_TIMEOUT_MS = 500;
static constexpr uint32_t GAMEPAD_CONNECTED_MS = 2000;
static constexpr uint32_t HOST_HEARTBEAT_MS = 1000;
static constexpr uint32_t LATENCY_SAMPLE_INTERVAL_MS = 120;
static constexpr uint32_t LATENCY_SAMPLE_TIMEOUT_MS = 400;
static constexpr uint32_t TIME_SYNC_INTERVAL_MS = 10000;
static constexpr uint32_t TIME_SYNC_TIMEOUT_MS = 500;
static constexpr uint32_t TIME_SYNC_BEST_WINDOW_MS = 60000;
static constexpr uint8_t PLAYER_COUNT = 2;

struct PairedGamepad {
    bool paired = false;
    uint8_t mac[6] = {0};
    volatile uint8_t buttons = 0;
    volatile uint32_t lastPacketMs = 0;
    volatile uint32_t lastSequence = 0;
    volatile uint8_t batteryPercent = 0;
    volatile bool batteryValid = false;
    bool managementCapable = false;
    uint32_t updateSessionId = 0;
    volatile uint8_t updateStatus = (uint8_t)DijiGamepadUpdateStatus::Idle;
    volatile uint8_t updateProgress = 0;
    char firmwareVersion[24] = {0};
    bool latencyTestRunning = false;
    bool latencyTestComplete = false;
    bool latencyWaiting = false;
    uint8_t latencyTargetSamples = 0;
    uint8_t latencySent = 0;
    uint8_t latencyReceived = 0;
    uint32_t latencyToken = 0;
    uint32_t latencySentMs = 0;
    uint32_t latencyNextSendMs = 0;
    uint32_t latencyTotalMs = 0;
    uint16_t latencyCurrentMs = 0;
    uint16_t latencyMinMs = UINT16_MAX;
    uint16_t latencyMaxMs = 0;
    bool timeSyncValid = false;
    bool timeSyncWaiting = false;
    uint32_t timeSyncT1 = 0;
    uint32_t timeSyncSentMs = 0;
    uint32_t timeSyncNextMs = 0;
    uint32_t timeSyncBestWindowStartedMs = 0;
    uint16_t timeSyncBestRttMs = UINT16_MAX;
    int32_t gamepadToHostOffsetMs = 0;
    bool realtimeLatencyValid = false;
    uint16_t realtimeLatencyMs = 0;
};

static PairedGamepad gamepads[PLAYER_COUNT];
static bool espnow_ready = false;
static volatile int pairing_player = -1;
static volatile bool pairing_succeeded = false;
static uint32_t pairing_deadline_ms = 0;
static uint32_t last_heartbeat_ms = 0;
static uint32_t host_sequence = 0;
static volatile bool pending_pair_save = false;
static volatile uint8_t pending_pair_player = 0;
static volatile bool pending_pair_finalize = false;
static volatile uint8_t pending_channel_ack_mask = 0;
static volatile uint32_t pairing_rx_requests = 0;
static volatile uint32_t pairing_rx_short = 0;
static volatile uint32_t pairing_rx_bad_version = 0;
static volatile uint8_t pairing_last_version = 0;
static volatile uint16_t pairing_last_length = 0;
static uint32_t pairing_last_log_ms = 0;

static bool macEquals(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, 6) == 0;
}

static void copyMac(uint8_t* dst, const uint8_t* src) {
    memcpy(dst, src, 6);
}

static void addPeerIfNeeded(const uint8_t* mac) {
    if (!espnow_ready || !mac) {
        return;
    }
    if (esp_now_is_peer_exist(mac)) {
        return;
    }

    esp_now_peer_info_t peer = {};
    copyMac(peer.peer_addr, mac);
    peer.channel = 0;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        Serial.printf("ESP-NOW add peer failed: %d\n", err);
    }
}

static uint8_t currentWifiChannel() {
    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;
    return esp_wifi_get_channel(&primary, &secondary) == ESP_OK ? primary : 0;
}

static void savePairedMac(uint8_t player) {
    if (player >= PLAYER_COUNT) {
        return;
    }

    Preferences preferences;
    if (!preferences.begin("gamepad", false)) {
        return;
    }
    char key[8];
    snprintf(key, sizeof(key), "p%u", (unsigned)(player + 1));
    preferences.putBytes(key, gamepads[player].mac, sizeof(gamepads[player].mac));
    preferences.end();
}

static void loadPairedMacs() {
    Preferences preferences;
    if (!preferences.begin("gamepad", true)) {
        return;
    }

    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        char key[8];
        snprintf(key, sizeof(key), "p%u", (unsigned)(player + 1));
        if (!preferences.isKey(key)) {
            gamepads[player].paired = false;
            gamepads[player].buttons = 0;
            gamepads[player].lastPacketMs = 0;
            continue;
        }
        size_t read = preferences.getBytes(key, gamepads[player].mac, sizeof(gamepads[player].mac));
        gamepads[player].paired = read == sizeof(gamepads[player].mac);
        gamepads[player].buttons = 0;
        gamepads[player].lastPacketMs = 0;
    }
    preferences.end();
}

static int findPlayerByMac(const uint8_t* mac) {
    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        if (gamepads[player].paired && macEquals(gamepads[player].mac, mac)) {
            return player;
        }
    }
    return -1;
}

static bool packetLooksValid(const DijiEspNowGamepadPacket& packet) {
    return packet.magic == DIJI_ESPNOW_GAMEPAD_MAGIC &&
           packet.version == DIJI_ESPNOW_GAMEPAD_VERSION;
}

static bool managementPacketLooksValid(const DijiEspNowManagementPacket& packet) {
    return packet.magic == DIJI_ESPNOW_MANAGEMENT_MAGIC &&
           packet.version == DIJI_ESPNOW_MANAGEMENT_VERSION;
}

static void handleManagementPacket(const uint8_t* mac,
                                   const DijiEspNowManagementPacket& packet) {
    int player = findPlayerByMac(mac);
    if (player < 0) return;

    PairedGamepad& gamepad = gamepads[player];
    gamepad.managementCapable = true;
    snprintf(gamepad.firmwareVersion, sizeof(gamepad.firmwareVersion), "%s",
             packet.firmwareVersion);
    gamepad.updateStatus = packet.status;
    gamepad.updateProgress = packet.progress;
    if (packet.flags & DIJI_GAMEPAD_FLAG_BATTERY_VALID) {
        gamepad.batteryPercent = packet.batteryPercent;
        gamepad.batteryValid = true;
    }

    if (packet.type == (uint8_t)DijiEspNowManagementType::Hello) {
        Serial.printf("手柄 P%d OTA 已就绪：版本=%s 电量=%s%u\n",
                      player + 1,
                      gamepad.firmwareVersion[0] ? gamepad.firmwareVersion : "未知",
                      gamepad.batteryValid ? "" : "未知/",
                      (unsigned)gamepad.batteryPercent);
    } else if (packet.type == (uint8_t)DijiEspNowManagementType::UpdateStatus) {
        Serial.printf("手柄 P%d OTA 状态=%u 进度=%u%% 版本=%s 会话=%08X\n",
                      player + 1, (unsigned)packet.status,
                      (unsigned)packet.progress,
                      gamepad.firmwareVersion[0] ? gamepad.firmwareVersion : "未知",
                      (unsigned)packet.sessionId);
    }
}

bool espNowHostStartUpdate(uint8_t player, const char* version, const char* url,
                           const char* sha256, size_t size) {
    if (!espnow_ready || player >= PLAYER_COUNT ||
        !gamepads[player].paired || !gamepads[player].managementCapable ||
        !espNowHostControllerConnected(player) ||
        WiFi.status() != WL_CONNECTED || !version || !url || !sha256 ||
        !version[0] || !url[0] || strlen(url) >= 129 ||
        strlen(sha256) != 64 || size == 0 || size > UINT32_MAX) {
        return false;
    }

    String currentSsid = WiFi.SSID();
    char password[65] = {0};
    if (currentSsid.isEmpty() ||
        !wifiProvisioningGetCredentialsForSsid(currentSsid.c_str(),
                                               password, sizeof(password))) {
        Serial.printf("手柄 P%d OTA 未下发：找不到当前 SSID 的保存凭据\n", player + 1);
        return false;
    }

    const uint32_t sessionId = esp_random();
    DijiEspNowUpdateOfferPacket offer;
    offer.controllerId = player;
    offer.sessionId = sessionId;
    offer.firmwareSize = (uint32_t)size;
    snprintf(offer.firmwareVersion, sizeof(offer.firmwareVersion), "%s", version);
    snprintf(offer.sha256, sizeof(offer.sha256), "%s", sha256);
    snprintf(offer.url, sizeof(offer.url), "%s", url);

    addPeerIfNeeded(gamepads[player].mac);
    esp_err_t result = esp_now_send(gamepads[player].mac,
                                    (const uint8_t*)&offer, sizeof(offer));
    if (result != ESP_OK) {
        memset(password, 0, sizeof(password));
        return false;
    }

    // The gamepad accepts the two frames in either order and matches them by
    // session ID. A short gap avoids filling the single radio TX queue.
    delay(30);
    DijiEspNowManagementPacket credentials;
    credentials.type = (uint8_t)DijiEspNowManagementType::WifiCredentials;
    credentials.controllerId = player;
    credentials.sessionId = sessionId;
    snprintf(credentials.ssid, sizeof(credentials.ssid), "%s", currentSsid.c_str());
    snprintf(credentials.password, sizeof(credentials.password), "%s", password);
    result = esp_now_send(gamepads[player].mac,
                          (const uint8_t*)&credentials, sizeof(credentials));
    memset(password, 0, sizeof(password));
    if (result != ESP_OK) return false;

    gamepads[player].updateSessionId = sessionId;
    gamepads[player].updateStatus = (uint8_t)DijiGamepadUpdateStatus::OfferReceived;
    gamepads[player].updateProgress = 0;
    Serial.printf("手柄 P%d OTA 升级申请已下发：%s -> %s 大小=%u 会话=%08X\n",
                  player + 1,
                  gamepads[player].firmwareVersion[0]
                      ? gamepads[player].firmwareVersion : "未知",
                  version, (unsigned)size, (unsigned)sessionId);
    return true;
}

static void sendHostPacket(uint8_t player, DijiEspNowPacketType type) {
    if (!espnow_ready || player >= PLAYER_COUNT || !gamepads[player].paired) {
        return;
    }

    addPeerIfNeeded(gamepads[player].mac);

    DijiEspNowGamepadPacket packet;
    packet.type = (uint8_t)type;
    packet.controllerId = player;
    packet.buttons = 0;
    packet.sequence = host_sequence++;
    packet.wifiChannel = currentWifiChannel();
    esp_now_send(gamepads[player].mac, (const uint8_t*)&packet, sizeof(packet));
}

static void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (!mac || !data || len < (int)sizeof(uint32_t)) {
        return;
    }

    uint32_t magic = 0;
    memcpy(&magic, data, sizeof(magic));
    if (magic == DIJI_ESPNOW_MANAGEMENT_MAGIC) {
        if (len < (int)sizeof(DijiEspNowManagementPacket)) return;
        DijiEspNowManagementPacket management;
        memcpy(&management, data, sizeof(management));
        if (managementPacketLooksValid(management)) {
            handleManagementPacket(mac, management);
        }
        return;
    }
    // 配对失败时需要区分“完全没收到”和“收到但协议不兼容”。
    // PairRequest 的类型位于稳定公共头中，因此可在复制完整结构体前统计。
    if (magic == DIJI_ESPNOW_GAMEPAD_MAGIC && len >= 6 &&
        data[5] == (uint8_t)DijiEspNowPacketType::PairRequest &&
        pairing_player >= 0) {
        pairing_rx_requests++;
        pairing_last_version = data[4];
        pairing_last_length = (uint16_t)len;
        if (len < (int)sizeof(DijiEspNowGamepadPacket)) pairing_rx_short++;
        if (data[4] != DIJI_ESPNOW_GAMEPAD_VERSION) pairing_rx_bad_version++;
    }
    if (len < (int)sizeof(DijiEspNowGamepadPacket)) return;

    DijiEspNowGamepadPacket packet;
    memcpy(&packet, data, sizeof(packet));
    if (!packetLooksValid(packet)) {
        return;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::PairRequest) {
        int target = pairing_player;
        if (target >= 0 && target < PLAYER_COUNT) {
            gamepads[target].paired = true;
            copyMac(gamepads[target].mac, mac);
            gamepads[target].buttons = packet.buttons;
            gamepads[target].lastPacketMs = millis();
            gamepads[target].batteryPercent = packet.batteryPercent;
            gamepads[target].batteryValid = (packet.flags & DIJI_GAMEPAD_FLAG_BATTERY_VALID) != 0;
            gamepads[target].managementCapable = false;
            gamepads[target].updateStatus = (uint8_t)DijiGamepadUpdateStatus::Idle;
            gamepads[target].updateProgress = 0;
            gamepads[target].firmwareVersion[0] = '\0';
            gamepads[target].timeSyncValid = false;
            gamepads[target].timeSyncWaiting = false;
            gamepads[target].timeSyncNextMs = 0;
            gamepads[target].timeSyncBestWindowStartedMs = 0;
            gamepads[target].timeSyncBestRttMs = UINT16_MAX;
            gamepads[target].realtimeLatencyValid = false;
            pending_pair_player = (uint8_t)target;
            pending_pair_save = true;
            pending_pair_finalize = true;
            pairing_player = -1;
        }
        return;
    }

    int player = findPlayerByMac(mac);
    if (packet.type == (uint8_t)DijiEspNowPacketType::ChannelProbe) {
        if (player >= 0) pending_channel_ack_mask |= (uint8_t)(1u << player);
        return;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::LatencyPong) {
        if (player >= 0) {
            PairedGamepad& gamepad = gamepads[player];
            if (gamepad.latencyTestRunning && gamepad.latencyWaiting &&
                packet.sequence == gamepad.latencyToken) {
                uint32_t elapsed = millis() - gamepad.latencySentMs;
                if (elapsed > UINT16_MAX) elapsed = UINT16_MAX;
                uint16_t rtt = (uint16_t)elapsed;
                gamepad.latencyCurrentMs = rtt;
                gamepad.latencyMinMs = min(gamepad.latencyMinMs, rtt);
                gamepad.latencyMaxMs = max(gamepad.latencyMaxMs, rtt);
                gamepad.latencyTotalMs += rtt;
                gamepad.latencyReceived++;
                gamepad.latencyWaiting = false;
                gamepad.latencyNextSendMs = millis() + LATENCY_SAMPLE_INTERVAL_MS;
            }
        }
        return;
    }

    if (packet.type == (uint8_t)DijiEspNowPacketType::TimeSyncResponse) {
        if (player >= 0) {
            PairedGamepad& gamepad = gamepads[player];
            uint32_t t4 = millis();
            if (gamepad.timeSyncWaiting &&
                packet.syncT1 == gamepad.timeSyncT1) {
                int32_t hostRoundTrip = (int32_t)(t4 - packet.syncT1);
                int32_t gamepadProcessing =
                    (int32_t)(packet.syncT3 - packet.syncT2);
                int32_t networkRtt = hostRoundTrip - gamepadProcessing;
                if (networkRtt >= 0 && networkRtt <= (int32_t)UINT16_MAX) {
                    uint16_t rtt = (uint16_t)networkRtt;
                    bool resetWindow =
                        gamepad.timeSyncBestWindowStartedMs == 0 ||
                        (uint32_t)(t4 - gamepad.timeSyncBestWindowStartedMs) >=
                            TIME_SYNC_BEST_WINDOW_MS;
                    if (resetWindow) {
                        gamepad.timeSyncBestWindowStartedMs = t4;
                        gamepad.timeSyncBestRttMs = UINT16_MAX;
                    }
                    if (rtt <= gamepad.timeSyncBestRttMs) {
                        int32_t t2MinusT1 =
                            (int32_t)(packet.syncT2 - packet.syncT1);
                        int32_t t3MinusT4 =
                            (int32_t)(packet.syncT3 - t4);
                        gamepad.gamepadToHostOffsetMs =
                            (t2MinusT1 + t3MinusT4) / 2;
                        gamepad.timeSyncBestRttMs = rtt;
                        gamepad.timeSyncValid = true;
                        Serial.printf("手柄 P%d 时间同步：RTT=%ums 偏移=%ldms\n",
                                      player + 1, (unsigned)rtt,
                                      (long)gamepad.gamepadToHostOffsetMs);
                    }
                }
                gamepad.timeSyncWaiting = false;
                gamepad.timeSyncNextMs = t4 + TIME_SYNC_INTERVAL_MS;
            }
        }
        return;
    }

    if (packet.type != (uint8_t)DijiEspNowPacketType::State) {
        return;
    }

    if (player < 0) {
        return;
    }
    uint32_t now = millis();
    if (gamepads[player].lastPacketMs != 0 &&
        (uint32_t)(now - gamepads[player].lastPacketMs) <= GAMEPAD_CONNECTED_MS &&
        (int32_t)(packet.sequence - gamepads[player].lastSequence) <= 0) return;
    gamepads[player].buttons = packet.buttons;
    gamepads[player].lastSequence = packet.sequence;
    gamepads[player].batteryPercent = packet.batteryPercent;
    gamepads[player].batteryValid = (packet.flags & DIJI_GAMEPAD_FLAG_BATTERY_VALID) != 0;
    gamepads[player].lastPacketMs = now;
    if (gamepads[player].timeSyncValid && packet.sampleTimeMs != 0) {
        uint32_t estimatedHostSample =
            (uint32_t)((int32_t)packet.sampleTimeMs -
                       gamepads[player].gamepadToHostOffsetMs);
        int32_t delay = (int32_t)(now - estimatedHostSample);
        if (delay >= 0 && delay <= 1000) {
            uint16_t sampleDelay = (uint16_t)delay;
            if (!gamepads[player].realtimeLatencyValid) {
                gamepads[player].realtimeLatencyMs = sampleDelay;
                gamepads[player].realtimeLatencyValid = true;
            } else {
                gamepads[player].realtimeLatencyMs =
                    (uint16_t)((gamepads[player].realtimeLatencyMs * 4u +
                                sampleDelay) / 5u);
            }
        }
    }
}

bool espNowHostBegin() {
    if (espnow_ready) {
        espNowHostEnd();
    }

    loadPairedMacs();

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW 主机初始化失败");
        espnow_ready = false;
        return false;
    }

    esp_now_register_recv_cb(onEspNowReceive);
    espnow_ready = true;

    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        if (gamepads[player].paired) {
            addPeerIfNeeded(gamepads[player].mac);
        }
    }

    Serial.printf("ESP-NOW 主机就绪：MAC=%s 信道=%u WiFi=%s\n",
                  WiFi.macAddress().c_str(), (unsigned)currentWifiChannel(),
                  WiFi.status() == WL_CONNECTED ? "已连接" : "离线");
    return true;
}

void espNowHostEnd() {
    if (!espnow_ready) {
        return;
    }
    esp_now_unregister_recv_cb();
    esp_now_deinit();
    espnow_ready = false;
    pairing_player = -1;
    pairing_succeeded = false;
    pairing_deadline_ms = 0;
    pending_pair_save = false;
    pending_pair_finalize = false;
    pending_channel_ack_mask = 0;
    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        gamepads[player].buttons = 0;
        gamepads[player].lastPacketMs = 0;
        gamepads[player].timeSyncValid = false;
        gamepads[player].timeSyncWaiting = false;
        gamepads[player].timeSyncNextMs = 0;
        gamepads[player].timeSyncBestWindowStartedMs = 0;
        gamepads[player].timeSyncBestRttMs = UINT16_MAX;
        gamepads[player].realtimeLatencyValid = false;
    }
}

void espNowHostUpdate() {
    if (!espnow_ready) {
        return;
    }

    uint32_t now = millis();
    if (pairing_player >= 0 &&
        (pairing_last_log_ms == 0 ||
         (uint32_t)(now - pairing_last_log_ms) >= 2000)) {
        pairing_last_log_ms = now;
        Serial.printf("ESP-NOW 配对监听：P%d CH=%u 收到请求=%u 短包=%u "
                      "版本不符=%u 最近版本=%u 最近包长=%u\n",
                      pairing_player + 1, (unsigned)currentWifiChannel(),
                      (unsigned)pairing_rx_requests,
                      (unsigned)pairing_rx_short,
                      (unsigned)pairing_rx_bad_version,
                      (unsigned)pairing_last_version,
                      (unsigned)pairing_last_length);
    }
    if (pending_pair_finalize) {
        uint8_t player = pending_pair_player;
        pending_pair_finalize = false;
        addPeerIfNeeded(gamepads[player].mac);
        savePairedMac(player);
        pending_pair_save = false;
        sendHostPacket(player, DijiEspNowPacketType::PairAck);
        pairing_succeeded = true;
        Serial.printf("ESP-NOW gamepad paired P%d: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      player + 1,
                      gamepads[player].mac[0], gamepads[player].mac[1],
                      gamepads[player].mac[2], gamepads[player].mac[3],
                      gamepads[player].mac[4], gamepads[player].mac[5]);
    }
    if (pending_pair_save) {
        uint8_t player = pending_pair_player;
        pending_pair_save = false;
        savePairedMac(player);
    }
    uint8_t ackMask = pending_channel_ack_mask;
    if (ackMask) {
        pending_channel_ack_mask &= (uint8_t)~ackMask;
        for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
            if (ackMask & (1u << player)) {
                sendHostPacket(player, DijiEspNowPacketType::ChannelAck);
            }
        }
    }
    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        if (gamepads[player].lastPacketMs != 0 &&
            (uint32_t)(now - gamepads[player].lastPacketMs) > GAMEPAD_TIMEOUT_MS) {
            gamepads[player].buttons = 0;
        }
    }

    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        PairedGamepad& gamepad = gamepads[player];
        if (!gamepad.latencyTestRunning) continue;
        if (!espNowHostControllerConnected(player)) {
            gamepad.latencyTestRunning = false;
            gamepad.latencyTestComplete = true;
            continue;
        }
        if (gamepad.latencyWaiting) {
            if ((uint32_t)(now - gamepad.latencySentMs) >= LATENCY_SAMPLE_TIMEOUT_MS) {
                gamepad.latencyWaiting = false;
                gamepad.latencyNextSendMs = now + LATENCY_SAMPLE_INTERVAL_MS;
            }
            continue;
        }
        if (gamepad.latencySent >= gamepad.latencyTargetSamples) {
            gamepad.latencyTestRunning = false;
            gamepad.latencyTestComplete = true;
            continue;
        }
        if ((int32_t)(now - gamepad.latencyNextSendMs) < 0) continue;

        DijiEspNowGamepadPacket packet;
        packet.type = (uint8_t)DijiEspNowPacketType::LatencyPing;
        packet.controllerId = player;
        packet.sequence = esp_random();
        packet.wifiChannel = currentWifiChannel();
        addPeerIfNeeded(gamepad.mac);
        if (esp_now_send(gamepad.mac, (const uint8_t*)&packet, sizeof(packet)) == ESP_OK) {
            gamepad.latencyToken = packet.sequence;
            gamepad.latencySentMs = now;
            gamepad.latencySent++;
            gamepad.latencyWaiting = true;
        } else {
            gamepad.latencySent++;
            gamepad.latencyNextSendMs = now + LATENCY_SAMPLE_INTERVAL_MS;
        }
    }

    for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
        PairedGamepad& gamepad = gamepads[player];
        if (!espNowHostControllerConnected(player)) {
            gamepad.timeSyncWaiting = false;
            gamepad.timeSyncValid = false;
            gamepad.timeSyncNextMs = 0;
            gamepad.timeSyncBestWindowStartedMs = 0;
            gamepad.timeSyncBestRttMs = UINT16_MAX;
            gamepad.realtimeLatencyValid = false;
            continue;
        }
        if (gamepad.timeSyncWaiting) {
            if ((uint32_t)(now - gamepad.timeSyncSentMs) >= TIME_SYNC_TIMEOUT_MS) {
                gamepad.timeSyncWaiting = false;
                gamepad.timeSyncNextMs = now + 1000;
            }
            continue;
        }
        if ((int32_t)(now - gamepad.timeSyncNextMs) < 0) continue;

        DijiEspNowGamepadPacket packet;
        packet.type = (uint8_t)DijiEspNowPacketType::TimeSyncRequest;
        packet.controllerId = player;
        packet.syncT1 = now;
        packet.wifiChannel = currentWifiChannel();
        addPeerIfNeeded(gamepad.mac);
        if (esp_now_send(gamepad.mac, (const uint8_t*)&packet,
                         sizeof(packet)) == ESP_OK) {
            gamepad.timeSyncT1 = now;
            gamepad.timeSyncSentMs = now;
            gamepad.timeSyncWaiting = true;
        } else {
            gamepad.timeSyncNextMs = now + 1000;
        }
    }

    if ((uint32_t)(now - last_heartbeat_ms) >= HOST_HEARTBEAT_MS) {
        last_heartbeat_ms = now;
        for (uint8_t player = 0; player < PLAYER_COUNT; player++) {
            sendHostPacket(player, DijiEspNowPacketType::HostHeartbeat);
        }
    }

    if (pairing_player >= 0 &&
        pairing_deadline_ms != 0 &&
        (int32_t)(now - pairing_deadline_ms) >= 0) {
        Serial.printf("ESP-NOW 配对超时：P%d CH=%u 收到请求=%u 短包=%u "
                      "版本不符=%u 最近版本=%u 最近包长=%u\n",
                      pairing_player + 1, (unsigned)currentWifiChannel(),
                      (unsigned)pairing_rx_requests,
                      (unsigned)pairing_rx_short,
                      (unsigned)pairing_rx_bad_version,
                      (unsigned)pairing_last_version,
                      (unsigned)pairing_last_length);
        pairing_player = -1;
    }
}

uint8_t espNowHostGetControllerState(uint8_t player) {
    if (player >= PLAYER_COUNT) {
        return 0;
    }
    return gamepads[player].buttons;
}

bool espNowHostHasPairedController(uint8_t player) {
    return player < PLAYER_COUNT && gamepads[player].paired;
}

bool espNowHostControllerConnected(uint8_t player) {
    if (player >= PLAYER_COUNT || !gamepads[player].paired || gamepads[player].lastPacketMs == 0) {
        return false;
    }
    return (uint32_t)(millis() - gamepads[player].lastPacketMs) <= GAMEPAD_CONNECTED_MS;
}

bool espNowHostGetPairedMacString(uint8_t player, char* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0 || player >= PLAYER_COUNT || !gamepads[player].paired) {
        if (buffer && bufferSize > 0) {
            buffer[0] = '\0';
        }
        return false;
    }

    snprintf(buffer, bufferSize, "%02X:%02X:%02X:%02X:%02X:%02X",
             gamepads[player].mac[0], gamepads[player].mac[1], gamepads[player].mac[2],
             gamepads[player].mac[3], gamepads[player].mac[4], gamepads[player].mac[5]);
    return true;
}

void espNowHostStartPairing(uint8_t player, uint32_t durationMs) {
    if (!espnow_ready || player >= PLAYER_COUNT) {
        pairing_player = -1;
        pairing_succeeded = false;
        return;
    }
    pairing_player = player;
    pairing_succeeded = false;
    pairing_deadline_ms = millis() + durationMs;
    pairing_rx_requests = 0;
    pairing_rx_short = 0;
    pairing_rx_bad_version = 0;
    pairing_last_version = 0;
    pairing_last_length = 0;
    pairing_last_log_ms = 0;
    Serial.printf("ESP-NOW gamepad pairing P%d for %u ms：CH=%u 协议=%u 包长=%u MAC=%s\n",
                  player + 1, (unsigned)durationMs,
                  (unsigned)currentWifiChannel(),
                  (unsigned)DIJI_ESPNOW_GAMEPAD_VERSION,
                  (unsigned)sizeof(DijiEspNowGamepadPacket),
                  WiFi.macAddress().c_str());
}

void espNowHostCancelPairing() {
    pairing_player = -1;
}

bool espNowHostPairingActive() {
    espNowHostUpdate();
    return pairing_player >= 0;
}

bool espNowHostPairingSucceeded() {
    return pairing_succeeded;
}

uint8_t espNowHostPairingPlayer() {
    if (pairing_player < 0 || pairing_player >= PLAYER_COUNT) {
        return DIJI_ESPNOW_GAMEPAD_BROADCAST_ID;
    }
    return (uint8_t)pairing_player;
}

bool espNowHostGetBatteryPercent(uint8_t player, uint8_t& percent) {
    if (player >= PLAYER_COUNT || !gamepads[player].batteryValid ||
        !espNowHostControllerConnected(player)) return false;
    percent = gamepads[player].batteryPercent;
    return true;
}

bool espNowHostSendRumble(uint8_t player, uint8_t strength, uint16_t durationMs) {
    if (!espnow_ready || player >= PLAYER_COUNT || !gamepads[player].paired) return false;
    addPeerIfNeeded(gamepads[player].mac);
    DijiEspNowGamepadPacket packet;
    packet.type = (uint8_t)DijiEspNowPacketType::Rumble;
    packet.controllerId = player;
    packet.sequence = host_sequence++;
    packet.wifiChannel = currentWifiChannel();
    packet.rumbleStrength = strength;
    packet.rumbleDurationMs = durationMs > 5000 ? 5000 : durationMs;
    return esp_now_send(gamepads[player].mac, (const uint8_t*)&packet, sizeof(packet)) == ESP_OK;
}

uint8_t espNowHostCurrentChannel() {
    return currentWifiChannel();
}

bool espNowHostGetFirmwareVersion(uint8_t player, char* buffer, int bufferSize) {
    if (!buffer || bufferSize <= 0 || player >= PLAYER_COUNT ||
        !gamepads[player].managementCapable || !gamepads[player].firmwareVersion[0] ||
        !espNowHostControllerConnected(player)) {
        if (buffer && bufferSize > 0) buffer[0] = '\0';
        return false;
    }
    snprintf(buffer, bufferSize, "%s", gamepads[player].firmwareVersion);
    return true;
}

bool espNowHostGetUpdateStatus(uint8_t player, uint8_t& status, uint8_t& progress) {
    if (player >= PLAYER_COUNT || !gamepads[player].managementCapable) return false;
    status = gamepads[player].updateStatus;
    progress = gamepads[player].updateProgress;
    return true;
}

bool espNowHostStartLatencyTest(uint8_t player, uint8_t sampleCount) {
    if (!espnow_ready || player >= PLAYER_COUNT ||
        !espNowHostControllerConnected(player) || sampleCount == 0) {
        return false;
    }
    PairedGamepad& gamepad = gamepads[player];
    gamepad.latencyTestRunning = true;
    gamepad.latencyTestComplete = false;
    gamepad.latencyWaiting = false;
    gamepad.latencyTargetSamples = sampleCount > 50 ? 50 : sampleCount;
    gamepad.latencySent = 0;
    gamepad.latencyReceived = 0;
    gamepad.latencyToken = 0;
    gamepad.latencySentMs = 0;
    gamepad.latencyNextSendMs = millis();
    gamepad.latencyTotalMs = 0;
    gamepad.latencyCurrentMs = 0;
    gamepad.latencyMinMs = UINT16_MAX;
    gamepad.latencyMaxMs = 0;
    Serial.printf("手柄 P%d 通信测试开始：样本=%u 信道=%u\n",
                  player + 1, (unsigned)gamepad.latencyTargetSamples,
                  (unsigned)currentWifiChannel());
    return true;
}

void espNowHostCancelLatencyTest(uint8_t player) {
    if (player >= PLAYER_COUNT) return;
    gamepads[player].latencyTestRunning = false;
    gamepads[player].latencyTestComplete = false;
    gamepads[player].latencyWaiting = false;
}

bool espNowHostGetLatencyTestResult(uint8_t player, EspNowLatencyTestResult& result) {
    result = EspNowLatencyTestResult{};
    if (player >= PLAYER_COUNT) return false;
    const PairedGamepad& gamepad = gamepads[player];
    result.running = gamepad.latencyTestRunning;
    result.complete = gamepad.latencyTestComplete;
    result.sent = gamepad.latencySent;
    result.received = gamepad.latencyReceived;
    result.currentMs = gamepad.latencyCurrentMs;
    result.minMs = gamepad.latencyReceived ? gamepad.latencyMinMs : 0;
    result.averageMs = gamepad.latencyReceived
                           ? (uint16_t)(gamepad.latencyTotalMs / gamepad.latencyReceived)
                           : 0;
    result.maxMs = gamepad.latencyMaxMs;
    return gamepad.latencyTestRunning || gamepad.latencyTestComplete;
}

bool espNowHostGetRealtimeLatency(uint8_t player, uint16_t& latencyMs) {
    if (player >= PLAYER_COUNT || !espNowHostControllerConnected(player) ||
        !gamepads[player].timeSyncValid ||
        !gamepads[player].realtimeLatencyValid) {
        return false;
    }
    latencyMs = gamepads[player].realtimeLatencyMs;
    return true;
}
