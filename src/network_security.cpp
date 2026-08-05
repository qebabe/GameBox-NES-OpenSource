#include "network_security.h"

#include <time.h>
#include <stdlib.h>
#include <WiFi.h>

// ISRG Root X1, valid until 2035-06-04. The GameBox services currently use
// a Let's Encrypt chain anchored by this root.
static const char kIsrgRootX1[] PROGMEM = R"CERT(-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

static constexpr const char* kGameBoxTimezone = "CST-8";
static constexpr const char* kPrimaryNtpServer = "pool.ntp.org";
static constexpr const char* kSecondaryNtpServer = "time.cloudflare.com";

void configureGameBoxTls(WiFiClientSecure& client) {
    client.setCACert(kIsrgRootX1);
}

void configureHashedDownloadTls(WiFiClientSecure& client) {
    // Aliyun OSS does not use the service's ISRG Root X1 chain. Integrity and
    // authenticity are still enforced by the SHA-256 manifest received over
    // the CA-verified GameBox API connection.
    client.setInsecure();
}

void applyGameBoxTimezone() {
    setenv("TZ", kGameBoxTimezone, 1);
    tzset();
}

void startGameBoxNetworkTimeSync() {
    // configTime(0, 0, ...) resets TZ to UTC in Arduino-ESP32. configTzTime
    // starts SNTP and applies the POSIX timezone without that intermediate
    // overwrite. CST-8 means UTC+8 because POSIX offset signs are reversed.
    configTzTime(kGameBoxTimezone, kPrimaryNtpServer, kSecondaryNtpServer);
    Serial.printf("网络校时 已启动SNTP：时区=%s 主服务器=%s 备用服务器=%s\n",
                  getenv("TZ") ? getenv("TZ") : "未设置",
                  kPrimaryNtpServer, kSecondaryNtpServer);
}

bool ensureGameBoxNetworkTime(String& error, uint32_t timeoutMs) {
    constexpr time_t kSaneEpoch = 1704067200;  // 2024-01-01 UTC
    time_t initialEpoch = time(nullptr);
    if (initialEpoch >= kSaneEpoch) {
        applyGameBoxTimezone();
        Serial.printf("网络校时 时间已经有效：时间戳=%lld WiFi状态码=%d "
                      "运行时间=%u毫秒 时区=%s\n",
                      (long long)initialEpoch, (int)WiFi.status(), (unsigned)millis(),
                      getenv("TZ") ? getenv("TZ") : "未设置");
        return true;
    }

    String ip = WiFi.localIP().toString();
    String dns = WiFi.dnsIP().toString();
    Serial.printf("网络校时 开始等待：当前时间戳=%lld 超时=%u毫秒 WiFi状态码=%d "
                  "SSID=%s 信号=%d IP=%s DNS=%s 可用堆=%u\n",
                  (long long)initialEpoch, (unsigned)timeoutMs, (int)WiFi.status(),
                  WiFi.SSID().c_str(), WiFi.RSSI(), ip.c_str(), dns.c_str(),
                  (unsigned)ESP.getFreeHeap());
    startGameBoxNetworkTimeSync();
    uint32_t started = millis();
    uint32_t lastProgressMs = 0;
    while (time(nullptr) < kSaneEpoch) {
        const uint32_t elapsedMs = millis() - started;
        if (elapsedMs - lastProgressMs >= 2000) {
            lastProgressMs = elapsedMs;
            Serial.printf("网络校时 等待中：已等待=%u毫秒 当前时间戳=%lld WiFi状态码=%d "
                          "信号=%d 可用堆=%u\n",
                          (unsigned)elapsedMs, (long long)time(nullptr),
                          (int)WiFi.status(), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
        }
        if (elapsedMs > timeoutMs) {
            error = "网络时间同步失败";
            ip = WiFi.localIP().toString();
            dns = WiFi.dnsIP().toString();
            Serial.printf("网络校时 超时：已等待=%u毫秒 当前时间戳=%lld WiFi状态码=%d "
                          "SSID=%s 信号=%d IP=%s DNS=%s 可用堆=%u\n",
                          (unsigned)elapsedMs, (long long)time(nullptr),
                          (int)WiFi.status(), WiFi.SSID().c_str(), WiFi.RSSI(),
                          ip.c_str(), dns.c_str(), (unsigned)ESP.getFreeHeap());
            return false;
        }
        delay(100);
    }
    applyGameBoxTimezone();
    Serial.printf("网络校时 成功：耗时=%u毫秒 时间戳=%lld WiFi状态码=%d 时区=%s\n",
                  (unsigned)(millis() - started), (long long)time(nullptr),
                  (int)WiFi.status(), getenv("TZ") ? getenv("TZ") : "未设置");
    return true;
}
