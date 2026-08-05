// ============================================================================
// ESP32 NES 模拟器 - PPU (Picture Processing Unit) 实现
// ============================================================================
// 优化版本: 逐行扫描渲染 + Tile 缓存 + IRAM 优化
//
// 优化策略:
//   Phase 1: 逐行扫描渲染 - 减少每帧计算量 3-4x
//   Phase 2: Tile 缓存 - 避免重复解码相同 tile
//   Phase 3: IRAM 优化 - 关键函数放入快速 IRAM
//
// 渲染流程:
//   1. 加载调色板缓存
//   2. 填充背景色
//   3. 逐行渲染背景 (renderBackgroundLine)
//   4. 逐行渲染精灵 (renderSpriteLine)
//   5. 设置 VBlank 标志
// ============================================================================
#include "ppu.h"
#include "nes.h"
#include "cartridge.h"

// ============================================================================
// NES 2C02 调色板 (BGR565 格式)
// ============================================================================
// BGR565 格式适用于 TFT_eSPI 的 TFT_BGR 配置
// 计算公式: BGR565 = ((B >> 3) << 11) | ((G >> 2) << 5) | (R >> 3)
//
// NES 有 64 种颜色 (实际只有 52 种唯一颜色，部分是重复的黑色)
// 索引 0x0D, 0x0E, 0x0F, 0x1E, 0x1F, 0x2E, 0x2F, 0x3E, 0x3F 是"坏"颜色
// ============================================================================
static const uint16_t DRAM_ATTR nesPalette[64] = {
    // 第 0 行 ($00-$0F): 灰色调和基础色
    0xAC52, 0xCA00, 0x6C08, 0x2C20, 0x0930, 0x2440, 0x4038, 0x8030,
    0x0019, 0x4009, 0x6016, 0x6101, 0x2501, 0x0000, 0x0000, 0x0000,
    // 第 1 行 ($10-$1F): 中等亮度
    0x14A5, 0x531A, 0xB739, 0x5759, 0x1271, 0x0B81, 0x6481, 0xE069,
    0x8052, 0x0033, 0x401B, 0x450B, 0xED12, 0x0000, 0x0000, 0x0000,
    // 第 2 行 ($20-$2F): 高亮度
    0xFFFF, 0xFF6C, 0x3F8C, 0xBFAB, 0x7ECB, 0x96E3, 0xEEDB, 0x87CC,
    0x24A5, 0xC585, 0x2866, 0x0F56, 0x9855, 0xE739, 0x0000, 0x0000,
    // 第 3 行 ($30-$3F): 最高亮度/淡色
    0xFFFF, 0xBFBF, 0x7FCE, 0x3FDE, 0x1FEE, 0x1BF6, 0x38F6, 0x95EE,
    0xD3DE, 0x13CF, 0x35BF, 0x38B7, 0xFCB6, 0x55AD, 0x0000, 0x0000,
};

// Packed 2bpp pattern row decode for background tiles.
// lane n stores pixel n in bits [2n+1:2n].
static uint16_t DRAM_ATTR bgPlaneLut[2][256];
static bool bgPlaneLutReady = false;

static void initBgPlaneLut() {
    if (bgPlaneLutReady) return;

    for (int value = 0; value < 256; value++) {
        uint16_t loPack = 0;
        uint16_t hiPack = 0;
        for (int px = 0; px < 8; px++) {
            int srcBit = 7 - px;
            if ((value >> srcBit) & 1) {
                loPack |= (uint16_t)(1u << (px * 2));
                hiPack |= (uint16_t)(2u << (px * 2));
            }
        }
        bgPlaneLut[0][value] = loPack;
        bgPlaneLut[1][value] = hiPack;
    }

    bgPlaneLutReady = true;
}

// ============================================================================
// 初始化函数
// ============================================================================

/**
 * 连接到 NES 总线
 * @param n NES 实例指针
 */
void PPU::connect(NES* n) {
    bus = n;
}

/**
 * 设置直接内存访问指针
 * 这些指针允许 PPU 直接访问内存，绕过总线调用，提高渲染性能
 *
 * @param vramPtr    VRAM 指针 (2KB Nametables)
 * @param palettePtr 调色板 RAM 指针 (32 bytes)
 * @param cartPtr    卡带指针 (用于 CHR ROM/RAM 访问)
 * @param mirrorVertPtr 镜像模式指针
 */
void PPU::setMemoryPointers(uint8_t* vramPtr, uint8_t* palettePtr, Cartridge* cartPtr, bool* mirrorVertPtr) {
    vramDirect = vramPtr;
    paletteDirect = palettePtr;
    cartDirect = cartPtr;
    chrDirect = cartPtr->getChrData();  // 直接获取 CHR 数据指针
    mirrorVertical = mirrorVertPtr;
}

/**
 * 重置 PPU 状态
 * 在模拟器启动或复位时调用
 */
void PPU::reset() {
    initBgPlaneLut();

    // 清除寄存器
    ppuCtrl = 0;
    ppuMask = 0;
    ppuStatus = 0;
    oamAddr = 0;

    // 清除内部寄存器
    vramAddr = 0;
    tempAddr = 0;
    fineX = 0;
    writeToggle = false;
    dataBuffer = 0;

    // 清除渲染滚动快照
    savedScrollAddr = 0;
    savedFineX = 0;

    // 清除 OAM
    memset(oam, 0, sizeof(oam));

    // 清除调色板缓存
    memset(bgPaletteCache, 0, sizeof(bgPaletteCache));
    memset(spPaletteCache, 0, sizeof(spPaletteCache));

    // 清除 Tile 缓存
    invalidateTileCache();

    // 重置 PPU 时序
    ppuCycle = 0;
    ppuScanline = 0;
    oddFrame = false;
    nmiPending = false;
    nmiOccurred = false;
    frameReady = false;
    sprite0HitThisFrame = false;
    sprite0CheckedThisLine = false;

    frameCount = 0;
}

/**
 * 清除 Tile 缓存
 * 当 CHR bank 切换时需要调用
 */
void PPU::invalidateTileCache() {
    for (int i = 0; i < TILE_CACHE_SIZE; i++) {
        tileCache[i].valid = false;
    }
}

// ============================================================================
// CPU 寄存器接口 ($2000-$2007)
// ============================================================================

/**
 * 读取 PPU 寄存器
 *
 * @param reg 寄存器索引 (0-7, 对应 $2000-$2007)
 * @return 寄存器值
 *
 * 可读寄存器:
 *   $2002 (reg 2): PPUSTATUS - 状态寄存器
 *   $2004 (reg 4): OAMDATA - OAM 数据读取
 *   $2007 (reg 7): PPUDATA - VRAM 数据读取
 */
uint8_t PPU::regRead(uint8_t reg) {
    uint8_t result = 0;

    switch (reg) {
        case 2: // $2002 PPUSTATUS
            // 读取状态寄存器
            // - 返回 VBlank、Sprite 0 Hit、Overflow 状态
            // - 清除 VBlank 标志 (bit 7)
            // - 重置写入切换 (writeToggle)
            result = ppuStatus;
            ppuStatus &= 0x7F;    // 清除 VBlank
            writeToggle = false;  // 重置 w 寄存器
            break;

        case 4: // $2004 OAMDATA
            // 读取 OAM 数据
            // 返回 oamAddr 位置的 OAM 字节
            result = oam[oamAddr];
            break;

        case 7: // $2007 PPUDATA
            // 读取 VRAM 数据
            // - 调色板区域 ($3F00+): 立即返回
            // - 其他区域: 返回缓冲，更新缓冲
            if (vramAddr < 0x3F00) {
                // 非调色板: 返回缓冲值，读取新值到缓冲
                result = dataBuffer;
                dataBuffer = bus->ppuRead(vramAddr);
            } else {
                // 调色板: 立即返回，缓冲更新为下面的镜像地址
                result = bus->ppuRead(vramAddr);
                dataBuffer = bus->ppuRead(vramAddr - 0x1000);
            }
            // 根据 PPUCTRL bit 2 增加地址 (+1 或 +32)
            vramAddr += (ppuCtrl & 0x04) ? 32 : 1;
            vramAddr &= 0x3FFF;  // 保持在 14 位地址空间
            break;
    }

    return result;
}

/**
 * 写入 PPU 寄存器
 *
 * @param reg 寄存器索引 (0-7, 对应 $2000-$2007)
 * @param val 要写入的值
 *
 * 可写寄存器:
 *   $2000 (reg 0): PPUCTRL - 控制寄存器
 *   $2001 (reg 1): PPUMASK - 掩码寄存器
 *   $2003 (reg 3): OAMADDR - OAM 地址
 *   $2004 (reg 4): OAMDATA - OAM 数据写入
 *   $2005 (reg 5): PPUSCROLL - 滚动寄存器 (两次写入)
 *   $2006 (reg 6): PPUADDR - VRAM 地址 (两次写入)
 *   $2007 (reg 7): PPUDATA - VRAM 数据写入
 */
void PPU::regWrite(uint8_t reg, uint8_t val) {
    switch (reg) {
        case 0: // $2000 PPUCTRL
            ppuCtrl = val;
            // 更新 tempAddr 的 nametable 选择位 (bit 10-11)
            tempAddr = (tempAddr & 0xF3FF) | ((val & 0x03) << 10);
            break;

        case 1: // $2001 PPUMASK
            ppuMask = val;
            break;

        case 3: // $2003 OAMADDR
            oamAddr = val;
            break;

        case 4: // $2004 OAMDATA
            oam[oamAddr++] = val;
            break;

        case 5: // $2005 PPUSCROLL (两次写入)
            if (!writeToggle) {
                // 第一次写入: X 滚动
                // fineX = val[2:0]
                // tempAddr.coarseX = val[7:3]
                fineX = val & 0x07;
                tempAddr = (tempAddr & 0xFFE0) | (val >> 3);
            } else {
                // 第二次写入: Y 滚动
                // tempAddr.fineY = val[2:0]
                // tempAddr.coarseY = val[7:3]
                tempAddr = (tempAddr & 0x8C1F) | ((val & 0x07) << 12) | ((val & 0xF8) << 2);
            }
            writeToggle = !writeToggle;
            break;

        case 6: // $2006 PPUADDR (两次写入)
            if (!writeToggle) {
                // 第一次写入: 高字节
                tempAddr = (tempAddr & 0x00FF) | ((val & 0x3F) << 8);
            } else {
                // 第二次写入: 低字节，同时复制到 vramAddr
                tempAddr = (tempAddr & 0xFF00) | val;
                vramAddr = tempAddr;
            }
            writeToggle = !writeToggle;
            break;

        case 7: // $2007 PPUDATA
            bus->ppuWrite(vramAddr, val);
            vramAddr += (ppuCtrl & 0x04) ? 32 : 1;
            vramAddr &= 0x3FFF;
            break;
    }
}

/**
 * OAM DMA 传输
 * 当 CPU 写入 $4014 时调用，从 CPU RAM 复制 256 字节到 OAM
 *
 * @param page 源页面 (高字节，0xXX00)
 * @param cpuRam CPU RAM 指针
 *
 * 注意: 实际 NES 的 DMA 需要 513-514 个 CPU 周期
 */
void PPU::oamDMA(uint8_t page, uint8_t* cpuRam) {
    uint16_t addr = page << 8;

    // 复制 256 字节到 OAM
    for (int i = 0; i < 256; i++) {
        if (addr < 0x0800) {
            oam[oamAddr] = cpuRam[addr & 0x07FF];
        }
        oamAddr++;  // OAM 地址自动递增
        addr++;
    }
}

// ============================================================================
// 快速内存访问 (IRAM 优化)
// ============================================================================
// 这些函数被标记为 IRAM_ATTR，在 ESP32 上会被放入快速 IRAM
// 减少 Flash 访问延迟，提高渲染性能

/**
 * 快速 CHR ROM/RAM 读取
 * 通过 Cartridge::ppuRead 访问，支持 bank 切换
 *
 * 注意: 对于有 CHR bank 切换的 mapper (MMC1, MMC3, CNROM)
 *       必须通过 Cartridge 来读取，而不是直接访问 chrDirect
 */
uint8_t IRAM_ATTR PPU::fastChrRead(uint16_t addr) {
    // 通过 Cartridge 读取，确保正确处理 bank 切换
    return cartDirect->ppuRead(addr);
}

/**
 * 快速 VRAM (Nametable) 读取
 * 通过 Cartridge 处理镜像，这样 MMC3 的动态镜像才能正确工作
 *
 * 镜像模式:
 *   - 垂直镜像: NT0/NT1 左右排列 (适合横向滚动游戏)
 *   - 水平镜像: NT0/NT1 上下排列 (适合纵向滚动游戏)
 */
uint8_t IRAM_ATTR PPU::fastVramRead(uint16_t addr) {
    // 让 Cartridge 处理镜像逻辑，这样 MMC3 运行时切换镜像才能生效
    return cartDirect->readNameTable(addr);
}

/**
 * 快速调色板读取
 * 直接访问 paletteDirect，处理镜像
 *
 * 镜像规则: $3F10, $3F14, $3F18, $3F1C 镜像到 $3F00, $3F04, $3F08, $3F0C
 */
uint8_t IRAM_ATTR PPU::fastPaletteRead(uint8_t addr) {
    addr &= 0x1F;  // 32 字节调色板
    // 镜像: 精灵调色板的背景色镜像到全局背景色
    if ((addr & 0x13) == 0x10) {
        addr &= 0x0F;
    }
    return paletteDirect[addr];
}

// ============================================================================
// 调色板缓存
// ============================================================================

/**
 * 加载调色板到缓存数组
 * 每帧渲染前调用一次，避免渲染时反复读取调色板
 *
 * NES 调色板结构:
 *   $3F00-$3F0F: 背景调色板 (4 组 x 4 色)
 *   $3F10-$3F1F: 精灵调色板 (4 组 x 4 色)
 *   每组的第 0 色是透明色 (背景使用 $3F00)
 */
void IRAM_ATTR PPU::loadPaletteCache() {
    // 获取全局背景色
    uint8_t bgColorIdx = fastPaletteRead(0) & 0x3F;
    uint16_t bgColor = nesPalette[bgColorIdx];

    // 加载背景调色板 (4 组 x 4 色 = 16 色)
    for (int i = 0; i < 16; i++) {
        if ((i & 0x03) == 0) {
            // 每组第 0 色是背景色
            bgPaletteCache[i] = bgColor;
        } else {
            // 实际颜色
            uint8_t palIdx = fastPaletteRead(i) & 0x3F;
            bgPaletteCache[i] = nesPalette[palIdx];
        }
    }

    // 加载精灵调色板 (4 组 x 4 色 = 16 色)
    for (int i = 0; i < 16; i++) {
        if ((i & 0x03) == 0) {
            // 精灵透明色 (不会被渲染，但为了完整性设置为背景色)
            spPaletteCache[i] = bgColor;
        } else {
            // 精灵调色板从 $3F10 开始
            uint8_t palIdx = fastPaletteRead(0x10 + i) & 0x3F;
            spPaletteCache[i] = nesPalette[palIdx];
        }
    }


}

/**
 * 递增 Y 滚动 (每条扫描线结束时调用)
 * 模拟 PPU 的 increment Y 行为
 * 参考 Anemoia 实现
 */
void IRAM_ATTR PPU::incrementY() {
    // 如果渲染未启用(背景或精灵)，不递增
    if (!(ppuMask & 0x18)) return;

    // 从 vramAddr 提取 fine Y (bits 12-14)
    int fineY = (vramAddr >> 12) & 0x07;

    if (fineY < 7) {
        // 递增 fine Y
        vramAddr += 0x1000;
        return;
    }

    // fine Y == 7, 溢出到 coarse Y
    vramAddr &= ~0x7000;  // 清除 fine Y

    int coarseY = (vramAddr >> 5) & 0x1F;

    if (coarseY == 29) {
        // row 29 是最后一行可见 tile，切换 nametable
        coarseY = 0;
        vramAddr ^= 0x0800;  // 切换垂直 nametable
    } else if (coarseY == 31) {
        // row 31 不切换 nametable
        coarseY = 0;
    } else {
        coarseY++;
    }

    vramAddr = (vramAddr & ~0x03E0) | (coarseY << 5);
}

/**
 * 跳帧时更新一条扫描线的 Y 滚动 (不渲染)
 * 仅维护 vramAddr，保持 PPU 状态同步
 */
void IRAM_ATTR PPU::skipScanlineForScrollUpdate() {
    // 每条扫描线开始时，同步 coarse X
    if ((ppuMask & 0x18) != 0) {
        vramAddr = (vramAddr & ~0x041F) | (tempAddr & 0x041F);
    }
    // 扫描线结束时递增 Y
    incrementY();
}

/**
 * 轻量级 Sprite 0 Hit 检测 (只检测 Sprite 0 覆盖区域)
 *
 * @param scanline 扫描线号 (0-239)
 * @return true 如果发生了 Hit
 *
 * 优化策略:
 *   - 只读取 Sprite 0 覆盖的 1-2 个背景 tile
 *   - 只检测 Sprite 0 的 8 个像素
 *   - 不填充完整的 bgPixelOpacity 数组
 *   - 比完整 renderLine(nullptr) 快 10-20 倍
 */
bool IRAM_ATTR PPU::checkSprite0HitFast(int scanline) {
    // 如果已经 hit 了，不需要再检测
    if (ppuStatus & 0x40) return true;

    // 检查背景和精灵是否都启用
    if ((ppuMask & 0x18) != 0x18) return false;

    // 获取 Sprite 0 信息
    uint8_t sprite0Y = oam[0];
    uint8_t sprite0Tile = oam[1];
    uint8_t sprite0Attr = oam[2];
    uint8_t sprite0X = oam[3];

    // Sprite 0 不在屏幕上
    if (sprite0Y >= 0xEF) return false;

    // Sprite Y 坐标需要 +1 (OAM 存储的是 Y-1)
    int spriteY = sprite0Y + 1;

    // 8x16 模式?
    bool is8x16 = (ppuCtrl & 0x20) != 0;
    int spriteHeight = is8x16 ? 16 : 8;

    // 检查 Sprite 0 是否在当前扫描线上
    if (scanline < spriteY || scanline >= spriteY + spriteHeight) return false;

    // X=255 时不产生 hit
    if (sprite0X >= 255) return false;

    // 左 8 像素被遮蔽时检测范围
    int startCheckX = sprite0X;
    int endCheckX = sprite0X + 8;
    if (startCheckX < 8 && !(ppuMask & 0x06)) {
        startCheckX = 8;
    }
    if (endCheckX > 255) endCheckX = 255;
    if (startCheckX >= endCheckX) return false;

    // ========== 读取 Sprite 0 Pattern ==========
    int spriteRow = scanline - spriteY;
    bool flipV = (sprite0Attr & 0x80) != 0;
    bool flipH = (sprite0Attr & 0x40) != 0;
    int patternRow = flipV ? (spriteHeight - 1 - spriteRow) : spriteRow;

    uint16_t spPatternBase = (ppuCtrl & 0x08) ? 0x1000 : 0x0000;
    uint16_t spTileAddr;

    if (is8x16) {
        uint16_t base = (sprite0Tile & 0x01) ? 0x1000 : 0x0000;
        uint8_t tile = sprite0Tile & 0xFE;
        if (patternRow >= 8) {
            tile++;
            patternRow -= 8;
        }
        spTileAddr = base + tile * 16 + patternRow;
    } else {
        spTileAddr = spPatternBase + sprite0Tile * 16 + patternRow;
    }

    uint8_t** chrPtrs_sp0 = cartDirect->chrBankPtrs;
    uint8_t** ntPtrs_sp0 = cartDirect->ntPtrs;

    uint8_t spPatLo = chrPtrs_sp0[(spTileAddr >> 10) & 7][spTileAddr & 0x3FF];
    uint16_t spTileAddr8 = spTileAddr + 8;
    uint8_t spPatHi = chrPtrs_sp0[(spTileAddr8 >> 10) & 7][spTileAddr8 & 0x3FF];

    // 如果精灵这行全透明，不可能 hit
    if ((spPatLo | spPatHi) == 0) return false;

    // 预计算精灵像素
    uint8_t spPixels[8];
    if (flipH) {
        for (int i = 0; i < 8; i++) {
            spPixels[i] = ((spPatLo >> i) & 1) | (((spPatHi >> i) & 1) << 1);
        }
    } else {
        for (int i = 0; i < 8; i++) {
            spPixels[i] = ((spPatLo >> (7 - i)) & 1) | (((spPatHi >> (7 - i)) & 1) << 1);
        }
    }

    // ========== 读取背景 Pattern (只读取 Sprite 0 覆盖的区域) ==========
    uint16_t bgPatternBase = (ppuCtrl & 0x10) ? 0x1000 : 0x0000;
    int coarseX = vramAddr & 0x001F;
    int coarseY = (vramAddr >> 5) & 0x1F;
    int fineYOffset = (vramAddr >> 12) & 0x07;
    int baseNt = (vramAddr >> 10) & 0x03;

    // 检测每个精灵像素
    for (int bit = 0; bit < 8; bit++) {
        int sx = sprite0X + bit;
        if (sx < startCheckX || sx >= endCheckX) continue;
        if (spPixels[bit] == 0) continue;  // 精灵透明

        // 计算这个屏幕X对应的背景tile
        int bgPixelX = sx + fineX;
        int bgTileX = coarseX + (bgPixelX >> 3);
        int nt = baseNt;
        if (bgTileX >= 32) {
            bgTileX -= 32;
            nt ^= 1;
        }
        int tileFineX = bgPixelX & 0x07;

        // 读取背景 tile — 直接通过 ntPtrs 内联
        uint8_t tileIndex = ntPtrs_sp0[nt][coarseY * 32 + bgTileX];

        // 读取背景 pattern — 直接通过 chrPtrs 内联
        uint16_t bgPatAddr = bgPatternBase + tileIndex * 16 + fineYOffset;
        uint8_t bgPatLo = chrPtrs_sp0[(bgPatAddr >> 10) & 7][bgPatAddr & 0x3FF];
        uint16_t bgPatAddr8 = bgPatAddr + 8;
        uint8_t bgPatHi = chrPtrs_sp0[(bgPatAddr8 >> 10) & 7][bgPatAddr8 & 0x3FF];

        // 计算这个像素的背景颜色
        int shift = 7 - tileFineX;
        uint8_t bgPx = ((bgPatLo >> shift) & 1) | (((bgPatHi >> shift) & 1) << 1);

        // 背景非透明 + 精灵非透明 = Hit!
        if (bgPx != 0) {
            ppuStatus |= 0x40;
            return true;
        }
    }

    return false;
}

/**
 * OAM 预评估 — 每帧开始时调用一次
 *
 * 为每条扫描线预先构建精灵索引列表，消除 renderSpriteLine 中
 * 每行扫描全部 64 个精灵的开销 (240×64=15360 → 64+240×~4=~1024)
 *
 * 使用反向扫描(63→0)以保持与原渲染顺序一致：
 * 低索引精灵后渲染 = 高优先级 = 显示在最上面
 */
void IRAM_ATTR PPU::evaluateOAM() {
    memset(spriteCountPerLine, 0, 240);

    int spriteHeight = (ppuCtrl & 0x20) ? 16 : 8;

    // NES 硬件按 OAM 正序选择每条扫描线最前面的 8 个精灵。
    // 真正的绘制顺序在 renderSpriteLine 中反向处理，以保持低索引优先级最高。
    for (int i = 0; i < 64; i++) {
        uint8_t y = oam[i * 4];
        if (y >= 0xEF) continue;

        int spriteY = y + 1;
        int endY = spriteY + spriteHeight;
        if (endY > 240) endY = 240;
        if (spriteY < 0) spriteY = 0;

        for (int sl = spriteY; sl < endY; sl++) {
            if (spriteCountPerLine[sl] < MAX_SPRITES_PER_LINE) {
                spriteIndicesPerLine[sl][spriteCountPerLine[sl]++] = i;
            }
        }
    }
}

// ============================================================================
// 逐行扫描渲染 (Phase 1 优化 + Tile 级别加速)
// ============================================================================
// 将原来的整帧渲染改为逐行渲染，好处:
//   1. 减少重复计算 (tile row 在 8 行内相同)
//   2. 更好的缓存利用率
//   3. 支持 DMA 逐行输出
//
// 关键优化: 以 Tile (8 像素) 为单位渲染，而不是逐像素

/**
 * 渲染单条背景扫描线 (Tile 级别优化版)
 *
 * @param scanline 扫描线号 (0-239)
 * @param lineBuffer 输出缓冲区 (256 像素)
 *
 * 优化策略:
 *   - 每个 tile 只读取一次 nametable、attribute、pattern
 *   - 一次展开 8 个像素
 *   - 使用查表代替位运算
 *   - 记录背景像素不透明度用于 Sprite 0 hit 检测
 *
 * 滚动计算:
 *   使用 tempAddr (t寄存器) 作为滚动基准，结合 scanline 计算实际渲染位置
 *   这是因为游戏在 VBlank 时设置 PPUSCROLL，我们需要用那个值来渲染整帧
 *
 *   t 寄存器格式: 0yyy NNYY YYYX XXXX
 *   yyy = fine Y scroll (3 bit)
 *   NN = nametable select (2 bit)
 *   YYYYY = coarse Y scroll (5 bit)
 *   XXXXX = coarse X scroll (5 bit)
 */
void IRAM_ATTR PPU::renderBackgroundLine(int scanline, uint16_t* lineBuffer) {
    // 清除背景像素不透明度数组 (用于 Sprite 0 hit 检测)
    uint32_t* p = (uint32_t*)bgPixelOpacity;
    for (int i = 0; i < 256 / 4; i++) {
        p[i] = 0;
    }

    // 检查背景是否启用
    if (!(ppuMask & 0x08)) {
        // 背景禁用: 用 bgColor 填充 lineBuffer (精灵仍需要底色)
        if (lineBuffer) {
            uint16_t bgColor = bgPaletteCache[0];
            uint32_t bgColor32 = (bgColor << 16) | bgColor;
            uint32_t* lb32 = (uint32_t*)lineBuffer;
            for (int i = 0; i < 128; i++) { lb32[i] = bgColor32; }
        }
        return;
    }

    // Pattern Table 基地址 (由 PPUCTRL bit 4 决定)
    uint16_t patternBase = (ppuCtrl & 0x10) ? 0x1000 : 0x0000;

    // ========== 缓存指针本地化 (消除函数调用开销) ==========
    // 每条扫描线 ~33 tiles × 4 reads = ~132 function calls → 0 function calls
    uint8_t** ntPtrs = cartDirect->ntPtrs;
    uint8_t** chrPtrs = cartDirect->chrBankPtrs;

    // ========== 从 vramAddr 解析滚动位置 ==========
    int coarseX = vramAddr & 0x001F;
    int coarseY = (vramAddr >> 5) & 0x1F;
    int fineYOffset = (vramAddr >> 12) & 0x07;
    int baseNt = (vramAddr >> 10) & 0x03;
    int tileY = coarseY;

    // 背景色 (用于无分支写入)
    uint16_t bgColor = bgPaletteCache[0];

    int screenX = 0;
    // 增量追踪 coarseX 和 nametable，避免每个 tile 重新计算
    int curCoarseX = coarseX + (fineX >> 3);
    int curNt = baseNt;
    if (curCoarseX >= 32) { curCoarseX -= 32; curNt ^= 1; }

    for (int tileCount = 0; tileCount < 33 && screenX < 256; tileCount++) {
        // nametable 和 属性地址 — 直接通过 ntPtrs 内联访问
        int ntIdx = curNt;
        uint16_t tileOffset = tileY * 32 + curCoarseX;
        uint8_t tileIndex = ntPtrs[ntIdx][tileOffset];

        uint16_t attrOffset = 0x3C0 + (tileY >> 2) * 8 + (curCoarseX >> 2);
        uint8_t attrByte = ntPtrs[ntIdx][attrOffset];
        int attrShift = ((tileY & 0x02) << 1) | (curCoarseX & 0x02);
        uint8_t paletteNum = (attrByte >> attrShift) & 0x03;

        // 构建含背景色的调色板（index 0 = bgColor）
        uint16_t* srcPal = &bgPaletteCache[paletteNum << 2];
        uint16_t tilePal[4] = { bgColor, srcPal[1], srcPal[2], srcPal[3] };

        // 读取 Pattern 数据 — 直接通过 chrPtrs 内联访问
        uint16_t patAddr = patternBase + tileIndex * 16 + fineYOffset;
        uint8_t patLo = chrPtrs[(patAddr >> 10) & 7][patAddr & 0x3FF];
        uint8_t patHi = chrPtrs[((patAddr + 8) >> 10) & 7][(patAddr + 8) & 0x3FF];

        int startBit = (tileCount == 0) ? (fineX & 7) : 0;

        if ((patLo | patHi) == 0) {
            // 全透明 tile: 直接填充 bgColor + 清零 opacity
            int pixelsToRender = 8 - startBit;
            if (lineBuffer) {
                for (int k = 0; k < pixelsToRender && screenX + k < 256; k++) {
                    lineBuffer[screenX + k] = bgColor;
                }
            }
            screenX += pixelsToRender;
            if (screenX > 256) screenX = 256;
        } else {
            uint16_t packedPixels = bgPlaneLut[0][patLo] | bgPlaneLut[1][patHi];
            uint8_t p0 = packedPixels & 0x03;
            uint8_t p1 = (packedPixels >> 2) & 0x03;
            uint8_t p2 = (packedPixels >> 4) & 0x03;
            uint8_t p3 = (packedPixels >> 6) & 0x03;
            uint8_t p4 = (packedPixels >> 8) & 0x03;
            uint8_t p5 = (packedPixels >> 10) & 0x03;
            uint8_t p6 = (packedPixels >> 12) & 0x03;
            uint8_t p7 = (packedPixels >> 14) & 0x03;

            // 无分支像素写入: tilePal[0]=bgColor, 无需 if(px)
            if (startBit == 0 && screenX + 8 <= 256 && lineBuffer) {
                // 快速路径: 完整 tile，4 组 32-bit 写入
                uint32_t* out32 = (uint32_t*)(lineBuffer + screenX);
                out32[0] = (uint32_t)tilePal[p0] | ((uint32_t)tilePal[p1] << 16);
                out32[1] = (uint32_t)tilePal[p2] | ((uint32_t)tilePal[p3] << 16);
                out32[2] = (uint32_t)tilePal[p4] | ((uint32_t)tilePal[p5] << 16);
                out32[3] = (uint32_t)tilePal[p6] | ((uint32_t)tilePal[p7] << 16);
                // 设置不透明度 (无分支)
                uint8_t* op = bgPixelOpacity + screenX;
                op[0] = (p0 != 0);
                op[1] = (p1 != 0);
                op[2] = (p2 != 0);
                op[3] = (p3 != 0);
                op[4] = (p4 != 0);
                op[5] = (p5 != 0);
                op[6] = (p6 != 0);
                op[7] = (p7 != 0);
                screenX += 8;
            } else {
                // 慢速路径: 首/尾 partial tile
                for (int bit = startBit; bit < 8 && screenX < 256; bit++, screenX++) {
                    uint8_t px = (packedPixels >> (bit * 2)) & 0x03;
                    if (lineBuffer) {
                        lineBuffer[screenX] = tilePal[px];
                    }
                    bgPixelOpacity[screenX] = (px != 0);
                }
            }
        }

        // 增量推进 coarseX
        curCoarseX++;
        if (curCoarseX >= 32) {
            curCoarseX = 0;
            curNt ^= 1;
        }
    }

    if (!(ppuMask & 0x02)) {
        for (int x = 0; x < 8; x++) {
            bgPixelOpacity[x] = 0;
            if (lineBuffer) lineBuffer[x] = bgColor;
        }
    }
}


/**
 * 渲染单条精灵扫描线
 *
 * @param scanline 扫描线号 (0-239)
 * @param lineBuffer 输出缓冲区 (256 像素)
 *
 * 精灵 OAM 格式 (每个精灵 4 字节):
 *   Byte 0: Y 坐标 - 1
 *   Byte 1: Tile 索引
 *   Byte 2: 属性
 *     bit 7: 垂直翻转
 *     bit 6: 水平翻转
 *     bit 5: 优先级 (0=前景, 1=背景后面)
 *     bit 1-0: 调色板 (0-3)
 *   Byte 3: X 坐标
 *
 * Sprite 0 Hit 检测:
 *   当 Sprite 0 的非透明像素与背景非透明像素重叠时设置
 */
void IRAM_ATTR PPU::renderSpriteLine(int scanline, uint16_t* lineBuffer) {
    // 检查精灵是否启用
    if (!(ppuMask & 0x10)) return;

    // Pattern Table 基地址 (由 PPUCTRL bit 3 决定)
    uint16_t patternBase = (ppuCtrl & 0x08) ? 0x1000 : 0x0000;

    // ========== 缓存指针本地化 (消除 fastChrRead 函数调用) ==========
    uint8_t** chrPtrs = cartDirect->chrBankPtrs;

    // 8x16 模式? (PPUCTRL bit 5)
    bool is8x16 = (ppuCtrl & 0x20) != 0;
    int spriteHeight = is8x16 ? 16 : 8;

    // Sprite 0 hit 检测标志
    bool checkSprite0Hit = ((ppuStatus & 0x40) == 0);  // 只有未 hit 时才检测
    bool showLeftSprites = (ppuMask & 0x04) != 0;

    // 使用预评估的精灵列表 (evaluateOAM 已在帧开始时构建)
    // 列表按 OAM 正序保存，需要反向绘制，才能让低索引精灵最后覆盖到最上层。
    int count = spriteCountPerLine[scanline];

    for (int j = count - 1; j >= 0; j--) {
        int i = spriteIndicesPerLine[scanline][j];

        uint8_t y = oam[i * 4 + 0];
        uint8_t tileIndex = oam[i * 4 + 1];
        uint8_t attr = oam[i * 4 + 2];
        uint8_t x = oam[i * 4 + 3];

        int spriteY = y + 1;

        // 解析属性
        int paletteNum = attr & 0x03;
        bool flipH = (attr & 0x40) != 0;
        bool flipV = (attr & 0x80) != 0;
        bool behindBg = (attr & 0x20) != 0;

        int palOffset = paletteNum << 2;

        // 计算精灵行 (相对于精灵顶部)
        int row = scanline - spriteY;
        int patternRow = flipV ? (spriteHeight - 1 - row) : row;

        // ========== 计算 Tile 地址 ==========
        uint16_t tileAddr;
        if (is8x16) {
            // 8x16 模式: tile 索引的 bit 0 决定 pattern table
            uint16_t base = (tileIndex & 0x01) ? 0x1000 : 0x0000;
            uint8_t tile = tileIndex & 0xFE;  // 清除 bit 0

            // 上半部分还是下半部分?
            if (patternRow >= 8) {
                tile++;           // 下一个 tile
                patternRow -= 8;  // 调整行号
            }
            tileAddr = base + tile * 16 + patternRow;
        } else {
            // 8x8 模式
            tileAddr = patternBase + tileIndex * 16 + patternRow;
        }

        // ========== 读取 Pattern 数据 — 直接通过 chrPtrs 内联 ==========
        uint8_t patternLo = chrPtrs[(tileAddr >> 10) & 7][tileAddr & 0x3FF];
        uint16_t tileAddr8 = tileAddr + 8;
        uint8_t patternHi = chrPtrs[(tileAddr8 >> 10) & 7][tileAddr8 & 0x3FF];

        // 如果整行都是透明的，跳过
        if ((patternLo | patternHi) == 0) continue;

        // 获取调色板指针
        uint16_t* spPal = &spPaletteCache[palOffset];

        // ========== 预计算 8 个像素值 ==========
        uint8_t pixels[8];
        if (flipH) {
            pixels[0] = (patternLo & 1) | ((patternHi & 1) << 1);
            pixels[1] = ((patternLo >> 1) & 1) | (((patternHi >> 1) & 1) << 1);
            pixels[2] = ((patternLo >> 2) & 1) | (((patternHi >> 2) & 1) << 1);
            pixels[3] = ((patternLo >> 3) & 1) | (((patternHi >> 3) & 1) << 1);
            pixels[4] = ((patternLo >> 4) & 1) | (((patternHi >> 4) & 1) << 1);
            pixels[5] = ((patternLo >> 5) & 1) | (((patternHi >> 5) & 1) << 1);
            pixels[6] = ((patternLo >> 6) & 1) | (((patternHi >> 6) & 1) << 1);
            pixels[7] = ((patternLo >> 7) & 1) | (((patternHi >> 7) & 1) << 1);
        } else {
            pixels[0] = ((patternLo >> 7) & 1) | (((patternHi >> 7) & 1) << 1);
            pixels[1] = ((patternLo >> 6) & 1) | (((patternHi >> 6) & 1) << 1);
            pixels[2] = ((patternLo >> 5) & 1) | (((patternHi >> 5) & 1) << 1);
            pixels[3] = ((patternLo >> 4) & 1) | (((patternHi >> 4) & 1) << 1);
            pixels[4] = ((patternLo >> 3) & 1) | (((patternHi >> 3) & 1) << 1);
            pixels[5] = ((patternLo >> 2) & 1) | (((patternHi >> 2) & 1) << 1);
            pixels[6] = ((patternLo >> 1) & 1) | (((patternHi >> 1) & 1) << 1);
            pixels[7] = (patternLo & 1) | ((patternHi & 1) << 1);
        }

        // ========== 渲染 8 个像素 (减少分支) ==========
        // 同时检测 Sprite 0 hit
        if (i == 0 && checkSprite0Hit && (ppuMask & 0x08)) {
            // Sprite 0: 需要检测 hit
            for (int bit = 0; bit < 8; bit++) {
                int sx = x + bit;
                if (sx >= 255) continue;  // X=255 时不产生 hit
                if (sx < 8 && (!showLeftSprites || !(ppuMask & 0x02))) continue;

                if (pixels[bit]) {
                    // 检测 Sprite 0 hit (精灵和背景都非透明)
                    if (bgPixelOpacity[sx]) {
                        ppuStatus |= 0x40;  // 设置 Sprite 0 Hit
                        checkSprite0Hit = false;  // 只触发一次
                    }
                    // 渲染像素 (只有 lineBuffer 有效时)
                    if (lineBuffer && (!behindBg || !bgPixelOpacity[sx])) {
                        lineBuffer[sx] = spPal[pixels[bit]];
                    }
                }
            }
        } else if (lineBuffer) {
            // 只有 lineBuffer 有效时才渲染非 Sprite 0 精灵
            if (!behindBg) {
                // 前景精灵: 完全在屏内时走无边界检查快路径，减轻多对象场景分支开销。
                if (x <= 248 && (showLeftSprites || x >= 8)) {
                    if (pixels[0]) lineBuffer[x + 0] = spPal[pixels[0]];
                    if (pixels[1]) lineBuffer[x + 1] = spPal[pixels[1]];
                    if (pixels[2]) lineBuffer[x + 2] = spPal[pixels[2]];
                    if (pixels[3]) lineBuffer[x + 3] = spPal[pixels[3]];
                    if (pixels[4]) lineBuffer[x + 4] = spPal[pixels[4]];
                    if (pixels[5]) lineBuffer[x + 5] = spPal[pixels[5]];
                    if (pixels[6]) lineBuffer[x + 6] = spPal[pixels[6]];
                    if (pixels[7]) lineBuffer[x + 7] = spPal[pixels[7]];
                } else {
                    int sx = x;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[0]) lineBuffer[sx] = spPal[pixels[0]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[1]) lineBuffer[sx] = spPal[pixels[1]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[2]) lineBuffer[sx] = spPal[pixels[2]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[3]) lineBuffer[sx] = spPal[pixels[3]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[4]) lineBuffer[sx] = spPal[pixels[4]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[5]) lineBuffer[sx] = spPal[pixels[5]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[6]) lineBuffer[sx] = spPal[pixels[6]]; sx++;
                    if (sx < 256 && (showLeftSprites || sx >= 8) && pixels[7]) lineBuffer[sx] = spPal[pixels[7]];
                }
            } else {
                // 背景后精灵: 只在背景透明处渲染
                for (int bit = 0; bit < 8; bit++) {
                    int sx = x + bit;
                    if (sx < 8 && !showLeftSprites) continue;
                    if (sx < 256 && pixels[bit] && !bgPixelOpacity[sx]) {
                        lineBuffer[sx] = spPal[pixels[bit]];
                    }
                }
            }
        }
    }
}

// ============================================================================
// 公共渲染接口
// ============================================================================

/**
 * 渲染单条扫描线 (供 DMA 输出使用)
 *
 * @param scanline 扫描线号 (0-239)
 * @param lineBuffer 输出缓冲区 (256 像素)，如果为 nullptr 则只执行 Sprite 0 Hit 检测
 */
void IRAM_ATTR PPU::renderLine(int scanline, uint16_t* lineBuffer) {
    // 检查渲染是否启用 (背景或精灵)
    bool renderingEnabled = (ppuMask & 0x18) != 0;

    if (scanline == 0) {
        // 第一行时加载调色板缓存
        loadPaletteCache();
        // 帧开始时，复制 tempAddr 的完整滚动值到 vramAddr
        if (renderingEnabled) {
            vramAddr = tempAddr;
        }
        // 预评估 OAM: 构建每条扫描线的精灵索引列表
        evaluateOAM();
    } else {
        // 每条扫描线开始时，同步 coarse X（模拟 PPU 行开始行为）
        if (renderingEnabled) {
            vramAddr = (vramAddr & ~0x041F) | (tempAddr & 0x041F);
        }
    }

    // 背景色填充已移至 renderBackgroundLine 内部（无分支写入）
    // 不再需要单独的 bgColor 填充 pass

    // 渲染背景 (即使 lineBuffer 为 nullptr 也会填充 bgPixelOpacity)
    renderBackgroundLine(scanline, lineBuffer);

    // 渲染精灵 (即使 lineBuffer 为 nullptr 也会检测 Sprite 0 Hit)
    renderSpriteLine(scanline, lineBuffer);

    // 渲染后递增 Y 滚动（模拟 PPU 行结束行为）
    incrementY();
}

/**
 * 渲染整帧
 * 这是主渲染入口点，由 main.cpp 每帧调用
 *
 * @param fb 帧缓冲区 (256 x 240 像素, RGB565 格式)
 *
 * 渲染流程:
 *   1. 加载调色板缓存
 *   2. 填充背景色
 *   3. 逐行渲染背景
 *   4. 逐行渲染精灵
 *   5. 设置 VBlank 标志
 *
 * 注意: 滚动值完全由 tempAddr 和 fineX 决定
 *       游戏在 VBlank 时设置 PPUSCROLL，我们直接使用那些值
 */
void PPU::render(uint16_t* fb) {

    // ========== 1. 加载调色板缓存 ==========
    loadPaletteCache();

    // ========== 2. 填充背景色 ==========
    uint16_t bgColor = bgPaletteCache[0];

    // 使用 32 位写入加速填充 (一次写 2 个像素)
    uint32_t bgColor32 = (bgColor << 16) | bgColor;
    uint32_t* fb32 = (uint32_t*)fb;
    int count32 = (256 * 240) / 2;
    for (int i = 0; i < count32; i++) {
        fb32[i] = bgColor32;
    }

    // 清除 Sprite 0 Hit 标志 (每帧开始时清除)
    ppuStatus &= ~0x40;

    // ========== 3 & 4. 逐行渲染 ==========
    for (int sl = 0; sl < 240; sl++) {
        uint16_t* lineBuffer = fb + sl * 256;

        // 渲染背景 (使用 tempAddr + fineX + scanline 计算滚动)
        renderBackgroundLine(sl, lineBuffer);

        // 渲染精灵
        renderSpriteLine(sl, lineBuffer);
    }

    // ========== 5. 设置 VBlank 标志 ==========
    ppuStatus |= 0x80;

    frameCount++;
}

// ============================================================================
// PPU 时序模拟 (Phase 4 优化)
// ============================================================================
// 每个 CPU 周期调用 3 次 (PPU 时钟 = CPU 时钟 x 3)
//
// 扫描线分布 (NTSC):
//   0-239:   可见扫描线
//   240:     Post-render 空闲扫描线
//   241:     VBlank 开始 (dot 1 设置 VBlank 标志)
//   242-260: VBlank 期间
//   261:     Pre-render 扫描线 (清除 VBlank 和 Sprite0 Hit)
//
// 每条扫描线 341 个 dot (0-340):
//   0:       空闲周期 (奇数帧跳过)
//   1-256:   渲染像素
//   257-320: 精灵取址
//   321-336: 下一扫描线背景预取
//   337-340: 未使用的取址

/**
 * PPU 单 dot 步进
 * 模拟 PPU 时序，处理 VBlank、NMI、Sprite0 Hit 等事件
 * 每个 CPU 周期调用 3 次 (PPU 时钟 = CPU 时钟 x 3)
 */
void IRAM_ATTR PPU::stepPPU() {
    // ========== 可见扫描线 (0-239) ==========
    if (scanline < 240) {
        // 在 dot 0 时检查 Sprite 0 Hit 可能性
        if (dot == 0) {
            // 检查 Sprite 0 是否在屏幕上可见
            uint8_t sprite0Y = oam[0];
            sprite0HitPossible = (sprite0Y < 0xEF) && (sprite0Y + 1 <= scanline) &&
                                  (scanline < sprite0Y + 1 + ((ppuCtrl & 0x20) ? 16 : 8));
            sprite0Rendered = false;
        }

        // Sprite 0 Hit 检测 (简化版，在背景和精灵都渲染的位置)
        // 真正的实现需要逐像素检测，这里为了性能使用简化版本
        if (sprite0HitPossible && dot >= 2 && dot <= 254 && !sprite0Rendered) {
            // 检查渲染是否启用
            if ((ppuMask & 0x18) == 0x18) {  // 背景和精灵都启用
                // 检查 Sprite 0 的 X 坐标是否在当前 dot
                uint8_t sprite0X = oam[3];
                if (dot >= sprite0X + 1 && dot < sprite0X + 9) {
                    // 简化: 假设 hit 发生 (实际需要检查非透明像素)
                    ppuStatus |= 0x40;  // 设置 Sprite 0 Hit
                    sprite0Rendered = true;
                }
            }
        }
    }

    // ========== VBlank 扫描线 (241) ==========
    if (scanline == 241 && dot == 1) {
        // 设置 VBlank 标志
        ppuStatus |= 0x80;
        nmiOccurred = true;

        // 如果 NMI 使能，触发 NMI
        if (ppuCtrl & 0x80) {
            nmiPending = true;
        }
    }

    // ========== Pre-render 扫描线 (261) ==========
    if (scanline == 261) {
        if (dot == 1) {
            // 清除 VBlank 标志、Sprite 0 Hit 和 Overflow
            ppuStatus &= 0x1F;  // 清除 bit 5, 6, 7
            nmiOccurred = false;
            nmiPending = false;
        }

        // 在渲染启用时，在 dot 280-304 期间复制垂直滚动
        if ((ppuMask & 0x18) && dot >= 280 && dot <= 304) {
            // 复制 t 的垂直位到 v
            // v: 0yyy NNYY YYYX XXXX
            // 复制: yyy, NY, YYYYY
            vramAddr = (vramAddr & 0x041F) | (tempAddr & 0x7BE0);
        }
    }

    // ========== Dot 和 Scanline 递增 ==========
    dot++;

    // 每条扫描线 341 个 dot (0-340)
    if (dot > 340) {
        dot = 0;
        scanline++;

        // 在可见扫描线 (0-239) 结束时，复制水平滚动
        if (scanline <= 240 && (ppuMask & 0x18)) {
            // 复制 t 的水平位到 v
            vramAddr = (vramAddr & 0xFBE0) | (tempAddr & 0x041F);
        }

        // 每帧 262 条扫描线 (0-261)
        if (scanline > 261) {
            scanline = 0;
            frameCount++;
            oddFrame = !oddFrame;

            // 奇数帧跳过 (0, 0) 周期
            if (oddFrame && (ppuMask & 0x18)) {
                dot = 1;
            }
        }
    }
}

// ============================================================================
// 批量 PPU 推进 (高性能版本 v2)
// ============================================================================
/**
 * 批量推进 PPU 时钟 - 极简快速路径优化
 *
 * 优化策略:
 *   1. 快速路径: 大多数调用不跨扫描线，直接累加
 *   2. 惰性检查: 只在跨扫描线时检查事件
 *   3. 预计算: Sprite 0 Hit 扫描线在帧开始时计算一次
 *
 * @param ppuCycles 要推进的 PPU 周期数 (通常是 CPU 周期 * 3)
 */
void IRAM_ATTR PPU::advanceCycles(int cycles) {
    while (cycles > 0) {

        // =====================================================
        // 计算本次最多能推进多少 PPU cycle
        // =====================================================
        int toLineEnd = 341 - ppuCycle;
        int step = (cycles < toLineEnd) ? cycles : toLineEnd;

        int oldCycle = ppuCycle;
        ppuCycle += step;
        cycles -= step;

        // =====================================================
        // 可见扫描线：在 dot == 1 时渲染整行
        // =====================================================
        if (ppuScanline < 240 && frameBuffer) {
            if (oldCycle < 1 && ppuCycle >= 1) {

                // 第 0 行，帧开始初始化
                if (ppuScanline == 0) {
                    loadPaletteCache();
                    // 新帧开始，重置渲染标志
                    renderedThisFrame = false;
                }

                uint16_t* line = frameBuffer + ppuScanline * 256;

                // 总是更新调色板缓存（需要在跳帧后恢复）
                // 背景清色（快速填写）
                uint16_t bgColor = bgPaletteCache[0];
                uint32_t bg32 = (bgColor << 16) | bgColor;
                uint32_t* p32 = (uint32_t*)line;
                for (int i = 0; i < 128; i++) p32[i] = bg32;

                // 如果启用渲染则执行完整渲染，否则跳过昂贵的 tile/sprite 解码
                if (renderEnabled) {
                    // 背景
                    if (ppuMask & 0x08) {
                        renderBackgroundLine(ppuScanline, line);
                    }

                    // 精灵（含 Sprite0 hit）
                    if (ppuMask & 0x10) {
                        renderSpriteLine(ppuScanline, line);
                    }
                    // 标记本帧已执行实际渲染
                    renderedThisFrame = true;
                } else {
                    // 渲染被禁用：保留背景清色，跳过细粒度渲染
                    // 这样可以在跳帧模式下快速前进时序，同时保持可控的输出内容
                }
            }

            // cycle 256: 递增 Y 滚动（真实 PPU 行为）
            if (oldCycle < 256 && ppuCycle >= 256 && (ppuMask & 0x18)) {
                // 递增 fine Y
                if ((vramAddr & 0x7000) != 0x7000) {
                    vramAddr += 0x1000;
                } else {
                    // fine Y 溢出，重置并递增 coarse Y
                    vramAddr &= ~0x7000;
                    int coarseY = (vramAddr & 0x03E0) >> 5;
                    if (coarseY == 29) {
                        // coarse Y 溢出（到达 nametable 底部）
                        coarseY = 0;
                        vramAddr ^= 0x0800;  // 切换 nametable Y
                    } else if (coarseY == 31) {
                        // 异常情况：coarse Y 在属性表区域
                        coarseY = 0;
                    } else {
                        coarseY++;
                    }
                    vramAddr = (vramAddr & ~0x03E0) | (coarseY << 5);
                }
            }

            // cycle 257: 复制水平位从 tempAddr 到 vramAddr
            if (oldCycle < 257 && ppuCycle >= 257 && (ppuMask & 0x18)) {
                // 复制水平位：coarse X (bits 0-4) 和 nametable X (bit 10)
                vramAddr = (vramAddr & ~0x041F) | (tempAddr & 0x041F);
            }
        }

        // =====================================================
        // VBlank 开始：241, dot 1
        // =====================================================
        if (ppuScanline == 241) {
            if (oldCycle < 1 && ppuCycle >= 1) {
                ppuStatus |= 0x80;
                frameReady = true;
                sprite0HitThisFrame = false;

                if (ppuCtrl & 0x80) {
                    nmiPending = true;
                }
            }
        }

        // =====================================================
        // Pre-render line：261
        // =====================================================
        if (ppuScanline == 261) {
            if (oldCycle < 1 && ppuCycle >= 1) {
                ppuStatus &= ~0x80; // clear VBlank
                ppuStatus &= ~0x40; // clear sprite0 hit
                ppuStatus &= ~0x20; // clear overflow

                // ⭐ 关键：帧开始时同步完整 VRAM 地址
                //vramAddr = tempAddr;
            }

            // cycle 280-304: 复制垂直位从 tempAddr 到 vramAddr
            // 这是真实 PPU 的行为，在帧开始前恢复 Y 滚动
            if (ppuCycle >= 280 && ppuCycle <= 304 && (ppuMask & 0x18)) {
                // 复制垂直位：coarse Y (bits 5-9), fine Y (bits 12-14), nametable Y (bit 11)
                vramAddr = (vramAddr & ~0x7BE0) | (tempAddr & 0x7BE0);
            }
        }

        // =====================================================
        // 行结束
        // =====================================================
        if (ppuCycle >= 341) {
            ppuCycle = 0;
            ppuScanline++;

            // MMC3 IRQ（可见行 + 渲染开启）
            if (ppuScanline < 240 && (ppuMask & 0x18)) {
                if (cartDirect) {
                    cartDirect->clockIrqCounter();
                }
            }

            // 帧结束
            if (ppuScanline >= 262) {
                ppuScanline = 0;
                frameCount++;
                oddFrame = !oddFrame;

                // odd frame skip
                if (oddFrame && (ppuMask & 0x08)) {
                    ppuCycle = 1;
                }
            }
        }
    }
}


// ============================================================================
// Save State
// ============================================================================

size_t PPU::getStateSize() const {
    size_t size = 0;
    // 寄存器
    size += sizeof(ppuCtrl);
    size += sizeof(ppuMask);
    size += sizeof(ppuStatus);
    size += sizeof(oamAddr);
    // 内部寄存器
    size += sizeof(vramAddr);
    size += sizeof(tempAddr);
    size += sizeof(fineX);
    size += sizeof(writeToggle);
    size += sizeof(dataBuffer);
    // OAM
    size += sizeof(oam);
    // 时序状态
    size += sizeof(scanline);
    size += sizeof(dot);
    size += sizeof(oddFrame);
    size += sizeof(nmiOccurred);
    size += sizeof(nmiPending);
    size += sizeof(frameCount);
    // Sprite 0 hit
    size += sizeof(sprite0HitPossible);
    size += sizeof(sprite0Rendered);
    return size;
}

void PPU::saveState(uint8_t* buf, size_t& offset) const {
    // 寄存器
    buf[offset++] = ppuCtrl;
    buf[offset++] = ppuMask;
    buf[offset++] = ppuStatus;
    buf[offset++] = oamAddr;

    // 内部寄存器
    buf[offset++] = vramAddr & 0xFF;
    buf[offset++] = (vramAddr >> 8) & 0xFF;
    buf[offset++] = tempAddr & 0xFF;
    buf[offset++] = (tempAddr >> 8) & 0xFF;
    buf[offset++] = fineX;
    buf[offset++] = writeToggle ? 1 : 0;
    buf[offset++] = dataBuffer;

    // OAM
    memcpy(buf + offset, oam, sizeof(oam));
    offset += sizeof(oam);

    // 时序状态
    buf[offset++] = scanline & 0xFF;
    buf[offset++] = (scanline >> 8) & 0xFF;
    buf[offset++] = dot & 0xFF;
    buf[offset++] = (dot >> 8) & 0xFF;
    buf[offset++] = oddFrame ? 1 : 0;
    buf[offset++] = nmiOccurred ? 1 : 0;
    buf[offset++] = nmiPending ? 1 : 0;

    // 帧计数 (4 字节)
    buf[offset++] = frameCount & 0xFF;
    buf[offset++] = (frameCount >> 8) & 0xFF;
    buf[offset++] = (frameCount >> 16) & 0xFF;
    buf[offset++] = (frameCount >> 24) & 0xFF;

    // Sprite 0 hit
    buf[offset++] = sprite0HitPossible ? 1 : 0;
    buf[offset++] = sprite0Rendered ? 1 : 0;
}

void PPU::loadState(const uint8_t* buf, size_t& offset) {
    // 寄存器
    ppuCtrl = buf[offset++];
    ppuMask = buf[offset++];
    ppuStatus = buf[offset++];
    oamAddr = buf[offset++];

    // 内部寄存器
    vramAddr = buf[offset] | (buf[offset + 1] << 8);
    offset += 2;
    tempAddr = buf[offset] | (buf[offset + 1] << 8);
    offset += 2;
    fineX = buf[offset++];
    writeToggle = buf[offset++] != 0;
    dataBuffer = buf[offset++];

    // OAM
    memcpy(oam, buf + offset, sizeof(oam));
    offset += sizeof(oam);

    // 时序状态
    scanline = buf[offset] | (buf[offset + 1] << 8);
    offset += 2;
    dot = buf[offset] | (buf[offset + 1] << 8);
    offset += 2;
    oddFrame = buf[offset++] != 0;
    nmiOccurred = buf[offset++] != 0;
    nmiPending = buf[offset++] != 0;

    // 帧计数
    frameCount = buf[offset] | (buf[offset + 1] << 8) |
                 (buf[offset + 2] << 16) | (buf[offset + 3] << 24);
    offset += 4;

    // Sprite 0 hit
    sprite0HitPossible = buf[offset++] != 0;
    sprite0Rendered = buf[offset++] != 0;

    // 刷新调色板缓存
    loadPaletteCache();
    invalidateTileCache();
}
