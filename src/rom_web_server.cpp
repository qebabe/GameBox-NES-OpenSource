#include "rom_web_server.h"

#include <algorithm>
#include <WebServer.h>
#include <WiFi.h>
#include <vector>

#include "device_identity.h"
#include "ines_header.h"
#include "rom_storage.h"
#include "storage.h"

namespace {
constexpr size_t kMaxRomUploadBytes = 8 * 1024 * 1024;
constexpr const char* kRomDirectory = "/rom";
constexpr const char* kUploadTempPath = "/rom/.upload.tmp";
WebServer* activeServer = nullptr;
bool routesRegistered = false;
bool romFilesChanged = false;
File uploadFile;
String uploadName;
String uploadError;
size_t uploadBytes = 0;

void removeUploadTempIfPresent() {
    if (DIJI_SD.exists(kUploadTempPath)) DIJI_SD.remove(kUploadTempPath);
}

struct RomFileEntry {
    String path;
    String name;
    size_t size = 0;
};

const char kRomManagerHtml[] PROGMEM = R"ROM_MANAGER_HTML(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GameBox ROM 管理</title><style>
*{box-sizing:border-box}body{margin:0;background:#07111f;color:#dcecff;font-family:system-ui,-apple-system,sans-serif}.wrap{max-width:820px;margin:auto;padding:18px}.card{background:#102033;border:1px solid #294760;border-radius:14px;padding:16px;margin-bottom:14px}h1{font-size:22px;margin:0 0 12px;color:#48d7ff}h2{font-size:16px;margin:0 0 12px}.drop{border:2px dashed #39799d;border-radius:12px;padding:28px 16px;text-align:center;background:#0a1929;cursor:pointer}.drop.over{border-color:#48d7ff;background:#102b43}button,input{font:inherit}button{border:0;border-radius:8px;background:#087fc2;color:white;padding:8px 12px;cursor:pointer}.danger{background:#a43b4c}.muted{color:#8fa9bd;font-size:13px}.rom{display:grid;grid-template-columns:minmax(0,1fr) auto auto;gap:10px;align-items:center;border-top:1px solid #294052;padding:11px 0}.name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.progress{height:8px;background:#07111f;border-radius:5px;overflow:hidden;margin-top:10px}.bar{height:100%;width:0;background:#42d39b}#status{min-height:24px;color:#62efb1}@media(max-width:520px){.rom{grid-template-columns:minmax(0,1fr) auto}.rom .size{grid-column:1}.wrap{padding:10px}}
</style></head><body><main class="wrap"><p><a href="/" style="color:#48d7ff">← 返回设置中心</a></p><h1>GameBox ROM 管理</h1>
<section class="card"><h2>上传到 SD 卡 /rom</h2><div id="drop" class="drop">点击选择或拖入一个或多个 .nes 文件<input id="files" type="file" accept=".nes,application/octet-stream" multiple hidden></div><div class="progress"><div id="bar" class="bar"></div></div><div id="status"></div><p class="muted">单个文件最大 8 MB；设备会校验 iNES 文件头和 ROM 完整长度。同名文件不会自动覆盖。</p></section>
<section class="card"><h2>SD 卡中的 ROM</h2><div id="summary" class="muted"></div><div id="list">正在读取...</div></section></main><script>
const drop=document.getElementById('drop'),input=document.getElementById('files'),bar=document.getElementById('bar'),statusEl=document.getElementById('status');
drop.onclick=()=>input.click();drop.ondragover=e=>{e.preventDefault();drop.classList.add('over')};drop.ondragleave=()=>drop.classList.remove('over');drop.ondrop=e=>{e.preventDefault();drop.classList.remove('over');uploadFiles([...e.dataTransfer.files])};input.onchange=()=>uploadFiles([...input.files]);
function esc(s){return s.replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}function enc(s){return encodeURIComponent(s).replace(/'/g,'%27')}
async function load(){try{const r=await fetch('/api/roms');const j=await r.json();if(!r.ok||!j.ok)throw Error(j.message||'读取失败');document.getElementById('summary').textContent='共 '+j.files.length+' 个 ROM';document.getElementById('list').innerHTML=j.files.map(x=>'<div class="rom"><div><div class="name">'+esc(x.name)+'</div><div class="muted">'+esc(x.path)+'</div></div><div class="size muted">'+formatSize(x.size)+'</div><button class="danger" onclick="removeRom(\''+enc(x.path)+'\')">删除</button></div>').join('')||'<p class="muted">SD 卡中还没有 ROM</p>'}catch(e){document.getElementById('list').textContent=e.message}}
function formatSize(n){return n>=1048576?(n/1048576).toFixed(2)+' MB':(n/1024).toFixed(1)+' KB'}
function uploadOne(file){return new Promise((resolve,reject)=>{const form=new FormData;form.append('file',file,file.name);const xhr=new XMLHttpRequest;xhr.open('POST','/api/roms/upload');xhr.upload.onprogress=e=>{if(e.lengthComputable)bar.style.width=(e.loaded/e.total*100)+'%'};xhr.onload=()=>{let j;try{j=JSON.parse(xhr.responseText)}catch(_){return reject(Error(xhr.responseText||'上传失败'))}if(xhr.status<200||xhr.status>=300||!j.ok)reject(Error(j.message||'上传失败'));else resolve(j)};xhr.onerror=()=>reject(Error('网络连接中断'));xhr.send(form)})}
async function uploadFiles(files){files=files.filter(f=>f.name.toLowerCase().endsWith('.nes'));if(!files.length){statusEl.textContent='请选择 .nes 文件';return}for(let i=0;i<files.length;i++){try{bar.style.width='0';statusEl.textContent='正在上传 '+(i+1)+'/'+files.length+': '+files[i].name;const j=await uploadOne(files[i]);statusEl.textContent=j.message}catch(e){statusEl.textContent=files[i].name+': '+e.message;break}}bar.style.width='0';input.value='';await load()}
async function removeRom(path){if(!confirm('确定删除这个 ROM？相关存档不会删除。'))return;try{const r=await fetch('/api/roms/delete',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+path});const j=await r.json();if(!r.ok||!j.ok)throw Error(j.message||'删除失败');statusEl.textContent=j.message;await load()}catch(e){statusEl.textContent=e.message}}
load();
</script></body></html>)ROM_MANAGER_HTML";

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

void sendJson(bool ok, const String& message, int code = 200) {
    String body = String("{\"ok\":") + (ok ? "true" : "false") +
                  ",\"message\":\"" + jsonEscape(message) + "\"}";
    activeServer->send(code, "application/json; charset=utf-8", body);
}

String safeRomName(String name) {
    int slash = std::max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
    if (slash >= 0) name = name.substring(slash + 1);
    String clean;
    clean.reserve(std::min((size_t)96, name.length()));
    for (size_t i = 0; i < name.length() && clean.length() < 96; i++) {
        const uint8_t c = (uint8_t)name[i];
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
            c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            clean += '_';
        } else {
            clean += (char)c;
        }
    }
    if (clean == "." || clean == ".." || !romStorageIsNesPath(clean.c_str())) return "";
    return clean;
}

bool safeManagedRomPath(const String& path) {
    return path.startsWith("/") && path.length() <= 180 &&
           path.indexOf("..") < 0 && path.indexOf('\\') < 0 &&
           romStorageIsNesPath(path.c_str());
}

void scanRomDirectory(const String& path, int depth, std::vector<RomFileEntry>& files) {
    if (depth > 4 || files.size() >= 256) return;
    File directory = DIJI_SD.open(path);
    if (!directory || !directory.isDirectory()) return;
    while (files.size() < 256) {
        File entry = directory.openNextFile();
        if (!entry) break;
        String entryPath = entry.name();
        if (!entryPath.startsWith("/")) {
            entryPath = path;
            if (!entryPath.endsWith("/")) entryPath += '/';
            entryPath += entry.name();
        }
        if (entry.isDirectory()) {
            int slash = entryPath.lastIndexOf('/');
            const String directoryName = slash >= 0 ? entryPath.substring(slash + 1)
                                                     : entryPath;
            const bool skippedDirectory = directoryName.startsWith(".") ||
                                          entryPath == "/System Volume Information" ||
                                          entryPath == "/saves" || entryPath == "/covers" ||
                                          entryPath == "/wallpapers";
            if (!skippedDirectory) {
                entry.close();
                scanRomDirectory(entryPath, depth + 1, files);
                continue;
            }
        } else if (safeManagedRomPath(entryPath) &&
                   !romStorageShouldSkipName(entryPath.c_str())) {
            RomFileEntry file;
            file.path = entryPath;
            int slash = entryPath.lastIndexOf('/');
            file.name = slash >= 0 ? entryPath.substring(slash + 1) : entryPath;
            file.size = entry.size();
            files.push_back(file);
        }
        entry.close();
    }
    directory.close();
}

std::vector<RomFileEntry> listRomFiles() {
    std::vector<RomFileEntry> files;
    scanRomDirectory("/", 0, files);
    std::sort(files.begin(), files.end(), [](const RomFileEntry& left,
                                             const RomFileEntry& right) {
        String a = left.name;
        String b = right.name;
        a.toLowerCase();
        b.toLowerCase();
        return a < b;
    });
    return files;
}

bool validateUploadedRom(String& error, uint8_t& mapper) {
    error = "";
    mapper = 0;
    File file = DIJI_SD.open(kUploadTempPath, FILE_READ);
    if (!file) {
        error = "无法读取上传文件";
        return false;
    }
    uint8_t header[16] = {};
    const bool headerRead = file.read(header, sizeof(header)) == sizeof(header);
    const size_t actualSize = file.size();
    file.close();
    InesHeaderInfo info;
    if (!headerRead || !parseInesHeader(header, sizeof(header), info)) {
        error = "文件不是有效的 iNES ROM";
        return false;
    }
    if (!info.supportedFormat) {
        error = "暂不支持 NES 2.0 ROM";
        return false;
    }
    if (info.prgBanks == 0) {
        error = "ROM 没有有效的 PRG 数据";
        return false;
    }
    const size_t expectedSize = 16u + (info.hasTrainer ? 512u : 0u) +
                                (size_t)info.prgBanks * 16384u +
                                (size_t)info.chrBanks * 8192u;
    if (actualSize < expectedSize) {
        error = "ROM 文件不完整";
        return false;
    }
    mapper = info.mapper;
    return true;
}

void handleRomList() {
    const std::vector<RomFileEntry> files = listRomFiles();
    String body = "{\"ok\":true,\"files\":[";
    for (size_t i = 0; i < files.size(); i++) {
        if (i) body += ',';
        body += "{\"name\":\"" + jsonEscape(files[i].name) +
                "\",\"path\":\"" + jsonEscape(files[i].path) +
                "\",\"size\":" + String((unsigned)files[i].size) + "}";
    }
    body += "]}";
    activeServer->send(200, "application/json; charset=utf-8", body);
}

void handleUploadData() {
    HTTPUpload& upload = activeServer->upload();
    if (upload.status == UPLOAD_FILE_START) {
        uploadError = "";
        uploadBytes = 0;
        uploadName = safeRomName(upload.filename);
        if (uploadName.isEmpty()) uploadError = "ROM 文件名无效";
        if (!DIJI_SD.exists(kRomDirectory) && !DIJI_SD.mkdir(kRomDirectory)) {
            uploadError = "无法创建 /rom 目录";
        }
        const String finalPath = String(kRomDirectory) + "/" + uploadName;
        if (uploadError.isEmpty() && DIJI_SD.exists(finalPath)) {
            uploadError = "同名 ROM 已存在，请先删除旧文件";
        }
        removeUploadTempIfPresent();
        if (uploadError.isEmpty()) {
            uploadFile = DIJI_SD.open(kUploadTempPath, FILE_WRITE);
            if (!uploadFile) uploadError = "无法创建临时 ROM 文件";
        }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        uploadBytes += upload.currentSize;
        if (uploadBytes > kMaxRomUploadBytes) {
            uploadError = "ROM 文件超过 8 MB";
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
        sendJson(false, uploadError.isEmpty() ? "没有收到 ROM 数据" : uploadError, 400);
        return;
    }
    String error;
    uint8_t mapper = 0;
    if (!validateUploadedRom(error, mapper)) {
        removeUploadTempIfPresent();
        sendJson(false, error, 400);
        return;
    }
    const String finalPath = String(kRomDirectory) + "/" + uploadName;
    if (!DIJI_SD.rename(kUploadTempPath, finalPath)) {
        removeUploadTempIfPresent();
        sendJson(false, "保存 ROM 失败", 500);
        return;
    }
    romFilesChanged = true;
    String message = String("已上传 ") + uploadName + "，Mapper " +
                     String((unsigned)mapper);
    if (mapper != 0 && mapper != 1 && mapper != 2 && mapper != 3 &&
        mapper != 4 && mapper != 7 && mapper != 66) {
        message += "（当前模拟器暂不支持）";
    }
    sendJson(true, message);
}

void registerRoutes() {
    if (routesRegistered) return;
    activeServer->on("/roms", HTTP_GET, []() {
        activeServer->send_P(200, "text/html; charset=utf-8", kRomManagerHtml);
    });
    activeServer->on("/api/roms", HTTP_GET, handleRomList);
    activeServer->on("/api/roms/upload", HTTP_POST, handleUploadFinished, handleUploadData);
    activeServer->on("/api/roms/delete", HTTP_POST, []() {
        const String path = activeServer->arg("path");
        if (!safeManagedRomPath(path) || !DIJI_SD.exists(path)) {
            sendJson(false, "ROM 文件不存在或路径无效", 400);
            return;
        }
        if (!DIJI_SD.remove(path)) {
            sendJson(false, "删除 ROM 失败", 500);
            return;
        }
        romFilesChanged = true;
        sendJson(true, "ROM 已删除，存档文件已保留");
    });
    routesRegistered = true;
}
}  // namespace

void romWebServerRegisterRoutes(WebServer& sharedServer) {
    activeServer = &sharedServer;
    registerRoutes();
}

void romWebServerPrepare(bool sdCardAvailable) {
    if (!sdCardAvailable) return;
    if (!DIJI_SD.exists(kRomDirectory)) DIJI_SD.mkdir(kRomDirectory);
    removeUploadTempIfPresent();
}

void romWebServerCleanup() {
    const bool cleanUpload = (bool)uploadFile;
    if (uploadFile) uploadFile.close();
    if (cleanUpload) removeUploadTempIfPresent();
}

bool romWebServerConsumeFilesChanged() {
    const bool changed = romFilesChanged;
    romFilesChanged = false;
    return changed;
}
