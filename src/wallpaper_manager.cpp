#include "wallpaper_manager.h"

#include <algorithm>
#include <Preferences.h>

#include "storage.h"

namespace {
constexpr const char* kPreferencesNamespace = "wallpaper";
constexpr const char* kSelectedKey = "selected";
bool selectionLoaded = false;
String selectedCache;

bool hasJpegExtension(const String& name) {
    String lower = name;
    lower.toLowerCase();
    return lower.endsWith(".jpg") || lower.endsWith(".jpeg");
}

bool saveSelection(const String& name) {
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, false)) return false;
    const bool ok = preferences.putString(kSelectedKey, name) > 0 || name.isEmpty();
    if (name.isEmpty()) preferences.remove(kSelectedKey);
    preferences.end();
    if (ok) {
        selectedCache = name;
        selectionLoaded = true;
    }
    return ok;
}

void loadSelectionOnce() {
    if (selectionLoaded) return;
    selectionLoaded = true;
    selectedCache = "";
    Preferences preferences;
    if (!preferences.begin(kPreferencesNamespace, true)) return;
    if (preferences.isKey(kSelectedKey)) {
        selectedCache = preferences.getString(kSelectedKey, "");
    }
    preferences.end();
}
}  // namespace

bool wallpaperEnsureDirectory() {
    return DIJI_SD.exists(DIJI_WALLPAPER_DIRECTORY) ||
           DIJI_SD.mkdir(DIJI_WALLPAPER_DIRECTORY);
}

bool wallpaperIsValidJpegName(const String& name) {
    if (name.isEmpty() || name.length() > 64 || !hasJpegExtension(name)) return false;
    if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0 || name.indexOf("..") >= 0) {
        return false;
    }
    return true;
}

String wallpaperPathForName(const String& name) {
    if (!wallpaperIsValidJpegName(name)) return "";
    return String(DIJI_WALLPAPER_DIRECTORY) + "/" + name;
}

String wallpaperSelectedName() {
    loadSelectionOnce();
    if (!wallpaperIsValidJpegName(selectedCache)) return "";
    const String path = wallpaperPathForName(selectedCache);
    return !path.isEmpty() && DIJI_SD.exists(path) ? selectedCache : String();
}

String wallpaperSelectedPath() {
    return wallpaperPathForName(wallpaperSelectedName());
}

String wallpaperSelectedDisplayName() {
    const String selected = wallpaperSelectedName();
    return selected.isEmpty() ? String("内置壁纸") : selected;
}

std::vector<WallpaperEntry> wallpaperListSd() {
    std::vector<WallpaperEntry> entries;
    File directory = DIJI_SD.open(DIJI_WALLPAPER_DIRECTORY);
    if (!directory || !directory.isDirectory()) return entries;

    const String selected = wallpaperSelectedName();
    while (true) {
        File file = directory.openNextFile();
        if (!file) break;
        if (!file.isDirectory()) {
            String name = file.name();
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (wallpaperIsValidJpegName(name)) {
                WallpaperEntry entry;
                entry.name = name;
                entry.size = file.size();
                entry.selected = name == selected;
                entries.push_back(entry);
            }
        }
        file.close();
    }
    directory.close();

    std::sort(entries.begin(), entries.end(), [](const WallpaperEntry& left,
                                                  const WallpaperEntry& right) {
        String a = left.name;
        String b = right.name;
        a.toLowerCase();
        b.toLowerCase();
        return a < b;
    });
    return entries;
}

bool wallpaperSelect(const String& name, String& error) {
    error = "";
    if (name.isEmpty()) {
        if (!saveSelection("")) {
            error = "保存内置壁纸设置失败";
            return false;
        }
        return true;
    }
    if (!wallpaperIsValidJpegName(name)) {
        error = "壁纸文件名无效";
        return false;
    }
    const String path = wallpaperPathForName(name);
    if (!DIJI_SD.exists(path)) {
        error = "壁纸文件不存在";
        return false;
    }
    if (!saveSelection(name)) {
        error = "保存壁纸设置失败";
        return false;
    }
    return true;
}

bool wallpaperDelete(const String& name, String& error) {
    error = "";
    if (!wallpaperIsValidJpegName(name)) {
        error = "壁纸文件名无效";
        return false;
    }
    const String path = wallpaperPathForName(name);
    if (!DIJI_SD.exists(path)) {
        error = "壁纸文件不存在";
        return false;
    }
    const bool wasSelected = wallpaperSelectedName() == name;
    if (!DIJI_SD.remove(path)) {
        error = "删除壁纸失败";
        return false;
    }
    if (wasSelected && !saveSelection("")) {
        error = "壁纸已删除，但恢复内置壁纸失败";
        return false;
    }
    return true;
}
