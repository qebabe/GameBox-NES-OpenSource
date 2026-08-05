#include "settings_web_server.h"

#include <WebServer.h>
#include <WiFi.h>

#include "device_identity.h"
#include "rom_web_server.h"
#include "wallpaper_web_server.h"

namespace {
WebServer server(80);
SettingsWebServerStatus status;
SettingsWebJsonProvider jsonProvider = nullptr;
SettingsWebApplyHandler applyHandler = nullptr;
bool routesRegistered = false;
bool startedFallbackAp = false;

const char kSettingsHtml[] PROGMEM = R"SETTINGS_HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GameBox 设置</title><style>
*{box-sizing:border-box}body{margin:0;background:#07111f;color:#dcecff;font-family:system-ui,-apple-system,sans-serif}.wrap{max-width:760px;margin:auto;padding:18px}h1{color:#48d7ff;font-size:24px}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(230px,1fr));gap:14px}.card{display:block;background:#102033;border:1px solid #294760;border-radius:14px;padding:16px;color:inherit;text-decoration:none}.link:hover{border-color:#48d7ff}.link h2{color:#48d7ff}.setting{display:grid;grid-template-columns:1fr 130px;align-items:center;gap:12px;padding:10px 0;border-top:1px solid #294052}select,input{width:100%;font:inherit;background:#07111f;color:#fff;border:1px solid #39799d;border-radius:8px;padding:8px}.muted{color:#8fa9bd;font-size:13px}#status{min-height:24px;color:#62efb1}@media(max-width:480px){.setting{grid-template-columns:1fr}.wrap{padding:10px}}
</style></head><body><main class="wrap"><h1>GameBox 设置中心</h1><div class="grid">
<section class="card"><h2>设备设置</h2><div class="setting"><label>音量</label><input id="volume" type="range" min="0" max="100" step="10"></div><div class="setting"><label id="displayLabel">显示模式</label><select id="display"><option id="displayFull" value="1">全屏</option><option id="displayNative" value="0">原始比例</option></select></div><div class="setting"><label>屏幕旋转</label><select id="rotation"><option value="0">默认方向</option><option value="1">旋转 180°</option></select></div><div class="setting" id="invertRow"><label>屏幕反色</label><select id="invert"><option value="1">开启</option><option value="0">关闭</option></select></div><div class="setting"><label>开机主页</label><select id="boot"><option value="1">壁纸时钟</option><option value="0">游戏列表</option></select></div><div class="setting"><label>倒带时长</label><select id="rewind"><option value="0">关闭</option><option value="5">5 秒</option><option value="10">10 秒</option><option value="20">20 秒</option></select></div><div class="setting"><label>自动关机</label><select id="autooff"><option value="0">关闭</option><option value="5">5 分钟</option><option value="10">10 分钟</option><option value="20">20 分钟</option></select></div><div id="status"></div></section>
<a class="card link" href="/wallpaper"><h2>壁纸管理</h2><p>上传、裁切、选择和删除 SD 卡壁纸。</p></a><a class="card link" href="/roms"><h2>ROM 管理</h2><p>批量上传、浏览和删除 SD 卡中的 NES ROM。</p></a></div><p class="muted">此网页仅在掌机停留于系统设置期间可用。</p></main><script>
const ids=['volume','display','rotation','invert','boot','rewind','autooff'],statusEl=document.getElementById('status');let loading=true;
async function load(){try{const r=await fetch('/api/device-settings');const j=await r.json();if(!r.ok||!j.ok)throw Error(j.message||'读取失败');ids.forEach(k=>document.getElementById(k).value=String(j[k]));document.getElementById('invertRow').style.display=j.st7789?'grid':'none';if(j.original_s3){document.getElementById('displayLabel').textContent='游戏分辨率';document.getElementById('displayFull').textContent='320×240 全屏';document.getElementById('displayNative').textContent='256×240 原生'}loading=false}catch(e){statusEl.textContent=e.message}}
async function save(key,value){if(loading)return;try{statusEl.textContent='正在保存...';const r=await fetch('/api/device-settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'key='+encodeURIComponent(key)+'&value='+encodeURIComponent(value)});const j=await r.json();if(!r.ok||!j.ok)throw Error(j.message||'保存失败');statusEl.textContent=j.message}catch(e){statusEl.textContent=e.message}}
ids.filter(k=>k!=='volume').forEach(k=>document.getElementById(k).onchange=e=>save(k,e.target.value));let volumeTimer;document.getElementById('volume').oninput=e=>{clearTimeout(volumeTimer);volumeTimer=setTimeout(()=>save('volume',e.target.value),180)};load();
</script></body></html>)SETTINGS_HTML";

String jsonEscape(const String& input) {
    String output;
    output.reserve(input.length() + 8);
    for (size_t i = 0; i < input.length(); i++) {
        const char c = input[i];
        if (c == '"' || c == '\\') {
            output += '\\';
            output += c;
        } else if ((uint8_t)c >= 0x20) {
            output += c;
        }
    }
    return output;
}

void sendResult(bool ok, const String& message, int code = 200) {
    const String body = String("{\"ok\":") + (ok ? "true" : "false") +
                        ",\"message\":\"" + jsonEscape(message) + "\"}";
    server.send(code, "application/json; charset=utf-8", body);
}

void registerRoutes() {
    if (routesRegistered) return;
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html; charset=utf-8", kSettingsHtml);
    });
    server.on("/api/device-settings", HTTP_GET, []() {
        if (!jsonProvider) {
            sendResult(false, "设置接口未就绪", 503);
            return;
        }
        server.send(200, "application/json; charset=utf-8", jsonProvider());
    });
    server.on("/api/device-settings", HTTP_POST, []() {
        if (!applyHandler) {
            sendResult(false, "设置接口未就绪", 503);
            return;
        }
        String error;
        if (!applyHandler(server.arg("key"), server.arg("value"), error)) {
            sendResult(false, error, 400);
            return;
        }
        sendResult(true, "设置已保存");
    });
    wallpaperWebServerRegisterRoutes(server);
    romWebServerRegisterRoutes(server);
    server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
    routesRegistered = true;
}
}  // namespace

bool settingsWebServerBegin(bool sdCardAvailable,
                            SettingsWebJsonProvider provider,
                            SettingsWebApplyHandler handler) {
    settingsWebServerStop();
    jsonProvider = provider;
    applyHandler = handler;
    status = SettingsWebServerStatus{};
    wallpaperWebServerPrepare(sdCardAvailable);
    romWebServerPrepare(sdCardAvailable);

    if (WiFi.status() == WL_CONNECTED) {
        status.url = String("http://") + WiFi.localIP().toString() + "/";
    } else {
        String id = gameBoxDeviceId();
        String suffix = id.length() > 6 ? id.substring(id.length() - 6) : id;
        status.ssid = String("GameBox-Settings-") + suffix;
        WiFi.mode(WIFI_AP_STA);
        if (!WiFi.softAP(status.ssid.c_str())) {
            status.message = "无法启动设置热点";
            return false;
        }
        startedFallbackAp = true;
        status.accessPoint = true;
        status.url = String("http://") + WiFi.softAPIP().toString() + "/";
    }

    registerRoutes();
    server.begin();
    status.active = true;
    status.message = "设置网页已启动";
    Serial.printf("设置网页已启动：%s%s%s\n", status.url.c_str(),
                  status.accessPoint ? " 热点=" : "",
                  status.accessPoint ? status.ssid.c_str() : "");
    return true;
}

void settingsWebServerLoop() {
    if (status.active) server.handleClient();
}

void settingsWebServerStop() {
    if (status.active) server.stop();
    wallpaperWebServerCleanup();
    romWebServerCleanup();
    if (startedFallbackAp) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
    }
    startedFallbackAp = false;
    status.active = false;
}

SettingsWebServerStatus settingsWebServerStatus() {
    return status;
}
