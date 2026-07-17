#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const uint32_t a26_palette[128] = {
    0x000000,0x404040,0x6C6C6C,0x909090,0xB0B0B0,0xC8C8C8,0xE0E0E0,0xF4F4F4,
    0x444400,0x646410,0x848424,0xA0A034,0xB8B840,0xD0D050,0xE8E85C,0xFCFC68,
    0x702800,0x844414,0x985C28,0xAC783C,0xBC8C4C,0xCCA05C,0xDCB468,0xECC878,
    0x841800,0x983418,0xAC5030,0xC06848,0xD0805C,0xE09470,0xECA880,0xFCBC94,
    0x880000,0x9C2020,0xB04040,0xC06060,0xD07878,0xE09090,0xECA8A8,0xFCC0C0,
    0x78005C,0x8C2070,0xA04088,0xB0609C,0xC078B0,0xD090C0,0xE0A8D0,0xF0C0E0,
    0x480078,0x602090,0x7840A4,0x8C60B8,0xA078C8,0xB490D8,0xC8A8E4,0xDCC0F4,
    0x140084,0x302098,0x4C40AC,0x685CC0,0x8078D0,0x9894E0,0xB0B0EC,0xC8C8FC,
    0x000088,0x1C209C,0x3840B0,0x505CC0,0x6878D0,0x8094E0,0x98B0EC,0xB0C8FC,
    0x00187C,0x1C3890,0x3858A8,0x5074BC,0x6890CC,0x80ACDC,0x98C4EC,0xB0DCFC,
    0x002C5C,0x1C4C78,0x386C94,0x5088AC,0x68A4C4,0x80BCD8,0x98D4EC,0xB0ECFC,
    0x003C2C,0x1C5C48,0x387C64,0x509C80,0x68B498,0x80CCB0,0x98E4C8,0xB0FCE0,
    0x004400,0x1C641C,0x388438,0x50A050,0x68BC68,0x80D480,0x98EC98,0xB0FCB0,
    0x143800,0x305C1C,0x4C7C38,0x689C50,0x80B868,0x98D080,0xB0E898,0xC8FCB0,
    0x2C2000,0x48441C,0x646438,0x808450,0x98A068,0xB0BC80,0xC8D498,0xE0ECB0,
    0x440000,0x601C1C,0x7C3838,0x985050,0xB06868,0xC88080,0xE09898,0xFCB0B0,
};

struct A2600 {
    uint8_t a, x, y, sp;
    uint8_t p; // N:7 V:6 - B:4 D:3 I:2 Z:1 C:0
    uint16_t pc;

    uint8_t ram[128];
    uint8_t rom[65536];
    uint32_t rom_mask;
    int bank_type; // 0=none, 1=F8, 2=F6, 3=F4
    int current_bank;
    int num_banks;

    // TIA
    uint8_t pf0, pf1, pf2;
    uint8_t grp0, grp1, grp0_old, grp1_old;
    int pos0, pos1, posm0, posm1, posbl;
    int hmp0, hmp1, hmm0, hmm1, hmbl;
    uint8_t nusiz0, nusiz1;
    uint8_t colubk, colupf, colup0, colup1;
    bool enam0, enam1, enabl;
    uint8_t ctrlpf, vblank, vsync;
    int sl_cycle; // CPU cycle within scanline (0-75)

    // TIA collision registers (latched, cleared by $0x1B write)
    uint8_t cxm0p, cxm1p, cxp0fb, cxp1fb, cxm0fb, cxm1fb, cxblpf, cxppmm;

    // Output
    uint32_t framebuffer[192 * 160];
    bool frame_done;

    // RIOT
    uint8_t swcha, swchb;
    int timer_val, timer_prescale;
    uint8_t audc0, audc1, audf0, audf1, audv0, audv1;
    bool inpt4, inpt5;

    // TIA audio state
    uint32_t audio_phase[2];
    uint32_t audio_lfsr[2];

    void init() {
        memset(this, 0, sizeof(*this));
        sp = 0xFF; pc = 0; p = 0x20;
        rom_mask = 0x1FFF;
        bank_type = 0;
        current_bank = 0;
        num_banks = 1;
        sl_cycle = 0; frame_done = false;
        timer_val = 0; timer_prescale = 1;
        swcha = 0xFF; swchb = 0xFF; inpt4 = inpt5 = true;
        pos0 = pos1 = posm0 = posm1 = posbl = 0;
        audio_phase[0] = audio_phase[1] = 0;
        audio_lfsr[0] = audio_lfsr[1] = 0x7FFF;
        cxm0p = cxm1p = cxp0fb = cxp1fb = cxm0fb = cxm1fb = cxblpf = cxppmm = 0;
    }

    bool load(const char* path) {
        FILE* fp = fopen(path, "rb");
        if (!fp) return false;
        fseek(fp, 0, SEEK_END);
        long sz = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        if (sz > 0x10000) sz = 0x10000;
        memset(rom, 0, sizeof(rom));
        fread(rom, 1, sz, fp);
        fclose(fp);
        for (uint32_t m = 1; m <= 0x10000; m <<= 1)
            if (m >= (uint32_t)sz) { rom_mask = m - 1; break; }
        if (rom_mask < 0x1FFF) rom_mask = 0x1FFF;

        // Detect bankswitching
        if (sz <= 4096) { bank_type = 0; num_banks = 1; }
        else if (sz <= 8192) { bank_type = 0; num_banks = 1; }
        else if (sz <= 16384) { bank_type = 1; num_banks = 2; }  // F8
        else if (sz <= 32768) { bank_type = 2; num_banks = 4; }  // F6
        else { bank_type = 3; num_banks = 8; }                    // F4
        if (bank_type > 0) current_bank = num_banks - 1; // start in last bank
        return true;
    }

    // Switch ROM bank for bankswitched carts
    void set_bank(int bank) {
        if (bank < 0) bank = 0;
        if (bank >= num_banks) bank = num_banks - 1;
        current_bank = bank;
    }

    uint8_t read_rom_banked(uint16_t a) {
        if (bank_type == 0) return rom[a & rom_mask];
        // Last bank always visible at $F000-$FFFF
        if (a >= 0xF000) return rom[((num_banks - 1) * 0x1000) + (a & 0xFFF)];
        // Current bank at $D000-$EFFF
        return rom[(current_bank * 0x2000) + (a & 0x1FFF)];
    }

    uint8_t rb(uint16_t a) {
        a &= 0xFFFF;
        if (a < 0x1000) {
            uint8_t r = a & 0x3F;
            if (r >= 0x30 && r <= 0x37) {
                uint8_t val;
                switch (r) {
                    case 0x30: val = cxm0p; break;
                    case 0x31: val = cxm1p; break;
                    case 0x32: val = cxp0fb; break;
                    case 0x33: val = cxp1fb; break;
                    case 0x34: val = cxm0fb; break;
                    case 0x35: val = cxm1fb; break;
                    case 0x36: val = cxblpf; break;
                    case 0x37: val = cxppmm; break;
                    default: val = 0xFF; break;
                }
                // Reading collision register clears it
                cxm0p = cxm1p = cxp0fb = cxp1fb = cxm0fb = cxm1fb = cxblpf = cxppmm = 0;
                return val;
            }
            if (r == 0x3C) return inpt4 ? 0x80 : 0;
            if (r == 0x3D) return inpt5 ? 0x80 : 0;
            return 0xFF;
        }
        if (a < 0x2000) {
            a &= 0x0FFF;
            if ((a & 0x0200) == 0) return ram[a & 0x7F];
            uint8_t r = a & 0xFF;
            if (r == 0x04) return timer_val;
            if (r == 0x05) return timer_val;
            return ram[a & 0x7F];
        }
        // Bankswitching detection via hotspot reads
        if (bank_type > 0 && a >= 0x1FF4 && a <= 0x1FFB) {
            if (bank_type == 1) { // F8: $1FF8-1FF9
                if (a == 0x1FF8) set_bank(0);
                else if (a == 0x1FF9) set_bank(1);
            } else if (bank_type == 2) { // F6: $1FF6-1FF9
                if (a >= 0x1FF6 && a <= 0x1FF9)
                    set_bank(a - 0x1FF6);
            } else { // F4: $1FF4-1FFB
                set_bank(a - 0x1FF4);
            }
        }
        return read_rom_banked(a);
    }

    uint16_t rw(uint16_t a) { return rb(a) | (rb(a + 1) << 8); }

    void wb(uint16_t a, uint8_t v) {
        a &= 0xFFFF;
        if (a < 0x1000) { w_tia(a, v); return; }
        if (a < 0x2000) {
            a &= 0x0FFF;
            if ((a & 0x0200) == 0) { ram[a & 0x7F] = v; return; }
            uint8_t r = a & 0xFF;
            if (r >= 0x14 && r <= 0x17) { timer_val = v; timer_prescale = 1 << (2 * (r - 0x14)); return; }
            ram[a & 0x7F] = v;
            return;
        }
    }

    void w_tia(uint16_t a, uint8_t v) {
        uint8_t r = a & 0x3F;
        switch (r) {
            case 0x00: vsync = v & 3; break;
            case 0x01: vblank = v; break;
            case 0x02: break; // WSYNC
            case 0x04: nusiz0 = v; break;
            case 0x05: nusiz1 = v; break;
            case 0x06: colup0 = v; break;
            case 0x07: colup1 = v; break;
            case 0x08: colupf = v; break;
            case 0x09: colubk = v; break;
            case 0x0A: ctrlpf = v; break;
            case 0x0B: pf0 = v; break;
            case 0x0C: pf1 = v; break;
            case 0x0D: pf2 = v; break;
            case 0x0E: pos0 = sl_cycle * 3; break; // RESP0
            case 0x0F: pos1 = sl_cycle * 3; break; // RESP1
            case 0x10: posm0 = sl_cycle * 3; break;
            case 0x11: posm1 = sl_cycle * 3; break;
            case 0x12: posbl = sl_cycle * 3; break;
            case 0x13: audc0 = v; break;
            case 0x14: audc1 = v; break;
            case 0x15: audf0 = v; break;
            case 0x16: audf1 = v; break;
            case 0x17: audv0 = v; break;
            case 0x18: audv1 = v; break;
            case 0x19: grp0_old = grp0; grp0 = v; break; // VDELP0
            case 0x1A: grp1_old = grp1; grp1 = v; break; // VDELP1
            case 0x1B: enam0 = v & 2; cxm0p = cxm1p = cxp0fb = cxp1fb = cxm0fb = cxm1fb = cxblpf = cxppmm = 0; break;
            case 0x1C: enam1 = v & 2; break;
            case 0x1D: enabl = v & 2; break;
            case 0x1E: hmp0 = v & 0x0F; if (hmp0 & 8) hmp0 |= 0xF0; break; // sign-extend 4 bits for HM
            case 0x1F: hmp1 = v & 0x0F; if (hmp1 & 8) hmp1 |= 0xF0; break;
            case 0x20: { // HMOVE - apply motion to all objects
                pos0 += hmp0; pos1 += hmp1;
                posm0 += hmm0; posm1 += hmm1;
                posbl += hmbl;
                break;
            }
            case 0x21: hmm0 = v & 0x0F; if (hmm0 & 8) hmm0 |= 0xF0; break; // HMM0
            case 0x22: hmm1 = v & 0x0F; if (hmm1 & 8) hmm1 |= 0xF0; break; // HMM1
            case 0x23: hmbl = v & 0x0F; if (hmbl & 8) hmbl |= 0xF0; break; // HMBL
            case 0x24: grp0_old = grp0; grp0 = v; break; // VDELP0 + GRP0
            case 0x25: grp1_old = grp1; grp1 = v; break; // VDELP1 + GRP1
        }
    }

    void push16(uint16_t v) { wb(0x0100 + sp, v >> 8); wb(0x0100 + sp - 1, v & 0xFF); sp -= 2; }
    uint16_t pop16() { sp += 2; return rb(0x0100 + sp - 2) | (rb(0x0100 + sp - 1) << 8); }
    void push8(uint8_t v) { wb(0x0100 + sp, v); sp--; }
    uint8_t pop8() { sp++; return rb(0x0100 + sp); }

    void set_nz(uint8_t v) { p = (p & 0x7C) | (v & 0x80) | (v == 0 ? 2 : 0); }

    int step() {
        uint8_t op = rb(pc++);
        uint8_t val, tmp;
        uint16_t addr;

        switch (op) {
            case 0xA9: a = rb(pc++); set_nz(a); return 2;
            case 0xA5: a = rb(rb(pc++)); set_nz(a); return 3;
            case 0xB5: addr = (rb(pc++) + x) & 0xFF; a = rb(addr); set_nz(a); return 4;
            case 0xAD: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a = rb(addr); set_nz(a); return 4;
            case 0xBD: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a = rb(addr); set_nz(a); return 4;
            case 0xB9: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a = rb(addr); set_nz(a); return 4;
            case 0xA1: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); a = rb(addr); set_nz(a); return 6;
            case 0xB1: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); a = rb(addr + y); set_nz(a); return 5;
            case 0xA2: x = rb(pc++); set_nz(x); return 2;
            case 0xA6: x = rb(rb(pc++)); set_nz(x); return 3;
            case 0xB6: addr = (rb(pc++) + y) & 0xFF; x = rb(addr); set_nz(x); return 4;
            case 0xAE: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; x = rb(addr); set_nz(x); return 4;
            case 0xBE: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; x = rb(addr); set_nz(x); return 4;
            case 0xA0: y = rb(pc++); set_nz(y); return 2;
            case 0xA4: y = rb(rb(pc++)); set_nz(y); return 3;
            case 0xB4: addr = (rb(pc++) + x) & 0xFF; y = rb(addr); set_nz(y); return 4;
            case 0xAC: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; y = rb(addr); set_nz(y); return 4;
            case 0xBC: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; y = rb(addr); set_nz(y); return 4;
            case 0x85: wb(rb(pc++), a); return 3;
            case 0x95: addr = (rb(pc++) + x) & 0xFF; wb(addr, a); return 4;
            case 0x8D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; wb(addr, a); return 4;
            case 0x9D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; wb(addr, a); return 5;
            case 0x99: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; wb(addr, a); return 5;
            case 0x81: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); wb(addr, a); return 6;
            case 0x91: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); wb(addr + y, a); return 6;
            case 0x86: wb(rb(pc++), x); return 3;
            case 0x96: addr = (rb(pc++) + y) & 0xFF; wb(addr, x); return 4;
            case 0x8E: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; wb(addr, x); return 4;
            case 0x84: wb(rb(pc++), y); return 3;
            case 0x94: addr = (rb(pc++) + x) & 0xFF; wb(addr, y); return 4;
            case 0x8C: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; wb(addr, y); return 4;

            case 0x69: val = rb(pc++); goto adc;
            case 0x65: val = rb(rb(pc++)); goto adc;
            case 0x75: addr = (rb(pc++) + x) & 0xFF; val = rb(addr); goto adc;
            case 0x6D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); goto adc;
            case 0x7D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr); goto adc;
            case 0x79: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; val = rb(addr); goto adc;
            case 0x61: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); val = rb(addr); goto adc;
            case 0x71: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); val = rb(addr + y); goto adc;
            adc: {
                uint16_t r = a + val + (p & 1);
                tmp = r & 0xFF; set_nz(tmp);
                p = (p & 0x3E) | (r & 0x80) | ((r & 0xFF) == 0 ? 2 : 0) | (r > 0xFF ? 1 : 0);
                p = (p & 0xBF) | (((a ^ val) & 0x80) == 0 && ((a ^ tmp) & 0x80) ? 0x40 : 0);
                a = tmp; return 2;
            }
            case 0xE9: val = rb(pc++); goto sbc;
            case 0xE5: val = rb(rb(pc++)); goto sbc;
            case 0xF5: addr = (rb(pc++) + x) & 0xFF; val = rb(addr); goto sbc;
            case 0xED: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); goto sbc;
            case 0xFD: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr); goto sbc;
            case 0xF9: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; val = rb(addr); goto sbc;
            case 0xE1: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); val = rb(addr); goto sbc;
            case 0xF1: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); val = rb(addr + y); goto sbc;
            sbc: {
                uint16_t r = a - val - (1 - (p & 1));
                tmp = r & 0xFF; set_nz(tmp);
                p = (p & 0x3E) | (tmp & 0x80) | (tmp == 0 ? 2 : 0) | (r < 0x100 ? 1 : 0);
                p = (p & 0xBF) | (((a ^ val) & 0x80) && ((a ^ tmp) & 0x80) ? 0x40 : 0);
                a = tmp; return 2;
            }

            case 0xC9: val = rb(pc++); goto cmp;
            case 0xC5: val = rb(rb(pc++)); goto cmp;
            case 0xD5: addr = (rb(pc++) + x) & 0xFF; val = rb(addr); goto cmp;
            case 0xCD: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); goto cmp;
            case 0xDD: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr); goto cmp;
            case 0xD9: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; val = rb(addr); goto cmp;
            case 0xC1: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); val = rb(addr); goto cmp;
            case 0xD1: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); val = rb(addr + y); goto cmp;
            cmp: tmp = a - val; set_nz(tmp); p = (p & 0xFE) | (a >= val ? 1 : 0); return 2;
            case 0xE0: val = rb(pc++); tmp = x - val; set_nz(tmp); p = (p & 0xFE) | (x >= val ? 1 : 0); return 2;
            case 0xE4: val = rb(rb(pc++)); tmp = x - val; set_nz(tmp); p = (p & 0xFE) | (x >= val ? 1 : 0); return 3;
            case 0xEC: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); tmp = x - val; set_nz(tmp); p = (p & 0xFE) | (x >= val ? 1 : 0); return 4;
            case 0xC0: val = rb(pc++); tmp = y - val; set_nz(tmp); p = (p & 0xFE) | (y >= val ? 1 : 0); return 2;
            case 0xC4: val = rb(rb(pc++)); tmp = y - val; set_nz(tmp); p = (p & 0xFE) | (y >= val ? 1 : 0); return 3;
            case 0xCC: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); tmp = y - val; set_nz(tmp); p = (p & 0xFE) | (y >= val ? 1 : 0); return 4;

            case 0x29: a &= rb(pc++); set_nz(a); return 2;
            case 0x25: a &= rb(rb(pc++)); set_nz(a); return 3;
            case 0x35: addr = (rb(pc++) + x) & 0xFF; a &= rb(addr); set_nz(a); return 4;
            case 0x2D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a &= rb(addr); set_nz(a); return 4;
            case 0x3D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a &= rb(addr); set_nz(a); return 4;
            case 0x39: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a &= rb(addr); set_nz(a); return 4;
            case 0x21: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); a &= rb(addr); set_nz(a); return 6;
            case 0x31: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); a &= rb(addr + y); set_nz(a); return 5;
            case 0x09: a |= rb(pc++); set_nz(a); return 2;
            case 0x05: a |= rb(rb(pc++)); set_nz(a); return 3;
            case 0x15: addr = (rb(pc++) + x) & 0xFF; a |= rb(addr); set_nz(a); return 4;
            case 0x0D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a |= rb(addr); set_nz(a); return 4;
            case 0x1D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a |= rb(addr); set_nz(a); return 4;
            case 0x19: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a |= rb(addr); set_nz(a); return 4;
            case 0x01: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); a |= rb(addr); set_nz(a); return 6;
            case 0x11: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); a |= rb(addr + y); set_nz(a); return 5;
            case 0x49: a ^= rb(pc++); set_nz(a); return 2;
            case 0x45: a ^= rb(rb(pc++)); set_nz(a); return 3;
            case 0x55: addr = (rb(pc++) + x) & 0xFF; a ^= rb(addr); set_nz(a); return 4;
            case 0x4D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a ^= rb(addr); set_nz(a); return 4;
            case 0x5D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a ^= rb(addr); set_nz(a); return 4;
            case 0x59: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a ^= rb(addr); set_nz(a); return 4;
            case 0x41: addr = rb(pc++); addr = rb((addr + x) & 0xFF) | (rb((addr + x + 1) & 0xFF) << 8); a ^= rb(addr); set_nz(a); return 6;
            case 0x51: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); a ^= rb(addr + y); set_nz(a); return 5;
            case 0x24: val = rb(rb(pc++)); p = (p & 0x3D) | (val & 0xC0) | ((a & val) == 0 ? 2 : 0); return 3;
            case 0x2C: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); p = (p & 0x3D) | (val & 0xC0) | ((a & val) == 0 ? 2 : 0); return 4;

            case 0x0A: p = (p & 0xFE) | (a >> 7); a <<= 1; set_nz(a); return 2;
            case 0x06: addr = rb(pc++); val = rb(addr) << 1; p = (p & 0xFE) | (rb(addr) >> 7); wb(addr, val); set_nz(val); return 5;
            case 0x16: addr = (rb(pc++) + x) & 0xFF; val = rb(addr) << 1; p = (p & 0xFE) | (rb(addr) >> 7); wb(addr, val); set_nz(val); return 6;
            case 0x0E: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr) << 1; p = (p & 0xFE) | (rb(addr) >> 7); wb(addr, val); set_nz(val); return 6;
            case 0x1E: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr) << 1; p = (p & 0xFE) | (rb(addr) >> 7); wb(addr, val); set_nz(val); return 7;
            case 0x4A: p = (p & 0xFE) | (a & 1); a >>= 1; set_nz(a); return 2;
            case 0x46: addr = rb(pc++); p = (p & 0xFE) | (rb(addr) & 1); val = rb(addr) >> 1; wb(addr, val); set_nz(val); return 5;
            case 0x56: addr = (rb(pc++) + x) & 0xFF; p = (p & 0xFE) | (rb(addr) & 1); val = rb(addr) >> 1; wb(addr, val); set_nz(val); return 6;
            case 0x4E: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; p = (p & 0xFE) | (rb(addr) & 1); val = rb(addr) >> 1; wb(addr, val); set_nz(val); return 6;
            case 0x5E: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; p = (p & 0xFE) | (rb(addr) & 1); val = rb(addr) >> 1; wb(addr, val); set_nz(val); return 7;
            case 0x2A: tmp = a; a = (a << 1) | (p & 1); p = (p & 0xFE) | (tmp >> 7); set_nz(a); return 2;
            case 0x26: addr = rb(pc++); tmp = rb(addr); val = (tmp << 1) | (p & 1); p = (p & 0xFE) | (tmp >> 7); wb(addr, val); set_nz(val); return 5;
            case 0x36: addr = (rb(pc++) + x) & 0xFF; tmp = rb(addr); val = (tmp << 1) | (p & 1); p = (p & 0xFE) | (tmp >> 7); wb(addr, val); set_nz(val); return 6;
            case 0x2E: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; tmp = rb(addr); val = (tmp << 1) | (p & 1); p = (p & 0xFE) | (tmp >> 7); wb(addr, val); set_nz(val); return 6;
            case 0x3E: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; tmp = rb(addr); val = (tmp << 1) | (p & 1); p = (p & 0xFE) | (tmp >> 7); wb(addr, val); set_nz(val); return 7;
            case 0x6A: tmp = a; a = (a >> 1) | ((p & 1) << 7); p = (p & 0xFE) | (tmp & 1); set_nz(a); return 2;
            case 0x66: addr = rb(pc++); tmp = rb(addr); val = (tmp >> 1) | ((p & 1) << 7); p = (p & 0xFE) | (tmp & 1); wb(addr, val); set_nz(val); return 5;
            case 0x76: addr = (rb(pc++) + x) & 0xFF; tmp = rb(addr); val = (tmp >> 1) | ((p & 1) << 7); p = (p & 0xFE) | (tmp & 1); wb(addr, val); set_nz(val); return 6;
            case 0x6E: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; tmp = rb(addr); val = (tmp >> 1) | ((p & 1) << 7); p = (p & 0xFE) | (tmp & 1); wb(addr, val); set_nz(val); return 6;
            case 0x7E: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; tmp = rb(addr); val = (tmp >> 1) | ((p & 1) << 7); p = (p & 0xFE) | (tmp & 1); wb(addr, val); set_nz(val); return 7;

            case 0xE8: x++; set_nz(x); return 2;
            case 0xCA: x--; set_nz(x); return 2;
            case 0xC8: y++; set_nz(y); return 2;
            case 0x88: y--; set_nz(y); return 2;
            case 0xE6: addr = rb(pc++); val = rb(addr) + 1; wb(addr, val); set_nz(val); return 5;
            case 0xF6: addr = (rb(pc++) + x) & 0xFF; val = rb(addr) + 1; wb(addr, val); set_nz(val); return 6;
            case 0xEE: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr) + 1; wb(addr, val); set_nz(val); return 6;
            case 0xFE: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr) + 1; wb(addr, val); set_nz(val); return 7;
            case 0xC6: addr = rb(pc++); val = rb(addr) - 1; wb(addr, val); set_nz(val); return 5;
            case 0xD6: addr = (rb(pc++) + x) & 0xFF; val = rb(addr) - 1; wb(addr, val); set_nz(val); return 6;
            case 0xCE: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr) - 1; wb(addr, val); set_nz(val); return 6;
            case 0xDE: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr) - 1; wb(addr, val); set_nz(val); return 7;

            case 0xAA: x = a; set_nz(x); return 2;
            case 0xA8: y = a; set_nz(y); return 2;
            case 0x8A: a = x; set_nz(a); return 2;
            case 0x98: a = y; set_nz(a); return 2;
            case 0xBA: x = sp; set_nz(x); return 2;
            case 0x9A: sp = x; return 2;
            case 0x48: push8(a); return 3;
            case 0x68: a = pop8(); set_nz(a); return 4;
            case 0x08: push8(p | 0x10); return 3;
            case 0x28: p = pop8(); p |= 0x20; return 4;

            case 0x10: tmp = rb(pc++); if (!(p & 0x80)) pc += (int8_t)tmp; return 2;
            case 0x30: tmp = rb(pc++); if (p & 0x80) pc += (int8_t)tmp; return 2;
            case 0x50: tmp = rb(pc++); if (!(p & 0x40)) pc += (int8_t)tmp; return 2;
            case 0x70: tmp = rb(pc++); if (p & 0x40) pc += (int8_t)tmp; return 2;
            case 0x90: tmp = rb(pc++); if (!(p & 1)) pc += (int8_t)tmp; return 2;
            case 0xB0: tmp = rb(pc++); if (p & 1) pc += (int8_t)tmp; return 2;
            case 0xD0: tmp = rb(pc++); if (!(p & 2)) pc += (int8_t)tmp; return 2;
            case 0xF0: tmp = rb(pc++); if (p & 2) pc += (int8_t)tmp; return 2;

            case 0x4C: pc = rb(pc) | (rb(pc + 1) << 8); return 3;
            case 0x6C: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; pc = rb(addr) | (rb(addr + 1) << 8); return 5;
            case 0x20: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; push16(pc); pc = addr; return 6;
            case 0x60: pc = pop16(); return 6;
            case 0x40: p = pop8(); p |= 0x20; pc = pop16(); return 6;
            case 0x00: pc++; push16(pc); push8(p | 0x10); p |= 4; pc = rw(0xFFFE); return 7;

            case 0x18: p &= ~1; return 2; case 0x38: p |= 1; return 2;
            case 0x58: p &= ~4; return 2; case 0x78: p |= 4; return 2;
            case 0xB8: p &= ~0x40; return 2; case 0xD8: p &= ~8; return 2; case 0xF8: p |= 8; return 2;
            case 0xEA: return 2;
            case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: return 2;
            case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: return 2;
            case 0x04: case 0x44: case 0x64: pc++; return 3;
            case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: pc++; return 4;
            case 0x0C: case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: pc += 2; return 4;
            default: return 2;
        }
    }

    void render_scanline_at(int sl) {
        if (sl < 40 || sl >= 232) return;
        int row = sl - 40;
        int reflect = ctrlpf & 1;

        uint8_t pfb[40];
        for (int i = 0; i < 20; i++) {
            int bit;
            if (i < 4) bit = (pf0 >> (4 + i)) & 1;
            else if (i < 12) bit = (pf1 >> (i - 4)) & 1;
            else bit = (pf2 >> (19 - i)) & 1;
            if (reflect) { pfb[i] = bit; pfb[39 - i] = bit; }
            else { pfb[i] = bit; pfb[i + 20] = bit; }
        }

        int p0_x = pos0;
        int p1_x = pos1;
        int m0_x = posm0;
        int m1_x = posm1;
        int bl_x = posbl;

        // Collision detection per pixel
        for (int x = 0; x < 160; x++) {
            bool pf_on = pfb[x / 4] != 0;
            bool p0_on = (x >= p0_x && x < p0_x + 8 && (grp0 & (0x80 >> (x - p0_x))));
            bool p1_on = (x >= p1_x && x < p1_x + 8 && (grp1 & (0x80 >> (x - p1_x))));
            bool m0_on = enam0 && (x >= m0_x && x < m0_x + 1);
            bool m1_on = enam1 && (x >= m1_x && x < m1_x + 1);
            bool bl_on = enabl && (x >= bl_x && x < bl_x + 1);

            // Collision detection (latched)
            if (p0_on && pf_on) cxp0fb |= 0x40;
            if (p0_on && bl_on) cxp0fb |= 0x80;
            if (p1_on && pf_on) cxp1fb |= 0x40;
            if (p1_on && bl_on) cxp1fb |= 0x80;
            if (m0_on && pf_on) cxm0fb |= 0x40;
            if (m0_on && bl_on) cxm0fb |= 0x80;
            if (m1_on && pf_on) cxm1fb |= 0x40;
            if (m1_on && bl_on) cxm1fb |= 0x80;
            if (m0_on && p1_on) cxm0p |= 0x40;
            if (m0_on && p0_on) cxm0p |= 0x80;
            if (m1_on && p0_on) cxm1p |= 0x40;
            if (m1_on && p1_on) cxm1p |= 0x80;
            if (bl_on && pf_on) cxblpf |= 0x40;
            if (p0_on && p1_on) cxppmm |= 0x40;
            if (m0_on && m1_on) cxppmm |= 0x80;

            uint32_t color;
            if (p0_on) color = a26_palette[colup0 & 0x7F];
            else if (p1_on) color = a26_palette[colup1 & 0x7F];
            else if (pf_on) color = a26_palette[colupf & 0x7F];
            else color = a26_palette[colubk & 0x7F];

            framebuffer[row * 160 + x] = color | 0xFF000000;
        }
    }

    void run_frame() {
        for (int sl = 0; sl < 262; sl++) {
            int c_total = 0;
            sl_cycle = 0;
            while (c_total < 76) {
                int c = step();
                c_total += c;
                sl_cycle += c;
            }
            if (sl >= 40 && sl < 232) render_scanline_at(sl);
        }
        frame_done = true;
    }

    void gen_audio(int16_t* buf, int samples, int sr) {
        for (int i = 0; i < samples; i++) {
            int mix = 0;
            for (int ch = 0; ch < 2; ch++) {
                uint8_t vol = ch ? audv1 : audv0;
                if (vol == 0) continue;
                uint8_t ctrl = ch ? audc1 : audc0;
                uint8_t freq = ch ? audf1 : audf0;
                int div = (freq + 1) * 2; // approximate divider

                uint32_t& phase = audio_phase[ch];
                uint32_t& lfsr = audio_lfsr[ch];

                phase += sr * 15750 / sr; // TIA clock ~1.19MHz / 76 = 15750Hz

                int bit = 0;
                switch (ctrl & 7) {
                    case 0: bit = 1; break; // always 1
                    case 1: // 4-bit polynomial
                        bit = lfsr & 1;
                        if (phase >= div) { phase = 0;
                            lfsr = (lfsr >> 1) | (((lfsr & 1) ^ ((lfsr >> 1) & 1) ^ 1) << 3);
                        }
                        break;
                    case 2: case 3: // div-by-15+poly
                        bit = lfsr & 1;
                        if (phase >= div) { phase = 0;
                            lfsr = (lfsr >> 1) | (((lfsr & 1) ^ ((lfsr >> 4) & 1)) << 14);
                        }
                        break;
                    case 4: case 5: // tone/noise
                        bit = (phase / div) & 1;
                        if (phase >= div*2) phase = 0;
                        break;
                    case 6: // div-by-31+poly
                        bit = lfsr & 1;
                        if (phase >= div) { phase = 0;
                            lfsr = (lfsr >> 1) | (((lfsr & 1) ^ ((lfsr >> 4) & 1) ^ 1) << 14);
                        }
                        break;
                    case 7: // div-by-31+tone
                        bit = (phase / div) & 1;
                        if (phase >= div*2) phase = 0;
                        break;
                }
                mix += bit ? vol * 500 : -vol * 500;
            }
            buf[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
        }
    }
};
