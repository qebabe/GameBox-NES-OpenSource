#pragma once
#include "cpu6502.h"
#include "ppu.h"
#include "cartridge.h"
#include "apu.h"

class NES {
public:
    bool loadROM(const char* path);
    void reset();
    // 执行一条 CPU 指令并同步 PPU，返回 CPU 周期数
    uint8_t IRAM_ATTR step();
    // Anemoia 风格帧级调度：执行完整一帧 (CPU + PPU + 渲染 + DMA)
    void IRAM_ATTR clock();
    // 行级推进：执行一条扫描线对应的 CPU 周期并推进 PPU 一行
    void IRAM_ATTR stepScanline();
    void IRAM_ATTR stepThreeScanlines();
    void render(uint16_t* fb);

    // 逐行渲染接口 (用于 DMA 输出)
    void renderLine(int scanline, uint16_t* lineBuffer);

    // CPU 总线
    uint8_t IRAM_ATTR cpuRead(uint16_t addr);
    void IRAM_ATTR cpuWrite(uint16_t addr, uint8_t val);

    // PPU 总线
    uint8_t IRAM_ATTR ppuRead(uint16_t addr);
    void IRAM_ATTR ppuWrite(uint16_t addr, uint8_t val);

    // 控制器
    void setController(uint8_t id, uint8_t state);  // id: 0 或 1
    // 帧结束处理 (兼容性保留，现在为空操作)
    void endFrame();

    // Save State 接口
    bool saveState(const char* path);
    bool loadState(const char* path);
    bool flushBatterySave(bool force = false);
    bool batterySaveDirty() const { return cart.hasSRAM() && cart.isSramDirty(); }
    size_t getStateSize() const;

    // Save State 到内存 (用于快速存档)
    bool saveStateToMemory(uint8_t* buffer, size_t bufferSize);
    bool loadStateFromMemory(const uint8_t* buffer, size_t bufferSize);

    // 公开访问接口
    CPU6502 cpu;
    APU apu;
    PPU& getPPU() { return ppu; }
    Cartridge& getCart() { return cart; }

    // 获取当前 ROM 名称 (用于生成存档文件名)
    const char* getCurrentRomPath() const { return currentRomPath; }

private:
    PPU ppu;
    Cartridge cart;
    uint8_t ram[0x800];      // 2KB CPU RAM
    uint8_t vram[0x800];     // 2KB PPU VRAM (Nametables)
    uint8_t palette[0x20];   // 32 bytes Palette RAM
    bool mirrorVertical = false;  // 镜像模式缓存

    // 控制器状态
    uint8_t controller[2] = {0, 0};     // 当前按键状态
    uint8_t controllerLatch[2] = {0, 0}; // 锁存的状态
    uint8_t controllerShift[2] = {0, 0}; // 移位计数器
    bool controllerStrobe = false;       // Strobe 信号

    // 当前 ROM 路径
    char currentRomPath[128] = {0};
    bool loadBatterySave();
    // 行级调度辅助：用于 113/114 周期交替
    bool scanlineParity = false;

    bool frameskipEnabled = true;  // 抽帧开关 (true=启用抽帧, false=每帧都渲染)

public:
    // 设置抽帧开关
    void setFrameskipEnabled(bool enabled) { frameskipEnabled = enabled; }
    bool getFrameskipEnabled() const { return frameskipEnabled; }
    void requestFrameSkip(bool skip) { skipNextFrame = skip; }

private:
    bool skipNextFrame = false;
};
