#include "cpu6502.h"
#include "nes.h"

// Anemoia 风格批量执行：执行指定数量的 CPU 周期
// 优化: 直接用 cycles 做差值，消除空转循环
// 原版每次 clock(113) 循环 113 次（~75 次空转），优化后只循环 ~38 次
void IRAM_ATTR CPU6502::clock(int targetCycles) {
    cycles -= targetCycles;
    while (cycles < 0) {
        cycles += step();
    }
}

void CPU6502::runCycles(int targetCycles) {
    clock(targetCycles);
}

void CPU6502::connect(NES* n) {
    bus = n;
}

void CPU6502::reset() {
    cycles = 0;  // 重置周期计数器
    // 从 RESET 向量读取起始地址
    uint8_t lo = bus->cpuRead(0xFFFC);
    uint8_t hi = bus->cpuRead(0xFFFD);
    PC = (hi << 8) | lo;

    // 如果向量无效 (全 0 或全 FF)，使用默认地址
    if (PC == 0x0000 || PC == 0xFFFF) {
        PC = 0x8000;
    }

    A = 0;
    X = 0;
    Y = 0;
    SP = 0xFD;
    P = 0x24;  // I flag set
}

void IRAM_ATTR CPU6502::nmi() {
    // 非屏蔽中断
    pushWord(PC);
    push((P & 0xEF) | 0x20);  // 清除 B flag, 设置 bit 5
    setFlag(FLAG_I, true);

    uint8_t lo = bus->cpuRead(0xFFFA);
    uint8_t hi = bus->cpuRead(0xFFFB);
    PC = (hi << 8) | lo;
    cycles += 7;  // NMI 需要 7 个周期 (保留前一条指令的剩余周期)
}

void IRAM_ATTR CPU6502::irq() {
    // 可屏蔽中断 (如果 I flag 未设置)
    if (getFlag(FLAG_I)) return;

    pushWord(PC);
    push((P & 0xEF) | 0x20);  // 清除 B flag, 设置 bit 5
    setFlag(FLAG_I, true);

    uint8_t lo = bus->cpuRead(0xFFFE);
    uint8_t hi = bus->cpuRead(0xFFFF);
    PC = (hi << 8) | lo;
    cycles += 7;  // IRQ 需要 7 个周期 (保留前一条指令的剩余周期)
}

uint8_t IRAM_ATTR CPU6502::fetch() {
    uint8_t v = read(PC);
    PC++;
    return v;
}

uint16_t IRAM_ATTR CPU6502::fetchWord() {
    uint8_t lo = fetch();
    uint8_t hi = fetch();
    return (hi << 8) | lo;
}

uint8_t IRAM_ATTR CPU6502::read(uint16_t addr) {
    return bus->cpuRead(addr);
}

void IRAM_ATTR CPU6502::write(uint16_t addr, uint8_t val) {
    bus->cpuWrite(addr, val);
}

void CPU6502::setFlag(uint8_t flag, bool set) {
    if (set) {
        P |= flag;
    } else {
        P &= ~flag;
    }
}

bool CPU6502::getFlag(uint8_t flag) const {
    return (P & flag) != 0;
}

void CPU6502::updateZNFlags(uint8_t val) {
    setFlag(FLAG_Z, val == 0);
    setFlag(FLAG_N, (val & 0x80) != 0);
}

// ==================== 寻址模式 ====================
uint16_t CPU6502::addrZeroPage() {
    return fetch();
}

uint16_t CPU6502::addrZeroPageX() {
    return (fetch() + X) & 0xFF;
}

uint16_t CPU6502::addrZeroPageY() {
    return (fetch() + Y) & 0xFF;
}

uint16_t CPU6502::addrAbsolute() {
    return fetchWord();
}

uint16_t CPU6502::addrAbsoluteX() {
    return fetchWord() + X;
}

uint16_t CPU6502::addrAbsoluteY() {
    return fetchWord() + Y;
}

uint16_t CPU6502::addrIndirectX() {
    uint8_t zp = (fetch() + X) & 0xFF;
    uint8_t lo = read(zp);
    uint8_t hi = read((zp + 1) & 0xFF);
    return (hi << 8) | lo;
}

uint16_t CPU6502::addrIndirectY() {
    uint8_t zp = fetch();
    uint8_t lo = read(zp);
    uint8_t hi = read((zp + 1) & 0xFF);
    return ((hi << 8) | lo) + Y;
}

// ==================== 栈操作 ====================
void CPU6502::push(uint8_t val) {
    write(0x100 + SP, val);
    SP--;
}

uint8_t CPU6502::pop() {
    SP++;
    return read(0x100 + SP);
}

void CPU6502::pushWord(uint16_t val) {
    push((val >> 8) & 0xFF);
    push(val & 0xFF);
}

uint16_t CPU6502::popWord() {
    uint8_t lo = pop();
    uint8_t hi = pop();
    return (hi << 8) | lo;
}

uint8_t IRAM_ATTR CPU6502::step() {
    // NOTE: 这是"指令级"实现（一次 step = 一条指令），这里返回的是估算周期数。
    // 目前不追求完全 cycle-accurate（例如 page-cross 额外周期），但足够用于按"每帧 CPU 周期数"节流。

    // Fast lookup table for base cycles (DRAM_ATTR for faster access)
    static const uint8_t DRAM_ATTR kCycles[256] = {
        /* 0x00 */ 7,2,2,2,3,3,5,2,3,2,2,2,4,4,6,2,
        /* 0x10 */ 2,5,2,2,4,4,6,2,2,4,2,2,4,4,7,2,
        /* 0x20 */ 6,6,2,2,3,3,5,2,4,2,2,2,4,4,6,2,
        /* 0x30 */ 2,5,2,2,4,4,6,2,2,4,2,2,4,4,7,2,
        /* 0x40 */ 6,6,2,2,3,3,5,2,4,2,2,2,3,4,6,2,
        /* 0x50 */ 2,5,2,2,4,4,6,2,2,4,2,2,4,4,7,2,
        /* 0x60 */ 6,6,2,2,3,3,5,2,4,2,2,2,5,4,6,2,
        /* 0x70 */ 2,5,2,2,4,4,6,2,2,4,2,2,4,4,7,2,
        /* 0x80 */ 2,6,2,2,3,3,3,2,2,2,2,2,4,4,4,2,
        /* 0x90 */ 2,6,2,2,4,4,4,2,2,5,2,2,4,5,5,2,
        /* 0xA0 */ 2,6,2,2,3,3,3,2,2,2,2,2,4,4,4,2,
        /* 0xB0 */ 2,5,2,2,4,4,4,2,2,4,2,2,4,4,4,2,
        /* 0xC0 */ 2,6,2,2,3,3,5,2,2,2,2,2,4,4,6,2,
        /* 0xD0 */ 2,5,2,2,4,4,6,2,2,4,2,2,4,4,7,2,
        /* 0xE0 */ 2,6,2,2,3,3,5,2,2,2,2,2,4,4,6,2,
        /* 0xF0 */ 2,5,2,2,4,4,6,2,2,4,2,2,4,4,7,2
    };

    uint8_t opcode = fetch();
    uint8_t cycles = kCycles[opcode];

    switch (opcode) {

    // ==================== LDA ====================
    case 0xA9: A = fetch(); updateZNFlags(A); break;                      // LDA #imm
    case 0xA5: A = read(addrZeroPage()); updateZNFlags(A); break;         // LDA zp
    case 0xB5: A = read(addrZeroPageX()); updateZNFlags(A); break;        // LDA zp,X
    case 0xAD: A = read(addrAbsolute()); updateZNFlags(A); break;         // LDA abs
    case 0xBD: A = read(addrAbsoluteX()); updateZNFlags(A); break;        // LDA abs,X
    case 0xB9: A = read(addrAbsoluteY()); updateZNFlags(A); break;        // LDA abs,Y
    case 0xA1: A = read(addrIndirectX()); updateZNFlags(A); break;        // LDA (zp,X)
    case 0xB1: A = read(addrIndirectY()); updateZNFlags(A); break;        // LDA (zp),Y

    // ==================== LDX ====================
    case 0xA2: X = fetch(); updateZNFlags(X); break;                      // LDX #imm
    case 0xA6: X = read(addrZeroPage()); updateZNFlags(X); break;         // LDX zp
    case 0xB6: X = read(addrZeroPageY()); updateZNFlags(X); break;        // LDX zp,Y
    case 0xAE: X = read(addrAbsolute()); updateZNFlags(X); break;         // LDX abs
    case 0xBE: X = read(addrAbsoluteY()); updateZNFlags(X); break;        // LDX abs,Y

    // ==================== LDY ====================
    case 0xA0: Y = fetch(); updateZNFlags(Y); break;                      // LDY #imm
    case 0xA4: Y = read(addrZeroPage()); updateZNFlags(Y); break;         // LDY zp
    case 0xB4: Y = read(addrZeroPageX()); updateZNFlags(Y); break;        // LDY zp,X
    case 0xAC: Y = read(addrAbsolute()); updateZNFlags(Y); break;         // LDY abs
    case 0xBC: Y = read(addrAbsoluteX()); updateZNFlags(Y); break;        // LDY abs,X

    // ==================== STA ====================
    case 0x85: write(addrZeroPage(), A); break;                           // STA zp
    case 0x95: write(addrZeroPageX(), A); break;                          // STA zp,X
    case 0x8D: write(addrAbsolute(), A); break;                           // STA abs
    case 0x9D: write(addrAbsoluteX(), A); break;                          // STA abs,X
    case 0x99: write(addrAbsoluteY(), A); break;                          // STA abs,Y
    case 0x81: write(addrIndirectX(), A); break;                          // STA (zp,X)
    case 0x91: write(addrIndirectY(), A); break;                          // STA (zp),Y

    // ==================== STX ====================
    case 0x86: write(addrZeroPage(), X); break;                           // STX zp
    case 0x96: write(addrZeroPageY(), X); break;                          // STX zp,Y
    case 0x8E: write(addrAbsolute(), X); break;                           // STX abs

    // ==================== STY ====================
    case 0x84: write(addrZeroPage(), Y); break;                           // STY zp
    case 0x94: write(addrZeroPageX(), Y); break;                          // STY zp,X
    case 0x8C: write(addrAbsolute(), Y); break;                           // STY abs

    // ==================== ADC (Add with Carry) ====================
    case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71: {
        uint8_t val;
        switch (opcode) {
            case 0x69: val = fetch(); break;
            case 0x65: val = read(addrZeroPage()); break;
            case 0x75: val = read(addrZeroPageX()); break;
            case 0x6D: val = read(addrAbsolute()); break;
            case 0x7D: val = read(addrAbsoluteX()); break;
            case 0x79: val = read(addrAbsoluteY()); break;
            case 0x61: val = read(addrIndirectX()); break;
            case 0x71: val = read(addrIndirectY()); break;
            default: val = 0; break;
        }
        uint16_t sum = A + val + (getFlag(FLAG_C) ? 1 : 0);
        setFlag(FLAG_C, sum > 0xFF);
        setFlag(FLAG_V, (~(A ^ val) & (A ^ sum) & 0x80) != 0);
        A = sum & 0xFF;
        updateZNFlags(A);
        break;
    }

    // ==================== SBC (Subtract with Carry) ====================
    case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1: {
        uint8_t val;
        switch (opcode) {
            case 0xE9: val = fetch(); break;
            case 0xE5: val = read(addrZeroPage()); break;
            case 0xF5: val = read(addrZeroPageX()); break;
            case 0xED: val = read(addrAbsolute()); break;
            case 0xFD: val = read(addrAbsoluteX()); break;
            case 0xF9: val = read(addrAbsoluteY()); break;
            case 0xE1: val = read(addrIndirectX()); break;
            case 0xF1: val = read(addrIndirectY()); break;
            default: val = 0; break;
        }
        uint16_t diff = A - val - (getFlag(FLAG_C) ? 0 : 1);
        setFlag(FLAG_C, diff < 0x100);
        setFlag(FLAG_V, ((A ^ val) & (A ^ diff) & 0x80) != 0);
        A = diff & 0xFF;
        updateZNFlags(A);
        break;
    }

    // ==================== AND ====================
    case 0x29: A &= fetch(); updateZNFlags(A); break;                     // AND #imm
    case 0x25: A &= read(addrZeroPage()); updateZNFlags(A); break;        // AND zp
    case 0x35: A &= read(addrZeroPageX()); updateZNFlags(A); break;       // AND zp,X
    case 0x2D: A &= read(addrAbsolute()); updateZNFlags(A); break;        // AND abs
    case 0x3D: A &= read(addrAbsoluteX()); updateZNFlags(A); break;       // AND abs,X
    case 0x39: A &= read(addrAbsoluteY()); updateZNFlags(A); break;       // AND abs,Y
    case 0x21: A &= read(addrIndirectX()); updateZNFlags(A); break;       // AND (zp,X)
    case 0x31: A &= read(addrIndirectY()); updateZNFlags(A); break;       // AND (zp),Y

    // ==================== ORA ====================
    case 0x09: A |= fetch(); updateZNFlags(A); break;                     // ORA #imm
    case 0x05: A |= read(addrZeroPage()); updateZNFlags(A); break;        // ORA zp
    case 0x15: A |= read(addrZeroPageX()); updateZNFlags(A); break;       // ORA zp,X
    case 0x0D: A |= read(addrAbsolute()); updateZNFlags(A); break;        // ORA abs
    case 0x1D: A |= read(addrAbsoluteX()); updateZNFlags(A); break;       // ORA abs,X
    case 0x19: A |= read(addrAbsoluteY()); updateZNFlags(A); break;       // ORA abs,Y
    case 0x01: A |= read(addrIndirectX()); updateZNFlags(A); break;       // ORA (zp,X)
    case 0x11: A |= read(addrIndirectY()); updateZNFlags(A); break;       // ORA (zp),Y

    // ==================== EOR ====================
    case 0x49: A ^= fetch(); updateZNFlags(A); break;                     // EOR #imm
    case 0x45: A ^= read(addrZeroPage()); updateZNFlags(A); break;        // EOR zp
    case 0x55: A ^= read(addrZeroPageX()); updateZNFlags(A); break;       // EOR zp,X
    case 0x4D: A ^= read(addrAbsolute()); updateZNFlags(A); break;        // EOR abs
    case 0x5D: A ^= read(addrAbsoluteX()); updateZNFlags(A); break;       // EOR abs,X
    case 0x59: A ^= read(addrAbsoluteY()); updateZNFlags(A); break;       // EOR abs,Y
    case 0x41: A ^= read(addrIndirectX()); updateZNFlags(A); break;       // EOR (zp,X)
    case 0x51: A ^= read(addrIndirectY()); updateZNFlags(A); break;       // EOR (zp),Y

    // ==================== ASL (Arithmetic Shift Left) ====================
    case 0x0A: { // ASL A
        setFlag(FLAG_C, (A & 0x80) != 0);
        A <<= 1;
        updateZNFlags(A);
        break;
    }
    case 0x06: case 0x16: case 0x0E: case 0x1E: {
        uint16_t addr;
        switch (opcode) {
            case 0x06: addr = addrZeroPage(); break;
            case 0x16: addr = addrZeroPageX(); break;
            case 0x0E: addr = addrAbsolute(); break;
            case 0x1E: addr = addrAbsoluteX(); break;
            default: addr = 0; break;
        }
        uint8_t val = read(addr);
        setFlag(FLAG_C, (val & 0x80) != 0);
        val <<= 1;
        write(addr, val);
        updateZNFlags(val);
        break;
    }

    // ==================== LSR (Logical Shift Right) ====================
    case 0x4A: { // LSR A
        setFlag(FLAG_C, (A & 0x01) != 0);
        A >>= 1;
        updateZNFlags(A);
        break;
    }
    case 0x46: case 0x56: case 0x4E: case 0x5E: {
        uint16_t addr;
        switch (opcode) {
            case 0x46: addr = addrZeroPage(); break;
            case 0x56: addr = addrZeroPageX(); break;
            case 0x4E: addr = addrAbsolute(); break;
            case 0x5E: addr = addrAbsoluteX(); break;
            default: addr = 0; break;
        }
        uint8_t val = read(addr);
        setFlag(FLAG_C, (val & 0x01) != 0);
        val >>= 1;
        write(addr, val);
        updateZNFlags(val);
        break;
    }

    // ==================== ROL (Rotate Left) ====================
    case 0x2A: { // ROL A
        uint8_t carry = getFlag(FLAG_C) ? 1 : 0;
        setFlag(FLAG_C, (A & 0x80) != 0);
        A = (A << 1) | carry;
        updateZNFlags(A);
        break;
    }
    case 0x26: case 0x36: case 0x2E: case 0x3E: {
        uint16_t addr;
        switch (opcode) {
            case 0x26: addr = addrZeroPage(); break;
            case 0x36: addr = addrZeroPageX(); break;
            case 0x2E: addr = addrAbsolute(); break;
            case 0x3E: addr = addrAbsoluteX(); break;
            default: addr = 0; break;
        }
        uint8_t val = read(addr);
        uint8_t carry = getFlag(FLAG_C) ? 1 : 0;
        setFlag(FLAG_C, (val & 0x80) != 0);
        val = (val << 1) | carry;
        write(addr, val);
        updateZNFlags(val);
        break;
    }

    // ==================== ROR (Rotate Right) ====================
    case 0x6A: { // ROR A
        uint8_t carry = getFlag(FLAG_C) ? 0x80 : 0;
        setFlag(FLAG_C, (A & 0x01) != 0);
        A = (A >> 1) | carry;
        updateZNFlags(A);
        break;
    }
    case 0x66: case 0x76: case 0x6E: case 0x7E: {
        uint16_t addr;
        switch (opcode) {
            case 0x66: addr = addrZeroPage(); break;
            case 0x76: addr = addrZeroPageX(); break;
            case 0x6E: addr = addrAbsolute(); break;
            case 0x7E: addr = addrAbsoluteX(); break;
            default: addr = 0; break;
        }
        uint8_t val = read(addr);
        uint8_t carry = getFlag(FLAG_C) ? 0x80 : 0;
        setFlag(FLAG_C, (val & 0x01) != 0);
        val = (val >> 1) | carry;
        write(addr, val);
        updateZNFlags(val);
        break;
    }

    // ==================== INC (Increment Memory) ====================
    case 0xE6: case 0xF6: case 0xEE: case 0xFE: {
        uint16_t addr;
        switch (opcode) {
            case 0xE6: addr = addrZeroPage(); break;
            case 0xF6: addr = addrZeroPageX(); break;
            case 0xEE: addr = addrAbsolute(); break;
            case 0xFE: addr = addrAbsoluteX(); break;
            default: addr = 0; break;
        }
        uint8_t val = read(addr) + 1;
        write(addr, val);
        updateZNFlags(val);
        break;
    }

    // ==================== DEC (Decrement Memory) ====================
    case 0xC6: case 0xD6: case 0xCE: case 0xDE: {
        uint16_t addr;
        switch (opcode) {
            case 0xC6: addr = addrZeroPage(); break;
            case 0xD6: addr = addrZeroPageX(); break;
            case 0xCE: addr = addrAbsolute(); break;
            case 0xDE: addr = addrAbsoluteX(); break;
            default: addr = 0; break;
        }
        uint8_t val = read(addr) - 1;
        write(addr, val);
        updateZNFlags(val);
        break;
    }

    // ==================== INX, INY, DEX, DEY ====================
    case 0xE8: X++; updateZNFlags(X); break;  // INX
    case 0xC8: Y++; updateZNFlags(Y); break;  // INY
    case 0xCA: X--; updateZNFlags(X); break;  // DEX
    case 0x88: Y--; updateZNFlags(Y); break;  // DEY

    // ==================== CMP ====================
    case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1: {
        uint8_t val;
        switch (opcode) {
            case 0xC9: val = fetch(); break;
            case 0xC5: val = read(addrZeroPage()); break;
            case 0xD5: val = read(addrZeroPageX()); break;
            case 0xCD: val = read(addrAbsolute()); break;
            case 0xDD: val = read(addrAbsoluteX()); break;
            case 0xD9: val = read(addrAbsoluteY()); break;
            case 0xC1: val = read(addrIndirectX()); break;
            case 0xD1: val = read(addrIndirectY()); break;
            default: val = 0; break;
        }
        setFlag(FLAG_C, A >= val);
        setFlag(FLAG_Z, A == val);
        setFlag(FLAG_N, ((A - val) & 0x80) != 0);
        break;
    }

    // ==================== CPX ====================
    case 0xE0: { uint8_t v = fetch(); setFlag(FLAG_C, X >= v); setFlag(FLAG_Z, X == v); setFlag(FLAG_N, ((X - v) & 0x80) != 0); break; }
    case 0xE4: { uint8_t v = read(addrZeroPage()); setFlag(FLAG_C, X >= v); setFlag(FLAG_Z, X == v); setFlag(FLAG_N, ((X - v) & 0x80) != 0); break; }
    case 0xEC: { uint8_t v = read(addrAbsolute()); setFlag(FLAG_C, X >= v); setFlag(FLAG_Z, X == v); setFlag(FLAG_N, ((X - v) & 0x80) != 0); break; }

    // ==================== CPY ====================
    case 0xC0: { uint8_t v = fetch(); setFlag(FLAG_C, Y >= v); setFlag(FLAG_Z, Y == v); setFlag(FLAG_N, ((Y - v) & 0x80) != 0); break; }
    case 0xC4: { uint8_t v = read(addrZeroPage()); setFlag(FLAG_C, Y >= v); setFlag(FLAG_Z, Y == v); setFlag(FLAG_N, ((Y - v) & 0x80) != 0); break; }
    case 0xCC: { uint8_t v = read(addrAbsolute()); setFlag(FLAG_C, Y >= v); setFlag(FLAG_Z, Y == v); setFlag(FLAG_N, ((Y - v) & 0x80) != 0); break; }

    // ==================== BIT ====================
    case 0x24: case 0x2C: {
        uint8_t val = (opcode == 0x24) ? read(addrZeroPage()) : read(addrAbsolute());
        setFlag(FLAG_Z, (A & val) == 0);
        setFlag(FLAG_N, (val & 0x80) != 0);
        setFlag(FLAG_V, (val & 0x40) != 0);
        break;
    }

    // ==================== Branches ====================
    case 0x10: { int8_t off = (int8_t)fetch(); if (!getFlag(FLAG_N)) { PC += off; cycles++; } break; } // BPL
    case 0x30: { int8_t off = (int8_t)fetch(); if (getFlag(FLAG_N))  { PC += off; cycles++; } break; } // BMI
    case 0x50: { int8_t off = (int8_t)fetch(); if (!getFlag(FLAG_V)) { PC += off; cycles++; } break; } // BVC
    case 0x70: { int8_t off = (int8_t)fetch(); if (getFlag(FLAG_V))  { PC += off; cycles++; } break; } // BVS
    case 0x90: { int8_t off = (int8_t)fetch(); if (!getFlag(FLAG_C)) { PC += off; cycles++; } break; } // BCC
    case 0xB0: { int8_t off = (int8_t)fetch(); if (getFlag(FLAG_C))  { PC += off; cycles++; } break; } // BCS
    case 0xD0: { int8_t off = (int8_t)fetch(); if (!getFlag(FLAG_Z)) { PC += off; cycles++; } break; } // BNE
    case 0xF0: { int8_t off = (int8_t)fetch(); if (getFlag(FLAG_Z))  { PC += off; cycles++; } break; } // BEQ

    // ==================== JMP ====================
    case 0x4C: PC = fetchWord(); break;  // JMP abs
    case 0x6C: { // JMP indirect
        uint16_t ptr = fetchWord();
        uint8_t lo = read(ptr);
        // 6502 bug: 如果低字节是 0xFF，高字节从同一页读取
        uint8_t hi = read((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
        PC = (hi << 8) | lo;
        break;
    }

    // ==================== JSR / RTS ====================
    case 0x20: { // JSR
        uint16_t addr = fetchWord();
        pushWord(PC - 1);
        PC = addr;
        break;
    }
    case 0x60: { // RTS
        PC = popWord() + 1;
        break;
    }

    // ==================== Stack ====================
    case 0x48: push(A); break;                                    // PHA
    case 0x68: A = pop(); updateZNFlags(A); break;                // PLA
    case 0x08: push(P | 0x10); break;                             // PHP (B flag set)
    case 0x28: P = (pop() & 0xEF) | 0x20; break;                  // PLP (ignore B, set bit 5)

    // ==================== Register Transfers ====================
    case 0xAA: X = A; updateZNFlags(X); break;  // TAX
    case 0xA8: Y = A; updateZNFlags(Y); break;  // TAY
    case 0x8A: A = X; updateZNFlags(A); break;  // TXA
    case 0x98: A = Y; updateZNFlags(A); break;  // TYA
    case 0xBA: X = SP; updateZNFlags(X); break; // TSX
    case 0x9A: SP = X; break;                   // TXS

    // ==================== Flag Operations ====================
    case 0x18: setFlag(FLAG_C, false); break;  // CLC
    case 0x38: setFlag(FLAG_C, true); break;   // SEC
    case 0x58: setFlag(FLAG_I, false); break;  // CLI
    case 0x78: setFlag(FLAG_I, true); break;   // SEI
    case 0xD8: setFlag(FLAG_D, false); break;  // CLD
    case 0xF8: setFlag(FLAG_D, true); break;   // SED
    case 0xB8: setFlag(FLAG_V, false); break;  // CLV

    // ==================== BRK / RTI ====================
    case 0x00: { // BRK
        PC++;
        pushWord(PC);
        push(P | 0x10);  // B flag set
        setFlag(FLAG_I, true);
        PC = read(0xFFFE) | (read(0xFFFF) << 8);
        break;
    }
    case 0x40: { // RTI
        P = (pop() & 0xEF) | 0x20;
        PC = popWord();
        break;
    }

    // ==================== NOP ====================
    case 0xEA: break;

    // ==================== Unofficial NOPs (common ones) ====================
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: break;  // NOP implied
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: fetch(); break;    // NOP #imm (2 bytes)
    case 0x04: case 0x44: case 0x64: fetch(); break;                           // NOP zp (2 bytes)
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: fetch(); break; // NOP zp,X (2 bytes)
    case 0x0C: fetchWord(); break;                                              // NOP abs (3 bytes)
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: fetchWord(); break; // NOP abs,X (3 bytes)

    default: {
        // 未实现的指令 - 静默忽略
        // 调试时可以打开:
        // Serial.printf("[CPU] Unknown opcode: $%02X at PC: $%04X\n", opcode, PC - 1);
        break;
    }
    }

    return cycles;
}

// ============================================================================
// Save State
// ============================================================================

size_t CPU6502::getStateSize() const {
    return sizeof(A) + sizeof(X) + sizeof(Y) + sizeof(SP) + sizeof(P) + sizeof(PC);
}

void CPU6502::saveState(uint8_t* buf, size_t& offset) const {
    buf[offset++] = A;
    buf[offset++] = X;
    buf[offset++] = Y;
    buf[offset++] = SP;
    buf[offset++] = P;
    buf[offset++] = PC & 0xFF;
    buf[offset++] = (PC >> 8) & 0xFF;
}

void CPU6502::loadState(const uint8_t* buf, size_t& offset) {
    A = buf[offset++];
    X = buf[offset++];
    Y = buf[offset++];
    SP = buf[offset++];
    P = buf[offset++];
    PC = buf[offset] | (buf[offset + 1] << 8);
    offset += 2;
}