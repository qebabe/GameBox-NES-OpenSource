#include "nes.h"
#include "storage.h"

bool NES::loadROM(const char* path) {
    flushBatterySave(true);
    cpu.connect(this);
    ppu.connect(this);

    if (!path || !cart.load(path)) {
        return false;
    }

    strncpy(currentRomPath, path, sizeof(currentRomPath) - 1);
    currentRomPath[sizeof(currentRomPath) - 1] = '\0';
    loadBatterySave();
    return true;
}

bool NES::loadBatterySave() {
    if (!cart.hasSRAM() || currentRomPath[0] == '\0') {
        return true;
    }

    char path[160];
    if (!romStorageMakeBatterySavePath(currentRomPath, path, sizeof(path))) {
        Serial.println("BatterySave: Path is too long");
        return false;
    }
    if (!dijiStoragePathExists(path)) {
        Serial.printf("BatterySave: No existing save for %s\n", currentRomPath);
        return true;
    }
    File file = dijiOpenStoragePath(path, FILE_READ);
    if (!file) {
        Serial.printf("BatterySave: No existing save for %s\n", currentRomPath);
        return true;
    }
    size_t expected = cart.getSRAMSize();
    if (file.size() != expected) {
        Serial.printf("BatterySave: Invalid size %u (expected %u)\n",
                      (unsigned)file.size(), (unsigned)expected);
        file.close();
        return false;
    }
    size_t read = file.read(cart.getSRAM(), expected);
    file.close();
    if (read != expected || !cart.loadSRAM(cart.getSRAM(), expected)) {
        Serial.println("BatterySave: Failed to load SRAM");
        cart.clearSRAM();
        return false;
    }
    Serial.printf("BatterySave: Loaded %s\n", path);
    return true;
}

bool NES::flushBatterySave(bool force) {
    if (!cart.hasSRAM() || currentRomPath[0] == '\0' ||
        (!force && !cart.isSramDirty())) {
        return true;
    }

    char path[160];
    char tempPath[168];
    char backupPath[168];
    int tempLen = 0;
    int backupLen = 0;
    if (!romStorageMakeBatterySavePath(currentRomPath, path, sizeof(path)) ||
        (tempLen = snprintf(tempPath, sizeof(tempPath), "%s.tmp", path)) <= 0 ||
        (size_t)tempLen >= sizeof(tempPath) ||
        (backupLen = snprintf(backupPath, sizeof(backupPath), "%s.bak", path)) <= 0 ||
        (size_t)backupLen >= sizeof(backupPath)) {
        Serial.println("BatterySave: Path is too long");
        return false;
    }

    dijiEnsureSaveDirectory(path);
    dijiRemoveStoragePath(tempPath);
    File file = dijiOpenStoragePath(tempPath, FILE_WRITE);
    if (!file) {
        Serial.printf("BatterySave: Failed to create %s\n", tempPath);
        return false;
    }
    size_t size = cart.getSRAMSize();
    size_t written = file.write(cart.getSRAM(), size);
    file.close();
    if (written != size) {
        dijiRemoveStoragePath(tempPath);
        Serial.println("BatterySave: Incomplete write");
        return false;
    }
    dijiRemoveStoragePath(backupPath);
    bool hadPrevious = dijiStoragePathExists(path);
    if (hadPrevious && !dijiRenameStoragePath(path, backupPath)) {
        dijiRemoveStoragePath(tempPath);
        Serial.println("BatterySave: Failed to preserve previous save");
        return false;
    }
    if (!dijiRenameStoragePath(tempPath, path)) {
        if (hadPrevious) dijiRenameStoragePath(backupPath, path);
        dijiRemoveStoragePath(tempPath);
        Serial.println("BatterySave: Failed to install save");
        return false;
    }
    if (hadPrevious) dijiRemoveStoragePath(backupPath);
    cart.clearSramDirty();
    Serial.printf("BatterySave: Saved %s\n", path);
    return true;
}

void NES::reset() {
    memset(ram, 0, sizeof(ram));
    memset(vram, 0, sizeof(vram));
    memset(palette, 0, sizeof(palette));
    controller[0] = controller[1] = 0;
    controllerLatch[0] = controllerLatch[1] = 0;
    controllerShift[0] = controllerShift[1] = 0;
    controllerStrobe = false;

    // 缓存镜像模式
    mirrorVertical = cart.getMirrorVertical();

    // 让 Cartridge 知道 VRAM 指针以及绑定 NES（用于 MMC3 动态镜像与 IRQ）
    cart.setVramPointer(vram);
    cart.setNES(this);

    // 给 PPU 设置直接内存访问指针
    ppu.setMemoryPointers(vram, palette, &cart, &mirrorVertical);

    cpu.reset();
    ppu.reset();
    apu.reset();
}

/**
 * NES::step()
 * ----------------------------------------------------------------------------
 * 标准 CPU 指令级驱动：
 *   - 执行 1 条 CPU 指令
 *   - 完整推进 cpuCycles * 3 个 PPU cycle
 *   - 不在扫描线边界提前返回
 *
 * 特点：
 *   - instruction-accurate
 *   - cycle-accurate（CPU:PPU = 1:3）
 *   - 性能最慢，但语义最“正统”
 */
uint8_t IRAM_ATTR NES::step() {
    // 1. 执行一条 CPU 指令
    uint8_t cpuCycles = cpu.step();

    // 2. 将 CPU 周期转换为 PPU 周期
    int ppuCycles = cpuCycles * 3;

    // 3. 行级推进 PPU：在跨越扫描线末尾时提前返回
    while (ppuCycles > 0) {
        int dot = ppu.getCurrentDot();           // 当前扫描线内的 dot
        int remainInLine = 341 - dot;            // 当前扫描线剩余 dot 数
        int step = (ppuCycles < remainInLine) ? ppuCycles : remainInLine;

        // 推进 PPU step 个周期
        ppu.advanceCycles(step);

        // 扣掉已推进的 PPU 周期
        ppuCycles -= step;

        // 如果推进到扫描线末尾，提前返回，让 CPU 有机会下一次继续
        if (step >= remainInLine) {
            break;
        }
    }

    // 4. 检查 NMI（VBlank + NMI 使能）
    if (ppu.isNmiPending()) {
        ppu.clearNmiPending();
        cpu.nmi();
    }

    // 5. 检查 Mapper IRQ
    if (cart.irqPending()) {
        cart.acknowledgeIrq();
        cpu.irq();
    }

    // 6. 返回 CPU 周期
    return cpuCycles;
}

/**
 * NES::stepScanline()
 * ----------------------------------------------------------------------------
 * 【粒度：单条扫描线】
 *
 * 执行“正好一条扫描线”所需的 CPU 周期：
 *   - 使用 113 / 114 CPU 周期交替，近似 113.666...
 *   - 每执行一条 CPU 指令，就同步推进 PPU（×3）
 *   - 在过程中实时响应：
 *       - NMI（VBlank）
 *       - Mapper IRQ（如 MMC3）
 *
 * 用途：
 *   - 行级精度（Sprite 0 Hit、MMC3 IRQ）
 *   - 比 step() 快得多，但仍保持行对齐
 *
 * 特点：
 *   ✅ 行级准确
 *   ✅ 性能与准确度的平衡点
 *   ❌ 仍然有一定函数调用开销
 */
void IRAM_ATTR NES::stepScanline() {
    // =期数在真实 NES 上为约 113.666...
    // 使用 113/114 交替近似
    int target = scanlineParity ? 114 : 113;
    int executed = 0;

    while (executed < target) {
        uint8_t c = cpu.step();
        executed += c;
        // 推进 PPU（CPU 周期 ×3）
        ppu.advanceCycles(c * 3);

        // 检查 NMI
        if (ppu.isNmiPending()) {
            ppu.clearNmiPending();
            cpu.nmi();
        }

        // MMC3 IRQ 检测
        if (cart.irqPending()) {
            cart.acknowledgeIrq();
            cpu.irq();
        }
    }

    // 切换用于下一行的周期数（113/114 交替）
    scanlineParity = !scanlineParity;
}


// void IRAM_ATTR NES::stepThreeScanlines() {
//     // Batch 3 scanlines to reduce call overhead (matches Anemoia pattern)
//     stepScanline();
//     stepScanline();
//     stepScanline();
// }

/**
 * NES::clock(bool skipRender)
 * ----------------------------------------------------------------------------
 * 【粒度：整帧（Frame-based 调度）】
 *
 * 高性能帧调度器（Anemoia 风格）：
 *   - 可见区：0–239 扫描线
 *       * 每 3 行为一组：113 + 114 + 114 CPU 周期
 *       * 行开始时触发 MMC3 IRQ
 *       * 按需调用 renderLine（支持 frameskip）
 *
 *   - Post-render 行（240）：113 CPU 周期
 *
 *   - VBlank（241–260）：
 *       * 设置 VBlank 标志
 *       * 触发 NMI（如果使能）
 *       * 一次性执行 2501 CPU 周期
 *
 *   - Pre-render 行（261）：
 *       * 清除 VBlank / Sprite 0 Hit
 *
 * 用途：
 *   - 实际跑游戏的主循环
 *   - 支持 frameskip / 抽帧
 *   - 在 ESP32 上追求最高 FPS
 *
 * 特点：
 *   ✅ 性能最好
 *   ✅ 与主流模拟器策略一致
 *   ❌ 不再是“指令级精确”，而是“行/帧级正确”
 *
 * 设计取舍：
 *   - MMC3 IRQ / Sprite 0 Hit：行级正确即可
 *   - CPU/PPU cycle 精度：为性能让步
 */
void IRAM_ATTR NES::clock() {
    // 自适应抽帧：仅在主循环显式请求时才跳过当前帧渲染，
    // 避免和游戏自身的闪烁节奏锁相，导致角色长期不可见。
    bool skipRender = frameskipEnabled && skipNextFrame;
    skipNextFrame = false;

    // 获取 Sprite 0 的 Y 范围（用于优化跳帧检测）
    int sprite0StartY = -1, sprite0EndY = -1;
    bool needSprite0Check = false;

    if (skipRender && ((ppu.getPpuMask() & 0x18) == 0x18)) {
        // 跳帧 + 背景和精灵都启用，需要检测 Sprite 0 Hit
        ppu.getSprite0YRange(sprite0StartY, sprite0EndY);
        // 只有 Sprite 0 在可见区域内才需要检测
        needSprite0Check = (sprite0StartY >= 0 && sprite0StartY < 240 && sprite0EndY > 0);
    }

    // 关键：跳帧时也必须初始化 PPU 帧状态（调色板 + vramAddr = tempAddr）
    // MMC3 游戏依赖正确的滚动状态来触发 IRQ 和 bank 切换
    if (skipRender) {
        ppu.initFrameForSprite0Check();
    }

    // 可见扫描线 0-239 (每 3 行一组)
    // 时序顺序: PPU渲染 → IRQ时钟(扫描线末尾) → IRQ分发 → CPU执行
    // 这确保 IRQ handler 在 cpu.clock() 中执行，其滚动/bank 修改
    // 能在下一条扫描线的 renderLine() 中生效
    for (int scanline = 0; scanline < 240; scanline += 3) {
        // 行 0
        if (!skipRender) {
            ppu.renderLine(scanline, ppu.frameBuffer + scanline * 256);
        } else {
            if (needSprite0Check && !(ppu.getPpuStatus() & 0x40) &&
                scanline >= sprite0StartY && scanline < sprite0EndY) {
                ppu.checkSprite0HitFast(scanline);
            }
            ppu.skipScanlineForScrollUpdate();
        }
        // MMC3 IRQ: 只在 PPU 渲染启用时时钟计数器 (A12 需要渲染活动才会翻转)
        // 但 pending IRQ 无论渲染状态都应被 CPU 接收
        if (ppu.getPpuMask() & 0x18) cart.clockIrqCounter();
        if (cart.irqPending()) cpu.irq();
        cpu.clock(113);

        // 行 1
        if (!skipRender) {
            ppu.renderLine(scanline + 1, ppu.frameBuffer + (scanline + 1) * 256);
        } else {
            if (needSprite0Check && !(ppu.getPpuStatus() & 0x40) &&
                (scanline + 1) >= sprite0StartY && (scanline + 1) < sprite0EndY) {
                ppu.checkSprite0HitFast(scanline + 1);
            }
            ppu.skipScanlineForScrollUpdate();
        }
        if (ppu.getPpuMask() & 0x18) cart.clockIrqCounter();
        if (cart.irqPending()) cpu.irq();
        cpu.clock(114);

        // 行 2
        if (!skipRender) {
            ppu.renderLine(scanline + 2, ppu.frameBuffer + (scanline + 2) * 256);
        } else {
            if (needSprite0Check && !(ppu.getPpuStatus() & 0x40) &&
                (scanline + 2) >= sprite0StartY && (scanline + 2) < sprite0EndY) {
                ppu.checkSprite0HitFast(scanline + 2);
            }
            ppu.skipScanlineForScrollUpdate();
        }
        if (ppu.getPpuMask() & 0x18) cart.clockIrqCounter();
        if (cart.irqPending()) cpu.irq();
        cpu.clock(114);
    }

    // 扫描线 240: Post-render (113 CPU 周期)
    cpu.clock(113);

    // 扫描线 241-260: VBlank
    ppu.setVBlank(true);
    if (ppu.nmiEnabled()) {
        cpu.nmi();
    }
    cpu.clock(2274);  // VBlank 期间的 CPU 周期 (20 scanlines × ~113.67)

    // 扫描线 261: Pre-render
    ppu.setVBlank(false);
    ppu.clearSprite0Hit();  // 清除 Sprite 0 Hit
    // Pre-render 扫描线也触发 MMC3 IRQ 时钟 (A12 脉冲)
    if (ppu.getPpuMask() & 0x18) cart.clockIrqCounter();
    if (cart.irqPending()) cpu.irq();
    cpu.clock(114);

    // 设置帧完成标志
    ppu.frameReady = true;
    ppu.renderedThisFrame = !skipRender;

}

/**
 * 渲染单条扫描线 (用于 DMA 逐行输出)
 */
void NES::renderLine(int scanline, uint16_t* lineBuffer) {
    ppu.renderLine(scanline, lineBuffer);
}

void NES::render(uint16_t* fb) {
    ppu.render(fb);
}

void NES::endFrame() {
    // 精确时序模式下，NMI 由 PPU::advanceCycles() 在正确时刻触发
    // 此函数保留用于兼容性，但不再需要手动触发 NMI
}

void NES::setController(uint8_t id, uint8_t state) {
    if (id < 2) {
        controller[id] = state;
    }
}

// ==================== CPU 总线 ====================
uint8_t IRAM_ATTR NES::cpuRead(uint16_t addr) {
    if (addr < 0x2000) {
        // $0000-$1FFF: 2KB RAM (镜像 4 次)
        return ram[addr & 0x07FF];
    }
    else if (addr < 0x4000) {
        // $2000-$3FFF: PPU 寄存器 (8 字节，镜像)
        return ppu.regRead(addr & 0x0007);
    }
    else if (addr == 0x4016) {
        // 控制器 1
        if (controllerStrobe) {
            return 0x40 | (controller[0] & 0x01);
        }
        uint8_t bit = (controllerLatch[0] >> controllerShift[0]) & 0x01;
        if (controllerShift[0] < 8) controllerShift[0]++;
        return 0x40 | bit;
    }
    else if (addr == 0x4017) {
        // 控制器 2
        if (controllerStrobe) {
            return 0x40 | (controller[1] & 0x01);
        }
        uint8_t bit = (controllerLatch[1] >> controllerShift[1]) & 0x01;
        if (controllerShift[1] < 8) controllerShift[1]++;
        return 0x40 | bit;
    }
    else if (addr < 0x4020) {
        // $4000-$401F: APU 和其他 I/O
        // 转发到 APU 寄存器读取
        return apu.regRead(addr);
    }
    else if (addr >= 0x6000) {
        // $6000-$FFFF: Cartridge (SRAM $6000-$7FFF + PRG ROM $8000-$FFFF)
        return cart.cpuRead(addr);
    }
    return 0;
}

void IRAM_ATTR NES::cpuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        // $0000-$1FFF: 2KB RAM (镜像 4 次)
        ram[addr & 0x07FF] = val;
    }
    else if (addr < 0x4000) {
        // $2000-$3FFF: PPU 寄存器 (8 字节，镜像)
        ppu.regWrite(addr & 0x0007, val);
    }
    else if (addr == 0x4014) {
        // OAM DMA
        ppu.oamDMA(val, ram);
    }
    else if (addr == 0x4016) {
        // 控制器 Strobe
        bool newStrobe = (val & 0x01) != 0;
        if (controllerStrobe && !newStrobe) {
            // Strobe 下降沿: 锁存当前按键状态
            controllerLatch[0] = controller[0];
            controllerLatch[1] = controller[1];
            controllerShift[0] = 0;
            controllerShift[1] = 0;
        }
        controllerStrobe = newStrobe;
    }
    else if (addr < 0x4020) {
        // $4000-$401F: APU 寄存器 (转发到 APU)
        apu.regWrite(addr, val);
    }
    else if (addr >= 0x6000) {
        // $6000-$7FFF: SRAM 写入, $8000+: Mapper 写入
        cart.cpuWrite(addr, val);
    }
}

// ==================== PPU 总线 ====================
uint8_t IRAM_ATTR NES::ppuRead(uint16_t addr) {
    addr &= 0x3FFF;  // PPU 地址空间是 14 位

    if (addr < 0x2000) {
        // $0000-$1FFF: CHR ROM/RAM (Pattern Tables)
        return cart.ppuRead(addr);
    }
    else if (addr < 0x3F00) {
        // $2000-$3EFF: Nametables (with mirroring)
        uint16_t vramAddr = addr & 0x0FFF;

        // 处理镜像
        if (cart.getMirrorVertical()) {
            // 垂直镜像: $2000=$2800, $2400=$2C00
            vramAddr = vramAddr & 0x07FF;
        } else {
            // 水平镜像: $2000=$2400, $2800=$2C00
            if (vramAddr >= 0x0800) {
                vramAddr = (vramAddr & 0x03FF) | 0x0400;
            } else {
                vramAddr = vramAddr & 0x03FF;
            }
        }
        return vram[vramAddr & 0x07FF];
    }
    else {
        // $3F00-$3FFF: Palette RAM
        uint8_t palAddr = addr & 0x1F;
        // 镜像: $3F10/$3F14/$3F18/$3F1C 映射到 $3F00/$3F04/$3F08/$3F0C
        if ((palAddr & 0x13) == 0x10) {
            palAddr &= 0x0F;
        }
        return palette[palAddr];
    }
}

void IRAM_ATTR NES::ppuWrite(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        // $0000-$1FFF: CHR RAM (如果是 RAM)
        cart.ppuWrite(addr, val);
    }
    else if (addr < 0x3F00) {
        // $2000-$3EFF: Nametables
        uint16_t vramAddr = addr & 0x0FFF;

        if (cart.getMirrorVertical()) {
            vramAddr = vramAddr & 0x07FF;
        } else {
            if (vramAddr >= 0x0800) {
                vramAddr = (vramAddr & 0x03FF) | 0x0400;
            } else {
                vramAddr = vramAddr & 0x03FF;
            }
        }
        vram[vramAddr & 0x07FF] = val;
    }
    else {
        // $3F00-$3FFF: Palette RAM
        uint8_t palAddr = addr & 0x1F;
        if ((palAddr & 0x13) == 0x10) {
            palAddr &= 0x0F;
        }
        palette[palAddr] = val;
    }
}

// ============================================================================
// Save State
// ============================================================================

// 魔数和版本号用于验证存档
static const uint32_t SAVESTATE_MAGIC = 0x4E455353;  // "NESS"
static const uint16_t SAVESTATE_VERSION_V1 = 1;
static const uint16_t SAVESTATE_VERSION = 2;
static const size_t SAVESTATE_V2_IDENTITY_SIZE = 17;  // CRC32, mapper, PRG, CHR, payload size

static void writeU32Le(uint8_t* buffer, size_t& offset, uint32_t value) {
    buffer[offset++] = (uint8_t)(value >> 0);
    buffer[offset++] = (uint8_t)(value >> 8);
    buffer[offset++] = (uint8_t)(value >> 16);
    buffer[offset++] = (uint8_t)(value >> 24);
}

static uint32_t readU32Le(const uint8_t* buffer, size_t& offset) {
    uint32_t value = (uint32_t)buffer[offset] |
                     ((uint32_t)buffer[offset + 1] << 8) |
                     ((uint32_t)buffer[offset + 2] << 16) |
                     ((uint32_t)buffer[offset + 3] << 24);
    offset += 4;
    return value;
}

static uint32_t saveStateCrc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

size_t NES::getStateSize() const {
    size_t size = 0;

    // 头部
    size += sizeof(SAVESTATE_MAGIC);
    size += sizeof(SAVESTATE_VERSION);
    size += SAVESTATE_V2_IDENTITY_SIZE;

    // CPU RAM
    size += sizeof(ram);

    // PPU VRAM 和调色板
    size += sizeof(vram);
    size += sizeof(palette);
    size += sizeof(mirrorVertical);

    // 控制器状态
    size += sizeof(controller);
    size += sizeof(controllerLatch);
    size += sizeof(controllerShift);
    size += sizeof(controllerStrobe);

    // CPU 状态
    size += cpu.getStateSize();

    // PPU 状态
    size += ppu.getStateSize();

    // APU 状态
    size += apu.getStateSize();

    // Cartridge 状态
    size += cart.getStateSize();

    return size;
}

bool NES::saveStateToMemory(uint8_t* buffer, size_t bufferSize) {
    size_t requiredSize = getStateSize();
    if (bufferSize < requiredSize) {
        Serial.printf("SaveState: Buffer too small (%d < %d)\n", bufferSize, requiredSize);
        return false;
    }

    size_t offset = 0;

    // 写入魔数和版本
    buffer[offset++] = (SAVESTATE_MAGIC >> 0) & 0xFF;
    buffer[offset++] = (SAVESTATE_MAGIC >> 8) & 0xFF;
    buffer[offset++] = (SAVESTATE_MAGIC >> 16) & 0xFF;
    buffer[offset++] = (SAVESTATE_MAGIC >> 24) & 0xFF;
    buffer[offset++] = (SAVESTATE_VERSION >> 0) & 0xFF;
    buffer[offset++] = (SAVESTATE_VERSION >> 8) & 0xFF;
    writeU32Le(buffer, offset, cart.getRomCrc32());
    buffer[offset++] = cart.getMapper();
    writeU32Le(buffer, offset, cart.getPrgSize());
    writeU32Le(buffer, offset, cart.getChrRomSize());
    writeU32Le(buffer, offset, (uint32_t)(requiredSize - offset - sizeof(uint32_t)));

    // CPU RAM
    memcpy(buffer + offset, ram, sizeof(ram));
    offset += sizeof(ram);

    // PPU VRAM 和调色板
    memcpy(buffer + offset, vram, sizeof(vram));
    offset += sizeof(vram);
    memcpy(buffer + offset, palette, sizeof(palette));
    offset += sizeof(palette);
    buffer[offset++] = mirrorVertical ? 1 : 0;

    // 控制器状态
    memcpy(buffer + offset, controller, sizeof(controller));
    offset += sizeof(controller);
    memcpy(buffer + offset, controllerLatch, sizeof(controllerLatch));
    offset += sizeof(controllerLatch);
    memcpy(buffer + offset, controllerShift, sizeof(controllerShift));
    offset += sizeof(controllerShift);
    buffer[offset++] = controllerStrobe ? 1 : 0;

    // CPU 状态
    cpu.saveState(buffer, offset);

    // PPU 状态
    ppu.saveState(buffer, offset);

    // APU 状态
    apu.saveState(buffer, offset);

    // Cartridge 状态
    cart.saveState(buffer, offset);

    return true;
}

bool NES::loadStateFromMemory(const uint8_t* buffer, size_t bufferSize) {
    const size_t v2Size = getStateSize();
    const size_t v1Size = v2Size - SAVESTATE_V2_IDENTITY_SIZE;
    if (!buffer || (bufferSize != v2Size && bufferSize != v1Size)) {
        Serial.printf("LoadState: Invalid state size (%u)\n", (unsigned)bufferSize);
        return false;
    }

    size_t offset = 0;

    // 验证魔数
    uint32_t magic = buffer[offset] | (buffer[offset + 1] << 8) |
                     (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
    offset += 4;

    if (magic != SAVESTATE_MAGIC) {
        Serial.printf("LoadState: Invalid magic (0x%08X)\n", magic);
        return false;
    }

    // 验证版本
    uint16_t version = buffer[offset] | (buffer[offset + 1] << 8);
    offset += 2;

    if (version != SAVESTATE_VERSION && version != SAVESTATE_VERSION_V1) {
        Serial.printf("LoadState: Unsupported version %d\n", version);
        return false;
    }
    if ((version == SAVESTATE_VERSION && bufferSize != v2Size) ||
        (version == SAVESTATE_VERSION_V1 && bufferSize != v1Size)) {
        Serial.println("LoadState: Version and size do not match");
        return false;
    }

    if (version == SAVESTATE_VERSION) {
        uint32_t romCrc = readU32Le(buffer, offset);
        uint8_t mapper = buffer[offset++];
        uint32_t prgSize = readU32Le(buffer, offset);
        uint32_t chrSize = readU32Le(buffer, offset);
        uint32_t payloadSize = readU32Le(buffer, offset);
        size_t expectedPayload = v2Size - offset;
        if (romCrc != cart.getRomCrc32() || mapper != cart.getMapper() ||
            prgSize != cart.getPrgSize() || chrSize != cart.getChrRomSize()) {
            Serial.println("LoadState: ROM identity mismatch");
            return false;
        }
        if (payloadSize != expectedPayload) {
            Serial.printf("LoadState: Payload size mismatch (%u != %u)\n",
                          (unsigned)payloadSize, (unsigned)expectedPayload);
            return false;
        }
    } else {
        Serial.println("LoadState: Loading legacy v1 state without ROM identity");
    }

    // CPU RAM
    memcpy(ram, buffer + offset, sizeof(ram));
    offset += sizeof(ram);

    // PPU VRAM 和调色板
    memcpy(vram, buffer + offset, sizeof(vram));
    offset += sizeof(vram);
    memcpy(palette, buffer + offset, sizeof(palette));
    offset += sizeof(palette);
    mirrorVertical = buffer[offset++] != 0;

    // 控制器状态
    memcpy(controller, buffer + offset, sizeof(controller));
    offset += sizeof(controller);
    memcpy(controllerLatch, buffer + offset, sizeof(controllerLatch));
    offset += sizeof(controllerLatch);
    memcpy(controllerShift, buffer + offset, sizeof(controllerShift));
    offset += sizeof(controllerShift);
    controllerStrobe = buffer[offset++] != 0;

    // CPU 状态
    cpu.loadState(buffer, offset);

    // PPU 状态
    ppu.loadState(buffer, offset);

    // APU 状态
    apu.loadState(buffer, offset);

    // Cartridge 状态
    cart.loadState(buffer, offset);

    // 重新设置 PPU 内存指针
    // 确保 Cartridge 也能访问 VRAM 并绑定 NES
    cart.setVramPointer(vram);
    cart.setNES(this);
    ppu.setMemoryPointers(vram, palette, &cart, &mirrorVertical);

    Serial.printf("LoadState: Loaded %d bytes\n", offset);
    return true;
}

bool NES::saveState(const char* path) {
    // 分配缓冲区
    size_t stateSize = getStateSize();
    size_t fileSize = stateSize + sizeof(uint32_t);
    uint8_t* buffer = (uint8_t*)ps_malloc(fileSize);
    if (!buffer) {
        buffer = (uint8_t*)malloc(fileSize);
    }
    if (!buffer) {
        Serial.println("SaveState: Failed to allocate buffer");
        return false;
    }

    // 保存到内存
    bool success = saveStateToMemory(buffer, stateSize);
    if (!success) {
        free(buffer);
        return false;
    }

    uint32_t crc = saveStateCrc32(buffer, stateSize);
    buffer[stateSize + 0] = (crc >> 0) & 0xFF;
    buffer[stateSize + 1] = (crc >> 8) & 0xFF;
    buffer[stateSize + 2] = (crc >> 16) & 0xFF;
    buffer[stateSize + 3] = (crc >> 24) & 0xFF;

    // Write to a temporary file first. A failed or interrupted write must not
    // destroy the last known-good save.
    dijiEnsureSaveDirectory(path);
    char tempPath[160];
    char backupPath[160];
    int tempLen = snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    int backupLen = snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
    if (tempLen <= 0 || (size_t)tempLen >= sizeof(tempPath) ||
        backupLen <= 0 || (size_t)backupLen >= sizeof(backupPath)) {
        Serial.println("SaveState: Save path too long");
        free(buffer);
        return false;
    }

    dijiRemoveStoragePath(tempPath);
    File f = dijiOpenStoragePath(tempPath, FILE_WRITE);
    if (!f) {
        Serial.printf("SaveState: Failed to create temp file %s\n", tempPath);
        free(buffer);
        return false;
    }

    size_t written = f.write(buffer, fileSize);
    f.close();
    free(buffer);

    if (written != fileSize) {
        Serial.printf("SaveState: Write error (%d != %d)\n", written, fileSize);
        dijiRemoveStoragePath(tempPath);
        return false;
    }

    dijiRemoveStoragePath(backupPath);
    bool hadPreviousSave = dijiStoragePathExists(path);
    if (hadPreviousSave && !dijiRenameStoragePath(path, backupPath)) {
        Serial.println("SaveState: Failed to preserve previous save");
        dijiRemoveStoragePath(tempPath);
        return false;
    }

    if (!dijiRenameStoragePath(tempPath, path)) {
        Serial.println("SaveState: Failed to install new save");
        if (hadPreviousSave) {
            dijiRenameStoragePath(backupPath, path);
        }
        dijiRemoveStoragePath(tempPath);
        return false;
    }
    if (hadPreviousSave) {
        dijiRemoveStoragePath(backupPath);
    }

    Serial.printf("SaveState: Saved to %s\n", path);
    return true;
}

bool NES::loadState(const char* path) {
    File f = dijiOpenStoragePath(path, FILE_READ);
    if (!f) {
        Serial.printf("LoadState: File not found %s\n", path);
        return false;
    }

    size_t fileSize = f.size();
    size_t v2Size = getStateSize();
    size_t v1Size = v2Size - SAVESTATE_V2_IDENTITY_SIZE;
    bool sizeCouldBeV2 = fileSize == v2Size + sizeof(uint32_t);
    bool sizeCouldBeV1 = fileSize == v1Size || fileSize == v1Size + sizeof(uint32_t);
    if (!sizeCouldBeV2 && !sizeCouldBeV1) {
        Serial.printf("LoadState: Invalid file size %u\n", (unsigned)fileSize);
        f.close();
        return false;
    }

    // 分配缓冲区
    uint8_t* buffer = (uint8_t*)ps_malloc(fileSize);
    if (!buffer) {
        buffer = (uint8_t*)malloc(fileSize);
    }
    if (!buffer) {
        Serial.println("LoadState: Failed to allocate buffer");
        f.close();
        return false;
    }

    // 读取文件
    size_t bytesRead = f.read(buffer, fileSize);
    f.close();

    if (bytesRead != fileSize) {
        Serial.printf("LoadState: Read error (%d != %d)\n", bytesRead, fileSize);
        free(buffer);
        return false;
    }

    if (fileSize < 6) {
        free(buffer);
        return false;
    }
    uint16_t version = (uint16_t)buffer[4] | ((uint16_t)buffer[5] << 8);
    size_t stateSize = 0;
    bool hasCrc = false;
    if (version == SAVESTATE_VERSION && sizeCouldBeV2) {
        stateSize = v2Size;
        hasCrc = true;
    } else if (version == SAVESTATE_VERSION_V1 && sizeCouldBeV1) {
        stateSize = v1Size;
        hasCrc = fileSize == v1Size + sizeof(uint32_t);
    } else {
        Serial.printf("LoadState: Version %u does not match file size\n", version);
        free(buffer);
        return false;
    }

    if (hasCrc) {
        uint32_t storedCrc = buffer[stateSize + 0] |
                             ((uint32_t)buffer[stateSize + 1] << 8) |
                             ((uint32_t)buffer[stateSize + 2] << 16) |
                             ((uint32_t)buffer[stateSize + 3] << 24);
        uint32_t actualCrc = saveStateCrc32(buffer, stateSize);
        if (storedCrc != actualCrc) {
            Serial.printf("LoadState: CRC mismatch (0x%08X != 0x%08X)\n",
                          storedCrc, actualCrc);
            free(buffer);
            return false;
        }
    }

    // 加载状态
    bool success = loadStateFromMemory(buffer, stateSize);
    free(buffer);

    if (success) {
        Serial.printf("LoadState: Loaded from %s\n", path);
    }

    return success;
}
