#include "wallpaper_web_server.h"

#include <algorithm>
#include <WebServer.h>
#include <WiFi.h>

#include "device_identity.h"
#include "storage.h"
#include "wallpaper_manager.h"

namespace {
constexpr size_t kMaxUploadBytes = 512 * 1024;
constexpr const char* kUploadTempPath = "/wallpapers/.upload.tmp";
WebServer* activeServer = nullptr;
bool routesRegistered = false;
File uploadFile;
String uploadName;
String uploadError;
size_t uploadBytes = 0;

void removeUploadTempIfPresent() {
    if (DIJI_SD.exists(kUploadTempPath)) DIJI_SD.remove(kUploadTempPath);
}

const char kWallpaperHtml[] PROGMEM = R"WALLPAPER_HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GameBox 壁纸</title><style>
*{box-sizing:border-box}body{margin:0;background:#07111f;color:#dcecff;font-family:system-ui,-apple-system,sans-serif}.wrap{max-width:760px;margin:auto;padding:18px}.card{background:#102033;border:1px solid #294760;border-radius:14px;padding:16px;margin-bottom:14px}h1{font-size:22px;margin:0 0 12px;color:#48d7ff}h2{font-size:16px;margin:0 0 12px}button,input{font:inherit}button{border:0;border-radius:9px;background:#087fc2;color:#fff;padding:9px 12px;margin:3px;cursor:pointer}.danger{background:#a43b4c}.muted{color:#8fa9bd;font-size:13px}.stage{touch-action:none;width:100%;max-width:640px;aspect-ratio:4/3;background:#000;border-radius:10px;display:block;margin:12px auto}input[type=range]{width:100%}#list{display:grid;grid-template-columns:repeat(auto-fill,minmax(145px,1fr));gap:12px}.row{min-width:0;border:1px solid #294760;border-radius:11px;padding:8px;background:#0b1828}.row img,.builtin{display:block;width:100%;aspect-ratio:4/3;object-fit:cover;border-radius:7px;background:#000;margin-bottom:8px}.builtin{background:linear-gradient(145deg,#153d68,#8b5b41);position:relative}.builtin:after{content:'内置壁纸';position:absolute;inset:auto 8px 8px;color:#fff}.row .info{min-width:0;margin-bottom:6px}.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.selected{color:#62efb1;font-weight:700;box-shadow:0 0 0 2px #36d598 inset}#status{min-height:22px;color:#62efb1}
</style></head><body><main class="wrap"><p><a href="/" style="color:#48d7ff">← 返回设置中心</a></p><h1>GameBox 壁纸管理</h1>
<section class="card"><h2>上传并处理为 320 × 240</h2><input id="file" type="file" accept="image/*"><canvas id="canvas" class="stage" width="320" height="240"></canvas><label>缩放 <input id="zoom" type="range" min="100" max="300" value="100"></label><div><input id="name" maxlength="40" placeholder="壁纸名称"><button id="upload">上传并设为当前壁纸</button></div><p class="muted">拖动图片调整取景；浏览器会裁切、缩放并压缩为 JPEG 后再上传。</p><div id="status"></div></section>
<section class="card"><h2>SD 卡壁纸</h2><div id="list">正在读取...</div></section></main><script>
const canvas=document.getElementById('canvas'),ctx=canvas.getContext('2d');let img=null,base=1,zoom=1,ox=0,oy=0,drag=false,lastX=0,lastY=0;
function clamp(){if(!img)return;const s=base*zoom,w=img.width*s,h=img.height*s,mx=Math.max(0,(w-320)/2),my=Math.max(0,(h-240)/2);ox=Math.max(-mx,Math.min(mx,ox));oy=Math.max(-my,Math.min(my,oy))}
function draw(){ctx.fillStyle='#000';ctx.fillRect(0,0,320,240);if(!img)return;clamp();const s=base*zoom,w=img.width*s,h=img.height*s;ctx.drawImage(img,(320-w)/2+ox,(240-h)/2+oy,w,h)}
document.getElementById('file').onchange=e=>{const f=e.target.files[0];if(!f)return;const u=URL.createObjectURL(f);img=new Image;img.onload=()=>{base=Math.max(320/img.width,240/img.height);zoom=1;ox=oy=0;document.getElementById('zoom').value=100;document.getElementById('name').value=f.name.replace(/\.[^.]+$/,'');draw();URL.revokeObjectURL(u)};img.src=u};
document.getElementById('zoom').oninput=e=>{zoom=Number(e.target.value)/100;draw()};
canvas.onpointerdown=e=>{drag=true;lastX=e.offsetX;lastY=e.offsetY;canvas.setPointerCapture(e.pointerId)};canvas.onpointermove=e=>{if(!drag)return;const sx=320/canvas.clientWidth,sy=240/canvas.clientHeight;ox+=(e.offsetX-lastX)*sx;oy+=(e.offsetY-lastY)*sy;lastX=e.offsetX;lastY=e.offsetY;draw()};canvas.onpointerup=()=>drag=false;canvas.onpointercancel=()=>drag=false;
async function api(path,opt){const r=await fetch(path,opt);const t=await r.text();let j;try{j=JSON.parse(t)}catch(_){throw Error(t||('HTTP '+r.status))}if(!r.ok||!j.ok)throw Error(j.message||'操作失败');return j}
async function load(){try{const c=await api('/api/wallpapers');const list=document.getElementById('list');list.innerHTML='<div class="row '+(!c.selected?'selected':'')+'"><div class="builtin"></div><div class="info"><div class="name">内置壁纸</div></div><button onclick="choose(\'\')">选择</button></div>'+c.files.map(x=>'<div class="row '+(x.selected?'selected':'')+'"><img src="/api/wallpapers/image?name='+encodeURIComponent(x.name)+'&t='+Date.now()+'"><div class="info"><div class="name">'+escapeHtml(x.name)+'</div><div class="muted">'+Math.round(x.size/1024)+' KB</div></div><button onclick="choose(\''+enc(x.name)+'\')">选择</button><button class="danger" onclick="removeFile(\''+enc(x.name)+'\')">删除</button></div>').join('')}catch(e){document.getElementById('list').textContent=e.message}}
function escapeHtml(s){return s.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function enc(s){return encodeURIComponent(s).replace(/'/g,'%27')}
async function choose(n){try{await api('/api/wallpapers/select',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+n});await load()}catch(e){alert(e.message)}}
async function removeFile(n){if(!confirm('删除这张壁纸？'))return;try{await api('/api/wallpapers/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'name='+n});await load()}catch(e){alert(e.message)}}
document.getElementById('upload').onclick=()=>{if(!img){document.getElementById('status').textContent='请先选择图片';return}let n=document.getElementById('name').value.trim()||'wallpaper';canvas.toBlob(async b=>{try{document.getElementById('status').textContent='正在上传...';const form=new FormData;form.append('file',b,'wallpaper.jpg');const j=await api('/api/wallpapers/upload?name='+encodeURIComponent(n),{method:'POST',body:form});document.getElementById('status').textContent=j.message;await load()}catch(e){document.getElementById('status').textContent=e.message}},'image/jpeg',0.86)};draw();load();
</script></body></html>)WALLPAPER_HTML";

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

String normalizedUploadName(String name) {
    int slash = std::max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
    if (slash >= 0) name = name.substring(slash + 1);
    int dot = name.lastIndexOf('.');
    if (dot > 0) name.remove(dot);
    String clean;
    clean.reserve(44);
    for (size_t i = 0; i < name.length() && clean.length() < 40; i++) {
        const char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            clean += c;
        } else if (clean.isEmpty() || clean[clean.length() - 1] != '_') {
            clean += '_';
        }
    }
    if (clean.isEmpty()) clean = "wallpaper";
    return clean + ".jpg";
}

String uniqueUploadName(const String& requested) {
    if (!DIJI_SD.exists(wallpaperPathForName(requested))) return requested;
    String stem = requested;
    if (stem.endsWith(".jpg")) stem.remove(stem.length() - 4);
    for (int index = 2; index <= 99; index++) {
        const String candidate = stem + "-" + index + ".jpg";
        if (!DIJI_SD.exists(wallpaperPathForName(candidate))) return candidate;
    }
    return String("wallpaper-") + millis() + ".jpg";
}

void sendJson(bool ok, const String& message, int code = 200) {
    String body = String("{\"ok\":") + (ok ? "true" : "false") +
                  ",\"message\":\"" + jsonEscape(message) + "\"}";
    activeServer->send(code, "application/json; charset=utf-8", body);
}

void handleConfig() {
    const String selected = wallpaperSelectedName();
    const std::vector<WallpaperEntry> files = wallpaperListSd();
    String body = "{\"ok\":true,\"display_width\":320,\"display_height\":240,\"selected\":\"";
    body += jsonEscape(selected);
    body += "\",\"files\":[";
    for (size_t i = 0; i < files.size(); i++) {
        if (i) body += ',';
        body += "{\"name\":\"";
        body += jsonEscape(files[i].name);
        body += "\",\"size\":";
        body += String((unsigned)files[i].size);
        body += ",\"selected\":";
        body += files[i].selected ? "true" : "false";
        body += '}';
    }
    body += "]}";
    activeServer->send(200, "application/json; charset=utf-8", body);
}

void handleImage() {
    const String path = wallpaperPathForName(activeServer->arg("name"));
    if (path.isEmpty() || !DIJI_SD.exists(path)) {
        activeServer->send(404, "text/plain", "wallpaper_not_found");
        return;
    }
    File file = DIJI_SD.open(path, FILE_READ);
    if (!file) {
        activeServer->send(500, "text/plain", "wallpaper_open_failed");
        return;
    }
    activeServer->sendHeader("Cache-Control", "no-store");
    activeServer->streamFile(file, "image/jpeg");
    file.close();
}

void handleUploadData() {
    HTTPUpload& upload = activeServer->upload();
    if (upload.status == UPLOAD_FILE_START) {
        uploadError = "";
        uploadBytes = 0;
        uploadName = uniqueUploadName(normalizedUploadName(activeServer->arg("name")));
        removeUploadTempIfPresent();
        uploadFile = DIJI_SD.open(kUploadTempPath, FILE_WRITE);
        if (!uploadFile) uploadError = "无法创建临时壁纸文件";
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        uploadBytes += upload.currentSize;
        if (uploadBytes > kMaxUploadBytes) {
            uploadError = "壁纸超过 512 KB";
        } else if (uploadError.isEmpty() &&
                   uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
            uploadError = "写入 SD 卡失败";
        }
    } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
        if (uploadFile) uploadFile.close();
        if (upload.status == UPLOAD_FILE_ABORTED && uploadError.isEmpty()) {
            uploadError = "上传已中止";
        }
        if (!uploadError.isEmpty()) removeUploadTempIfPresent();
    }
}

void handleUploadFinished() {
    if (uploadFile) uploadFile.close();
    if (!uploadError.isEmpty() || uploadBytes == 0) {
        removeUploadTempIfPresent();
        sendJson(false, uploadError.isEmpty() ? "没有收到图片数据" : uploadError, 400);
        return;
    }
    File check = DIJI_SD.open(kUploadTempPath, FILE_READ);
    const bool jpeg = check && check.size() >= 4 && check.read() == 0xFF && check.read() == 0xD8;
    if (check) check.close();
    if (!jpeg) {
        removeUploadTempIfPresent();
        sendJson(false, "上传内容不是有效 JPEG", 400);
        return;
    }
    const String finalPath = wallpaperPathForName(uploadName);
    if (!DIJI_SD.rename(kUploadTempPath, finalPath)) {
        removeUploadTempIfPresent();
        sendJson(false, "保存壁纸失败", 500);
        return;
    }
    String error;
    if (!wallpaperSelect(uploadName, error)) {
        sendJson(false, error, 500);
        return;
    }
    sendJson(true, String("已保存并选择 ") + uploadName);
}

void registerRoutes() {
    if (routesRegistered) return;
    activeServer->on("/wallpaper", HTTP_GET, []() {
        activeServer->send_P(200, "text/html; charset=utf-8", kWallpaperHtml);
    });
    activeServer->on("/api/wallpapers", HTTP_GET, handleConfig);
    activeServer->on("/api/wallpapers/image", HTTP_GET, handleImage);
    activeServer->on("/api/wallpapers/select", HTTP_POST, []() {
        String name = activeServer->arg("name");
        name = name.isEmpty() ? String() : activeServer->urlDecode(name);
        String error;
        if (!wallpaperSelect(name, error)) sendJson(false, error, 400);
        else sendJson(true, "壁纸已切换");
    });
    activeServer->on("/api/wallpapers/delete", HTTP_POST, []() {
        String name = activeServer->urlDecode(activeServer->arg("name"));
        String error;
        if (!wallpaperDelete(name, error)) sendJson(false, error, 400);
        else sendJson(true, "壁纸已删除");
    });
    activeServer->on("/api/wallpapers/upload", HTTP_POST, handleUploadFinished,
                     handleUploadData);
    routesRegistered = true;
}
}  // namespace

void wallpaperWebServerRegisterRoutes(WebServer& sharedServer) {
    activeServer = &sharedServer;
    registerRoutes();
}

void wallpaperWebServerPrepare(bool sdCardAvailable) {
    if (sdCardAvailable && wallpaperEnsureDirectory()) removeUploadTempIfPresent();
}

void wallpaperWebServerCleanup() {
    const bool cleanUpload = (bool)uploadFile;
    if (uploadFile) uploadFile.close();
    if (cleanUpload) removeUploadTempIfPresent();
}
