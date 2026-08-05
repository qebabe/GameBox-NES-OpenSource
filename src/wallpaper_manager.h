#pragma once

#include <Arduino.h>
#include <vector>

struct WallpaperEntry {
    String name;
    size_t size = 0;
    bool selected = false;
};

constexpr const char* DIJI_WALLPAPER_DIRECTORY = "/wallpapers";

bool wallpaperEnsureDirectory();
bool wallpaperIsValidJpegName(const String& name);
String wallpaperPathForName(const String& name);
String wallpaperSelectedName();
String wallpaperSelectedPath();
String wallpaperSelectedDisplayName();
std::vector<WallpaperEntry> wallpaperListSd();
bool wallpaperSelect(const String& name, String& error);
bool wallpaperDelete(const String& name, String& error);
