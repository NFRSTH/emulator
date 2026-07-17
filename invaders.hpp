#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static uint8_t inv_parity(uint8_t v) {
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return (v & 1) ^ 1;
}

struct Invaders8080 {
    uint8_t a, b, c, d, e, h, l;
    uint8_t f; // S:7 Z:6 0:5 AC:4 0:3 P:2 1:1 CY:0
    uint16_t sp, pc;
    bool int_enable, halted;

    uint8_t memory[0x4000];

    uint16_t shift_reg;
    uint8_t shift_off;

    uint8_t in0, in1, in2;
    bool int_pending;

    static const int W = 256, H = 224;
    uint32_t framebuffer[W * H];

    uint16_t get_bc() { return (b << 8) | c; }
    void set_bc(uint16_t v) { b = v >> 8; c = v & 0xFF; }
    uint16_t get_de() { return (d << 8) | e; }
    void set_de(uint16_t v) { d = v >> 8; e = v & 0xFF; }
    uint16_t get_hl() { return (h << 8) | l; }
    void set_hl(uint16_t v) { h = v >> 8; l = v & 0xFF; }

    uint8_t rb(uint16_t a) { return memory[a & 0x3FFF]; }
    void wb(uint16_t a, uint8_t v) { a &= 0x3FFF; if (a >= 0x2000) memory[a] = v; }
    uint16_t rw(uint16_t a) { return rb(a) | (rb(a + 1) << 8); }
    void ww(uint16_t a, uint16_t v) { wb(a, v & 0xFF); wb(a + 1, v >> 8); }

    void push(uint16_t v) { sp -= 2; ww(sp, v); }
    uint16_t pop() { uint16_t v = rw(sp); sp += 2; return v; }

    // Set S, Z, P flags preserving CY(0), AC(4), and bit1(1)
    void set_szp(uint8_t v) {
        f = (f & 0x13) | (v & 0x80) | (v == 0 ? 0x40 : 0) | (inv_parity(v) ? 0x04 : 0);
    }

    void init() {
        memset(this, 0, sizeof(*this));
        f = 0x02;
        pc = 0;
        int_enable = false;
        halted = false;
        shift_reg = 0;
        shift_off = 0;
        in0 = in1 = in2 = 0;
        int_pending = false;
    }

    bool load(const char* path) {
        FILE* fp = fopen(path, "rb");
        if (!fp) return false;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz > 0x2000) sz = 0x2000;
        fread(memory, 1, sz, fp);
        fclose(fp);
        return true;
    }

    void handle_interrupt() {
        if (!int_pending || !int_enable) return;
        int_pending = false;
        int_enable = false;
        halted = false;
        push(pc);
        pc = 0x08;
    }

    int step() {
        if (halted) {
            if (int_pending && int_enable) handle_interrupt();
            else return 4;
        }
        handle_interrupt();

        uint8_t op = rb(pc++);
        switch (op) {
            case 0x00: return 4;
            case 0x01: set_bc(rw(pc)); pc += 2; return 10;
            case 0x02: wb(get_bc(), a); return 7;
            case 0x03: set_bc(get_bc() + 1); return 5;
            case 0x04: f = (f & 0x01) | ((b & 0x0F) == 0x0F ? 0x10 : 0); b++; set_szp(b); return 5;
            case 0x05: f = (f & 0x01) | 0x40 | ((b & 0x0F) == 0 ? 0x10 : 0); b--; set_szp(b); return 5;
            case 0x06: b = rb(pc++); return 7;
            case 0x07: f = (f & 0x12) | ((a >> 7) & 1); a = (a << 1) | (a >> 7); return 4;
            case 0x08: return 4;
            case 0x09: { uint32_t r = get_hl() + get_bc(); f = (f & 0x12) | (r > 0xFFFF ? 1 : 0); set_hl(r); return 10; }
            case 0x0A: a = rb(get_bc()); return 7;
            case 0x0B: set_bc(get_bc() - 1); return 5;
            case 0x0C: f = (f & 0x01) | ((c & 0x0F) == 0x0F ? 0x10 : 0); c++; set_szp(c); return 5;
            case 0x0D: f = (f & 0x01) | 0x40 | ((c & 0x0F) == 0 ? 0x10 : 0); c--; set_szp(c); return 5;
            case 0x0E: c = rb(pc++); return 7;
            case 0x0F: f = (f & 0x12) | (a & 1); a = (a >> 1) | (a << 7); return 4;
            case 0x10: return 4;
            case 0x11: set_de(rw(pc)); pc += 2; return 10;
            case 0x12: wb(get_de(), a); return 7;
            case 0x13: set_de(get_de() + 1); return 5;
            case 0x14: f = (f & 0x01) | ((d & 0x0F) == 0x0F ? 0x10 : 0); d++; set_szp(d); return 5;
            case 0x15: f = (f & 0x01) | 0x40 | ((d & 0x0F) == 0 ? 0x10 : 0); d--; set_szp(d); return 5;
            case 0x16: d = rb(pc++); return 7;
            case 0x17: { uint8_t cy = f & 1; f = (f & 0x12) | ((a >> 7) & 1); a = (a << 1) | cy; return 4; }
            case 0x18: return 4;
            case 0x19: { uint32_t r = get_hl() + get_de(); f = (f & 0x12) | (r > 0xFFFF ? 1 : 0); set_hl(r); return 10; }
            case 0x1A: a = rb(get_de()); return 7;
            case 0x1B: set_de(get_de() - 1); return 5;
            case 0x1C: f = (f & 0x01) | ((e & 0x0F) == 0x0F ? 0x10 : 0); e++; set_szp(e); return 5;
            case 0x1D: f = (f & 0x01) | 0x40 | ((e & 0x0F) == 0 ? 0x10 : 0); e--; set_szp(e); return 5;
            case 0x1E: e = rb(pc++); return 7;
            case 0x1F: { uint8_t cy = f & 1; f = (f & 0x12) | (a & 1); a = (a >> 1) | (cy << 7); return 4; }
            case 0x20: return 4;
            case 0x21: set_hl(rw(pc)); pc += 2; return 10;
            case 0x22: { uint16_t a2 = rw(pc); pc += 2; wb(a2, l); wb(a2 + 1, h); return 16; }
            case 0x23: set_hl(get_hl() + 1); return 5;
            case 0x24: f = (f & 0x01) | ((h & 0x0F) == 0x0F ? 0x10 : 0); h++; set_szp(h); return 5;
            case 0x25: f = (f & 0x01) | 0x40 | ((h & 0x0F) == 0 ? 0x10 : 0); h--; set_szp(h); return 5;
            case 0x26: h = rb(pc++); return 7;
            case 0x27: { // DAA
                uint16_t v = a; uint8_t cy = f & 1, ac = (f >> 4) & 1;
                if (ac || (v & 0x0F) > 9) { v += 6; }
                if (cy || (v & 0xF0) > 0x90 || (v > 0x99 && !ac)) { v += 0x60; f |= 1; } else f &= ~1;
                a = v & 0xFF; set_szp(a); return 4;
            }
            case 0x28: return 4;
            case 0x29: { uint32_t r = get_hl() + get_hl(); f = (f & 0x12) | (r > 0xFFFF ? 1 : 0); set_hl(r); return 10; }
            case 0x2A: { uint16_t a2 = rw(pc); pc += 2; l = rb(a2); h = rb(a2 + 1); return 16; }
            case 0x2B: set_hl(get_hl() - 1); return 5;
            case 0x2C: f = (f & 0x01) | ((l & 0x0F) == 0x0F ? 0x10 : 0); l++; set_szp(l); return 5;
            case 0x2D: f = (f & 0x01) | 0x40 | ((l & 0x0F) == 0 ? 0x10 : 0); l--; set_szp(l); return 5;
            case 0x2E: l = rb(pc++); return 7;
            case 0x2F: a = ~a; return 4;
            case 0x30: return 4;
            case 0x31: sp = rw(pc); pc += 2; return 10;
            case 0x32: { uint16_t a2 = rw(pc); pc += 2; wb(a2, a); return 13; }
            case 0x33: sp++; return 5;
            case 0x34: { uint8_t v = rb(get_hl()); f = (f & 0x01) | ((v & 0x0F) == 0x0F ? 0x10 : 0); v++; wb(get_hl(), v); set_szp(v); return 10; }
            case 0x35: { uint8_t v = rb(get_hl()); f = (f & 0x01) | ((v & 0x0F) == 0 ? 0x10 : 0) | 0x40; v--; wb(get_hl(), v); set_szp(v); return 10; }
            case 0x36: wb(get_hl(), rb(pc++)); return 10;
            case 0x37: f |= 1; return 4;
            case 0x38: return 4;
            case 0x39: { uint32_t r = get_hl() + sp; f = (f & 0x12) | (r > 0xFFFF ? 1 : 0); set_hl(r); return 10; }
            case 0x3A: { uint16_t a2 = rw(pc); pc += 2; a = rb(a2); return 13; }
            case 0x3B: sp--; return 5;
            case 0x3C: f = (f & 0x01) | ((a & 0x0F) == 0x0F ? 0x10 : 0); a++; set_szp(a); return 5;
            case 0x3D: f = (f & 0x01) | 0x40 | ((a & 0x0F) == 0 ? 0x10 : 0); a--; set_szp(a); return 5;
            case 0x3E: a = rb(pc++); return 7;
            case 0x3F: f = (f & 0xFE) | ((f & 1) ^ 1); return 4;

            case 0x40: return 4; case 0x41: b = c; return 4;
            case 0x42: b = d; return 4; case 0x43: b = e; return 4;
            case 0x44: b = h; return 4; case 0x45: b = l; return 4;
            case 0x46: b = rb(get_hl()); return 7; case 0x47: b = a; return 4;
            case 0x48: c = b; return 4; case 0x49: return 4;
            case 0x4A: c = d; return 4; case 0x4B: c = e; return 4;
            case 0x4C: c = h; return 4; case 0x4D: c = l; return 4;
            case 0x4E: c = rb(get_hl()); return 7; case 0x4F: c = a; return 4;
            case 0x50: d = b; return 4; case 0x51: d = c; return 4;
            case 0x52: return 4; case 0x53: d = e; return 4;
            case 0x54: d = h; return 4; case 0x55: d = l; return 4;
            case 0x56: d = rb(get_hl()); return 7; case 0x57: d = a; return 4;
            case 0x58: e = b; return 4; case 0x59: e = c; return 4;
            case 0x5A: e = d; return 4; case 0x5B: return 4;
            case 0x5C: e = h; return 4; case 0x5D: e = l; return 4;
            case 0x5E: e = rb(get_hl()); return 7; case 0x5F: e = a; return 4;
            case 0x60: h = b; return 4; case 0x61: h = c; return 4;
            case 0x62: h = d; return 4; case 0x63: h = e; return 4;
            case 0x64: return 4; case 0x65: h = l; return 4;
            case 0x66: h = rb(get_hl()); return 7; case 0x67: h = a; return 4;
            case 0x68: l = b; return 4; case 0x69: l = c; return 4;
            case 0x6A: l = d; return 4; case 0x6B: l = e; return 4;
            case 0x6C: l = h; return 4; case 0x6D: return 4;
            case 0x6E: l = rb(get_hl()); return 7; case 0x6F: l = a; return 4;
            case 0x70: wb(get_hl(), b); return 7; case 0x71: wb(get_hl(), c); return 7;
            case 0x72: wb(get_hl(), d); return 7; case 0x73: wb(get_hl(), e); return 7;
            case 0x74: wb(get_hl(), h); return 7; case 0x75: wb(get_hl(), l); return 7;
            case 0x76: halted = true; return 4;
            case 0x77: wb(get_hl(), a); return 7;
            case 0x78: a = b; return 4; case 0x79: a = c; return 4;
            case 0x7A: a = d; return 4; case 0x7B: a = e; return 4;
            case 0x7C: a = h; return 4; case 0x7D: a = l; return 4;
            case 0x7E: a = rb(get_hl()); return 7; case 0x7F: return 4;

            case 0x80: add(b); return 4; case 0x81: add(c); return 4;
            case 0x82: add(d); return 4; case 0x83: add(e); return 4;
            case 0x84: add(h); return 4; case 0x85: add(l); return 4;
            case 0x86: add(rb(get_hl())); return 7; case 0x87: add(a); return 4;
            case 0x88: adc(b); return 4; case 0x89: adc(c); return 4;
            case 0x8A: adc(d); return 4; case 0x8B: adc(e); return 4;
            case 0x8C: adc(h); return 4; case 0x8D: adc(l); return 4;
            case 0x8E: adc(rb(get_hl())); return 7; case 0x8F: adc(a); return 4;
            case 0x90: sub(b); return 4; case 0x91: sub(c); return 4;
            case 0x92: sub(d); return 4; case 0x93: sub(e); return 4;
            case 0x94: sub(h); return 4; case 0x95: sub(l); return 4;
            case 0x96: sub(rb(get_hl())); return 7; case 0x97: sub(a); return 4;
            case 0x98: sbb(b); return 4; case 0x99: sbb(c); return 4;
            case 0x9A: sbb(d); return 4; case 0x9B: sbb(e); return 4;
            case 0x9C: sbb(h); return 4; case 0x9D: sbb(l); return 4;
            case 0x9E: sbb(rb(get_hl())); return 7; case 0x9F: sbb(a); return 4;
            case 0xA0: ana(b); return 4; case 0xA1: ana(c); return 4;
            case 0xA2: ana(d); return 4; case 0xA3: ana(e); return 4;
            case 0xA4: ana(h); return 4; case 0xA5: ana(l); return 4;
            case 0xA6: ana(rb(get_hl())); return 7; case 0xA7: ana(a); return 4;
            case 0xA8: xra(b); return 4; case 0xA9: xra(c); return 4;
            case 0xAA: xra(d); return 4; case 0xAB: xra(e); return 4;
            case 0xAC: xra(h); return 4; case 0xAD: xra(l); return 4;
            case 0xAE: xra(rb(get_hl())); return 7; case 0xAF: xra(a); return 4;
            case 0xB0: ora(b); return 4; case 0xB1: ora(c); return 4;
            case 0xB2: ora(d); return 4; case 0xB3: ora(e); return 4;
            case 0xB4: ora(h); return 4; case 0xB5: ora(l); return 4;
            case 0xB6: ora(rb(get_hl())); return 7; case 0xB7: ora(a); return 4;
            case 0xB8: cmp(b); return 4; case 0xB9: cmp(c); return 4;
            case 0xBA: cmp(d); return 4; case 0xBB: cmp(e); return 4;
            case 0xBC: cmp(h); return 4; case 0xBD: cmp(l); return 4;
            case 0xBE: cmp(rb(get_hl())); return 7; case 0xBF: cmp(a); return 4;

            case 0xC0: if (!(f & 0x40)) { pc = pop(); return 11; } return 5;
            case 0xC1: set_bc(pop()); return 10;
            case 0xC2: { uint16_t a = rw(pc); pc += 2; if (!(f & 0x40)) pc = a; return 10; }
            case 0xC3: pc = rw(pc); return 10;
            case 0xC4: { uint16_t a = rw(pc); pc += 2; if (!(f & 0x40)) { push(pc); pc = a; } return 11; }
            case 0xC5: push(get_bc()); return 11;
            case 0xC6: add(rb(pc++)); return 7;
            case 0xC7: push(pc); pc = 0x00; return 11;
            case 0xC8: if (f & 0x40) { pc = pop(); return 11; } return 5;
            case 0xC9: pc = pop(); return 10;
            case 0xCA: { uint16_t a = rw(pc); pc += 2; if (f & 0x40) pc = a; return 10; }
            case 0xCB: return 4;
            case 0xCC: { uint16_t a = rw(pc); pc += 2; if (f & 0x40) { push(pc); pc = a; } return 11; }
            case 0xCD: { uint16_t a = rw(pc); pc += 2; push(pc); pc = a; return 17; }
            case 0xCE: adc(rb(pc++)); return 7;
            case 0xCF: push(pc); pc = 0x08; return 11;

            case 0xD0: if (!(f & 1)) { pc = pop(); return 11; } return 5;
            case 0xD1: set_de(pop()); return 10;
            case 0xD2: { uint16_t a = rw(pc); pc += 2; if (!(f & 1)) pc = a; return 10; }
            case 0xD3: {
                uint8_t port = rb(pc++);
                switch (port) {
                    case 2: shift_reg = (a << 8) | (shift_reg >> 8); break;
                    case 3: shift_off = a & 7; break;
                    case 4: break;
                    case 5: sound_port = a; break;
                }
                return 10;
            }
            case 0xD4: { uint16_t a = rw(pc); pc += 2; if (!(f & 1)) { push(pc); pc = a; } return 11; }
            case 0xD5: push(get_de()); return 11;
            case 0xD6: sub(rb(pc++)); return 7;
            case 0xD7: push(pc); pc = 0x10; return 11;
            case 0xD8: if (f & 1) { pc = pop(); return 11; } return 5;
            case 0xD9: pc = pop(); int_enable = true; return 10;
            case 0xDA: { uint16_t a = rw(pc); pc += 2; if (f & 1) pc = a; return 10; }
            case 0xDB: {
                uint8_t port = rb(pc++);
                switch (port) {
                    case 0: a = in0; break;
                    case 1: a = in1; break;
                    case 2: a = in2; break;
                    case 3: a = (shift_reg >> (8 - shift_off)) & 0xFF; break;
                    default: a = 0xFF; break;
                }
                return 10;
            }
            case 0xDC: { uint16_t a = rw(pc); pc += 2; if (f & 1) { push(pc); pc = a; } return 11; }
            case 0xDD: return 4;
            case 0xDE: sbb(rb(pc++)); return 7;
            case 0xDF: push(pc); pc = 0x18; return 11;

            case 0xE0: if (!(f & 0x04)) { pc = pop(); return 11; } return 5;
            case 0xE1: set_hl(pop()); return 10;
            case 0xE2: { uint16_t a = rw(pc); pc += 2; if (!(f & 0x04)) pc = a; return 10; }
            case 0xE3: { uint16_t t = rw(sp); ww(sp, get_hl()); set_hl(t); return 18; }
            case 0xE4: { uint16_t a = rw(pc); pc += 2; if (!(f & 0x04)) { push(pc); pc = a; } return 11; }
            case 0xE5: push(get_hl()); return 11;
            case 0xE6: ana(rb(pc++)); return 7;
            case 0xE7: push(pc); pc = 0x20; return 11;
            case 0xE8: if (f & 0x04) { pc = pop(); return 11; } return 5;
            case 0xE9: pc = get_hl(); return 4;
            case 0xEA: { uint16_t a = rw(pc); pc += 2; if (f & 0x04) pc = a; return 10; }
            case 0xEB: { uint16_t t = get_de(); set_de(get_hl()); set_hl(t); return 4; }
            case 0xEC: { uint16_t a = rw(pc); pc += 2; if (f & 0x04) { push(pc); pc = a; } return 11; }
            case 0xED: return 4;
            case 0xEE: xra(rb(pc++)); return 7;
            case 0xEF: push(pc); pc = 0x28; return 11;

            case 0xF0: if (!(f & 0x80)) { pc = pop(); return 11; } return 5;
            case 0xF1: { uint16_t v = pop(); a = v >> 8; f = (v & 0xD7) | 0x02; return 10; }
            case 0xF2: { uint16_t a = rw(pc); pc += 2; if (!(f & 0x80)) pc = a; return 10; }
            case 0xF3: int_enable = false; return 4;
            case 0xF4: { uint16_t a = rw(pc); pc += 2; if (!(f & 0x80)) { push(pc); pc = a; } return 11; }
            case 0xF5: push((a << 8) | (f | 0x02)); return 11;
            case 0xF6: ora(rb(pc++)); return 7;
            case 0xF7: push(pc); pc = 0x30; return 11;
            case 0xF8: if (f & 0x80) { pc = pop(); return 11; } return 5;
            case 0xF9: sp = get_hl(); return 5;
            case 0xFA: { uint16_t a = rw(pc); pc += 2; if (f & 0x80) pc = a; return 10; }
            case 0xFB: int_enable = true; return 4;
            case 0xFC: { uint16_t a = rw(pc); pc += 2; if (f & 0x80) { push(pc); pc = a; } return 11; }
            case 0xFD: return 4;
            case 0xFE: cmp(rb(pc++)); return 7;
            case 0xFF: push(pc); pc = 0x38; return 11;

            default: return 4;
        }
        return 4;
    }

    void add(uint8_t v) {
        uint16_t r = a + v;
        uint8_t nf = ((a & 0x0F) + (v & 0x0F)) > 0x0F ? 0x10 : 0;
        uint8_t cy = r > 0xFF ? 1 : 0;
        a = r & 0xFF;
        f = (f & 0x02) | cy | nf | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void adc(uint8_t v) {
        uint8_t cy = f & 1;
        uint16_t r = a + v + cy;
        uint8_t ac = ((a & 0x0F) + (v & 0x0F) + cy) > 0x0F ? 0x10 : 0;
        uint8_t ncy = r > 0xFF ? 1 : 0;
        a = r & 0xFF;
        f = (f & 0x02) | ncy | ac | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void sub(uint8_t v) {
        uint8_t ac = ((a & 0x0F) < (v & 0x0F)) ? 0x10 : 0;
        uint8_t cy = a < v ? 1 : 0;
        a -= v;
        f = (f & 0x02) | cy | ac | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void sbb(uint8_t v) {
        uint8_t cy = f & 1;
        uint16_t r = (uint16_t)a - v - cy;
        uint8_t ac = ((a & 0x0F) < ((v + cy) & 0x0F)) ? 0x10 : 0;
        uint8_t ncy = r > 0xFF ? 1 : 0;
        a = r & 0xFF;
        f = (f & 0x02) | ncy | ac | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void ana(uint8_t v) {
        a &= v;
        f = (f & 0x02) | 0x10 | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void xra(uint8_t v) {
        a ^= v;
        f = (f & 0x02) | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void ora(uint8_t v) {
        a |= v;
        f = (f & 0x02) | (a & 0x80) | (a == 0 ? 0x40 : 0) | (inv_parity(a) ? 0x04 : 0);
    }
    void cmp(uint8_t v) {
        uint8_t r = a - v;
        uint8_t ac = ((a & 0x0F) < (v & 0x0F)) ? 0x10 : 0;
        uint8_t cy = a < v ? 1 : 0;
        f = (f & 0x02) | 0x40 | cy | ac | (r & 0x80) | (r == 0 ? 0x40 : 0) | (inv_parity(r) ? 0x04 : 0);
    }

    uint8_t sound_port;

    void render() {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                framebuffer[y * W + x] = (memory[0x2400 + y * 32 + (x >> 3)] >> (7 - (x & 7))) & 1
                    ? 0x00CC33FF : 0x001A0033;
    }

    void gen_audio(int16_t* buf, int samples, int sr) {
        static int phase = 0;
        // Port 5 bits select sound effect: 0x01=ufo, 0x02=shoot, 0x04=explosion, 0x08=invader_hit, 0x10=enemy_ufo
        int active = sound_port & 0x1F;
        if (active) {
            // Simple tones per sound
            int freq = 440;
            if (active & 0x02) freq = 880;    // shoot
            else if (active & 0x04) freq = 220; // explosion
            else if (active & 0x08) freq = 660; // invader
            else if (active & 0x10) freq = 110; // ufo
            else if (active & 0x01) freq = 550; // extra
            int inc = freq * 65536 / sr;
            for (int i = 0; i < samples; i++) {
                phase += inc;
                uint8_t vol = (active & 0x04) ? 10 : 8; // quieter for explosion
                buf[i] = (phase & 0x10000) ? vol * 600 : -vol * 600;
            }
        } else {
            memset(buf, 0, samples * sizeof(int16_t));
        }
    }
};
