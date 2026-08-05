#pragma once
#include <Arduino.h>

// Forward declaration for save state
class NES;

class Cartridge {
public:
    Cartridge();
    ~Cartridge();

    bool load(const char* path);
    uint8_t IRAM_ATTR cpuRead(uint16_t addr);
    void IRAM_ATTR cpuWrite(uint16_t addr, uint8_t val);

    uint8_t IRAM_ATTR ppuRead(uint16_t addr);
    void ppuWrite(uint16_t addr, uint8_t val);

    // 设置 VRAM 指针（用于 NameTable 读取时处理动态镜像）
    void setVramPointer(uint8_t* vramPtr) { vram = vramPtr; updateNtPtrs(); }

    // NameTable 读取（处理动态镜像，MMC3 可在运行时切换）
    uint8_t IRAM_ATTR readNameTable(uint16_t addr);

    // ROM 信息
    uint8_t getPrgBanks() const { return prgBanks; }
    uint8_t getChrBanks() const { return chrBanks; }
    uint8_t getMapper() const { return mapper; }
    uint32_t getPrgSize() const { return prgSize; }
    uint32_t getChrRomSize() const { return (uint32_t)chrBanks * 0x2000u; }
    uint32_t getRomCrc32() const { return romCrc32; }
    bool getMirrorVertical() const { return mirrorVertical; }
    bool hasChrRam() const { return chrBanks == 0; }
    bool hasSRAM() const { return hasBattery; }

    // 直接 CHR 访问 (用于 PPU 快速渲染)
    uint8_t* getChrData() { return chrWindow; }

    // 镜像模式设置 (用于 MMC1/MMC3)
    void setMirrorVertical(bool v) { mirrorVertical = v; updateNtPtrs(); }

    // IRQ 接口 (用于 MMC3)
    bool irqPending() const { return mmc3IrqPending; }
    void acknowledgeIrq() { mmc3IrqPending = false; }
    void IRAM_ATTR clockIrqCounter();  // 每条扫描线调用
    void IRAM_ATTR ppuScanline();      // 简化的扫描线 IRQ 触发 (Anemoia 风格)

    // 绑定 NES 实例（用于在 MMC3 IRQ 触发时调用 CPU IRQ）
    void setNES(NES* n) { nes = n; }

    // SRAM 访问 (用于电池备份存档)
    uint8_t* getSRAM() { return sram; }
    const uint8_t* getSRAM() const { return sram; }
    size_t getSRAMSize() const { return sizeof(sram); }
    bool isSramDirty() const { return sramDirty; }
    void clearSramDirty() { sramDirty = false; }
    void clearSRAM();
    bool loadSRAM(const uint8_t* data, size_t size);

    // Save state 接口
    void saveState(uint8_t* buf, size_t& offset) const;
    void loadState(const uint8_t* buf, size_t& offset);
    size_t getStateSize() const;

private:
    // iNES 头信息
    uint8_t prgBanks = 0;      // PRG ROM 数量 (16KB 单位)
    uint8_t chrBanks = 0;      // CHR ROM 数量 (8KB 单位)
    uint8_t mapper = 0;        // Mapper 编号
    bool mirrorVertical = false;  // true=垂直镜像, false=水平镜像
    bool hasBattery = false;   // 是否有电池备份

    // ROM 数据 - 使用动态分配 (PSRAM)
    uint8_t* prg = nullptr;    // PRG ROM (动态分配)
    uint8_t* chr = nullptr;    // CHR ROM (动态分配, 最多 256KB)
    uint8_t* vram = nullptr;   // VRAM 指针 (来自 NES，用于 NameTable 镜像)
    uint8_t chrRam[0x2000];    // 8KB CHR RAM (当无 CHR ROM 时使用)
    uint8_t* chrWindow = nullptr; // 当前 CHR 窗口指针 (用于快速访问)
    uint32_t prgSize = 0;
    uint32_t chrSize = 0;
    uint32_t romCrc32 = 0;

    // SRAM (用于电池备份)
    uint8_t sram[0x2000];      // 8KB SRAM ($6000-$7FFF)
    volatile bool sramDirty = false;

    // ========== PRG Bank Cache ==========
    uint8_t prgBankSelect = 0;  // Mapper 2: 选择的 PRG bank
    uint32_t prgBank0Offset = 0; // $8000 (16KB) 或 $8000-$9FFF (MMC3 8KB)
    uint32_t prgBank1Offset = 0; // $C000 (16KB) 或 $A000-$BFFF (MMC3 8KB)
    uint32_t prgBank2Offset = 0; // $C000-$DFFF (MMC3 8KB)
    uint32_t prgBank3Offset = 0; // $E000-$FFFF (MMC3 8KB)

    // ========== CHR Bank Pointer Cache ==========
public:
    uint8_t* chrBankPtrs[8] = {nullptr}; // 8 x 1KB CHR 指针缓存，消除运行时 bank 计算

    // ========== Nametable Pointer Cache ==========
    uint8_t* ntPtrs[4] = {nullptr};  // 4 x 1KB nametable 指针，消除镜像分支
    void updateNtPtrs();  // 在镜像模式变更时调用
private:

    // ========== Mapper 1 (MMC1) ==========
    uint8_t mmc1ShiftReg = 0x10;   // 移位寄存器 (bit 4 = 重置标志)
    uint8_t mmc1WriteCount = 0;    // 写入计数 (0-4)
    uint8_t mmc1Control = 0x0C;    // 控制寄存器 ($8000-$9FFF)
    uint8_t mmc1ChrBank0 = 0;      // CHR bank 0 ($A000-$BFFF)
    uint8_t mmc1ChrBank1 = 0;      // CHR bank 1 ($C000-$DFFF)
    uint8_t mmc1PrgBank = 0;       // PRG bank ($E000-$FFFF)

    // ========== Mapper 3 (CNROM) ==========
    uint8_t cnromChrBank = 0;      // CHR bank 选择

    // Mapper 7 (AxROM)
    uint8_t axromPrgBank = 0;
    bool axromUpperScreen = false;

    // Mapper 66 (GxROM)
    uint8_t gxromPrgBank = 0;
    uint8_t gxromChrBank = 0;

    // ========== Mapper 4 (MMC3) ==========
    uint8_t mmc3BankSelect = 0;    // Bank 选择寄存器
    uint8_t mmc3Banks[8] = {0};    // 8 个 bank 寄存器
    bool mmc3PrgMode = false;      // PRG bank 模式
    bool mmc3ChrMode = false;      // CHR bank 模式
    uint8_t mmc3IrqLatch = 0;      // IRQ 计数器锁存值
    uint8_t mmc3IrqCounter = 0;    // IRQ 计数器
    bool mmc3IrqEnabled = false;   // IRQ 使能
    bool mmc3IrqReload = false;    // IRQ 重载标志
    bool mmc3IrqPending = false;   // IRQ 待处理
    bool mmc3PrevA12 = false;      // 上一次 A12 的电平（用于上升沿检测）
    uint32_t mmc3A12LowStart = 0;   // A12 进入低电平时的绝对 PPU cycle（用于抖动抑制）

    // 指向主机 NES 的指针（用于触发 IRQ）
    NES* nes = nullptr;

    // Mapper 特定函数
    void updateBankCache();
    void updateMmc1Banks();
    void updateMmc3Banks();
    void updateChrBankCache();

    // Mapper 读写函数 (IRAM_ATTR 确保热路径在 SRAM 执行)
    uint8_t IRAM_ATTR cpuReadMapper0(uint16_t addr);
    uint8_t IRAM_ATTR cpuReadMapper1(uint16_t addr);
    uint8_t IRAM_ATTR cpuReadMapper2(uint16_t addr);
    uint8_t IRAM_ATTR cpuReadMapper3(uint16_t addr);
    uint8_t IRAM_ATTR cpuReadMapper4(uint16_t addr);
    uint8_t IRAM_ATTR cpuReadMapper7(uint16_t addr);
    uint8_t IRAM_ATTR cpuReadMapper66(uint16_t addr);

    void cpuWriteMapper1(uint16_t addr, uint8_t val);
    void cpuWriteMapper2(uint16_t addr, uint8_t val);
    void cpuWriteMapper3(uint16_t addr, uint8_t val);
    void cpuWriteMapper4(uint16_t addr, uint8_t val);
    void cpuWriteMapper7(uint16_t addr, uint8_t val);
    void cpuWriteMapper66(uint16_t addr, uint8_t val);

    uint8_t IRAM_ATTR ppuReadMapper1(uint16_t addr);
    uint8_t IRAM_ATTR ppuReadMapper3(uint16_t addr);
    uint8_t IRAM_ATTR ppuReadMapper4(uint16_t addr);

    void ppuWriteMapper1(uint16_t addr, uint8_t val);
    void ppuWriteMapper4(uint16_t addr, uint8_t val);
};
