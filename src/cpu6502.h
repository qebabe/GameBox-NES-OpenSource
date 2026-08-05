#pragma once
#include <Arduino.h>

class NES;

class CPU6502 {
public:
    // 寄存器
    uint8_t A = 0;      // 累加器
    uint8_t X = 0;      // X 寄存器
    uint8_t Y = 0;      // Y 寄存器
    uint8_t SP = 0xFD;  // 栈指针
    uint8_t P = 0x24;   // 处理器状态标志
    uint16_t PC = 0x8000; // 程序计数器

    // 内部周期计数器 (用于 Anemoia 风格批量执行)
    int cycles = 0;

    // 方法
    void reset();
    // 执行一条指令并返回"估算的 6502 周期数"(不含精确的 page-cross/部分额外周期)
    uint8_t IRAM_ATTR step();
    // Anemoia 风格批量执行：执行指定周期数 (高性能)
    void IRAM_ATTR clock(int targetCycles);
    // 旧版接口 (兼容性)
    void runCycles(int cycles);
    void connect(NES* nes);
    void IRAM_ATTR nmi();   // 非屏蔽中断
    void IRAM_ATTR irq();   // 可屏蔽中断

    // 调试接口
    uint16_t getPC() const { return PC; }
    uint8_t getA() const { return A; }
    uint8_t getX() const { return X; }
    uint8_t getY() const { return Y; }
    uint8_t getSP() const { return SP; }
    uint8_t getP() const { return P; }

    // Save State 接口
    void saveState(uint8_t* buf, size_t& offset) const;
    void loadState(const uint8_t* buf, size_t& offset);
    size_t getStateSize() const;

private:
    NES* bus;

    // 内部方法 (IRAM 优化)
    uint8_t IRAM_ATTR fetch();
    uint16_t IRAM_ATTR fetchWord();
    uint8_t IRAM_ATTR read(uint16_t addr);
    void IRAM_ATTR write(uint16_t addr, uint8_t val);

    // 标志位操作
    void setFlag(uint8_t flag, bool set);
    bool getFlag(uint8_t flag) const;
    void updateZNFlags(uint8_t val);

    // 寻址模式辅助
    uint16_t addrZeroPage();
    uint16_t addrZeroPageX();
    uint16_t addrZeroPageY();
    uint16_t addrAbsolute();
    uint16_t addrAbsoluteX();
    uint16_t addrAbsoluteY();
    uint16_t addrIndirectX();  // (zp,X)
    uint16_t addrIndirectY();  // (zp),Y

    // 栈操作
    void push(uint8_t val);
    uint8_t pop();
    void pushWord(uint16_t val);
    uint16_t popWord();

    // 标志位定义
    static constexpr uint8_t FLAG_C = 0x01; // Carry
    static constexpr uint8_t FLAG_Z = 0x02; // Zero
    static constexpr uint8_t FLAG_I = 0x04; // Interrupt
    static constexpr uint8_t FLAG_D = 0x08; // Decimal
    static constexpr uint8_t FLAG_B = 0x10; // Break
    static constexpr uint8_t FLAG_V = 0x40; // Overflow
    static constexpr uint8_t FLAG_N = 0x80; // Negative
};
