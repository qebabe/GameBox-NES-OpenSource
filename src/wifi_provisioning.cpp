#include "wifi_provisioning.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "device_identity.h"

static constexpr uint16_t kDnsPort = 53;
static constexpr uint16_t kHttpPort = 80;
static constexpr uint32_t kConnectTimeoutMs = 20000;
static constexpr const char* kPrefsNamespace = "wifi";
static constexpr const char* kPrefsSsid = "ssid";
static constexpr const char* kPrefsPass = "pass";
static constexpr const char* kPrefsCount = "count";
static constexpr const char* kPrefsLastSuccessful = "last_ok";
static constexpr uint8_t kMaxSavedNetworks = 8;

static DNSServer dnsServer;
static WebServer webServer(kHttpPort);
static WifiProvisioningStatus status;
static bool portalActive = false;
static bool portalFinished = false;
static uint32_t connectStartMs = 0;
static String pendingPassword;
static String portalSsid;
static bool routesRegistered = false;

static String htmlEscape(const String& text) {
    String out;
    out.reserve(text.length());
    for (size_t i = 0; i < text.length(); i++) {
        char c = text[i];
        switch (c) {
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            case '"': out += F("&quot;"); break;
            case '\'': out += F("&#39;"); break;
            default: out += c; break;
        }
    }
    return out;
}

static String jsonEscape(const String& text) {
    String out;
    out.reserve(text.length() + 8);
    for (size_t i = 0; i < text.length(); i++) {
        uint8_t c = (uint8_t)text[i];
        switch (c) {
            case '"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\b': out += F("\\b"); break;
            case '\f': out += F("\\f"); break;
            case '\n': out += F("\\n"); break;
            case '\r': out += F("\\r"); break;
            case '\t': out += F("\\t"); break;
            default:
                if (c < 0x20) {
                    char escaped[7];
                    snprintf(escaped, sizeof(escaped), "\\u%04x", c);
                    out += escaped;
                } else {
                    out += (char)c;
                }
                break;
        }
    }
    return out;
}

static String makePortalSsid() {
    String deviceId = gameBoxDeviceId();
    String suffix = deviceId.length() >= 6 ? deviceId.substring(deviceId.length() - 6)
                                           : String("000000");
    return String("GameBox-") + suffix;
}

static void makeCredentialKey(const char* prefix, int index, char* out, size_t outSize) {
    snprintf(out, outSize, "%s%d", prefix, index);
}

static int savedCredentialCount(Preferences& prefs) {
    int count = (int)prefs.getUChar(kPrefsCount, 0);
    if (count > kMaxSavedNetworks) {
        count = kMaxSavedNetworks;
    }

    String legacySsid = prefs.getString(kPrefsSsid, "");
    if (count == 0 && !legacySsid.isEmpty()) {
        String legacyPass = prefs.getString(kPrefsPass, "");
        prefs.putString("ssid0", legacySsid);
        prefs.putString("pass0", legacyPass);
        prefs.putUChar(kPrefsCount, 1);
        return 1;
    }
    return count;
}

static bool loadSavedCredential(Preferences& prefs, int index, String& ssid, String& password) {
    if (index < 0 || index >= kMaxSavedNetworks) {
        return false;
    }
    char ssidKey[12];
    char passKey[12];
    makeCredentialKey("ssid", index, ssidKey, sizeof(ssidKey));
    makeCredentialKey("pass", index, passKey, sizeof(passKey));
    ssid = prefs.getString(ssidKey, "");
    password = prefs.getString(passKey, "");
    return !ssid.isEmpty();
}

static bool saveCredential(const String& ssid, const String& password) {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }

    String ssids[kMaxSavedNetworks];
    String passwords[kMaxSavedNetworks];
    int count = savedCredentialCount(prefs);
    int existing = -1;
    for (int i = 0; i < count; i++) {
        loadSavedCredential(prefs, i, ssids[i], passwords[i]);
        if (ssids[i] == ssid) {
            existing = i;
        }
    }

    if (existing >= 0) {
        ssids[existing] = ssid;
        passwords[existing] = password;
        for (int i = existing; i > 0; i--) {
            ssids[i] = ssids[i - 1];
            passwords[i] = passwords[i - 1];
        }
        ssids[0] = ssid;
        passwords[0] = password;
    } else {
        if (count < kMaxSavedNetworks) {
            count++;
        }
        for (int i = count - 1; i > 0; i--) {
            ssids[i] = ssids[i - 1];
            passwords[i] = passwords[i - 1];
        }
        ssids[0] = ssid;
        passwords[0] = password;
    }

    for (int i = 0; i < count; i++) {
        char ssidKey[12];
        char passKey[12];
        makeCredentialKey("ssid", i, ssidKey, sizeof(ssidKey));
        makeCredentialKey("pass", i, passKey, sizeof(passKey));
        prefs.putString(ssidKey, ssids[i]);
        prefs.putString(passKey, passwords[i]);
    }
    prefs.putUChar(kPrefsCount, (uint8_t)count);
    prefs.putString(kPrefsSsid, ssid);
    prefs.putString(kPrefsPass, password);
    prefs.putString(kPrefsLastSuccessful, ssid);
    prefs.end();
    return true;
}

static int portalScanNetworks() {
    int state = WiFi.scanComplete();
    if (state == WIFI_SCAN_RUNNING) {
        Serial.println("WiFi 配网页面：检测到已有扫描正在运行，等待并复用结果");
        const uint32_t startedMs = millis();
        while (state == WIFI_SCAN_RUNNING &&
               (uint32_t)(millis() - startedMs) < 8000) {
            delay(50);
            state = WiFi.scanComplete();
        }
        Serial.printf("WiFi 配网页面：复用扫描结束，状态=%d 等待=%u毫秒\n",
                      state, (unsigned)(millis() - startedMs));
    }

    if (state > 0) {
        Serial.printf("WiFi 配网页面：使用已有扫描结果，发现=%d\n", state);
        return state;
    }

    if (state == 0) {
        // Switching from STA to AP+STA can complete the handed-off boot scan
        // with an empty result. Run one fresh portal scan before declaring that
        // no access points exist.
        Serial.println("WiFi 配网页面：已有扫描结果为空，重新执行一次 AP+STA 扫描");
    }

    WiFi.scanDelete();
    const uint32_t startedMs = millis();
    Serial.printf("WiFi 配网页面：开始扫描，初始状态=%d\n", state);
    const int count = WiFi.scanNetworks(false, true, false, 200);
    Serial.printf("WiFi 配网页面：扫描结束，结果=%d 耗时=%u毫秒 WiFi状态码=%d\n",
                  count, (unsigned)(millis() - startedMs), (int)WiFi.status());
    return count;
}

static void sendPortalPage(const String& selected = "") {
    int count = portalScanNetworks();

    String html;
    html.reserve(4096);
    html += F("<!doctype html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>GameBox WiFi</title><style>");
    html += F("body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#101418;color:#eef;margin:0;padding:18px}");
    html += F("h1{font-size:24px;margin:0 0 12px}.card{background:#1b232c;border:1px solid #3a4b5c;border-radius:10px;padding:14px;margin:12px 0}");
    html += F("select,input,button{box-sizing:border-box;width:100%;font-size:18px;padding:12px;margin:8px 0;border-radius:8px;border:1px solid #506070}");
    html += F("button{background:#00d8ff;color:#041015;border:0;font-weight:700}.hint{color:#9fb0bd;font-size:14px;line-height:1.5}");
    html += F("</style></head><body><h1>GameBox WiFi 配网</h1>");
    html += F("<div class='card'><form method='post' action='/save'>");
    html += F("<label>选择 WiFi</label><select name='ssid'>");

    if (count == WIFI_SCAN_RUNNING) {
        html += F("<option value=''>WiFi 仍在扫描，请稍后重新扫描</option>");
    } else if (count == WIFI_SCAN_FAILED) {
        html += F("<option value=''>WiFi 扫描失败，请点击重新扫描</option>");
    } else if (count == 0) {
        html += F("<option value=''>没有扫描到 WiFi</option>");
    } else {
        for (int i = 0; i < count; i++) {
            String ssid = WiFi.SSID(i);
            html += F("<option value='");
            html += htmlEscape(ssid);
            html += "'";
            if (ssid == selected) {
                html += F(" selected");
            }
            html += ">";
            html += htmlEscape(ssid);
            html += " (";
            html += WiFi.RSSI(i);
            html += F(" dBm)");
            html += F("</option>");
        }
    }

    html += F("</select><label>密码</label>");
    html += F("<input name='password' type='password' autocomplete='current-password' placeholder='输入 WiFi 密码'>");
    html += F("<button type='submit'>保存并连接</button></form>");
    html += F("<form method='get' action='/'><button type='submit'>重新扫描</button></form></div>");
    html += F("<p class='hint'>提交后请回到掌机屏幕查看连接状态。连接成功后可以断开这个临时热点。</p>");
    html += F("</body></html>");

    webServer.send(200, "text/html; charset=utf-8", html);
    // A running scan has no result buffer yet. Deleting here would not stop the
    // driver scan and would reproduce the stale "scan running" state on reload.
    if (count != WIFI_SCAN_RUNNING) WiFi.scanDelete();
}

static void handleRoot() {
    sendPortalPage();
}

static void handleSave() {
    String ssid = webServer.arg("ssid");
    String password = webServer.arg("password");
    ssid.trim();

    if (ssid.isEmpty()) {
        webServer.send(400, "text/plain; charset=utf-8", "SSID 不能为空");
        return;
    }

    status.state = WifiProvisioningState::Connecting;
    status.selectedSsid = ssid;
    status.message = "正在连接 WiFi...";
    pendingPassword = password;
    connectStartMs = millis();

    WiFi.begin(ssid.c_str(), password.c_str());

    String html;
    html += F("<!doctype html><html><head><meta charset='utf-8'>");
    html += F("<meta name='viewport' content='width=device-width,initial-scale=1'>");
    html += F("<title>正在连接</title></head><body style='font-family:sans-serif;padding:20px'>");
    html += F("<h2>已提交 WiFi 信息</h2><p>请回到掌机屏幕查看连接状态。</p>");
    html += F("<p><a href='/status'>查看状态</a></p></body></html>");
    webServer.send(200, "text/html; charset=utf-8", html);
}

static void handleStatus() {
    String body = "{";
    body += "\"state\":\"";
    switch (status.state) {
        case WifiProvisioningState::PortalRunning: body += "portal"; break;
        case WifiProvisioningState::Connecting: body += "connecting"; break;
        case WifiProvisioningState::Connected: body += "connected"; break;
        case WifiProvisioningState::Failed: body += "failed"; break;
        case WifiProvisioningState::Idle:
        default: body += "idle"; break;
    }
    body += "\",\"ssid\":\"";
    body += jsonEscape(status.selectedSsid);
    body += "\",\"ip\":\"";
    body += jsonEscape(status.ip);
    body += "\",\"message\":\"";
    body += jsonEscape(status.message);
    body += "\"}";
    webServer.send(200, "application/json; charset=utf-8", body);
}

static void handleNotFound() {
    webServer.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    webServer.send(302, "text/plain", "");
}

void wifiProvisioningBegin() {
    wifiProvisioningStop();

    portalActive = true;
    portalFinished = false;
    pendingPassword = "";
    portalSsid = makePortalSsid();
    connectStartMs = 0;

    WiFi.mode(WIFI_AP_STA);
    if (!WiFi.softAP(portalSsid.c_str())) {
        status = WifiProvisioningStatus{};
        status.state = WifiProvisioningState::Failed;
        status.message = "无法启动配网热点";
        portalActive = false;
        portalFinished = true;
        return;
    }

    IPAddress apIp = WiFi.softAPIP();
    dnsServer.start(kDnsPort, "*", apIp);

    if (!routesRegistered) {
        webServer.on("/", HTTP_GET, handleRoot);
        webServer.on("/save", HTTP_POST, handleSave);
        webServer.on("/status", HTTP_GET, handleStatus);
        webServer.onNotFound(handleNotFound);
        routesRegistered = true;
    }
    webServer.begin();

    status = WifiProvisioningStatus{};
    status.state = WifiProvisioningState::PortalRunning;
    status.apSsid = portalSsid;
    status.ip = apIp.toString();
    status.message = "等待手机提交 WiFi 信息";

    Serial.printf("WiFi 配网热点已启动：SSID=%s IP=%s\n",
                  status.apSsid.c_str(), status.ip.c_str());
}

void wifiProvisioningLoop() {
    if (!portalActive) {
        return;
    }

    dnsServer.processNextRequest();
    webServer.handleClient();

    if (status.state == WifiProvisioningState::Connecting) {
        wl_status_t wifiStatus = WiFi.status();
        if (wifiStatus == WL_CONNECTED) {
            status.state = WifiProvisioningState::Connected;
            status.ip = WiFi.localIP().toString();
            bool saved = saveCredential(status.selectedSsid, pendingPassword);
            status.message = saved ? "WiFi 连接成功并已保存" : "已连接，但保存配置失败";
            pendingPassword = "";
            portalFinished = true;
            Serial.printf("WiFi 配网连接成功：SSID=%s IP=%s\n",
                          status.selectedSsid.c_str(), status.ip.c_str());
        } else if ((uint32_t)(millis() - connectStartMs) > kConnectTimeoutMs) {
            WiFi.disconnect(false, false);
            status.state = WifiProvisioningState::Failed;
            status.message = "连接失败，请检查密码";
            pendingPassword = "";
            portalFinished = true;
            Serial.printf("WiFi 配网连接失败：SSID=%s\n", status.selectedSsid.c_str());
        }
    }
}

void wifiProvisioningStop() {
    if (!portalActive) {
        return;
    }

    webServer.stop();
    dnsServer.stop();
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) {
        esp_wifi_scan_stop();
        delay(30);
    }
    WiFi.scanDelete();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    portalActive = false;
    portalFinished = false;
    pendingPassword = "";
    portalSsid = "";
    status.state = WifiProvisioningState::Idle;
}

bool wifiProvisioningActive() {
    return portalActive;
}

bool wifiProvisioningFinished() {
    return portalFinished;
}

bool wifiProvisioningConnected() {
    return status.state == WifiProvisioningState::Connected;
}

WifiProvisioningStatus wifiProvisioningStatus() {
    return status;
}

bool wifiProvisioningHasSavedConfig() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }
    int count = savedCredentialCount(prefs);
    prefs.end();
    return count > 0;
}

int wifiProvisioningSavedNetworkCount() {
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return 0;
    }
    int count = savedCredentialCount(prefs);
    prefs.end();
    return count;
}

bool wifiProvisioningGetSavedSsid(char* out, size_t outSize) {
    if (!out || outSize == 0) {
        return false;
    }
    out[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }
    savedCredentialCount(prefs);
    String ssid;
    String pass;
    bool ok = loadSavedCredential(prefs, 0, ssid, pass);
    prefs.end();

    if (!ok) {
        return false;
    }
    snprintf(out, outSize, "%s", ssid.c_str());
    return true;
}

bool wifiProvisioningGetSavedCredentials(char* ssidOut, size_t ssidSize,
                                         char* passOut, size_t passSize) {
    if (!ssidOut || ssidSize == 0 || !passOut || passSize == 0) {
        return false;
    }
    ssidOut[0] = '\0';
    passOut[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }
    savedCredentialCount(prefs);
    String ssid;
    String pass;
    bool ok = loadSavedCredential(prefs, 0, ssid, pass);
    prefs.end();

    if (!ok) {
        return false;
    }
    snprintf(ssidOut, ssidSize, "%s", ssid.c_str());
    snprintf(passOut, passSize, "%s", pass.c_str());
    return true;
}

bool wifiProvisioningGetLastSuccessfulCredentials(char* ssidOut, size_t ssidSize,
                                                  char* passOut, size_t passSize) {
    if (!ssidOut || ssidSize == 0 || !passOut || passSize == 0) return false;
    ssidOut[0] = '\0';
    passOut[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return false;
    const int count = savedCredentialCount(prefs);
    const String lastSuccessful = prefs.getString(kPrefsLastSuccessful, "");
    bool found = false;
    String password;
    for (int index = 0; index < count; index++) {
        String savedSsid;
        String savedPass;
        if (loadSavedCredential(prefs, index, savedSsid, savedPass) &&
            !lastSuccessful.isEmpty() && savedSsid == lastSuccessful) {
            password = savedPass;
            found = true;
            break;
        }
    }
    prefs.end();
    if (!found) return false;

    snprintf(ssidOut, ssidSize, "%s", lastSuccessful.c_str());
    snprintf(passOut, passSize, "%s", password.c_str());
    return true;
}

bool wifiProvisioningGetCredentialsForSsid(const char* ssid,
                                           char* passOut, size_t passSize) {
    if (!ssid || !ssid[0] || !passOut || passSize == 0) return false;
    passOut[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return false;
    const int count = savedCredentialCount(prefs);
    bool found = false;
    String password;
    for (int index = 0; index < count; index++) {
        String savedSsid;
        String savedPass;
        if (loadSavedCredential(prefs, index, savedSsid, savedPass) &&
            savedSsid == ssid) {
            password = savedPass;
            found = true;
            break;
        }
    }
    prefs.end();
    if (!found) return false;

    snprintf(passOut, passSize, "%s", password.c_str());
    return true;
}

void wifiProvisioningRememberSuccessfulSsid(const char* ssid) {
    if (!ssid || !ssid[0]) return;
    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) return;
    const String previous = prefs.getString(kPrefsLastSuccessful, "");
    if (previous == ssid) {
        prefs.end();
        return;
    }
    prefs.putString(kPrefsLastSuccessful, ssid);
    prefs.end();
    Serial.printf("无线网络 已记录上次成功网络：SSID=%s\n", ssid);
}

bool wifiProvisioningGetBestSavedCredentialsFromScan(int scanCount,
                                                     char* ssidOut, size_t ssidSize,
                                                     char* passOut, size_t passSize,
                                                     int* rssiOut) {
    if (!ssidOut || ssidSize == 0 || !passOut || passSize == 0) {
        return false;
    }
    ssidOut[0] = '\0';
    passOut[0] = '\0';

    Preferences prefs;
    if (!prefs.begin(kPrefsNamespace, false)) {
        return false;
    }
    int savedCount = savedCredentialCount(prefs);
    if (savedCount <= 0) {
        prefs.end();
        return false;
    }

    Serial.printf("无线网络 扫描结果：发现=%d 已保存=%d\n", scanCount, savedCount);
    int bestSavedIndex = -1;
    int bestRssi = -1000;
    int matchedCount = 0;
    for (int scanIndex = 0; scanIndex < scanCount; scanIndex++) {
        String scannedSsid = WiFi.SSID(scanIndex);
        int rssi = WiFi.RSSI(scanIndex);
        for (int savedIndex = 0; savedIndex < savedCount; savedIndex++) {
            String savedSsid;
            String savedPass;
            if (loadSavedCredential(prefs, savedIndex, savedSsid, savedPass) &&
                savedSsid == scannedSsid) {
                matchedCount++;
                Serial.printf("无线网络 已保存候选：SSID=%s 信号=%d 信道=%d 保存序号=%d\n",
                              scannedSsid.c_str(), rssi, WiFi.channel(scanIndex),
                              savedIndex + 1);
                if (rssi > bestRssi) {
                    bestRssi = rssi;
                    bestSavedIndex = savedIndex;
                }
            }
        }
    }

    if (bestSavedIndex < 0) {
        Serial.printf("无线网络 扫描匹配完成：候选=%d，没有已保存网络处于可见状态\n",
                      matchedCount);
        prefs.end();
        return false;
    }

    String ssid;
    String pass;
    bool ok = loadSavedCredential(prefs, bestSavedIndex, ssid, pass);
    prefs.end();
    if (!ok) {
        return false;
    }

    snprintf(ssidOut, ssidSize, "%s", ssid.c_str());
    snprintf(passOut, passSize, "%s", pass.c_str());
    if (rssiOut) *rssiOut = bestRssi;
    Serial.printf("无线网络 选择最强网络：SSID=%s 信号=%d 保存序号=%d\n",
                  ssid.c_str(), bestRssi, bestSavedIndex + 1);
    return true;
}

bool wifiProvisioningGetMatchingSavedCredentials(char* ssidOut, size_t ssidSize,
                                                char* passOut, size_t passSize) {
    int scanCount = WiFi.scanNetworks(false, true, false, 200);
    bool matched = scanCount >= 0 &&
                   wifiProvisioningGetBestSavedCredentialsFromScan(
                       scanCount, ssidOut, ssidSize, passOut, passSize, nullptr);
    WiFi.scanDelete();
    return matched;
}
