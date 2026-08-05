#include "wireless_manager.h"

#include <WiFi.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include "cloud_save_client.h"
#include "espnow_host.h"
#include "game_store.h"
#include "remote_config.h"
#include "usage_client.h"
#include "wifi_provisioning.h"

static const char* wifiStatusName(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS: return "等待连接";
        case WL_NO_SSID_AVAIL: return "未找到网络";
        case WL_SCAN_COMPLETED: return "扫描完成";
        case WL_CONNECTED: return "已连接";
        case WL_CONNECT_FAILED: return "连接失败";
        case WL_CONNECTION_LOST: return "连接丢失";
        case WL_DISCONNECTED: return "已断开";
        case WL_NO_SHIELD: return "无线模块不可用";
        default: return "未知";
    }
}

static void logConnectedWifi(const char* phase, uint32_t elapsedMs) {
    String ip = WiFi.localIP().toString();
    String gateway = WiFi.gatewayIP().toString();
    String dns = WiFi.dnsIP().toString();
    Serial.printf("无线网络 %s：状态=%s SSID=%s 信道=%d 信号=%d IP=%s "
                  "网关=%s DNS=%s 耗时=%u毫秒 可用堆=%u\n",
                  phase, wifiStatusName(WiFi.status()), WiFi.SSID().c_str(),
                  WiFi.channel(), WiFi.RSSI(), ip.c_str(), gateway.c_str(), dns.c_str(),
                  (unsigned)elapsedMs, (unsigned)ESP.getFreeHeap());
}

static void syncPendingCloudSavesIfNeeded() {
    if (!cloudSaveHasPending()) return;
    cloudSaveRequestBackgroundSync(2000);
    Serial.println("云存档：检测到待上传存档，已安排后台同步");
}

bool WirelessManager::startGamepads() {
    if (gamepadsActive_.load()) return true;
    bool started = espNowHostBegin();
    gamepadsActive_.store(started);
    Serial.println(started ? "ESP-NOW 手柄主机：已启用" :
                                     "ESP-NOW 手柄主机：启用失败");
    return started;
}

void WirelessManager::stopGamepads() {
    if (gamepadsActive_.exchange(false)) espNowHostEnd();
}

bool WirelessManager::startBootWifiScan(const char* reason, uint32_t maxMsPerChannel) {
    bootFastConnectActive_.store(false);
    bootWifiScanActive_.store(false);
    WiFi.disconnect(false, false);
    bootWifiScanLastLogMs_.store(0);
    bootWifiScanFailureSeenMs_.store(0);
    bootWifiScanAttempt_.store(1);
    wifiAttemptStartedMs_.store(millis());
    const int result = WiFi.scanNetworks(true, true, false, maxMsPerChannel);
    if (result == WIFI_SCAN_FAILED) {
        Serial.printf("无线网络 开机扫描启动失败：原因=%s 每信道最长=%u毫秒\n",
                      reason ? reason : "未说明", (unsigned)maxMsPerChannel);
        return false;
    }
    bootWifiScanActive_.store(true);
    Serial.printf("无线网络 开机扫描已启动：原因=%s 返回值=%d 每信道最长=%u毫秒\n",
                  reason ? reason : "未说明", result, (unsigned)maxMsPerChannel);
    return true;
}

void WirelessManager::serviceBootFastConnect() {
    if (!bootFastConnectActive_.load()) return;
    if (WiFi.status() == WL_CONNECTED) {
        bootFastConnectActive_.store(false);
        Serial.printf("无线网络 上次网络快速连接成功：SSID=%s 耗时=%u毫秒\n",
                      WiFi.SSID().c_str(),
                      (unsigned)(millis() - bootFastConnectStartedMs_.load()));
        return;
    }

    const uint32_t elapsedMs = millis() - bootFastConnectStartedMs_.load();
    const wl_status_t status = WiFi.status();
    const bool explicitFailure = status == WL_NO_SSID_AVAIL ||
                                 status == WL_CONNECT_FAILED;
    if (!explicitFailure && elapsedMs < 2500) return;

    bootFastConnectActive_.store(false);
    Serial.printf("无线网络 上次网络快速连接未成功：状态=%s 耗时=%u毫秒，转入环境扫描\n",
                  wifiStatusName(status), (unsigned)elapsedMs);
    if (!startBootWifiScan(explicitFailure ? "上次网络明确不可用" : "上次网络连接超时",
                           500)) {
        // A scan can fail to start while the WiFi driver is finishing an
        // association event. Retain the known credential as a safe fallback;
        // a foreground network screen can still perform its own scan later.
        WiFi.reconnect();
        Serial.println("无线网络 环境扫描未能启动，已恢复上次网络自动重连");
    }
}

void WirelessManager::cancelBootWifiScan(const char* reason) {
    bootFastConnectActive_.store(false);
    if (!bootWifiScanActive_.exchange(false)) return;
    const esp_err_t stopResult = esp_wifi_scan_stop();
    const uint32_t startedMs = millis();
    while (WiFi.scanComplete() == WIFI_SCAN_RUNNING &&
           (uint32_t)(millis() - startedMs) < 500) {
        delay(10);
    }
    const int finalState = WiFi.scanComplete();
    WiFi.scanDelete();
    Serial.printf("无线网络 取消开机扫描：原因=%s 停止结果=%s 最终扫描状态=%d 耗时=%u毫秒\n",
                  reason ? reason : "未说明", esp_err_to_name(stopResult), finalState,
                  (unsigned)(millis() - startedMs));
}

void WirelessManager::serviceBootWifiScan() {
    if (!bootWifiScanActive_.load()) return;

    const int scanCount = WiFi.scanComplete();
    if (scanCount == WIFI_SCAN_RUNNING) {
        const uint32_t now = millis();
        const uint32_t lastLogMs = bootWifiScanLastLogMs_.load();
        if (lastLogMs == 0 || (uint32_t)(now - lastLogMs) >= 2000) {
            bootWifiScanLastLogMs_.store(now);
            Serial.printf("无线网络 开机扫描中：已用时=%u毫秒\n",
                          (unsigned)(now - wifiAttemptStartedMs_.load()));
        }
        return;
    }

    if (scanCount == WIFI_SCAN_FAILED) {
        const uint32_t now = millis();
        const uint32_t failureSeenMs = bootWifiScanFailureSeenMs_.load();
        if (failureSeenMs == 0) {
            bootWifiScanFailureSeenMs_.store(now);
            Serial.printf("无线网络 开机扫描暂未完成：状态=%d，等待底层扫描完成事件收尾\n",
                          scanCount);
            return;
        }
        if ((uint32_t)(now - failureSeenMs) < 1500) return;

        if (bootWifiScanAttempt_.load() < 2) {
            const esp_err_t stopResult = esp_wifi_scan_stop();
            delay(30);
            WiFi.scanDelete();
            bootWifiScanAttempt_.store(2);
            bootWifiScanFailureSeenMs_.store(0);
            bootWifiScanLastLogMs_.store(0);
            const int retryResult = WiFi.scanNetworks(true, true, false, 600);
            Serial.printf("无线网络 开机扫描自动重试：停止结果=%s 重试返回值=%d "
                          "本次每信道最长=600毫秒\n",
                          esp_err_to_name(stopResult), retryResult);
            if (retryResult != WIFI_SCAN_FAILED) return;
        }

        Serial.println("无线网络 开机扫描两次均失败，将使用保存记录回退连接");
    }

    bootWifiScanActive_.store(false);
    char ssidBuffer[64] = {0};
    char passBuffer[96] = {0};
    int selectedRssi = -1000;
    bool matched = scanCount >= 0 &&
                   wifiProvisioningGetBestSavedCredentialsFromScan(
                       scanCount, ssidBuffer, sizeof(ssidBuffer),
                       passBuffer, sizeof(passBuffer), &selectedRssi);
    WiFi.scanDelete();

    if (!matched) {
        Serial.printf("无线网络 开机扫描未选中网络：扫描结果=%d，将尝试第一条已保存网络\n",
                      scanCount);
        if (!wifiProvisioningGetSavedCredentials(ssidBuffer, sizeof(ssidBuffer),
                                                 passBuffer, sizeof(passBuffer))) {
            Serial.println("无线网络 开机连接终止：没有可用的已保存凭据");
            startGamepads();
            return;
        }
    }

    Serial.printf("无线网络 开机发起连接：SSID=%s 选择方式=%s 信号=%d 扫描耗时=%u毫秒\n",
                  ssidBuffer, matched ? "现场最强已保存网络" : "保存记录回退",
                  matched ? selectedRssi : 0,
                  (unsigned)(millis() - wifiAttemptStartedMs_.load()));
    WiFi.begin(ssidBuffer, passBuffer);
    Serial.printf("无线网络 开机连接已转入后台：SSID=%s 当前状态=%s\n",
                  ssidBuffer, wifiStatusName(WiFi.status()));
    startGamepads();
}

bool WirelessManager::beginSavedWifi(String& ssid, String& error) {
    error = "";
    if (WiFi.status() == WL_CONNECTED) {
        ssid = WiFi.SSID();
        wifiActive_.store(true);
        WiFi.setAutoReconnect(true);
        startGamepads();
        syncPendingCloudSavesIfNeeded();
        usageReportOnline();
        logConnectedWifi("开机时已经连接", 0);
        return true;
    }

    char savedSsid[64] = {0};
    char savedPass[96] = {0};
    bool hasLastSuccessful =
        wifiProvisioningGetLastSuccessfulCredentials(savedSsid, sizeof(savedSsid),
                                                     savedPass, sizeof(savedPass));
    if (!hasLastSuccessful &&
        !wifiProvisioningGetSavedCredentials(savedSsid, sizeof(savedSsid),
                                             savedPass, sizeof(savedPass))) {
        error = "未保存 WiFi，请先配网";
        return false;
    }

    ssid = savedSsid;
    bootWifiScanActive_.store(false);
    bootWifiScanFailureSeenMs_.store(0);
    WiFi.mode(WIFI_STA);
    // Stop the driver's persisted-profile auto-association until selection is
    // complete, otherwise it may briefly connect to an arbitrary old AP while
    // the asynchronous scan is still comparing signal strengths.
    WiFi.disconnect(false, false);
    WiFi.setAutoReconnect(true);
    const uint32_t startedMs = millis();
    wifiAttemptStartedMs_.store(startedMs);
    bootFastConnectStartedMs_.store(startedMs);
    bootFastConnectActive_.store(true);
    Serial.printf("无线网络 优先连接上次成功网络：SSID=%s 来源=%s "
                  "快速连接窗口=2500毫秒 当前状态=%s\n",
                  savedSsid, hasLastSuccessful ? "上次成功记录" : "首条保存记录兼容回退",
                  wifiStatusName(WiFi.status()));
    WiFi.begin(savedSsid, savedPass);
    wifiActive_.store(false);
    startGamepads();
    return true;
}

bool WirelessManager::connectSavedWifi(String& ssid, String& error) {
    cancelBootWifiScan("前台网络功能开始连接");
    if (WiFi.status() == WL_CONNECTED) {
        ssid = WiFi.SSID();
        error = "";
        wifiActive_.store(true);
        startGamepads();
        syncPendingCloudSavesIfNeeded();
        usageReportOnline();
        logConnectedWifi("前台任务复用现有连接", 0);
        return true;
    }

    // Association may scan/change channels. Reinitialize ESP-NOW afterwards so
    // it follows the AP's channel, then keep both services active together.
    stopGamepads();
    const uint32_t startedMs = millis();
    wifiAttemptStartedMs_.store(startedMs);
    Serial.printf("无线网络 前台连接开始：当前状态=%s 运行时间=%u毫秒 可用堆=%u\n",
                  wifiStatusName(WiFi.status()), (unsigned)startedMs,
                  (unsigned)ESP.getFreeHeap());
    bool networkReady = gameStoreConnectSavedWifi(ssid, error);
    bool connected = WiFi.status() == WL_CONNECTED;
    wifiActive_.store(connected);
    WiFi.setAutoReconnect(true);
    if (!connected && ssid.length() != 0) {
        // gameStoreConnectSavedWifi retains the station credentials on a
        // timeout. Keep reconnecting in the driver instead of waiting for the
        // user to open another network screen.
        WiFi.reconnect();
    }
    if (!startGamepads()) {
        Serial.println("ESP-NOW 手柄主机：WiFi 连接后重新启动失败");
    }
    if (networkReady) syncPendingCloudSavesIfNeeded();
    if (connected) {
        usageReportOnline();
        logConnectedWifi(networkReady ? "前台网络准备完成" : "前台仅完成 WiFi 连接",
                         millis() - startedMs);
    } else {
        Serial.printf("无线网络 前台连接失败：状态=%s 耗时=%u毫秒 原因=%s 可用堆=%u\n",
                      wifiStatusName(WiFi.status()), (unsigned)(millis() - startedMs),
                      error.c_str(), (unsigned)ESP.getFreeHeap());
    }
    return networkReady;
}

void WirelessManager::releaseNetworkTask() {
    bool connected = WiFi.status() == WL_CONNECTED;
    wifiActive_.store(connected);
    if (connected) {
        WiFi.setAutoReconnect(true);
        usageReportOnline();
    }
    startGamepads();
}

void WirelessManager::prepareForExternalWifiChange() {
    externalWifiChange_.store(true);
    const bool canceledFastConnect = bootFastConnectActive_.exchange(false);
    if (bootWifiScanActive_.exchange(false)) {
        // Do not delete or abort the scan here. The provisioning web page can
        // wait for and reuse this result instead of starting a conflicting scan.
        Serial.println("无线网络 开机扫描移交给配网页面复用");
    } else if (canceledFastConnect) {
        // Stop the pending station association before the provisioning portal
        // starts AP+STA scanning with credentials chosen by the user.
        WiFi.disconnect(false, false);
        Serial.println("无线网络 已取消开机快速连接，准备进入配网页面");
    }
    stopGamepads();
}

void WirelessManager::reconcileAfterExternalWifiChange() {
    wifiActive_.store(WiFi.status() == WL_CONNECTED);
    if (wifiActive_.load()) {
        WiFi.setAutoReconnect(true);
        usageReportOnline();
    }
    stopGamepads();
    startGamepads();
    externalWifiChange_.store(false);
}

void WirelessManager::updateBootConnectivity() {
    if (externalWifiChange_.load()) return;
    // During the boot animation LittleFS and the cloud-save queue may still be
    // mounting on the worker task. Only advance scan/association here; the
    // normal update() performs cloud work after the boot completion barrier.
    serviceBootFastConnect();
    serviceBootWifiScan();
}

void WirelessManager::update() {
    if (externalWifiChange_.load()) return;
    serviceBootFastConnect();
    serviceBootWifiScan();
    remoteConfigUpdate();
    cloudSaveUpdate();
    bool connected = WiFi.status() == WL_CONNECTED;
    if (connected) usageReportOnline();
    bool previous = wifiActive_.exchange(connected);
    if (connected == previous) return;
    const uint32_t attemptStartedMs = wifiAttemptStartedMs_.load();
    const uint32_t elapsedMs = attemptStartedMs ? millis() - attemptStartedMs : 0;
    Serial.printf("无线网络 状态变化：之前=%s 当前=%s 原始状态码=%d "
                  "连接耗时=%u毫秒 运行时间=%u毫秒\n",
                  previous ? "已连接" : "离线", connected ? "已连接" : "离线",
                  (int)WiFi.status(), (unsigned)elapsedMs, (unsigned)millis());
    if (connected) {
        logConnectedWifi("连接成功", elapsedMs);
        usageReportOnline();
        wifiProvisioningRememberSuccessfulSsid(WiFi.SSID().c_str());
        // Auto-reconnect may select a different AP/channel. Rebind ESP-NOW to
        // the newly associated channel; controllers will discover it.
        stopGamepads();
        startGamepads();
        syncPendingCloudSavesIfNeeded();
        wifiAttemptStartedMs_.store(0);
    } else {
        Serial.printf("无线网络 离线详情：状态=%s 自动重连=已启用 可用堆=%u\n",
                      wifiStatusName(WiFi.status()), (unsigned)ESP.getFreeHeap());
    }
}

void WirelessManager::shutdown() {
    externalWifiChange_.store(true);
    cancelBootWifiScan("设备正在关机");
    stopGamepads();
    gameStoreDisconnectWifi();
    wifiActive_.store(false);
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_OFF);
}
