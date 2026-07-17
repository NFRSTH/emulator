#pragma once
#include <cstdint>
#include <cstdio>
#include "mmu.hpp"

struct CPU {
    MMU* mmu;
    uint16_t PC, SP, AF, BC, DE, HL;
    bool halted;
    bool ime;
    bool ime_scheduled;

    CPU() {
        PC = 0x0100;
        SP = 0xFFFE;
        AF = 0x01B0;
        BC = 0x0013;
        DE = 0x00D8;
        HL = 0x014D;
        halted = false;
        ime = false;
        ime_scheduled = false;
    }

    uint8_t& A() { return ((uint8_t*)&AF)[1]; }
    uint8_t& F() { return ((uint8_t*)&AF)[0]; }
    uint8_t& B() { return ((uint8_t*)&BC)[1]; }
    uint8_t& C() { return ((uint8_t*)&BC)[0]; }
    uint8_t& D() { return ((uint8_t*)&DE)[1]; }
    uint8_t& E() { return ((uint8_t*)&DE)[0]; }
    uint8_t& H() { return ((uint8_t*)&HL)[1]; }
    uint8_t& L() { return ((uint8_t*)&HL)[0]; }

    void set_z(bool v) { F() = (F() & ~0x80) | (v ? 0x80 : 0); }
    void set_n(bool v) { F() = (F() & ~0x40) | (v ? 0x40 : 0); }
    void set_h(bool v) { F() = (F() & ~0x20) | (v ? 0x20 : 0); }
    void set_c(bool v) { F() = (F() & ~0x10) | (v ? 0x10 : 0); }
    bool get_z() { return F() & 0x80; }
    bool get_n() { return F() & 0x40; }
    bool get_h() { return F() & 0x20; }
    bool get_c() { return F() & 0x10; }

    uint8_t rb(uint16_t a) { return mmu->read(a); }
    void wb(uint16_t a, uint8_t v) { mmu->write(a, v); }
    uint16_t rw(uint16_t a) { return mmu->read_word(a); }
    void ww(uint16_t a, uint16_t v) { mmu->write_word(a, v); }

    void push(uint16_t v) { SP -= 2; ww(SP, v); }
    uint16_t pop() { uint16_t v = rw(SP); SP += 2; return v; }

    int tick() {
        if (ime_scheduled) {
            ime = true;
            ime_scheduled = false;
        }

        if (halted) {
            uint8_t flags = rb(0xFF0F);
            uint8_t ie = rb(0xFFFF);
            if (flags & ie) halted = false;
            return 4;
        }

        if (ime) {
            uint8_t flags = rb(0xFF0F);
            uint8_t ie = rb(0xFFFF);
            if (flags & ie) {
                for (int i = 0; i < 5; i++) {
                    if (flags & (1 << i) && ie & (1 << i)) {
                        ime = false;
                        halted = false;
                        uint16_t vectors[5] = {0x40, 0x48, 0x50, 0x58, 0x60};
                        push(PC);
                        PC = vectors[i];
                        wb(0xFF0F, flags & ~(1 << i));
                        return 20;
                    }
                }
            }
        }

        uint8_t op = rb(PC++);

        switch (op) {
            // NOP
            case 0x00: return 4;

            // LD BC, imm16
            case 0x01: BC = rw(PC); PC += 2; return 12;
            // LD (BC), A
            case 0x02: wb(BC, A()); return 8;
            // INC BC
            case 0x03: BC++; return 8;
            // INC B
            case 0x04: inc(B()); return 4;
            // DEC B
            case 0x05: dec(B()); return 4;
            // LD B, imm8
            case 0x06: B() = rb(PC++); return 8;
            // RLCA
            case 0x07: { A() = (A() << 1) | (A() >> 7); set_c(A() & 1); set_z(0); set_n(0); set_h(0); return 4; }
            // LD (imm16), SP
            case 0x08: ww(rw(PC), SP); PC += 2; return 20;
            // ADD HL, BC
            case 0x09: add_hl(BC); return 8;
            // LD A, (BC)
            case 0x0A: A() = rb(BC); return 8;
            // DEC BC
            case 0x0B: BC--; return 8;
            // INC C
            case 0x0C: inc(C()); return 4;
            // DEC C
            case 0x0D: dec(C()); return 4;
            // LD C, imm8
            case 0x0E: C() = rb(PC++); return 8;
            // RRCA
            case 0x0F: { set_c(A() & 1); A() = (A() >> 1) | (A() << 7); set_z(0); set_n(0); set_h(0); return 4; }

            // LD DE, imm16
            case 0x11: DE = rw(PC); PC += 2; return 12;
            // LD (DE), A
            case 0x12: wb(DE, A()); return 8;
            // INC DE
            case 0x13: DE++; return 8;
            // INC D
            case 0x14: inc(D()); return 4;
            // DEC D
            case 0x15: dec(D()); return 4;
            // LD D, imm8
            case 0x16: D() = rb(PC++); return 8;
            // RLA
            case 0x17: { uint8_t old_c = get_c() ? 1 : 0; set_c(A() & 0x80); A() = (A() << 1) | old_c; set_z(0); set_n(0); set_h(0); return 4; }
            // JR rel8
            case 0x18: { PC += (int8_t)rb(PC) + 1; return 12; }
            // ADD HL, DE
            case 0x19: add_hl(DE); return 8;
            // LD A, (DE)
            case 0x1A: A() = rb(DE); return 8;
            // DEC DE
            case 0x1B: DE--; return 8;
            // INC E
            case 0x1C: inc(E()); return 4;
            // DEC E
            case 0x1D: dec(E()); return 4;
            // LD E, imm8
            case 0x1E: E() = rb(PC++); return 8;
            // RRA
            case 0x1F: { uint8_t old_c = get_c() ? 1 : 0; set_c(A() & 1); A() = (A() >> 1) | (old_c << 7); set_z(0); set_n(0); set_h(0); return 4; }

            // JR NZ, rel8
            case 0x20: { int8_t r = (int8_t)rb(PC++); if (!get_z()) { PC += r; return 12; } return 8; }
            // LD HL, imm16
            case 0x21: HL = rw(PC); PC += 2; return 12;
            // LDI (HL), A
            case 0x22: wb(HL, A()); HL++; return 8;
            // INC HL
            case 0x23: HL++; return 8;
            // INC H
            case 0x24: inc(H()); return 4;
            // DEC H
            case 0x25: dec(H()); return 4;
            // LD H, imm8
            case 0x26: H() = rb(PC++); return 8;
            // DAA
            case 0x27: daa(); return 4;
            // JR Z, rel8
            case 0x28: { int8_t r = (int8_t)rb(PC++); if (get_z()) { PC += r; return 12; } return 8; }
            // ADD HL, HL
            case 0x29: add_hl(HL); return 8;
            // LDI A, (HL)
            case 0x2A: A() = rb(HL); HL++; return 8;
            // DEC HL
            case 0x2B: HL--; return 8;
            // INC L
            case 0x2C: inc(L()); return 4;
            // DEC L
            case 0x2D: dec(L()); return 4;
            // LD L, imm8
            case 0x2E: L() = rb(PC++); return 8;
            // CPL
            case 0x2F: A() = ~A(); set_n(1); set_h(1); return 4;

            // JR NC, rel8
            case 0x30: { int8_t r = (int8_t)rb(PC++); if (!get_c()) { PC += r; return 12; } return 8; }
            // LD SP, imm16
            case 0x31: SP = rw(PC); PC += 2; return 12;
            // LDD (HL), A
            case 0x32: wb(HL, A()); HL--; return 8;
            // INC SP
            case 0x33: SP++; return 8;
            // INC (HL)
            case 0x34: { uint8_t v = rb(HL); inc(v); wb(HL, v); return 12; }
            // DEC (HL)
            case 0x35: { uint8_t v = rb(HL); dec(v); wb(HL, v); return 12; }
            // LD (HL), imm8
            case 0x36: wb(HL, rb(PC++)); return 12;
            // SCF
            case 0x37: set_c(1); set_n(0); set_h(0); return 4;
            // JR C, rel8
            case 0x38: { int8_t r = (int8_t)rb(PC++); if (get_c()) { PC += r; return 12; } return 8; }
            // ADD HL, SP
            case 0x39: add_hl(SP); return 8;
            // LDD A, (HL)
            case 0x3A: A() = rb(HL); HL--; return 8;
            // DEC SP
            case 0x3B: SP--; return 8;
            // INC A
            case 0x3C: inc(A()); return 4;
            // DEC A
            case 0x3D: dec(A()); return 4;
            // LD A, imm8
            case 0x3E: A() = rb(PC++); return 8;
            // CCF
            case 0x3F: set_c(!get_c()); set_n(0); set_h(0); return 4;

            // LD B, B
            case 0x40: return 4;
            // LD B, C
            case 0x41: B() = C(); return 4;
            // LD B, D
            case 0x42: B() = D(); return 4;
            // LD B, E
            case 0x43: B() = E(); return 4;
            // LD B, H
            case 0x44: B() = H(); return 4;
            // LD B, L
            case 0x45: B() = L(); return 4;
            // LD B, (HL)
            case 0x46: B() = rb(HL); return 8;
            // LD B, A
            case 0x47: B() = A(); return 4;

            // LD C, B
            case 0x48: C() = B(); return 4;
            // LD C, C
            case 0x49: return 4;
            // LD C, D
            case 0x4A: C() = D(); return 4;
            // LD C, E
            case 0x4B: C() = E(); return 4;
            // LD C, H
            case 0x4C: C() = H(); return 4;
            // LD C, L
            case 0x4D: C() = L(); return 4;
            // LD C, (HL)
            case 0x4E: C() = rb(HL); return 8;
            // LD C, A
            case 0x4F: C() = A(); return 4;

            // LD D, B
            case 0x50: D() = B(); return 4;
            // LD D, C
            case 0x51: D() = C(); return 4;
            // LD D, D
            case 0x52: return 4;
            // LD D, E
            case 0x53: D() = E(); return 4;
            // LD D, H
            case 0x54: D() = H(); return 4;
            // LD D, L
            case 0x55: D() = L(); return 4;
            // LD D, (HL)
            case 0x56: D() = rb(HL); return 8;
            // LD D, A
            case 0x57: D() = A(); return 4;

            // LD E, B
            case 0x58: E() = B(); return 4;
            // LD E, C
            case 0x59: E() = C(); return 4;
            // LD E, D
            case 0x5A: E() = D(); return 4;
            // LD E, E
            case 0x5B: return 4;
            // LD E, H
            case 0x5C: E() = H(); return 4;
            // LD E, L
            case 0x5D: E() = L(); return 4;
            // LD E, (HL)
            case 0x5E: E() = rb(HL); return 8;
            // LD E, A
            case 0x5F: E() = A(); return 4;

            // LD H, B
            case 0x60: H() = B(); return 4;
            // LD H, C
            case 0x61: H() = C(); return 4;
            // LD H, D
            case 0x62: H() = D(); return 4;
            // LD H, E
            case 0x63: H() = E(); return 4;
            // LD H, H
            case 0x64: return 4;
            // LD H, L
            case 0x65: H() = L(); return 4;
            // LD H, (HL)
            case 0x66: H() = rb(HL); return 8;
            // LD H, A
            case 0x67: H() = A(); return 4;

            // LD L, B
            case 0x68: L() = B(); return 4;
            // LD L, C
            case 0x69: L() = C(); return 4;
            // LD L, D
            case 0x6A: L() = D(); return 4;
            // LD L, E
            case 0x6B: L() = E(); return 4;
            // LD L, H
            case 0x6C: L() = H(); return 4;
            // LD L, L
            case 0x6D: return 4;
            // LD L, (HL)
            case 0x6E: L() = rb(HL); return 8;
            // LD L, A
            case 0x6F: L() = A(); return 4;

            // LD (HL), B
            case 0x70: wb(HL, B()); return 8;
            // LD (HL), C
            case 0x71: wb(HL, C()); return 8;
            // LD (HL), D
            case 0x72: wb(HL, D()); return 8;
            // LD (HL), E
            case 0x73: wb(HL, E()); return 8;
            // LD (HL), H
            case 0x74: wb(HL, H()); return 8;
            // LD (HL), L
            case 0x75: wb(HL, L()); return 8;
            // HALT
            case 0x76: halted = true; return 4;
            // LD (HL), A
            case 0x77: wb(HL, A()); return 8;

            // LD A, B
            case 0x78: A() = B(); return 4;
            // LD A, C
            case 0x79: A() = C(); return 4;
            // LD A, D
            case 0x7A: A() = D(); return 4;
            // LD A, E
            case 0x7B: A() = E(); return 4;
            // LD A, H
            case 0x7C: A() = H(); return 4;
            // LD A, L
            case 0x7D: A() = L(); return 4;
            // LD A, (HL)
            case 0x7E: A() = rb(HL); return 8;
            // LD A, A
            case 0x7F: return 4;

            // ADD A, B
            case 0x80: add_a(B()); return 4;
            // ADD A, C
            case 0x81: add_a(C()); return 4;
            // ADD A, D
            case 0x82: add_a(D()); return 4;
            // ADD A, E
            case 0x83: add_a(E()); return 4;
            // ADD A, H
            case 0x84: add_a(H()); return 4;
            // ADD A, L
            case 0x85: add_a(L()); return 4;
            // ADD A, (HL)
            case 0x86: add_a(rb(HL)); return 8;
            // ADD A, A
            case 0x87: add_a(A()); return 4;

            // ADC A, B
            case 0x88: adc_a(B()); return 4;
            // ADC A, C
            case 0x89: adc_a(C()); return 4;
            // ADC A, D
            case 0x8A: adc_a(D()); return 4;
            // ADC A, E
            case 0x8B: adc_a(E()); return 4;
            // ADC A, H
            case 0x8C: adc_a(H()); return 4;
            // ADC A, L
            case 0x8D: adc_a(L()); return 4;
            // ADC A, (HL)
            case 0x8E: adc_a(rb(HL)); return 8;
            // ADC A, A
            case 0x8F: adc_a(A()); return 4;

            // SUB B
            case 0x90: sub(B()); return 4;
            // SUB C
            case 0x91: sub(C()); return 4;
            // SUB D
            case 0x92: sub(D()); return 4;
            // SUB E
            case 0x93: sub(E()); return 4;
            // SUB H
            case 0x94: sub(H()); return 4;
            // SUB L
            case 0x95: sub(L()); return 4;
            // SUB (HL)
            case 0x96: sub(rb(HL)); return 8;
            // SUB A
            case 0x97: sub(A()); return 4;

            // SBC A, B
            case 0x98: sbc(B()); return 4;
            // SBC A, C
            case 0x99: sbc(C()); return 4;
            // SBC A, D
            case 0x9A: sbc(D()); return 4;
            // SBC A, E
            case 0x9B: sbc(E()); return 4;
            // SBC A, H
            case 0x9C: sbc(H()); return 4;
            // SBC A, L
            case 0x9D: sbc(L()); return 4;
            // SBC A, (HL)
            case 0x9E: sbc(rb(HL)); return 8;
            // SBC A, A
            case 0x9F: sbc(A()); return 4;

            // AND B
            case 0xA0: and_a(B()); return 4;
            // AND C
            case 0xA1: and_a(C()); return 4;
            // AND D
            case 0xA2: and_a(D()); return 4;
            // AND E
            case 0xA3: and_a(E()); return 4;
            // AND H
            case 0xA4: and_a(H()); return 4;
            // AND L
            case 0xA5: and_a(L()); return 4;
            // AND (HL)
            case 0xA6: and_a(rb(HL)); return 8;
            // AND A
            case 0xA7: and_a(A()); return 4;

            // XOR B
            case 0xA8: xor_a(B()); return 4;
            // XOR C
            case 0xA9: xor_a(C()); return 4;
            // XOR D
            case 0xAA: xor_a(D()); return 4;
            // XOR E
            case 0xAB: xor_a(E()); return 4;
            // XOR H
            case 0xAC: xor_a(H()); return 4;
            // XOR L
            case 0xAD: xor_a(L()); return 4;
            // XOR (HL)
            case 0xAE: xor_a(rb(HL)); return 8;
            // XOR A
            case 0xAF: xor_a(A()); return 4;

            // OR B
            case 0xB0: or_a(B()); return 4;
            // OR C
            case 0xB1: or_a(C()); return 4;
            // OR D
            case 0xB2: or_a(D()); return 4;
            // OR E
            case 0xB3: or_a(E()); return 4;
            // OR H
            case 0xB4: or_a(H()); return 4;
            // OR L
            case 0xB5: or_a(L()); return 4;
            // OR (HL)
            case 0xB6: or_a(rb(HL)); return 8;
            // OR A
            case 0xB7: or_a(A()); return 4;

            // CP B
            case 0xB8: cp(B()); return 4;
            // CP C
            case 0xB9: cp(C()); return 4;
            // CP D
            case 0xBA: cp(D()); return 4;
            // CP E
            case 0xBB: cp(E()); return 4;
            // CP H
            case 0xBC: cp(H()); return 4;
            // CP L
            case 0xBD: cp(L()); return 4;
            // CP (HL)
            case 0xBE: cp(rb(HL)); return 8;
            // CP A
            case 0xBF: cp(A()); return 4;

            // RET NZ
            case 0xC0: if (!get_z()) { PC = pop(); return 20; } return 8;
            // POP BC
            case 0xC1: BC = pop(); return 12;
            // JP NZ, imm16
            case 0xC2: { uint16_t a = rw(PC); PC += 2; if (!get_z()) { PC = a; return 16; } return 12; }
            // JP imm16
            case 0xC3: PC = rw(PC); return 16;
            // CALL NZ, imm16
            case 0xC4: { uint16_t a = rw(PC); PC += 2; if (!get_z()) { push(PC); PC = a; return 24; } return 12; }
            // PUSH BC
            case 0xC5: push(BC); return 16;
            // ADD A, imm8
            case 0xC6: add_a(rb(PC++)); return 8;
            // RST 00h
            case 0xC7: push(PC); PC = 0x00; return 16;
            // RET Z
            case 0xC8: if (get_z()) { PC = pop(); return 20; } return 8;
            // RET
            case 0xC9: PC = pop(); return 16;
            // JP Z, imm16
            case 0xCA: { uint16_t a = rw(PC); PC += 2; if (get_z()) { PC = a; return 16; } return 12; }
            // CB prefix
            case 0xCB: return cb();
            // CALL Z, imm16
            case 0xCC: { uint16_t a = rw(PC); PC += 2; if (get_z()) { push(PC); PC = a; return 24; } return 12; }
            // CALL imm16
            case 0xCD: { uint16_t a = rw(PC); PC += 2; push(PC); PC = a; return 24; }
            // ADC A, imm8
            case 0xCE: adc_a(rb(PC++)); return 8;
            // RST 08h
            case 0xCF: push(PC); PC = 0x08; return 16;

            // RET NC
            case 0xD0: if (!get_c()) { PC = pop(); return 20; } return 8;
            // POP DE
            case 0xD1: DE = pop(); return 12;
            // JP NC, imm16
            case 0xD2: { uint16_t a = rw(PC); PC += 2; if (!get_c()) { PC = a; return 16; } return 12; }
            // unused
            case 0xD3: return 4;
            // CALL NC, imm16
            case 0xD4: { uint16_t a = rw(PC); PC += 2; if (!get_c()) { push(PC); PC = a; return 24; } return 12; }
            // PUSH DE
            case 0xD5: push(DE); return 16;
            // SUB imm8
            case 0xD6: sub(rb(PC++)); return 8;
            // RST 10h
            case 0xD7: push(PC); PC = 0x10; return 16;
            // RET C
            case 0xD8: if (get_c()) { PC = pop(); return 20; } return 8;
            // RETI
            case 0xD9: ime = true; PC = pop(); return 16;
            // JP C, imm16
            case 0xDA: { uint16_t a = rw(PC); PC += 2; if (get_c()) { PC = a; return 16; } return 12; }
            // unused
            case 0xDB: return 4;
            // CALL C, imm16
            case 0xDC: { uint16_t a = rw(PC); PC += 2; if (get_c()) { push(PC); PC = a; return 24; } return 12; }
            // unused
            case 0xDD: return 4;
            // SBC A, imm8
            case 0xDE: sbc(rb(PC++)); return 8;
            // RST 18h
            case 0xDF: push(PC); PC = 0x18; return 16;

            // LDH (imm8), A
            case 0xE0: wb(0xFF00 + rb(PC++), A()); return 12;
            // POP HL
            case 0xE1: HL = pop(); return 12;
            // LD (C), A
            case 0xE2: wb(0xFF00 + C(), A()); return 8;
            // unused
            case 0xE3: return 4;
            // unused
            case 0xE4: return 4;
            // PUSH HL
            case 0xE5: push(HL); return 16;
            // AND imm8
            case 0xE6: and_a(rb(PC++)); return 8;
            // RST 20h
            case 0xE7: push(PC); PC = 0x20; return 16;
            // ADD SP, rel8
            case 0xE8: { int8_t r = (int8_t)rb(PC++); set_z(0); set_n(0); set_h((SP & 0xF) + (r & 0xF) > 0xF); set_c((SP & 0xFF) + (uint8_t)r > 0xFF); SP += r; return 16; }
            // JP (HL)
            case 0xE9: PC = HL; return 4;
            // LD (imm16), A
            case 0xEA: wb(rw(PC), A()); PC += 2; return 16;
            // unused
            case 0xEB: return 4;
            // unused
            case 0xEC: return 4;
            // unused
            case 0xED: return 4;
            // XOR imm8
            case 0xEE: xor_a(rb(PC++)); return 8;
            // RST 28h
            case 0xEF: push(PC); PC = 0x28; return 16;

            // LDH A, (imm8)
            case 0xF0: A() = rb(0xFF00 + rb(PC++)); return 12;
            // POP AF
            case 0xF1: AF = pop(); AF &= 0xFFF0; return 12;
            // LD A, (C)
            case 0xF2: A() = rb(0xFF00 + C()); return 8;
            // DI
            case 0xF3: ime = false; return 4;
            // unused
            case 0xF4: return 4;
            // PUSH AF
            case 0xF5: push(AF); return 16;
            // OR imm8
            case 0xF6: or_a(rb(PC++)); return 8;
            // RST 30h
            case 0xF7: push(PC); PC = 0x30; return 16;
            // LD HL, SP+rel8
            case 0xF8: { int8_t r = (int8_t)rb(PC++); set_z(0); set_n(0); set_h((SP & 0xF) + (r & 0xF) > 0xF); set_c((SP & 0xFF) + (uint8_t)r > 0xFF); HL = SP + r; return 12; }
            // LD SP, HL
            case 0xF9: SP = HL; return 8;
            // LD A, (imm16)
            case 0xFA: A() = rb(rw(PC)); PC += 2; return 16;
            // EI
            case 0xFB: ime_scheduled = true; return 4;
            // unused
            case 0xFC: return 4;
            // unused
            case 0xFD: return 4;
            // CP imm8
            case 0xFE: cp(rb(PC++)); return 8;
            // RST 38h
            case 0xFF: push(PC); PC = 0x38; return 16;

            default: return 4;
        }
    }

    int cb() {
        uint8_t op = rb(PC++);
        uint8_t bit, reg_val, new_val;
        int reg_idx = op & 7;

        auto get_reg = [&](int idx) -> uint8_t& {
            switch (idx) {
                case 0: return B(); case 1: return C();
                case 2: return D(); case 3: return E();
                case 4: return H(); case 5: return L();
                default: static uint8_t dummy = 0; return dummy;
            }
        };

        auto get_val = [&](int idx) -> uint8_t {
            if (idx == 6) return rb(HL);
            return get_reg(idx);
        };

        auto set_val = [&](int idx, uint8_t v) {
            if (idx == 6) wb(HL, v);
            else get_reg(idx) = v;
        };

        auto rlc = [&](uint8_t v) -> uint8_t {
            set_c(v & 0x80); v = (v << 1) | (v >> 7); set_z(v == 0); set_n(0); set_h(0); return v;
        };

        auto rrc = [&](uint8_t v) -> uint8_t {
            set_c(v & 1); v = (v >> 1) | (v << 7); set_z(v == 0); set_n(0); set_h(0); return v;
        };

        auto rl = [&](uint8_t v) -> uint8_t {
            uint8_t oc = get_c() ? 1 : 0; set_c(v & 0x80); v = (v << 1) | oc; set_z(v == 0); set_n(0); set_h(0); return v;
        };

        auto rr = [&](uint8_t v) -> uint8_t {
            uint8_t oc = get_c() ? 1 : 0; set_c(v & 1); v = (v >> 1) | (oc << 7); set_z(v == 0); set_n(0); set_h(0); return v;
        };

        auto sla = [&](uint8_t v) -> uint8_t {
            set_c(v & 0x80); v <<= 1; set_z(v == 0); set_n(0); set_h(0); return v;
        };

        auto sra = [&](uint8_t v) -> uint8_t {
            set_c(v & 1); v = (v & 0x80) | (v >> 1); set_z(v == 0); set_n(0); set_h(0); return v;
        };

        auto swap = [&](uint8_t v) -> uint8_t {
            v = (v << 4) | (v >> 4); set_z(v == 0); set_n(0); set_h(0); set_c(0); return v;
        };

        auto srl = [&](uint8_t v) -> uint8_t {
            set_c(v & 1); v >>= 1; set_z(v == 0); set_n(0); set_h(0); return v;
        };

        int cycles = 8;
        if (op < 0x08) { reg_val = get_val(reg_idx); set_val(reg_idx, rlc(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x10) { reg_val = get_val(reg_idx); set_val(reg_idx, rrc(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x18) { reg_val = get_val(reg_idx); set_val(reg_idx, rl(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x20) { reg_val = get_val(reg_idx); set_val(reg_idx, rr(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x28) { reg_val = get_val(reg_idx); set_val(reg_idx, sla(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x30) { reg_val = get_val(reg_idx); set_val(reg_idx, sra(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x38) { reg_val = get_val(reg_idx); set_val(reg_idx, swap(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x40) { reg_val = get_val(reg_idx); set_val(reg_idx, srl(reg_val)); if (reg_idx == 6) cycles = 16; }
        else if (op < 0x80) {
            bit = (op >> 3) & 7; reg_val = get_val(reg_idx);
            set_z(!(reg_val & (1 << bit))); set_n(0); set_h(1);
            cycles = (reg_idx == 6) ? 16 : 8;
        }
        else if (op < 0xC0) {
            bit = (op >> 3) & 7; reg_val = get_val(reg_idx);
            set_val(reg_idx, reg_val & ~(1 << bit));
            cycles = (reg_idx == 6) ? 16 : 8;
        }
        else {
            bit = (op >> 3) & 7; reg_val = get_val(reg_idx);
            set_val(reg_idx, reg_val | (1 << bit));
            cycles = (reg_idx == 6) ? 16 : 8;
        }
        return cycles;
    }

    void inc(uint8_t& r) {
        set_h((r & 0x0F) == 0x0F);
        r++;
        set_z(r == 0);
        set_n(0);
    }

    void dec(uint8_t& r) {
        set_h((r & 0x0F) == 0);
        r--;
        set_z(r == 0);
        set_n(1);
    }

    void add_a(uint8_t v) {
        uint16_t result = A() + v;
        set_c(result > 0xFF);
        set_h((A() & 0x0F) + (v & 0x0F) > 0x0F);
        A() = result & 0xFF;
        set_z(A() == 0);
        set_n(0);
    }

    void adc_a(uint8_t v) {
        uint8_t carry = get_c() ? 1 : 0;
        uint16_t result = A() + v + carry;
        set_c(result > 0xFF);
        set_h((A() & 0x0F) + (v & 0x0F) + carry > 0x0F);
        A() = result & 0xFF;
        set_z(A() == 0);
        set_n(0);
    }

    void sub(uint8_t v) {
        set_c(A() < v);
        set_h((A() & 0x0F) < (v & 0x0F));
        A() -= v;
        set_z(A() == 0);
        set_n(1);
    }

    void sbc(uint8_t v) {
        uint8_t carry = get_c() ? 1 : 0;
        set_c(A() < v + carry);
        set_h((A() & 0x0F) < ((v & 0x0F) + carry));
        A() -= v + carry;
        set_z(A() == 0);
        set_n(1);
    }

    void and_a(uint8_t v) {
        A() &= v;
        set_z(A() == 0);
        set_n(0);
        set_h(1);
        set_c(0);
    }

    void xor_a(uint8_t v) {
        A() ^= v;
        set_z(A() == 0);
        set_n(0);
        set_h(0);
        set_c(0);
    }

    void or_a(uint8_t v) {
        A() |= v;
        set_z(A() == 0);
        set_n(0);
        set_h(0);
        set_c(0);
    }

    void cp(uint8_t v) {
        set_z(A() == v);
        set_n(1);
        set_h((A() & 0x0F) < (v & 0x0F));
        set_c(A() < v);
    }

    void add_hl(uint16_t v) {
        uint32_t result = HL + v;
        set_n(0);
        set_h((HL & 0x0FFF) + (v & 0x0FFF) > 0x0FFF);
        set_c(result > 0xFFFF);
        HL = result & 0xFFFF;
    }

    void daa() {
        uint8_t a = A();
        if (!get_n()) {
            if (get_h() || (a & 0x0F) > 9) a += 6;
            if (get_c() || a > 0x9F) a += 0x60;
        } else {
            if (get_h() || (a & 0x0F) > 9) a -= 6;
            if (get_c() || a > 0x9F) a -= 0x60;
        }
        set_c(get_c() || (a > 0x9F));
        set_z(a == 0);
        set_h(0);
        A() = a;
    }
};
