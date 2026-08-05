// ============================================================================
// ESP32 NES 模拟器 - PPU (Picture Processing Unit) 头文件
// ============================================================================
// 优化版本: 逐行扫描渲染 + Tile 缓存 + IRAM 优化
//
// PPU 架构:
//   - 256x240 像素输出 (NTSC)
//   - 每帧 262 条扫描线 (0-239 可见, 240-260 VBlank, 261 预渲染)
//   - 每条扫描线 341 个点 (dot)
//   - 主时钟: CPU 时钟 x 3
// ============================================================================
#pragma once
#include <Arduino.h>

// ==================== 前向声明 ====================
class NES;
class Cartridge;

// ==================== Tile 缓存结构 ====================
// 用于缓存已解码的 tile 数据，避免重复解码
// 每个 tile 是 8x8 像素，存储为 2bpp 索引 (0-3)
struct TileCache {
    uint8_t pixels[8];     // 每行存储 8 个 2-bit 像素 (打包为 1 字节 x 8 行，或直接存 8 字节)
    uint16_t tileAddr;     // tile 的 CHR 地址 (用于验证缓存)
    bool valid;            // 缓存是否有效
};

// ==================== PPU 主类 ====================
class PPU {
public:
    // ========== 初始化接口 ==========
    void connect(NES* nes);
    void reset();

    // 设置直接内存访问指针 (用于快速渲染，绕过总线)
    void setMemoryPointers(uint8_t* vramPtr, uint8_t* palettePtr, Cartridge* cartPtr, bool* mirrorVertPtr);

    // ========== CPU 接口 (通过 $2000-$2007) ==========
    uint8_t regRead(uint8_t reg);
    void regWrite(uint8_t reg, uint8_t val);

    // ========== OAM DMA ($4014) ==========
    void oamDMA(uint8_t page, uint8_t* cpuRam);

    // ========== 渲染接口 ==========
    // 整帧渲染 (供 main.cpp 调用)
    void render(uint16_t* fb);
    // 渲染单条扫描线 (供 DMA/外部调用)
    void IRAM_ATTR renderLine(int scanline, uint16_t* lineBuffer);

    // ========== VBlank 状态 ==========
    bool isVBlank() const { return (ppuStatus & 0x80) != 0; }
    void setVBlank(bool v) { if (v) ppuStatus |= 0x80; else ppuStatus &= 0x7F; }
    bool nmiEnabled() const { return (ppuCtrl & 0x80) != 0; }
    // 清除 Sprite 0 Hit 标志 (用于帧开始)
    void clearSprite0Hit() { ppuStatus &= ~0x40; }

    // ========== PPU 寄存器 Getter (用于跳帧时 Sprite 0 Hit 检测) ==========
    uint8_t getPpuMask() const { return ppuMask; }
    uint8_t getPpuStatus() const { return ppuStatus; }

    // 获取 Sprite 0 的 Y 范围 (用于优化跳帧检测)
    // 返回 Sprite 0 覆盖的扫描线范围 [startY, endY)
    void getSprite0YRange(int& startY, int& endY) const {
        int y = oam[0] + 1;  // OAM[0] 存储的是 Y-1
        int height = (ppuCtrl & 0x20) ? 16 : 8;  // 8x16 模式?
        startY = y;
        endY = y + height;
    }

    // 获取 Sprite 0 的 X 范围 (用于优化跳帧检测)
    void getSprite0XRange(int& startX, int& endX) const {
        startX = oam[3];
        endX = startX + 8;
    }

    // 轻量级 Sprite 0 Hit 检测 (只检测 Sprite 0 覆盖区域，不渲染)
    // 返回 true 表示发生了 hit
    bool IRAM_ATTR checkSprite0HitFast(int scanline);

    // 跳帧时的帧初始化（加载调色板缓存和初始化滚动寄存器）
    void IRAM_ATTR initFrameForSprite0Check() {
        loadPaletteCache();
        if ((ppuMask & 0x18) != 0) {
            vramAddr = tempAddr;
        }
    }

    // 跳帧时更新一条扫描线的 Y 滚动 (不渲染, 只维护 vramAddr)
    void IRAM_ATTR skipScanlineForScrollUpdate();

    // ========== NMI 接口 (用于精确时序) ==========
    bool IRAM_ATTR isNmiPending() const { return nmiPending; }
    void IRAM_ATTR clearNmiPending() { nmiPending = false; }

    // ========== 时序模拟 ==========
    void IRAM_ATTR stepPPU();

    // PPU 时序
    int ppuCycle = 0;        // 0..340
    int ppuScanline = 0;     // 0..261
    bool oddFrame = false;

    // 渲染目标
    uint16_t* frameBuffer = nullptr;
    // 是否启用实际渲染（用于帧跳过 fast-path）
    bool renderEnabled = true;

    // Sprite 0 hit 延迟锁
    bool sprite0HitThisFrame = false;

    bool sprite0CheckedThisLine = false;

    // 帧完成标志 (VBlank 开始时置 true，main.cpp 读取后清除)
    volatile bool frameReady = false;

    // 批量推进 PPU 时钟 (高性能版本)
    // 返回是否触发了 NMI
    void IRAM_ATTR advanceCycles(int ppuCycles);

    // 标记本帧是否有实际渲染（用于主循环决定是否显示上一帧）
    bool renderedThisFrame = false;

    // 获取当前扫描线 (用于同步)
    int getCurrentScanline() const { return scanline; }
    int getCurrentDot() const { return dot; }

    // 设置当前扫描线和点 (用于帧级调度模式)
    void setScanline(int sl) { scanline = sl; }
    void setDot(int d) { dot = d; }

    // 判断是否在渲染中 (scanline 0-239, 且渲染已启用)
    bool isRendering() const {
        return (ppuMask & 0x18) && (scanline < 240);
    }

    // ========== 调试信息 ==========
    uint32_t getFrameCount() const { return frameCount; }

private:
    NES* bus = nullptr;

    // ========== 直接内存访问指针 (用于快速渲染) ==========
    uint8_t* vramDirect = nullptr;      // 2KB VRAM (Nametables)
    uint8_t* paletteDirect = nullptr;   // 32 bytes Palette RAM
    Cartridge* cartDirect = nullptr;    // Cartridge 指针 (用于 Mapper 访问)
    uint8_t* chrDirect = nullptr;       // 直接 CHR 指针 (8KB)
    bool* mirrorVertical = nullptr;     // 镜像模式 (true=垂直, false=水平)

    // ========== PPU 寄存器 ==========
    uint8_t ppuCtrl = 0;      // $2000 PPUCTRL - 控制寄存器
                               //   bit 7: NMI 使能
                               //   bit 6: PPU 主/从 (未使用)
                               //   bit 5: 精灵大小 (0=8x8, 1=8x16)
                               //   bit 4: 背景 Pattern Table (0=$0000, 1=$1000)
                               //   bit 3: 精灵 Pattern Table (0=$0000, 1=$1000)
                               //   bit 2: VRAM 地址增量 (0=+1, 1=+32)
                               //   bit 1-0: 基础 Nametable 地址

    uint8_t ppuMask = 0;      // $2001 PPUMASK - 掩码寄存器
                               //   bit 7-5: 颜色强调 (BGR)
                               //   bit 4: 显示精灵
                               //   bit 3: 显示背景
                               //   bit 2: 左侧 8 像素显示精灵
                               //   bit 1: 左侧 8 像素显示背景
                               //   bit 0: 灰度模式

    uint8_t ppuStatus = 0;    // $2002 PPUSTATUS - 状态寄存器
                               //   bit 7: VBlank 标志
                               //   bit 6: Sprite 0 Hit
                               //   bit 5: Sprite Overflow

    uint8_t oamAddr = 0;      // $2003 OAMADDR - OAM 地址

    // ========== 内部寄存器 (滚动/地址) ==========
    uint16_t vramAddr = 0;    // v - 当前 VRAM 地址 (15 bit)
                               //   格式: 0yyy NNYY YYYX XXXX
                               //   yyy: fine Y scroll (3 bit)
                               //   NN: nametable select (2 bit)
                               //   YYYYY: coarse Y scroll (5 bit)
                               //   XXXXX: coarse X scroll (5 bit)

    uint16_t tempAddr = 0;    // t - 临时 VRAM 地址 (用于滚动)
    uint8_t fineX = 0;        // x - Fine X scroll (3 bit)
    bool writeToggle = false; // w - 写入切换 (用于 $2005/$2006)
    uint8_t dataBuffer = 0;   // $2007 读取缓冲

    // ========== OAM (Object Attribute Memory) ==========
    // 64 个精灵，每个 4 字节: Y, Tile, Attr, X
    uint8_t oam[256];

    // ========== 调色板缓存 ==========
    // 每帧开始时加载，避免渲染时反复读取
    uint16_t bgPaletteCache[16];    // 背景调色板 (4组 x 4色)
    uint16_t spPaletteCache[16];    // 精灵调色板 (4组 x 4色)

    // ========== Tile 缓存 (Phase 2 优化) ==========
    // 缓存最近使用的 tile 解码结果
    static const int TILE_CACHE_SIZE = 128;
    TileCache tileCache[TILE_CACHE_SIZE];

    // ========== 快速内存访问 (IRAM 优化) ==========
    uint8_t IRAM_ATTR fastChrRead(uint16_t addr);
    uint8_t IRAM_ATTR fastVramRead(uint16_t addr);
    uint8_t IRAM_ATTR fastPaletteRead(uint8_t addr);

    // ========== 逐行渲染 (Phase 1 优化) ==========
    void IRAM_ATTR renderBackgroundLine(int scanline, uint16_t* lineBuffer);
    void IRAM_ATTR renderSpriteLine(int scanline, uint16_t* lineBuffer);

    // 预加载调色板到缓存
    void IRAM_ATTR loadPaletteCache();

    // 递增 Y 滚动 (每条扫描线结束时调用)
    void IRAM_ATTR incrementY();

    // 清除 tile 缓存 (切换 CHR bank 时调用)
    void invalidateTileCache();

    // ========== 帧计数 ==========
    uint32_t frameCount = 0;

    // ========== 渲染单条扫描线 (供 DMA 输出使用) ==========
    // (declaration moved to public so external callers can invoke it)

    // ========== 时序状态 (Phase 4 优化) ==========
    int scanline = 0;         // 当前扫描线 (0-261, NTSC)
    int dot = 0;              // 当前点 (0-340)
    // oddFrame 已在 public 区域声明
    bool nmiOccurred = false; // NMI 已触发标志
    bool nmiPending = false;  // NMI 待处理标志

    // ========== Sprite 0 Hit 检测 ==========
    bool sprite0HitPossible = false;  // Sprite 0 在当前帧是否可能 hit
    bool sprite0Rendered = false;     // Sprite 0 是否已渲染到当前扫描线
    uint8_t bgPixelOpacity[256];      // 当前扫描线背景像素不透明度 (用于精确 Sprite 0 hit)

    // ========== OAM 预评估缓存 (消除每行 64 精灵扫描) ==========
    static const int MAX_SPRITES_PER_LINE = 8;
    uint8_t spriteIndicesPerLine[240][MAX_SPRITES_PER_LINE];
    uint8_t spriteCountPerLine[240];
    void IRAM_ATTR evaluateOAM();

    // ========== 渲染滚动快照 ==========
    // 在帧开始时保存滚动状态，用于事后渲染
    uint16_t savedScrollAddr = 0;     // 帧开始时的 tempAddr 快照
    uint8_t savedFineX = 0;           // 帧开始时的 fineX 快照

public:
    // ========== Save State 接口 ==========
    void saveState(uint8_t* buf, size_t& offset) const;
    void loadState(const uint8_t* buf, size_t& offset);
    size_t getStateSize() const;
};
