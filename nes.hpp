#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const uint32_t nes_palette[64] = {
    0x626262,0x0000FC,0x0000FC,0x0024D0,0x0044A8,0x006084,0x007060,0x007040,
    0x006000,0x004400,0x002800,0x001000,0x000000,0x000000,0x000000,0x000000,
    0xBCBCBC,0x0040F8,0x0060F8,0x0080F8,0x0098F0,0x00A8D0,0x00B0A8,0x00AC78,
    0x009C48,0x008818,0x007400,0x006C00,0x006800,0x000000,0x000000,0x000000,
    0xF8F8F8,0x006CFC,0x0090FC,0x00A8FC,0x00C0E8,0x00CCCC,0x00D0A4,0x00CC78,
    0x00C048,0x00B41C,0x00B404,0x00B400,0x00B000,0x000000,0x000000,0x000000,
    0xFCFCFC,0x20A0FC,0x48BCFC,0x68D0FC,0x88E0F0,0x9CE8D8,0xA8E8BC,0xACE09C,
    0xA8D87C,0xA0D05C,0x98D044,0x90D438,0x90D438,0x000000,0x000000,0x000000,
};

struct NES {
    uint8_t a, x, y, sp;
    uint8_t p;
    uint16_t pc;

    uint8_t ram[2048];
    uint8_t *prg;  // PRG ROM
    uint8_t *chr;  // CHR ROM
    uint32_t prg_size, chr_size;
    int mapper;

    // PRG banking (NROM: -1 = fixed, UNROM)
    uint8_t *prg_bank[2];

    // MMC1 state
    uint8_t mmc1_shift, mmc1_control, mmc1_chr0, mmc1_chr1, mmc1_prg;
    int mmc1_loaded;

    // PPU registers
    uint8_t ppuctrl, ppumask, ppustatus, oamaddr;
    uint8_t ppuscroll_x, ppuscroll_y;
    uint16_t ppuaddr;
    uint8_t ppuaddr_latch;
    uint8_t ppu_data_buffer;

    // PPU internal state
    uint8_t oam[256];
    uint8_t vram[2048];
    uint8_t palette[32];
    int scanline, cycle;
    bool nmi_pending;
    int nmi_delay;

    bool frame_done;
    uint32_t framebuffer[240 * 256];

    // Sprite rendering state
    int sprite_count;
    uint8_t sprite_y[8], sprite_tile[8], sprite_attr[8], sprite_x[8];

    // APU state
    uint8_t apu_regs[0x18];
    uint32_t apu_phase[4];      // phase accumulators for pulse/noise
    uint32_t apu_seq[4];        // duty sequence position
    uint16_t apu_lfsr;
    uint32_t apu_tri_phase;
    uint8_t apu_frame_count;

    // Controllers
    uint8_t joy1_strobe, joy2_strobe;
    uint8_t joy1_bits, joy2_bits;
    uint8_t joy1_read, joy2_read;
    bool joy1[8], joy2[8];

    // Audio output
    int audio_pos;
    int16_t audio_buf[44100 / 60];
    int16_t *audio_out;

    // iNES header detection
    uint8_t header[16];

    void init() {
        memset(this, 0, sizeof(*this));
        sp = 0xFD;
        p = 0x20 | 0x04;
        pc = 0;
        ppustatus = 0x80;
        prg = nullptr; chr = nullptr;
        prg_bank[0] = prg_bank[1] = nullptr;
        apu_lfsr = 1;
        scanline = 0; cycle = 0;
        frame_done = false;
        audio_pos = 0;
        audio_out = audio_buf;
    }

    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fread(header, 1, 16, f);
        if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
            fclose(f);
            return false;
        }
        int prg_16 = header[4];
        int chr_8 = header[5];
        mapper = (header[6] >> 4) | (header[7] & 0xF0);

        prg_size = prg_16 * 16384;
        chr_size = chr_8 * 8192;
        if (prg_size == 0) prg_size = 16384;
        
        prg = new uint8_t[prg_size];
        fread(prg, 1, prg_size, f);

        if (chr_size > 0) {
            chr = new uint8_t[chr_size];
            fread(chr, 1, chr_size, f);
        } else {
            chr = new uint8_t[8192];
            memset(chr, 0, 8192);
        }

        // Skip trainer if present
        if (header[6] & 0x04) {
            fseek(f, 512, SEEK_CUR);
        }

        fclose(f);

        // Setup banking
        if (mapper == 0) { // NROM
            prg_bank[0] = prg;
            prg_bank[1] = prg_size >= 32768 ? prg + 16384 : prg;
        } else {
            prg_bank[0] = prg;
            prg_bank[1] = prg + prg_size - 16384;
        }

        // VRAM mirroring handled in ppu_read/ppu_write using header[6] & 1

        init();
        // Set PC from reset vector (at $FFFC-$FFFD in CPU space)
        pc = rb(0xFFFC) | (rb(0xFFFD) << 8);
        return true;
    }

    void set_prg_bank_16k(int slot, int bank) {
        bank %= (prg_size / 16384);
        prg_bank[slot] = prg + bank * 16384;
    }

    uint8_t ppu_read(uint16_t addr) {
        addr &= 0x3FFF;
        if (addr < 0x2000) {
            if (chr_size > 0) return chr[addr % chr_size];
            return chr[addr];
        }
        if (addr < 0x3F00) {
            addr -= 0x2000;
            if (addr < 0x400) {
                int mirror = header[6] & 1;
                if (mirror == 0) { // vertical
                    if (addr >= 0x800) addr -= 0x800;
                } else { // horizontal
                    if (addr >= 0x400 && addr < 0x800) addr -= 0x400;
                    else if (addr >= 0xC00) addr -= 0x800;
                }
                return vram[addr % 0x800];
            }
            // Mirror $2000-$2EFF to $3000-$3EFF
            addr -= 0x400;
            if (addr < 0x800) {
                int mirror = header[6] & 1;
                if (mirror == 0) {
                    if (addr >= 0x800) addr -= 0x800;
                } else {
                    if (addr >= 0x400 && addr < 0x800) addr -= 0x400;
                    else if (addr >= 0xC00) addr -= 0x800;
                }
                return vram[addr % 0x800];
            }
            return 0;
        }
        // Palette
        addr &= 0x1F;
        if (addr >= 0x10 && addr % 4 == 0) addr -= 0x10;
        return palette[addr] & 0x3F;
    }

    void ppu_write(uint16_t addr, uint8_t val) {
        addr &= 0x3FFF;
        if (addr < 0x2000) {
            if (chr_size > 0) chr[addr % chr_size] = val;
            else chr[addr] = val;
            return;
        }
        if (addr < 0x3F00) {
            addr -= 0x2000;
            if (addr < 0x400) {
                int mirror = header[6] & 1;
                if (mirror == 0) {
                    if (addr >= 0x800) addr -= 0x800;
                } else {
                    if (addr >= 0x400 && addr < 0x800) addr -= 0x400;
                    else if (addr >= 0xC00) addr -= 0x800;
                }
                vram[addr % 0x800] = val;
                return;
            }
            addr -= 0x400;
            if (addr < 0x800) {
                int mirror = header[6] & 1;
                if (mirror == 0) {
                    if (addr >= 0x800) addr -= 0x800;
                } else {
                    if (addr >= 0x400 && addr < 0x800) addr -= 0x400;
                    else if (addr >= 0xC00) addr -= 0x800;
                }
                vram[addr % 0x800] = val;
                return;
            }
            return;
        }
        addr &= 0x1F;
        if (addr >= 0x10 && addr % 4 == 0) addr -= 0x10;
        palette[addr] = val & 0x3F;
    }

    uint8_t ppu_read_data() {
        uint8_t val = ppu_data_buffer;
        ppu_data_buffer = ppu_read(ppuaddr);
        if (ppuaddr >= 0x3F00) val = ppu_data_buffer;
        ppuaddr += (ppuctrl & 4) ? 32 : 1;
        return val;
    }

    void ppu_write_data(uint8_t val) {
        ppu_write(ppuaddr, val);
        ppuaddr += (ppuctrl & 4) ? 32 : 1;
    }

    uint8_t rb(uint16_t addr) {
        if (addr < 0x2000) return ram[addr & 0x7FF];
        if (addr < 0x4000) {
            int reg = addr & 7;
            if (reg == 2) {
                uint8_t val = (ppustatus & 0xE0) | (ppu_data_buffer & 0x1F);
                ppustatus &= ~0x80;
                ppuaddr_latch = 0;
                return val;
            }
            if (reg == 4) return oam[oamaddr];
            if (reg == 7) return ppu_read_data();
            return ppu_data_buffer;
        }
        if (addr < 0x4016) {
            if (addr == 0x4015) {
                uint8_t val = 0;
                // APU status - simplified, just say all channels are off
                return val;
            }
            return 0;
        }
        if (addr == 0x4016) {
            uint8_t val = (joy1_read & 1) | 0x40;
            joy1_read >>= 1;
            if (joy1_strobe) joy1_read = 0;
            return val;
        }
        if (addr == 0x4017) {
            uint8_t val = (joy2_read & 1) | 0x40;
            joy2_read >>= 1;
            if (joy2_strobe) joy2_read = 0;
            return val;
        }
        if (addr >= 0x4020) {
            // Cartridge space
            if (mapper == 0) {
                if (addr < 0x6000) return 0;
                if (addr < 0x8000) return 0; // PRG RAM not implemented
                return prg_bank[(addr - 0x8000) / 0x4000][addr & 0x3FFF];
            }
            // MMC1
            if (addr >= 0x8000) {
                return prg_bank[(addr - 0x8000) / 0x4000][addr & 0x3FFF];
            }
            return 0;
        }
        return 0;
    }

    uint16_t rw(uint16_t addr) {
        return rb(addr) | (rb(addr + 1) << 8);
    }

    void wb(uint16_t addr, uint8_t val) {
        if (addr < 0x2000) {
            ram[addr & 0x7FF] = val;
            return;
        }
        if (addr < 0x4000) {
            int reg = addr & 7;
            switch (reg) {
                case 0: ppuctrl = val; break;
                case 1: ppumask = val; break;
                case 2: break; // PPUSTATUS is read-only
                case 3: oamaddr = val; break;
                case 4: oam[oamaddr++] = val; break;
                case 5: {
                    if (ppuaddr_latch == 0) ppuscroll_x = val;
                    else ppuscroll_y = val;
                    ppuaddr_latch ^= 1;
                    break;
                }
                case 6: {
                    if (ppuaddr_latch == 0) ppuaddr = (val << 8) | (ppuaddr & 0xFF);
                    else ppuaddr = (ppuaddr & 0xFF00) | val;
                    ppuaddr &= 0x3FFF;
                    ppuaddr_latch ^= 1;
                    break;
                }
                case 7: ppu_write_data(val); break;
            }
            return;
        }
        if (addr < 0x4018) {
            switch (addr) {
                case 0x4000: case 0x4001: case 0x4002: case 0x4003:
                case 0x4004: case 0x4005: case 0x4006: case 0x4007:
                case 0x4008: case 0x4009: case 0x400A: case 0x400B:
                case 0x400C: case 0x400D: case 0x400E: case 0x400F:
                case 0x4010: case 0x4011: case 0x4012: case 0x4013:
                    apu_regs[addr - 0x4000] = val;
                    if (addr == 0x4010) {
                        // DMC control - not implemented
                    }
                    break;
                case 0x4014: // OAM DMA
                    {
                        uint16_t src = val << 8;
                        for (int i = 0; i < 256; i++)
                            oam[i] = rb(src + i);
                    }
                    break;
                case 0x4015:
                    break; // APU status, read-only bits
                case 0x4016:
                    joy1_strobe = val & 1;
                    if (joy1_strobe) {
                        joy1_read = 0;
                        for (int i = 0; i < 8; i++)
                            joy1_read |= (joy1[i] ? 1 : 0) << i;
                    }
                    break;
                case 0x4017:
                    // Frame counter control - simplified
                    break;
            }
            return;
        }
        // Mapper registers
        if (addr >= 0x8000) {
            if (mapper == 1) {
                // MMC1
                if (val & 0x80) {
                    mmc1_shift = 0;
                    mmc1_loaded = 0;
                    mmc1_control |= 0x0C;
                    set_prg_bank_16k(1, -1);
                    return;
                }
                mmc1_shift = (mmc1_shift >> 1) | ((val & 1) << 4);
                mmc1_loaded++;
                if (mmc1_loaded >= 5) {
                    uint8_t data = mmc1_shift;
                    if (addr <= 0x9FFF) {
                        mmc1_control = data & 0x1F;
                        // Mirroring handled
                    } else if (addr <= 0xBFFF) {
                        // CHR bank 0
                        if (mmc1_control & 0x10) mmc1_chr0 = data & 0x1F;
                        else mmc1_chr0 = data & 0x1E;
                    } else if (addr <= 0xDFFF) {
                        mmc1_chr1 = data & 0x1F;
                    } else {
                        mmc1_prg = data & 0x0F;
                        // PRG mode
                        int prg_mode = (mmc1_control >> 2) & 3;
                        int prg_banks = prg_size / 16384;
                        int bank0 = 0, bank1 = prg_banks - 1;
                        if (prg_mode == 0 || prg_mode == 1) {
                            bank0 = (mmc1_prg >> 1) & (prg_banks - 1);
                            bank1 = bank0 + 1;
                        } else if (prg_mode == 2) {
                            bank0 = 0;
                            bank1 = mmc1_prg;
                        } else {
                            bank0 = mmc1_prg;
                            bank1 = prg_banks - 1;
                        }
                        set_prg_bank_16k(0, bank0);
                        set_prg_bank_16k(1, bank1);
                    }
                    mmc1_loaded = false;
                    mmc1_shift = 0;
                }
                return;
            }
            if (mapper == 2 || mapper == 71) {
                // UNROM / Camerica
                set_prg_bank_16k(0, val & 0x0F);
                return;
            }
            if (mapper == 3) {
                // CNROM - no PRG banking
                return;
            }
            if (mapper == 66) {
                // GxROM
                set_prg_bank_16k(0, (val >> 4) & 3);
                return;
            }
            return;
        }
    }

    void push16(uint16_t v) {
        wb(0x0100 + sp, v >> 8);
        sp--;
        wb(0x0100 + sp, v & 0xFF);
        sp--;
    }

    uint16_t pop16() {
        sp += 2;
        return rb(0x0100 + sp) | (rb(0x0100 + sp - 1) << 8);
    }

    void push8(uint8_t v) {
        wb(0x0100 + sp, v);
        sp--;
    }

    uint8_t pop8() {
        sp++;
        return rb(0x0100 + sp);
    }

    void set_nz(uint8_t v) {
        p = (p & 0x7D) | (v & 0x80) | (v == 0 ? 2 : 0);
    }

    int step() {
        uint8_t op = rb(pc++);
        uint8_t val, tmp;
        uint16_t addr;

        // Check for pending NMI
        if (nmi_pending) {
            nmi_pending = false;
            push16(pc);
            push8(p | 0x20);
            p |= 4;
            pc = rw(0xFFFA);
            return 7;
        }

        switch (op) {
            case 0xA9: a = rb(pc++); set_nz(a); return 2;
            case 0xA5: a = rb(rb(pc++)); set_nz(a); return 3;
            case 0xB5: addr = (rb(pc++) + x) & 0xFF; a = rb(addr); set_nz(a); return 4;
            case 0xAD: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a = rb(addr); set_nz(a); return 4;
            case 0xBD: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a = rb(addr); set_nz(a); return 4;
            case 0xB9: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a = rb(addr); set_nz(a); return 4;
            case 0xA1: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); a = rb(addr); set_nz(a); return 6;
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
            case 0x81: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); wb(addr, a); return 6;
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
            case 0x61: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); val = rb(addr); goto adc;
            case 0x71: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); val = rb(addr + y); goto adc;
            adc: {
                uint16_t r = a + val + (p & 1);
                tmp = r & 0xFF;
                p = (p & 0x3E) | (r & 0x80) | (tmp == 0 ? 2 : 0) | (r > 0xFF ? 1 : 0);
                p = (p & 0xBF) | (((a ^ val) & 0x80) == 0 && ((a ^ tmp) & 0x80) ? 0x40 : 0);
                set_nz(tmp);
                a = tmp; return 2;
            }

            case 0xE9: val = rb(pc++); goto sbc;
            case 0xE5: val = rb(rb(pc++)); goto sbc;
            case 0xF5: addr = (rb(pc++) + x) & 0xFF; val = rb(addr); goto sbc;
            case 0xED: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); goto sbc;
            case 0xFD: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr); goto sbc;
            case 0xF9: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; val = rb(addr); goto sbc;
            case 0xE1: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); val = rb(addr); goto sbc;
            case 0xF1: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); val = rb(addr + y); goto sbc;
            sbc: {
                uint16_t r = a - val - (1 - (p & 1));
                tmp = r & 0xFF;
                p = (p & 0x3E) | (tmp & 0x80) | (tmp == 0 ? 2 : 0) | (r < 0x100 ? 1 : 0);
                p = (p & 0xBF) | (((a ^ val) & 0x80) && ((a ^ tmp) & 0x80) ? 0x40 : 0);
                set_nz(tmp);
                a = tmp; return 2;
            }

            case 0xC9: val = rb(pc++); goto cmp;
            case 0xC5: val = rb(rb(pc++)); goto cmp;
            case 0xD5: addr = (rb(pc++) + x) & 0xFF; val = rb(addr); goto cmp;
            case 0xCD: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; val = rb(addr); goto cmp;
            case 0xDD: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; val = rb(addr); goto cmp;
            case 0xD9: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; val = rb(addr); goto cmp;
            case 0xC1: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); val = rb(addr); goto cmp;
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
            case 0x21: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); a &= rb(addr); set_nz(a); return 6;
            case 0x31: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); a &= rb(addr + y); set_nz(a); return 5;

            case 0x09: a |= rb(pc++); set_nz(a); return 2;
            case 0x05: a |= rb(rb(pc++)); set_nz(a); return 3;
            case 0x15: addr = (rb(pc++) + x) & 0xFF; a |= rb(addr); set_nz(a); return 4;
            case 0x0D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a |= rb(addr); set_nz(a); return 4;
            case 0x1D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a |= rb(addr); set_nz(a); return 4;
            case 0x19: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a |= rb(addr); set_nz(a); return 4;
            case 0x01: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); a |= rb(addr); set_nz(a); return 6;
            case 0x11: addr = rb(pc++); addr = rb(addr & 0xFF) | (rb((addr + 1) & 0xFF) << 8); a |= rb(addr + y); set_nz(a); return 5;

            case 0x49: a ^= rb(pc++); set_nz(a); return 2;
            case 0x45: a ^= rb(rb(pc++)); set_nz(a); return 3;
            case 0x55: addr = (rb(pc++) + x) & 0xFF; a ^= rb(addr); set_nz(a); return 4;
            case 0x4D: addr = rb(pc) | (rb(pc + 1) << 8); pc += 2; a ^= rb(addr); set_nz(a); return 4;
            case 0x5D: addr = (rb(pc) | (rb(pc + 1) << 8)) + x; pc += 2; a ^= rb(addr); set_nz(a); return 4;
            case 0x59: addr = (rb(pc) | (rb(pc + 1) << 8)) + y; pc += 2; a ^= rb(addr); set_nz(a); return 4;
            case 0x41: addr = rb(pc++); addr = (rb((addr + x) & 0xFF)) | (rb((addr + x + 1) & 0xFF) << 8); a ^= rb(addr); set_nz(a); return 6;
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

    // PPU rendering
    void render_pixel() {
        int x = cycle - 1;
        int y = scanline;
        if (x < 0 || x >= 256 || y < 0 || y >= 240) return;
        if (!(ppumask & 0x18)) { // both bg and sprite disabled
            framebuffer[y * 256 + x] = nes_palette[palette[0] & 0x3F] | 0xFF000000;
            return;
        }

        // Background
        uint8_t bg_pixel = 0, bg_pal = 0;
        bool bg_visible = (ppumask & 0x08) && (!(ppumask & 0x02) || x >= 8);
        if (bg_visible) {
            int scroll_x = ppuscroll_x;
            int scroll_y = ppuscroll_y;
            int nt_x = (x + scroll_x) / 8;
            int nt_y = (y + scroll_y) / 8;
            int fine_x = (x + scroll_x) % 8;
            int fine_y = (y + scroll_y) % 8;

            int nt = (ppuctrl & 3);
            if (nt_x >= 32) { nt_x -= 32; nt ^= 1; }
            if (nt_y >= 30) { nt_y -= 30; nt ^= 2; }

            int nt_addr = 0x2000 + nt * 0x400 + nt_y * 32 + nt_x;
            int tile = ppu_read(nt_addr);

            int attr_addr = 0x23C0 + nt * 0x400 + (nt_y / 4) * 8 + (nt_x / 4);
            uint8_t attr = ppu_read(attr_addr);
            int attr_shift = ((nt_y % 4) / 2) * 4 + ((nt_x % 4) / 2) * 2;
            int pal_idx = (attr >> attr_shift) & 3;

            int pattern_base = (ppuctrl & 0x10) ? 0x1000 : 0;
            int tile_addr = pattern_base + tile * 16 + fine_y;
            uint8_t tile_lo = ppu_read(tile_addr);
            uint8_t tile_hi = ppu_read(tile_addr + 8);
            int pixel = ((tile_lo >> (7 - fine_x)) & 1) | (((tile_hi >> (7 - fine_x)) & 1) << 1);

            bg_pixel = pixel;
            bg_pal = pal_idx;

            if (pixel == 0) bg_pixel = 0;
        }

        // Sprites
        uint8_t sp_pixel = 0, sp_pal = 0;
        bool sp_visible = (ppumask & 0x10) && (!(ppumask & 0x04) || x >= 8);
        bool sp_priority = false;
        int sp_found = -1;

        if (sp_visible) {
            for (int i = 0; i < sprite_count; i++) {
                int sx = sprite_x[i];
                int sy = sprite_y[i];
                if (x < sx || x >= sx + 8) continue;

                int tile = sprite_tile[i];
                int attr = sprite_attr[i];
                int sp_fine_x = x - sx;

                if (attr & 0x40) sp_fine_x = 7 - sp_fine_x;

                int pattern_base = (ppuctrl & 8) ? 0x1000 : 0;
                int sp_size = (ppuctrl & 0x20) ? 16 : 8;

                int sp_fine_y = y - sy;
                if (attr & 0x80) sp_fine_y = sp_size - 1 - sp_fine_y;

                int tile_addr;
                if (sp_size == 16) {
                    if (sp_fine_y < 8) {
                        tile_addr = (tile & 1) * 0x1000 + (tile & 0xFE) * 16 + sp_fine_y;
                    } else {
                        tile_addr = (tile & 1) * 0x1000 + (tile & 0xFE) * 16 + 8 + sp_fine_y - 8;
                    }
                } else {
                    tile_addr = pattern_base + tile * 16 + sp_fine_y;
                }

                uint8_t sp_lo = ppu_read(tile_addr);
                uint8_t sp_hi = ppu_read(tile_addr + 8);
                int sp_pix = ((sp_lo >> (7 - sp_fine_x)) & 1) | (((sp_hi >> (7 - sp_fine_x)) & 1) << 1);

                if (sp_pix != 0) {
                    if (sp_found < 0 || i < sp_found) {
                        sp_pixel = sp_pix;
                        sp_pal = 4 + (attr & 3);
                        sp_priority = (attr & 0x20) != 0;
                        sp_found = i;

                        // Sprite 0 hit detection
                        if (i == 0 && bg_pixel != 0 && x < 255 && !(ppustatus & 0x40)) {
                            ppustatus |= 0x40;
                        }
                    }
                }
            }
        }

        // Final pixel
        uint32_t color;
        if (sp_pixel != 0 && (bg_pixel == 0 || !sp_priority)) {
            color = nes_palette[palette[sp_pal * 4 + sp_pixel] & 0x3F];
        } else if (bg_pixel != 0) {
            color = nes_palette[palette[bg_pal * 4 + bg_pixel] & 0x3F];
        } else {
            color = nes_palette[palette[0] & 0x3F];
        }
        framebuffer[y * 256 + x] = color | 0xFF000000;
    }

    void run_frame() {
        frame_done = false;
        for (int sl = 0; sl < 262; sl++) {
            scanline = sl;
            for (int c = 0; c < 341; c++) {
                cycle = c;

                // NMI on scanline 241, cycle 1
                if (sl == 241 && c == 1) {
                    ppustatus |= 0x80;
                    if (ppuctrl & 0x80) {
                        nmi_pending = true;
                    }
                }

                // VBlank clear at scanline 0
                if (sl == 0 && c == 1) {
                    ppustatus &= ~0x80;
                }

                // Sprite evaluation at start of visible scanlines
                if (sl < 240 && c == 64) {
                    // Evaluate sprites for this scanline
                    sprite_count = 0;
                    for (int i = 0; i < 64 && sprite_count < 8; i++) {
                        int sy = oam[i * 4];
                        int ty = sl - sy;
                        int sp_size = (ppuctrl & 0x20) ? 16 : 8;
                        if (ty >= 0 && ty < sp_size) {
                            sprite_y[sprite_count] = sy;
                            sprite_tile[sprite_count] = oam[i * 4 + 1];
                            sprite_attr[sprite_count] = oam[i * 4 + 2];
                            sprite_x[sprite_count] = oam[i * 4 + 3];
                            sprite_count++;
                        }
                    }
                    if (sprite_count > 8) ppustatus |= 0x20;
                }

                // Render visible pixels
                if (sl < 240 && c > 0 && c <= 256) {
                    render_pixel();
                }

                // Run CPU (3 PPU cycles per CPU cycle)
                if (c % 3 == 0 && sl < 240) {
                    step();
                } else if (c % 3 == 0 && sl >= 240) {
                    step();
                }
            }
        }
        frame_done = true;
    }

    void gen_audio(int16_t* buf, int samples, int sr) {
        static const int pulse_bits[4][8] = {
            {0,0,0,0,0,0,0,1},
            {0,0,0,0,0,0,1,1},
            {0,0,0,0,1,1,1,1},
            {1,1,1,1,1,1,0,0},
        };
        for (int i = 0; i < samples; i++) {
            int mix = 0;
            int cpu_clocks = sr / 60; // approximate clocks per sample

            // Pulse channel 1
            {
                uint8_t reg0 = apu_regs[0];
                uint8_t reg2 = apu_regs[2];
                uint8_t reg3 = apu_regs[3];
                int duty = (reg0 >> 6) & 3;
                int freq = ((reg3 & 7) << 8) | reg2;
                int vol = (reg0 >> 4) & 0x0F;
                int period = (freq + 1) * 2;
                if (period > 0 && vol > 0) {
                    apu_seq[0] += cpu_clocks * 8;
                    int seq = (apu_seq[0] / period) & 7;
                    apu_seq[0] %= (period * 8);
                    if (pulse_bits[duty][seq]) mix += vol * 800;
                }
            }

            // Pulse channel 2
            {
                uint8_t reg4 = apu_regs[4];
                uint8_t reg6 = apu_regs[6];
                uint8_t reg7 = apu_regs[7];
                int duty = (reg4 >> 6) & 3;
                int freq = ((reg7 & 7) << 8) | reg6;
                int vol = (reg4 >> 4) & 0x0F;
                int period = (freq + 1) * 2;
                if (period > 0 && vol > 0) {
                    apu_seq[1] += cpu_clocks * 8;
                    int seq = (apu_seq[1] / period) & 7;
                    apu_seq[1] %= (period * 8);
                    if (pulse_bits[duty][seq]) mix += vol * 800;
                }
            }

            // Triangle
            {
                uint8_t regB = apu_regs[0x0B];
                uint8_t freq_reg = apu_regs[0x0A];
                int tri_freq = ((regB & 7) << 8) | freq_reg;
                if (tri_freq > 0) {
                    int tri_period = (tri_freq + 1) * 2;
                    apu_tri_phase += cpu_clocks * 32;
                    int t = (apu_tri_phase / tri_period) & 0x1F;
                    apu_tri_phase %= (tri_period * 32);
                    int val;
                    if (t < 16) val = t;
                    else val = 31 - t;
                    mix += (val - 8) * 200;
                }
            }

            // Noise
            {
                uint8_t regC = apu_regs[0x0C];
                uint8_t regE = apu_regs[0x0E];
                int vol = (regC >> 4) & 0x0F;
                if (vol > 0) {
                    int mode = (regE >> 7) & 1;
                    int shift = (regE >> 4) & 0x0F;
                    int r = regE & 0x0F;
                    int period = (r + 1) * (1 << (shift + 1));
                    if (period > 0) {
                        apu_phase[2] += cpu_clocks;
                        int steps = apu_phase[2] / period;
                        apu_phase[2] %= period;
                        for (int s = 0; s < steps && s < 64; s++) {
                            uint16_t feedback = (apu_lfsr & 1) ^ ((apu_lfsr >> 1) & 1);
                            apu_lfsr >>= 1;
                            apu_lfsr |= feedback << (mode ? 6 : 14);
                        }
                    }
                    if (!(apu_lfsr & 1)) mix += vol * 600;
                }
            }

            buf[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
        }
    }
};
