#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const uint32_t sms_palette[64] = {
    0x000000,0x000000,0x00A0C4,0x00D0E8,0x0060FC,0x0028FC,0x9C20FC,0xA020B0,
    0x8C206C,0x842020,0x841400,0x7C2800,0x7C4400,0x345C00,0x007000,0x006834,
    0x000000,0x000000,0x58E8FC,0x60FCFC,0x58B4FC,0x507CFC,0x9058FC,0xD058F8,
    0xE858A8,0xE85850,0xD04808,0xC05800,0xAC7800,0x689800,0x28A40C,0x28A44C,
    0x000000,0x000000,0xB4FCF4,0xB8FCFC,0xB0D4FC,0xA8B8FC,0xB8ACFC,0xCCA8FC,
    0xECA8E0,0xECA898,0xE09848,0xD8AC30,0xD0C430,0x9CE028,0x68E834,0x68EC68,
    0x000000,0x000000,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,
    0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,0xFCFCFC,
};

struct SMS;

struct SMS_PSG {
    int tone[3];
    int volume[4];
    int noise_mode;
    int noise_freq;
    uint32_t phase[4];
    uint32_t noise_phase;
    uint16_t noise_lfsr;

    void reset() {
        for (int i = 0; i < 3; i++) tone[i] = 0;
        for (int i = 0; i < 4; i++) volume[i] = 0xF;
        phase[0] = phase[1] = phase[2] = phase[3] = 0;
        noise_phase = 0;
        noise_lfsr = 0x8000;
        noise_freq = 0;
        noise_mode = 0;
    }

    void write(uint8_t v) {
        if (v & 0x80) {
            int ch = (v >> 5) & 3;
            if (ch == 3) {
                noise_mode = (v >> 2) & 1;
                int fb = v & 7;
                noise_freq = fb;
            } else {
                int data = (v & 0x3F);
                if (v & 0x10) {
                    tone[ch] = (tone[ch] & 0x3F0) | (data & 0xF);
                } else {
                    tone[ch] = (tone[ch] & 0x00F) | ((data & 0x3F) << 4);
                }
            }
        } else {
            int ch = (v >> 5) & 3;
            volume[ch] = v & 0x0F;
        }
    }

    void gen(int16_t* buf, int samples, int sr) {
        int cpu_clocks = sr / 60;
        for (int i = 0; i < samples; i++) {
            int mix = 0;
            for (int ch = 0; ch < 3; ch++) {
                if (volume[ch] >= 0x0F) continue;
                int period = tone[ch];
                if (period == 0) continue;
                phase[ch] += cpu_clocks * 2;
                int bit = (phase[ch] / period) & 1;
                if (bit) {
                    int vol = (0x0F - volume[ch]);
                    mix += vol * 400;
                }
            }
            if (volume[3] < 0x0F) {
                int div = (noise_freq == 0) ? 0x10 : ((noise_freq & 3) == 3) ? tone[2] : (int)(0x10 << (noise_freq & 3));
                if (div < 1) div = 1;
                noise_phase += cpu_clocks * 2;
                int steps = noise_phase / div;
                noise_phase %= div;
                for (int s = 0; s < steps && s < 16; s++) {
                    uint16_t fb = (noise_lfsr & 1) ^ ((noise_lfsr >> (noise_mode ? 8 : 1)) & 1);
                    noise_lfsr = (noise_lfsr >> 1) | (fb << 15);
                }
                if (!(noise_lfsr & 1)) {
                    int vol = (0x0F - volume[3]);
                    mix += vol * 600;
                }
            }
            buf[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
        }
    }
};

struct SMS_VDP {
    uint8_t vram[0x4000];
    uint8_t cram[32];
    int vdp_regs[16];
    uint8_t status;
    uint8_t data_buf;
    uint16_t addr;
    int addr_latch;
    int code;
    int scanline;
    int v_counter;
    bool frame_done;
    uint32_t framebuffer[192 * 256];

    void reset() {
        memset(vram, 0, sizeof(vram));
        memset(cram, 0, sizeof(cram));
        memset(vdp_regs, 0, sizeof(vdp_regs));
        status = 0;
        data_buf = 0;
        addr = 0;
        addr_latch = 0;
        code = 0;
        scanline = 0;
        v_counter = 0;
        frame_done = false;
        memset(framebuffer, 0, sizeof(framebuffer));
    }

    int read_data() {
        uint8_t val = data_buf;
        data_buf = vram[addr & 0x3FFF];
        addr += (vdp_regs[15] & 0x40) ? 0x40 : 1;
        addr &= 0x3FFF;
        return val;
    }

    int read_control() {
        uint8_t val = status;
        status &= ~0x80;
        addr_latch = 0;
        return val;
    }

    void write_control(uint8_t val) {
        if (addr_latch == 0) {
            addr = (addr & 0xFF00) | val;
            addr_latch = 1;
        } else {
            addr = ((val & 0x3F) << 8) | (addr & 0xFF);
            code = (val >> 6) & 3;
            addr_latch = 0;
            if (code == 2) {
                int reg = addr & 0x0F;
                int data = addr >> 8;
                if (reg < 16) vdp_regs[reg] = data;
            }
        }
    }

    void write_data(uint8_t val) {
        if (code == 0) {
            vram[addr & 0x3FFF] = val;
            addr += (vdp_regs[15] & 0x40) ? 0x40 : 1;
            addr &= 0x3FFF;
        } else if (code == 1) {
            // VRAM read mode doesn't write
            addr += (vdp_regs[15] & 0x40) ? 0x40 : 1;
            addr &= 0x3FFF;
        } else if (code == 3) {
            int pal = (addr >> 4) & 1;
            int color = addr & 0x0F;
            cram[pal ? 16 + color : color] = val & 0x3F;
            addr += (vdp_regs[15] & 0x40) ? 0x40 : 1;
            addr &= 0x3FFF;
        }
    }

    void render_line(int y) {
        if (y < 0 || y >= 192) return;
        if (!(vdp_regs[1] & 0x40)) {
            for (int x = 0; x < 256; x++)
                framebuffer[y * 256 + x] = sms_palette[cram[0] & 0x3F] | 0xFF000000;
            return;
        }
        int nt_addr = 0x3800 | ((vdp_regs[2] & 0x0E) << 10);
        int hscroll = vdp_regs[8];
        int vscroll = vdp_regs[9];
        int line_scroll = vdp_regs[0x0F] & 0x80 ? 0 : hscroll;
        if (vdp_regs[0] & 0x40) line_scroll = hscroll;
        int sat_addr = (vdp_regs[5] & 0x7E) << 7;
        if (sat_addr > 0x3F00) sat_addr = 0x3F00;
        int spg_base = (vdp_regs[6] & 0x04) << 11;

        for (int x = 0; x < 256; x++) {
            int sx = (x + line_scroll) & 511;
            int sy = (y + vscroll) & 255;
            int tile_x = sx / 8;
            int tile_y = sy / 8;
            if (tile_y >= 24) tile_y -= 24;
            int tma = nt_addr + tile_y * 32 + tile_x;
            uint16_t te = vram[tma & 0x3FFF] | (vram[(tma + 1) & 0x3FFF] << 8);
            int tnum = te & 0x1FF;
            bool hf = te & 0x200, vf = te & 0x400;
            int pb = (te >> 11) & 1;
            int px = sx % 8, py = sy % 8;
            if (hf) px = 7 - px;
            if (vf) py = 7 - py;
            int ta = tnum * 32 + py * 4 + (px / 2);
            uint8_t bits = vram[ta & 0x3FFF];
            if (px & 1) bits >>= 4; else bits &= 0x0F;
            int pb_base = pb ? 16 : 0;
            int ci = bits ? pb_base + bits : 0;
            framebuffer[y * 256 + x] = sms_palette[cram[ci & 0x1F] & 0x3F] | 0xFF000000;
        }

        // Sprites (simplified)
        for (int i = 0; i < 64; i++) {
            int sy = vram[(sat_addr + i * 4) & 0x3FFF];
            int sx = vram[(sat_addr + i * 4 + 1) & 0x3FFF];
            int tile = vram[(sat_addr + i * 4 + 2) & 0x3FFF];
            int flags = vram[(sat_addr + i * 4 + 3) & 0x3FFF];
            int sr = y - (sy + 1);
            if (sr < 0 || sr >= 8) continue;
            int spn = (tile & 0x1FF);
            bool shf = flags & 4, svf = flags & 8;
            int spal = flags & 3;
            int sfy = svf ? 7 - sr : sr;
            for (int spx = 0; spx < 8; spx++) {
                int pxs = shf ? 7 - spx : spx;
                int px2 = sx + spx;
                if (px2 >= 256) px2 -= 256;
                if (px2 < 0 || px2 >= 256) continue;
                int sta = spg_base + spn * 32 + sfy * 4 + (pxs / 2);
                uint8_t bits = vram[sta & 0x3FFF];
                if (pxs & 1) bits >>= 4; else bits &= 0x0F;
                if (bits == 0) continue;
                int ci2 = 16 + spal * 16 + bits;
                framebuffer[y * 256 + px2] = sms_palette[cram[ci2 & 0x1F] & 0x3F] | 0xFF000000;
            }
        }
    }
};

struct Z80 {
    uint8_t b, c, d, e, h, l, a, f;
    uint8_t b_, c_, d_, e_, h_, l_, a_, f_;
    uint8_t ixh, ixl, iyh, iyl;
    uint16_t sp, pc;
    uint8_t i, r;
    int im;
    bool iff1, iff2;
    bool halted;
    SMS* sms;

    uint8_t mem_rb(uint16_t a);
    void mem_wb(uint16_t a, uint8_t v);
    uint16_t mem_rw(uint16_t a);
    void mem_ww(uint16_t a, uint16_t v);
    uint8_t io_in(uint8_t p);
    void io_out(uint8_t p, uint8_t v);

    void reset() {
        memset(&b, 0, sizeof(Z80) - sizeof(SMS*));
        f = 0x02;
        sp = 0xDFF0;
        iff1 = iff2 = false;
        im = 1;
        halted = false;
    }

    uint16_t bc() { return (b << 8) | c; }
    uint16_t de() { return (d << 8) | e; }
    uint16_t hl() { return (h << 8) | l; }
    uint16_t ix() { return (ixh << 8) | ixl; }
    uint16_t iy() { return (iyh << 8) | iyl; }
    uint16_t af() { return (a << 8) | f; }
    void set_bc(uint16_t v) { b = v >> 8; c = v & 0xFF; }
    void set_de(uint16_t v) { d = v >> 8; e = v & 0xFF; }
    void set_hl(uint16_t v) { h = v >> 8; l = v & 0xFF; }
    void set_ix(uint16_t v) { ixh = v >> 8; ixl = v & 0xFF; }
    void set_iy(uint16_t v) { iyh = v >> 8; iyl = v & 0xFF; }
    void set_af(uint16_t v) { a = v >> 8; f = v & 0xFF; }

    uint8_t flags_szp(uint8_t v) {
        uint8_t res = (v & 0x80) | (v == 0 ? 0x40 : 0);
        uint8_t p = 0; uint8_t t = v;
        for (int i = 0; i < 8; i++) { if (t & 1) p++; t >>= 1; }
        if (!(p & 1)) res |= 4;
        res |= (v & 0x28);
        return res;
    }

    void push(uint16_t v) { sp -= 2; mem_ww(sp, v); }
    uint16_t pop() { uint16_t v = mem_rw(sp); sp += 2; return v; }

    int cb_exec();
    int ed_exec();
    int ix_exec();
    int iy_exec();
    int step();
};

struct SMS {
    uint8_t *rom;
    uint32_t rom_size;
    uint8_t wram[0x2000];
    Z80 cpu;
    SMS_VDP vdp;
    SMS_PSG psg;
    uint8_t port_3E, port_3F;
    int mapper;
    uint8_t *page0, *page1, *page2;
    uint8_t joy1_state;
    bool joy1_pins[8];

    ~SMS() {
        delete[] rom;
    }

    void init() {
        memset(wram, 0, sizeof(wram));
        port_3E = 0;
        port_3F = 0xFF;
        mapper = 0;
        page0 = page1 = page2 = nullptr;
        joy1_state = 0xFF;
        memset(joy1_pins, 0, sizeof(joy1_pins));
        cpu.reset();
        cpu.sms = this;
        vdp.reset();
        psg.reset();
        cpu.pc = mem_rw(0xFFFC);
    }

    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t hdr[512];
        int skip = 0;
        fread(hdr, 1, 512, f);
        if (hdr[0x100] == 'T' && hdr[0x101] == 'M' && hdr[0x102] == 'R' && hdr[0x103] == ' ')
            skip = 512;
        else
            fseek(f, 0, SEEK_SET);
        sz -= skip;
        if (sz < 0x4000) { fclose(f); return false; }
        if (sz > 0x80000) sz = 0x80000;
        rom = new uint8_t[sz + 0x4000];
        memset(rom, 0, sz + 0x4000);
        memcpy(rom, hdr + skip, 512 - skip > 0 ? 512 - skip : 0);
        fread(rom + (512 - skip), 1, sz - (512 - skip), f);
        fclose(f);
        rom_size = sz;
        page0 = rom;
        page1 = rom + (rom_size >= 0x4000 ? 0x4000 : 0);
        page2 = rom + (rom_size >= 0x8000 ? 0x8000 : 0);
        if (rom_size < 0x4000) {
            page1 = rom; page2 = rom;
        } else if (rom_size < 0x8000) {
            page2 = page1;
        }
        mapper = (rom_size >= 0x20000) ? 1 : 0;
        init();
        page0 = rom;
        page1 = rom + (rom_size >= 0x4000 ? 0x4000 : 0);
        page2 = rom + (rom_size >= 0x8000 ? 0x8000 : 0);
        if (rom_size < 0x4000) {
            page1 = rom; page2 = rom;
        } else if (rom_size < 0x8000) {
            page2 = page1;
        }
        mapper = (rom_size >= 0x20000) ? 1 : 0;
        cpu.pc = mem_rw(0xFFFC);
        return true;
    }

    uint8_t mem_rb(uint16_t addr) {
        if (addr < 0x4000) return page0[addr];
        if (addr < 0x8000) return page1[addr & 0x3FFF];
        if (addr < 0xC000) return page2[addr & 0x3FFF];
        return wram[addr & 0x1FFF];
    }

    void mem_wb(uint16_t addr, uint8_t val) {
        if (addr < 0xC000) {
            if (mapper == 1 && addr >= 0x4000 && addr < 0x8000) {
                int bank = (val & 0x0F) % (rom_size / 0x4000);
                page1 = rom + bank * 0x4000;
                return;
            }
            if (mapper == 2 && addr >= 0x0000 && addr < 0x4000) {
                int bank = (val >> 1) & 0x0F;
                bank %= (rom_size / 0x4000);
                page1 = rom + bank * 0x4000;
                return;
            }
            return;
        }
        wram[addr & 0x1FFF] = val;
    }

    uint16_t mem_rw(uint16_t addr) {
        return mem_rb(addr) | (mem_rb(addr + 1) << 8);
    }

    void mem_ww(uint16_t addr, uint16_t val) {
        mem_wb(addr, val & 0xFF);
        mem_wb(addr + 1, val >> 8);
    }

    uint8_t io_read(uint8_t port) {
        if (((port & 0xC0) == 0xC0) && (port & 1)) return vdp.read_control();
        if (((port & 0xC0) == 0xC0) && !(port & 1)) return vdp.read_data();
        if (port == 0xDC) {
            uint8_t val = 0xFF;
            for (int i = 0; i < 8; i++) if (joy1_pins[i]) val &= ~(1 << i);
            return val;
        }
        if (port == 0xDD) return 0xFF;
        return 0xFF;
    }

    void io_write(uint8_t port, uint8_t val) {
        if (port == 0x7E || port == 0x7F) { psg.write(val); return; }
        if ((port & 0xC0) == 0x80) {
            if (port & 1) vdp.write_control(val);
            else vdp.write_data(val);
            return;
        }
    }

    void run_frame() {
        int cycles_per_frame = 3579545 / 60;
        int cycles_per_scanline = cycles_per_frame / 262;
        int cycles_done = 0;
        vdp.frame_done = false;

        for (int sl = 0; sl < 262; sl++) {
            vdp.scanline = sl;
            int line_cycles = 0;
            if (sl >= 0 && sl < 192) vdp.render_line(sl);
            if (sl == 192) vdp.status |= 0x80;
            while (line_cycles < cycles_per_scanline && cycles_done < cycles_per_frame) {
                int c = cpu.step();
                line_cycles += c;
                cycles_done += c;
            }
        }
        vdp.frame_done = true;
        vdp.status &= ~0x80;
    }

    void gen_audio(int16_t* buf, int samples, int sr) {
        psg.gen(buf, samples, sr);
    }
};

// Z80 implementations that need SMS
inline uint8_t Z80::mem_rb(uint16_t a) { return sms->mem_rb(a); }
inline void Z80::mem_wb(uint16_t a, uint8_t v) { sms->mem_wb(a, v); }
inline uint16_t Z80::mem_rw(uint16_t a) { return sms->mem_rw(a); }
inline void Z80::mem_ww(uint16_t a, uint16_t v) { sms->mem_ww(a, v); }
inline uint8_t Z80::io_in(uint8_t p) { return sms->io_read(p); }
inline void Z80::io_out(uint8_t p, uint8_t v) { sms->io_write(p, v); }

int Z80::step() {
    if (halted) {
        r++;
        return 4;
    }
    uint8_t op = mem_rb(pc++);
    uint8_t val, tmp;
    uint16_t addr, v16, tmp16;
    int cycles = 4;

    switch (op) {
        case 0x00: break;
        case 0x01: c = mem_rb(pc++); b = mem_rb(pc++); cycles = 10; break;
        case 0x02: mem_wb(bc(), a); cycles = 7; break;
        case 0x03: set_bc(bc() + 1); cycles = 6; break;
        case 0x04: b++; f = flags_szp(b); cycles = 4; break;
        case 0x05: b--; f = flags_szp(b); f |= 2; cycles = 4; break;
        case 0x06: b = mem_rb(pc++); cycles = 7; break;
        case 0x07: {
            f = (f & 0xC4) | (a >> 7);
            a = (a << 1) | (a >> 7);
            f |= (a & 0x28);
            cycles = 4; break;
        }
        case 0x08: { uint8_t ta = a, tf = f; a = a_; f = f_; a_ = ta; f_ = tf; cycles = 4; break; }
        case 0x09: {
            uint32_t sum = hl() + bc();
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((hl() & 0x0FFF) + (bc() & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_hl(sum & 0xFFFF); f |= (h & 0x28) | (l & 0x08);
            cycles = 11; break;
        }
        case 0x0A: a = mem_rb(bc()); cycles = 7; break;
        case 0x0B: set_bc(bc() - 1); cycles = 6; break;
        case 0x0C: c++; f = flags_szp(c); cycles = 4; break;
        case 0x0D: c--; f = flags_szp(c); f |= 2; cycles = 4; break;
        case 0x0E: c = mem_rb(pc++); cycles = 7; break;
        case 0x0F: {
            f = (f & 0xC4) | (a & 1);
            a = (a >> 1) | ((a & 1) << 7);
            f |= (a & 0x28);
            cycles = 4; break;
        }
        case 0x10: { b--; if (b) pc += (int8_t)mem_rb(pc); pc++; cycles = b ? 13 : 8; break; }
        case 0x11: e = mem_rb(pc++); d = mem_rb(pc++); cycles = 10; break;
        case 0x12: mem_wb(de(), a); cycles = 7; break;
        case 0x13: set_de(de() + 1); cycles = 6; break;
        case 0x14: d++; f = flags_szp(d); cycles = 4; break;
        case 0x15: d--; f = flags_szp(d); f |= 2; cycles = 4; break;
        case 0x16: d = mem_rb(pc++); cycles = 7; break;
        case 0x17: { uint8_t oldc = f & 1; f = (f & 0xC4) | (a >> 7); a = (a << 1) | oldc; f |= (a & 0x28); cycles = 4; break; }
        case 0x18: pc += (int8_t)mem_rb(pc++); cycles = 12; break;
        case 0x19: {
            uint32_t sum = hl() + de();
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((hl() & 0x0FFF) + (de() & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_hl(sum & 0xFFFF); f |= (h & 0x28) | (l & 0x08);
            cycles = 11; break;
        }
        case 0x1A: a = mem_rb(de()); cycles = 7; break;
        case 0x1B: set_de(de() - 1); cycles = 6; break;
        case 0x1C: e++; f = flags_szp(e); cycles = 4; break;
        case 0x1D: e--; f = flags_szp(e); f |= 2; cycles = 4; break;
        case 0x1E: e = mem_rb(pc++); cycles = 7; break;
        case 0x1F: { uint8_t oldc = f & 1; f = (f & 0xC4) | (a & 1); a = (a >> 1) | (oldc << 7); f |= (a & 0x28); cycles = 4; break; }
        case 0x20: if (!(f & 0x40)) pc += (int8_t)mem_rb(pc); else pc++; cycles = (f & 0x40) ? 7 : 12; break;
        case 0x21: l = mem_rb(pc++); h = mem_rb(pc++); cycles = 10; break;
        case 0x22: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, hl()); cycles = 16; break;
        case 0x23: set_hl(hl() + 1); cycles = 6; break;
        case 0x24: h++; f = flags_szp(h); cycles = 4; break;
        case 0x25: h--; f = flags_szp(h); f |= 2; cycles = 4; break;
        case 0x26: h = mem_rb(pc++); cycles = 7; break;
        case 0x27: {
            uint16_t v = a;
            if ((f & 0x10) || (a & 0x0F) > 9) v += 6;
            if ((f & 1) || (a >> 4) > 9) v += 0x60;
            a = v & 0xFF;
            f = (f & 0xFE) | ((v >> 8) & 1) | flags_szp(a);
            f &= ~0x10;
            cycles = 4; break;
        }
        case 0x28: if (f & 0x40) pc += (int8_t)mem_rb(pc); else pc++; cycles = (f & 0x40) ? 7 : 12; break;
        case 0x29: {
            uint32_t sum = hl() + hl();
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((hl() & 0x0FFF) + (hl() & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_hl(sum & 0xFFFF); f |= (h & 0x28) | (l & 0x08);
            cycles = 11; break;
        }
        case 0x2A: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; set_hl(mem_rw(addr)); cycles = 16; break;
        case 0x2B: set_hl(hl() - 1); cycles = 6; break;
        case 0x2C: l++; f = flags_szp(l); cycles = 4; break;
        case 0x2D: l--; f = flags_szp(l); f |= 2; cycles = 4; break;
        case 0x2E: l = mem_rb(pc++); cycles = 7; break;
        case 0x2F: a = ~a; f = (f & 0xC4) | 0x12 | (a & 0x28); cycles = 4; break;
        case 0x30: if (!(f & 1)) pc += (int8_t)mem_rb(pc); else pc++; cycles = (f & 1) ? 7 : 12; break;
        case 0x31: l = mem_rb(pc++); h = mem_rb(pc++); sp = (h << 8) | l; cycles = 10; break;
        case 0x32: mem_wb(mem_rb(pc) | (mem_rb(pc + 1) << 8), a); pc += 2; cycles = 13; break;
        case 0x33: sp++; cycles = 6; break;
        case 0x34: val = mem_rb(hl()) + 1; mem_wb(hl(), val); f = flags_szp(val); cycles = 11; break;
        case 0x35: val = mem_rb(hl()) - 1; mem_wb(hl(), val); f = flags_szp(val); f |= 2; cycles = 11; break;
        case 0x36: mem_wb(hl(), mem_rb(pc++)); cycles = 10; break;
        case 0x37: f = (f & 0xC4) | 1 | (a & 0x28); cycles = 4; break;
        case 0x38: if (f & 1) pc += (int8_t)mem_rb(pc); else pc++; cycles = (f & 1) ? 7 : 12; break;
        case 0x39: {
            uint32_t sum = hl() + sp;
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((hl() & 0x0FFF) + (sp & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_hl(sum & 0xFFFF); f |= (h & 0x28) | (l & 0x08);
            cycles = 11; break;
        }
        case 0x3A: a = mem_rb(mem_rb(pc) | (mem_rb(pc + 1) << 8)); pc += 2; cycles = 13; break;
        case 0x3B: sp--; cycles = 6; break;
        case 0x3C: a++; f = flags_szp(a); cycles = 4; break;
        case 0x3D: a--; f = flags_szp(a); f |= 2; cycles = 4; break;
        case 0x3E: a = mem_rb(pc++); cycles = 7; break;
        case 0x3F: f = (f & 0xC4) | ((f & 1) << 4) | (a >> 7); cycles = 4; break;

        case 0x40: b = b; break; case 0x41: b = c; break; case 0x42: b = d; break;
        case 0x43: b = e; break; case 0x44: b = h; break; case 0x45: b = l; break;
        case 0x46: b = mem_rb(hl()); cycles = 7; break; case 0x47: b = a; break;
        case 0x48: c = b; break; case 0x49: c = c; break; case 0x4A: c = d; break;
        case 0x4B: c = e; break; case 0x4C: c = h; break; case 0x4D: c = l; break;
        case 0x4E: c = mem_rb(hl()); cycles = 7; break; case 0x4F: c = a; break;
        case 0x50: d = b; break; case 0x51: d = c; break; case 0x52: d = d; break;
        case 0x53: d = e; break; case 0x54: d = h; break; case 0x55: d = l; break;
        case 0x56: d = mem_rb(hl()); cycles = 7; break; case 0x57: d = a; break;
        case 0x58: e = b; break; case 0x59: e = c; break; case 0x5A: e = d; break;
        case 0x5B: e = e; break; case 0x5C: e = h; break; case 0x5D: e = l; break;
        case 0x5E: e = mem_rb(hl()); cycles = 7; break; case 0x5F: e = a; break;
        case 0x60: h = b; break; case 0x61: h = c; break; case 0x62: h = d; break;
        case 0x63: h = e; break; case 0x64: h = h; break; case 0x65: h = l; break;
        case 0x66: h = mem_rb(hl()); cycles = 7; break; case 0x67: h = a; break;
        case 0x68: l = b; break; case 0x69: l = c; break; case 0x6A: l = d; break;
        case 0x6B: l = e; break; case 0x6C: l = h; break; case 0x6D: l = l; break;
        case 0x6E: l = mem_rb(hl()); cycles = 7; break; case 0x6F: l = a; break;
        case 0x70: mem_wb(hl(), b); cycles = 7; break; case 0x71: mem_wb(hl(), c); cycles = 7; break;
        case 0x72: mem_wb(hl(), d); cycles = 7; break; case 0x73: mem_wb(hl(), e); cycles = 7; break;
        case 0x74: mem_wb(hl(), h); cycles = 7; break; case 0x75: mem_wb(hl(), l); cycles = 7; break;
        case 0x76: halted = true; cycles = 4; break;
        case 0x77: mem_wb(hl(), a); cycles = 7; break;

        case 0x78: a = b; f = (f & 0xC4) | flags_szp(b) & ~0xC4; break;
        case 0x79: a = c; f = (f & 0xC4) | flags_szp(c) & ~0xC4; break;
        case 0x7A: a = d; f = (f & 0xC4) | flags_szp(d) & ~0xC4; break;
        case 0x7B: a = e; f = (f & 0xC4) | flags_szp(e) & ~0xC4; break;
        case 0x7C: a = h; f = (f & 0xC4) | flags_szp(h) & ~0xC4; break;
        case 0x7D: a = l; f = (f & 0xC4) | flags_szp(l) & ~0xC4; break;
        case 0x7E: a = mem_rb(hl()); f = (f & 0xC4) | flags_szp(a) & ~0xC4; cycles = 7; break;
        case 0x7F: break;

        case 0x80: val = b; goto add; case 0x81: val = c; goto add; case 0x82: val = d; goto add;
        case 0x83: val = e; goto add; case 0x84: val = h; goto add; case 0x85: val = l; goto add;
        case 0x86: val = mem_rb(hl()); cycles = 7; goto add; case 0x87: val = a; goto add;
        add: {
            uint16_t r = a + val;
            int hc = (a & 0x0F) + (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28); break;
        }

        case 0x88: val = b; goto adc; case 0x89: val = c; goto adc; case 0x8A: val = d; goto adc;
        case 0x8B: val = e; goto adc; case 0x8C: val = h; goto adc; case 0x8D: val = l; goto adc;
        case 0x8E: val = mem_rb(hl()); cycles = 7; goto adc; case 0x8F: val = a; goto adc;
        adc: {
            uint16_t r = a + val + (f & 1);
            int hc = (a & 0x0F) + (val & 0x0F) + (f & 1);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28); break;
        }

        case 0x90: val = b; goto sub; case 0x91: val = c; goto sub; case 0x92: val = d; goto sub;
        case 0x93: val = e; goto sub; case 0x94: val = h; goto sub; case 0x95: val = l; goto sub;
        case 0x96: val = mem_rb(hl()); cycles = 7; goto sub; case 0x97: val = a; goto sub;
        sub: {
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28); break;
        }

        case 0x98: val = b; goto sbc; case 0x99: val = c; goto sbc; case 0x9A: val = d; goto sbc;
        case 0x9B: val = e; goto sbc; case 0x9C: val = h; goto sbc; case 0x9D: val = l; goto sbc;
        case 0x9E: val = mem_rb(hl()); cycles = 7; goto sbc; case 0x9F: val = a; goto sbc;
        sbc: {
            int c = f & 1;
            uint16_t r = a - val - c;
            int hc = (a & 0x0F) - (val & 0x0F) - c;
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28); break;
        }

        case 0xA0: a &= b; f = flags_szp(a); f |= 0x10; break;
        case 0xA1: a &= c; f = flags_szp(a); f |= 0x10; break;
        case 0xA2: a &= d; f = flags_szp(a); f |= 0x10; break;
        case 0xA3: a &= e; f = flags_szp(a); f |= 0x10; break;
        case 0xA4: a &= h; f = flags_szp(a); f |= 0x10; break;
        case 0xA5: a &= l; f = flags_szp(a); f |= 0x10; break;
        case 0xA6: a &= mem_rb(hl()); f = flags_szp(a); f |= 0x10; cycles = 7; break;
        case 0xA7: a &= a; f = flags_szp(a); f |= 0x10; break;

        case 0xA8: a ^= b; f = flags_szp(a); break;
        case 0xA9: a ^= c; f = flags_szp(a); break;
        case 0xAA: a ^= d; f = flags_szp(a); break;
        case 0xAB: a ^= e; f = flags_szp(a); break;
        case 0xAC: a ^= h; f = flags_szp(a); break;
        case 0xAD: a ^= l; f = flags_szp(a); break;
        case 0xAE: a ^= mem_rb(hl()); f = flags_szp(a); cycles = 7; break;
        case 0xAF: a ^= a; f = flags_szp(a); break;

        case 0xB0: a |= b; f = flags_szp(a); break;
        case 0xB1: a |= c; f = flags_szp(a); break;
        case 0xB2: a |= d; f = flags_szp(a); break;
        case 0xB3: a |= e; f = flags_szp(a); break;
        case 0xB4: a |= h; f = flags_szp(a); break;
        case 0xB5: a |= l; f = flags_szp(a); break;
        case 0xB6: a |= mem_rb(hl()); f = flags_szp(a); cycles = 7; break;
        case 0xB7: a |= a; f = flags_szp(a); break;

        case 0xB8: val = b; goto cp; case 0xB9: val = c; goto cp; case 0xBA: val = d; goto cp;
        case 0xBB: val = e; goto cp; case 0xBC: val = h; goto cp; case 0xBD: val = l; goto cp;
        case 0xBE: val = mem_rb(hl()); cycles = 7; goto cp; case 0xBF: val = a; goto cp;
        cp: {
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            f |= (val & 0x28); break;
        }

        case 0xC0: if (!(f & 0x40)) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xC1: set_bc(pop()); cycles = 10; break;
        case 0xC2: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 0x40)) pc = addr; cycles = 10; break;
        case 0xC3: pc = mem_rb(pc) | (mem_rb(pc + 1) << 8); cycles = 10; break;
        case 0xC4: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 0x40)) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xC5: push(bc()); cycles = 11; break;
        case 0xC6: {
            val = mem_rb(pc++);
            uint16_t r = a + val;
            int hc = (a & 0x0F) + (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 7; break;
        }
        case 0xC7: push(pc); pc = 0x00; cycles = 11; break;
        case 0xC8: if (f & 0x40) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xC9: pc = pop(); cycles = 10; break;
        case 0xCA: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 0x40) pc = addr; cycles = 10; break;
        case 0xCB: return cb_exec();
        case 0xCC: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 0x40) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xCD: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; push(pc); pc = addr; cycles = 17; break;
        case 0xCE: {
            val = mem_rb(pc++);
            uint16_t r = a + val + (f & 1);
            int hc = (a & 0x0F) + (val & 0x0F) + (f & 1);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 7; break;
        }
        case 0xCF: push(pc); pc = 0x08; cycles = 11; break;

        case 0xD0: if (!(f & 1)) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xD1: set_de(pop()); cycles = 10; break;
        case 0xD2: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 1)) pc = addr; cycles = 10; break;
        case 0xD3: io_out(mem_rb(pc++), a); cycles = 11; break;
        case 0xD4: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 1)) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xD5: push(de()); cycles = 11; break;
        case 0xD6: {
            val = mem_rb(pc++);
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 7; break;
        }
        case 0xD7: push(pc); pc = 0x10; cycles = 11; break;
        case 0xD8: if (f & 1) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xD9: {
            uint8_t t; t = b; b = b_; b_ = t; t = c; c = c_; c_ = t;
            t = d; d = d_; d_ = t; t = e; e = e_; e_ = t;
            t = h; h = h_; h_ = t; t = l; l = l_; l_ = t;
            cycles = 4; break;
        }
        case 0xDA: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 1) pc = addr; cycles = 10; break;
        case 0xDB: a = io_in(mem_rb(pc++)); cycles = 11; break;
        case 0xDC: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 1) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xDE: {
            val = mem_rb(pc++);
            int c = f & 1;
            uint16_t r = a - val - c;
            int hc = (a & 0x0F) - (val & 0x0F) - c;
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 7; break;
        }
        case 0xDF: push(pc); pc = 0x18; cycles = 11; break;

        case 0xE0: if (!(f & 4)) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xE1: set_hl(pop()); cycles = 10; break;
        case 0xE2: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 4)) pc = addr; cycles = 10; break;
        case 0xE3: tmp16 = mem_rw(sp); mem_ww(sp, hl()); set_hl(tmp16); cycles = 19; break;
        case 0xE4: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 4)) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xE5: push(hl()); cycles = 11; break;
        case 0xE6: a &= mem_rb(pc++); f = flags_szp(a); f |= 0x10; cycles = 7; break;
        case 0xE7: push(pc); pc = 0x20; cycles = 11; break;
        case 0xE8: if (f & 4) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xE9: pc = hl(); cycles = 4; break;
        case 0xEA: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 4) pc = addr; cycles = 10; break;
        case 0xEB: tmp16 = hl(); set_hl(de()); set_de(tmp16); cycles = 4; break;
        case 0xEC: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 4) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xEE: a ^= mem_rb(pc++); f = flags_szp(a); cycles = 7; break;
        case 0xEF: push(pc); pc = 0x28; cycles = 11; break;

        case 0xF0: if (!(f & 0x80)) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xF1: set_af(pop()); f = (f & 0xD7) | 2; cycles = 10; break;
        case 0xF2: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 0x80)) pc = addr; cycles = 10; break;
        case 0xF3: iff1 = iff2 = false; cycles = 4; break;
        case 0xF4: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (!(f & 0x80)) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xF5: push(af()); cycles = 11; break;
        case 0xF6: a |= mem_rb(pc++); f = flags_szp(a); cycles = 7; break;
        case 0xF7: push(pc); pc = 0x30; cycles = 11; break;
        case 0xF8: if (f & 0x80) { pc = pop(); cycles = 11; } else cycles = 5; break;
        case 0xF9: sp = hl(); cycles = 6; break;
        case 0xFA: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 0x80) pc = addr; cycles = 10; break;
        case 0xFB: iff1 = iff2 = true; cycles = 4; break;
        case 0xFC: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; if (f & 0x80) { push(pc); pc = addr; cycles = 17; } else cycles = 10; break;
        case 0xFE: {
            val = mem_rb(pc++);
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            f |= (val & 0x28);
            cycles = 7; break;
        }
        case 0xFF: push(pc); pc = 0x38; cycles = 11; break;

        case 0xDD: return ix_exec();
        case 0xED: return ed_exec();
        case 0xFD: return iy_exec();

        default: break;
    }
    return cycles;
}

int Z80::cb_exec() {
    uint8_t op2 = mem_rb(pc++);
    uint8_t val, res;
    int cycles = 8;
    auto get_reg = [&](int r) -> uint8_t& {
        if (r == 0) return b; if (r == 1) return c; if (r == 2) return d;
        if (r == 3) return e; if (r == 4) return h; if (r == 5) return l;
        if (r == 7) return a;
        val = mem_rb(hl()); return val;
    };
    auto set_reg = [&](int r, uint8_t v) {
        if (r == 0) b = v; else if (r == 1) c = v; else if (r == 2) d = v;
        else if (r == 3) e = v; else if (r == 4) h = v; else if (r == 5) l = v;
        else if (r == 7) a = v;
        else mem_wb(hl(), v);
    };
    int r = op2 & 7;
    int bitn = (op2 >> 3) & 7;
    int group = op2 >> 6;
    if (group == 0) {
        uint8_t v = get_reg(r);
        switch (bitn) {
            case 0: res = (v << 1) | (v >> 7); f = (v >> 7) | flags_szp(res); break;
            case 1: res = (v >> 1) | (v << 7); f = (v & 1) | flags_szp(res); break;
            case 2: res = (v << 1) | (f & 1); f = (v >> 7) | flags_szp(res); break;
            case 3: res = (v >> 1) | ((f & 1) << 7); f = (v & 1) | flags_szp(res); break;
            case 4: res = v << 1; f = (v >> 7) | flags_szp(res); break;
            case 5: res = (v >> 1) | (v & 0x80); f = (v & 1) | flags_szp(res); break;
            case 6: res = (v << 1) | 1; f = (v >> 7) | flags_szp(res); break;
            case 7: res = v >> 1; f = (v & 1) | flags_szp(res); break;
        }
        set_reg(r, res);
        cycles = (r == 6) ? 15 : 8;
    } else if (group == 1) {
        val = get_reg(r);
        int bit = (val >> bitn) & 1;
        f = (f & 1) | 0x10 | (bit ? 0 : 0x40) | ((bitn == 7) ? 0x80 : 0);
        if (r == 6) f |= 0x28; else f |= (val & 0x28);
        cycles = (r == 6) ? 12 : 8;
    } else if (group == 2) {
        val = get_reg(r);
        set_reg(r, val & ~(1 << bitn));
        cycles = (r == 6) ? 15 : 8;
    } else {
        val = get_reg(r);
        set_reg(r, val | (1 << bitn));
        cycles = (r == 6) ? 15 : 8;
    }
    return cycles;
}

int Z80::ed_exec() {
    uint8_t op2 = mem_rb(pc++);
    int cycles = 8;
    uint16_t addr;
    uint8_t val;

    switch (op2) {
        case 0x40: b = io_in(bc() & 0xFF); f = flags_szp(b); cycles = 12; break;
        case 0x41: io_out(bc() & 0xFF, b); cycles = 12; break;
        case 0x42: {
            uint32_t r = hl() - bc() - (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0) | 2;
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x43: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, bc()); cycles = 20; break;
        case 0x44: case 0x4C: case 0x54: case 0x5C: case 0x64: case 0x6C: case 0x74: case 0x7C: {
            uint8_t old = a; a = 0 - a;
            f = flags_szp(a) | 2; if (old) f |= 4; if (old == 0x80) f |= 1;
            cycles = 8; break;
        }
        case 0x45: case 0x4D: case 0x55: case 0x5D: case 0x65: case 0x6D: case 0x75: case 0x7D:
            iff1 = iff2; pc = pop(); cycles = 14; break;
        case 0x46: case 0x4E: case 0x56: case 0x5E: case 0x66: case 0x6E: case 0x76: case 0x7E:
            im = (op2 == 0x56 || op2 == 0x5E || op2 == 0x76) ? 1 : (op2 == 0x7E) ? 2 : 0;
            cycles = 8; break;
        case 0x47: i = a; cycles = 9; break;
        case 0x48: c = io_in(bc() & 0xFF); f = flags_szp(c); cycles = 12; break;
        case 0x49: io_out(bc() & 0xFF, c); cycles = 12; break;
        case 0x4A: {
            uint32_t r = hl() + bc() + (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0);
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x4B: set_bc(mem_rw(mem_rb(pc) | (mem_rb(pc + 1) << 8))); pc += 2; cycles = 20; break;
        case 0x50: d = io_in(bc() & 0xFF); f = flags_szp(d); cycles = 12; break;
        case 0x51: io_out(bc() & 0xFF, d); cycles = 12; break;
        case 0x52: {
            uint32_t r = hl() - de() - (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0) | 2;
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x53: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, de()); cycles = 20; break;
        case 0x57: a = i; f = (f & 1) | flags_szp(a); if (iff2) f |= 4; cycles = 9; break;
        case 0x58: e = io_in(bc() & 0xFF); f = flags_szp(e); cycles = 12; break;
        case 0x59: io_out(bc() & 0xFF, e); cycles = 12; break;
        case 0x5A: {
            uint32_t r = hl() + de() + (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0);
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x5B: set_de(mem_rw(mem_rb(pc) | (mem_rb(pc + 1) << 8))); pc += 2; cycles = 20; break;
        case 0x60: h = io_in(bc() & 0xFF); f = flags_szp(h); cycles = 12; break;
        case 0x61: io_out(bc() & 0xFF, h); cycles = 12; break;
        case 0x62: {
            uint32_t r = hl() - hl() - (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0) | 2;
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x63: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, hl()); cycles = 20; break;
        case 0x67: {
            addr = hl(); val = mem_rb(addr);
            mem_wb(addr, (a << 4) | (val >> 4));
            a = (a & 0xF0) | (val & 0x0F);
            f = flags_szp(a); cycles = 18; break;
        }
        case 0x68: l = io_in(bc() & 0xFF); f = flags_szp(l); cycles = 12; break;
        case 0x69: io_out(bc() & 0xFF, l); cycles = 12; break;
        case 0x6A: {
            uint32_t r = hl() + hl() + (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0);
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x6B: set_hl(mem_rw(mem_rb(pc) | (mem_rb(pc + 1) << 8))); pc += 2; cycles = 20; break;
        case 0x6F: {
            addr = hl(); val = mem_rb(addr);
            mem_wb(addr, (val << 4) | (a & 0x0F));
            a = (a & 0xF0) | (val >> 4);
            f = flags_szp(a); cycles = 18; break;
        }
        case 0x70: io_in(bc() & 0xFF); cycles = 12; break;
        case 0x71: io_out(bc() & 0xFF, 0); cycles = 12; break;
        case 0x72: {
            uint32_t r = hl() - sp - (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0) | 2;
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x73: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, sp); cycles = 20; break;
        case 0x78: a = io_in(bc() & 0xFF); f = flags_szp(a); cycles = 12; break;
        case 0x79: io_out(bc() & 0xFF, a); cycles = 12; break;
        case 0x7A: {
            uint32_t r = hl() + sp + (f & 1);
            f = ((r & 0x8000) ? 0x80 : 0) | ((r & 0xFFFF) == 0 ? 0x40 : 0);
            set_hl(r & 0xFFFF);
            cycles = 15; break;
        }
        case 0x7B: sp = mem_rw(mem_rb(pc) | (mem_rb(pc + 1) << 8)); pc += 2; cycles = 20; break;
        case 0xA0: case 0xB0: {
            val = mem_rb(hl()); mem_wb(de(), val);
            set_hl(hl() + 1); set_de(de() + 1);
            uint16_t bc_new = bc() - 1; set_bc(bc_new);
            f = (f & 0xC1) | (bc_new ? 4 : 0) | ((a + val) & 0x08 ? 0x10 : 0) | ((a + val) & 1 ? 0x20 : 0);
            if (op2 == 0xB0 && bc_new) { pc -= 2; cycles = 21; } else cycles = 16;
            break;
        }
        case 0xA1: case 0xB1: {
            val = mem_rb(hl()); set_hl(hl() + 1);
            uint16_t bc_new = bc() - 1; set_bc(bc_new);
            uint16_t r = a - val;
            f = (f & 1) | 2 | ((r & 0x80) ? 0x80 : 0) | (r == 0 ? 0x40 : 0) | (bc_new ? 4 : 0);
            if ((a & 0x0F) - (val & 0x0F) < 0) f |= 0x10;
            if (op2 == 0xB1 && bc_new && a != val) { pc -= 2; cycles = 21; } else cycles = 16;
            break;
        }
        default: break;
    }
    return cycles;
}

int Z80::ix_exec() {
    uint8_t op2 = mem_rb(pc++);
    int cycles = 8;
    uint16_t addr, tmp16;
    uint8_t val;
    auto ix = [&]() { return (ixh << 8) | ixl; };
    auto set_ix = [&](uint16_t v) { ixh = v >> 8; ixl = v & 0xFF; };

    switch (op2) {
        case 0x09: {
            uint32_t sum = ix() + bc();
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((ix() & 0x0FFF) + (bc() & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_ix(sum & 0xFFFF); f |= (ixh & 0x28) | (ixl & 0x08);
            cycles = 15; break;
        }
        case 0x19: {
            uint32_t sum = ix() + de();
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((ix() & 0x0FFF) + (de() & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_ix(sum & 0xFFFF); f |= (ixh & 0x28) | (ixl & 0x08);
            cycles = 15; break;
        }
        case 0x21: ixl = mem_rb(pc++); ixh = mem_rb(pc++); cycles = 14; break;
        case 0x22: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, ix()); cycles = 20; break;
        case 0x23: set_ix(ix() + 1); cycles = 10; break;
        case 0x24: ixh++; f = flags_szp(ixh); cycles = 8; break;
        case 0x25: ixh--; f = flags_szp(ixh); f |= 2; cycles = 8; break;
        case 0x26: ixh = mem_rb(pc++); cycles = 11; break;
        case 0x29: {
            uint32_t sum = ix() + ix();
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((ix() & 0x0FFF) + (ix() & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_ix(sum & 0xFFFF); f |= (ixh & 0x28) | (ixl & 0x08);
            cycles = 15; break;
        }
        case 0x2A: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; set_ix(mem_rw(addr)); cycles = 20; break;
        case 0x2B: set_ix(ix() - 1); cycles = 10; break;
        case 0x2C: ixl++; f = flags_szp(ixl); cycles = 8; break;
        case 0x2D: ixl--; f = flags_szp(ixl); f |= 2; cycles = 8; break;
        case 0x2E: ixl = mem_rb(pc++); cycles = 11; break;
        case 0x34: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(ix() + d) + 1; mem_wb(ix() + d, val); f = flags_szp(val); cycles = 23; break; }
        case 0x35: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(ix() + d) - 1; mem_wb(ix() + d, val); f = flags_szp(val); f |= 2; cycles = 23; break; }
        case 0x36: { int8_t d = (int8_t)mem_rb(pc++); mem_wb(ix() + d, mem_rb(pc++)); cycles = 19; break; }
        case 0x39: {
            uint32_t sum = ix() + sp;
            f = (f & 0xC4) | ((sum >> 16) ? 1 : 0);
            if ((ix() & 0x0FFF) + (sp & 0x0FFF) > 0x0FFF) f |= 0x10;
            set_ix(sum & 0xFFFF); f |= (ixh & 0x28) | (ixl & 0x08);
            cycles = 15; break;
        }
        case 0x44: b = ixh; break; case 0x45: b = ixl; break;
        case 0x46: { int8_t d = (int8_t)mem_rb(pc++); b = mem_rb(ix() + d); cycles = 19; break; }
        case 0x4C: c = ixh; break; case 0x4D: c = ixl; break;
        case 0x4E: { int8_t d = (int8_t)mem_rb(pc++); c = mem_rb(ix() + d); cycles = 19; break; }
        case 0x54: d = ixh; break; case 0x55: d = ixl; break;
        case 0x56: { int8_t d = (int8_t)mem_rb(pc++); d = mem_rb(ix() + d); cycles = 19; break; }
        case 0x5C: e = ixh; break; case 0x5D: e = ixl; break;
        case 0x5E: { int8_t d = (int8_t)mem_rb(pc++); e = mem_rb(ix() + d); cycles = 19; break; }
        case 0x60: ixh = b; break; case 0x61: ixh = c; break; case 0x62: ixh = d; break;
        case 0x63: ixh = e; break; case 0x64: ixh = ixh; break; case 0x65: ixh = ixl; break;
        case 0x66: { int8_t d = (int8_t)mem_rb(pc++); h = mem_rb(ix() + d); cycles = 19; break; }
        case 0x67: ixh = a; break;
        case 0x68: ixl = b; break; case 0x69: ixl = c; break; case 0x6A: ixl = d; break;
        case 0x6B: ixl = e; break; case 0x6C: ixl = ixh; break; case 0x6D: ixl = ixl; break;
        case 0x6E: { int8_t d = (int8_t)mem_rb(pc++); l = mem_rb(ix() + d); cycles = 19; break; }
        case 0x6F: ixl = a; break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77: {
            int8_t disp = (int8_t)mem_rb(pc++);
            static const uint8_t rmap[] = {b,c,d,e,h,l,0,a};
            mem_wb(ix() + disp, rmap[op2 & 7]);
            cycles = 19; break;
        }
        case 0x7E: { int8_t d = (int8_t)mem_rb(pc++); a = mem_rb(ix() + d); cycles = 19; break; }
        case 0x86: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(ix() + d);
            uint16_t r = a + val;
            int hc = (a & 0x0F) + (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0x8E: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(ix() + d);
            uint16_t r = a + val + (f & 1);
            int hc = (a & 0x0F) + (val & 0x0F) + (f & 1);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0x96: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(ix() + d);
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0x9E: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(ix() + d);
            int c = f & 1;
            uint16_t r = a - val - c;
            int hc = (a & 0x0F) - (val & 0x0F) - c;
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0xA6: { int8_t d = (int8_t)mem_rb(pc++); a &= mem_rb(ix() + d); f = flags_szp(a); f |= 0x10; cycles = 19; break; }
        case 0xAE: { int8_t d = (int8_t)mem_rb(pc++); a ^= mem_rb(ix() + d); f = flags_szp(a); cycles = 19; break; }
        case 0xB6: { int8_t d = (int8_t)mem_rb(pc++); a |= mem_rb(ix() + d); f = flags_szp(a); cycles = 19; break; }
        case 0xBE: { int8_t d = (int8_t)mem_rb(pc++);
            val = mem_rb(ix() + d);
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            f |= (val & 0x28);
            cycles = 19; break;
        }
        case 0xE1: set_ix(pop()); cycles = 14; break;
        case 0xE3: tmp16 = mem_rw(sp); mem_ww(sp, ix()); set_ix(tmp16); cycles = 23; break;
        case 0xE5: push(ix()); cycles = 15; break;
        case 0xE9: pc = ix(); cycles = 8; break;
        case 0xCB: {
            int8_t d = (int8_t)mem_rb(pc++);
            uint8_t ixop = mem_rb(pc++);
            addr = ix() + d;
            val = mem_rb(addr);
            int bitn = (ixop >> 3) & 7;
            int grp = ixop >> 6;
            if (grp == 0) {
                uint8_t res;
                switch (bitn) {
                    case 0: res = (val << 1) | (val >> 7); f = (val >> 7) | flags_szp(res); break;
                    case 1: res = (val >> 1) | (val << 7); f = (val & 1) | flags_szp(res); break;
                    case 2: res = (val << 1) | (f & 1); f = (val >> 7) | flags_szp(res); break;
                    case 3: res = (val >> 1) | ((f & 1) << 7); f = (val & 1) | flags_szp(res); break;
                    case 4: res = val << 1; f = (val >> 7) | flags_szp(res); break;
                    case 5: res = (val >> 1) | (val & 0x80); f = (val & 1) | flags_szp(res); break;
                    case 6: res = (val << 1) | 1; f = (val >> 7) | flags_szp(res); break;
                    case 7: res = val >> 1; f = (val & 1) | flags_szp(res); break;
                }
                mem_wb(addr, res);
            } else if (grp == 1) {
                int bit = (val >> bitn) & 1;
                f = (f & 1) | 0x10 | (bit ? 0 : 0x40) | ((bitn == 7) ? 0x80 : 0);
                f |= (val & 0x28);
            } else if (grp == 2) {
                mem_wb(addr, val & ~(1 << bitn));
            } else {
                mem_wb(addr, val | (1 << bitn));
            }
            cycles = 23;
            break;
        }
        default: break;
    }
    return cycles;
}

int Z80::iy_exec() {
    uint8_t op2 = mem_rb(pc++);
    int cycles = 8;
    uint16_t addr, tmp16;
    uint8_t val;
    auto iy = [&]() { return (iyh << 8) | iyl; };
    auto set_iy = [&](uint16_t v) { iyh = v >> 8; iyl = v & 0xFF; };

    switch (op2) {
        case 0x09: { uint32_t sum = iy() + bc(); f = (f & 0xC4) | ((sum >> 16) ? 1 : 0); if ((iy() & 0x0FFF) + (bc() & 0x0FFF) > 0x0FFF) f |= 0x10; set_iy(sum & 0xFFFF); f |= (iyh & 0x28) | (iyl & 0x08); cycles = 15; break; }
        case 0x19: { uint32_t sum = iy() + de(); f = (f & 0xC4) | ((sum >> 16) ? 1 : 0); if ((iy() & 0x0FFF) + (de() & 0x0FFF) > 0x0FFF) f |= 0x10; set_iy(sum & 0xFFFF); f |= (iyh & 0x28) | (iyl & 0x08); cycles = 15; break; }
        case 0x21: iyl = mem_rb(pc++); iyh = mem_rb(pc++); cycles = 14; break;
        case 0x22: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; mem_ww(addr, iy()); cycles = 20; break;
        case 0x23: set_iy(iy() + 1); cycles = 10; break;
        case 0x24: iyh++; f = flags_szp(iyh); cycles = 8; break;
        case 0x25: iyh--; f = flags_szp(iyh); f |= 2; cycles = 8; break;
        case 0x26: iyh = mem_rb(pc++); cycles = 11; break;
        case 0x29: { uint32_t sum = iy() + iy(); f = (f & 0xC4) | ((sum >> 16) ? 1 : 0); if ((iy() & 0x0FFF) + (iy() & 0x0FFF) > 0x0FFF) f |= 0x10; set_iy(sum & 0xFFFF); f |= (iyh & 0x28) | (iyl & 0x08); cycles = 15; break; }
        case 0x2A: addr = mem_rb(pc) | (mem_rb(pc + 1) << 8); pc += 2; set_iy(mem_rw(addr)); cycles = 20; break;
        case 0x2B: set_iy(iy() - 1); cycles = 10; break;
        case 0x2C: iyl++; f = flags_szp(iyl); cycles = 8; break;
        case 0x2D: iyl--; f = flags_szp(iyl); f |= 2; cycles = 8; break;
        case 0x2E: iyl = mem_rb(pc++); cycles = 11; break;
        case 0x34: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(iy() + d) + 1; mem_wb(iy() + d, val); f = flags_szp(val); cycles = 23; break; }
        case 0x35: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(iy() + d) - 1; mem_wb(iy() + d, val); f = flags_szp(val); f |= 2; cycles = 23; break; }
        case 0x36: { int8_t d = (int8_t)mem_rb(pc++); mem_wb(iy() + d, mem_rb(pc++)); cycles = 19; break; }
        case 0x39: { uint32_t sum = iy() + sp; f = (f & 0xC4) | ((sum >> 16) ? 1 : 0); if ((iy() & 0x0FFF) + (sp & 0x0FFF) > 0x0FFF) f |= 0x10; set_iy(sum & 0xFFFF); f |= (iyh & 0x28) | (iyl & 0x08); cycles = 15; break; }
        case 0x44: b = iyh; break; case 0x45: b = iyl; break;
        case 0x46: { int8_t d = (int8_t)mem_rb(pc++); b = mem_rb(iy() + d); cycles = 19; break; }
        case 0x4C: c = iyh; break; case 0x4D: c = iyl; break;
        case 0x4E: { int8_t d = (int8_t)mem_rb(pc++); c = mem_rb(iy() + d); cycles = 19; break; }
        case 0x54: d = iyh; break; case 0x55: d = iyl; break;
        case 0x56: { int8_t d = (int8_t)mem_rb(pc++); d = mem_rb(iy() + d); cycles = 19; break; }
        case 0x5C: e = iyh; break; case 0x5D: e = iyl; break;
        case 0x5E: { int8_t d = (int8_t)mem_rb(pc++); e = mem_rb(iy() + d); cycles = 19; break; }
        case 0x60: iyh = b; break; case 0x61: iyh = c; break; case 0x62: iyh = d; break;
        case 0x63: iyh = e; break; case 0x64: iyh = iyh; break; case 0x65: iyh = iyl; break;
        case 0x66: { int8_t d = (int8_t)mem_rb(pc++); h = mem_rb(iy() + d); cycles = 19; break; }
        case 0x67: iyh = a; break;
        case 0x68: iyl = b; break; case 0x69: iyl = c; break; case 0x6A: iyl = d; break;
        case 0x6B: iyl = e; break; case 0x6C: iyl = iyh; break; case 0x6D: iyl = iyl; break;
        case 0x6E: { int8_t d = (int8_t)mem_rb(pc++); l = mem_rb(iy() + d); cycles = 19; break; }
        case 0x6F: iyl = a; break;
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x77: {
            int8_t disp = (int8_t)mem_rb(pc++);
            static const uint8_t rmap[] = {b,c,d,e,h,l,0,a};
            mem_wb(iy() + disp, rmap[op2 & 7]);
            cycles = 19; break;
        }
        case 0x7E: { int8_t d = (int8_t)mem_rb(pc++); a = mem_rb(iy() + d); cycles = 19; break; }
        case 0x86: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(iy() + d);
            uint16_t r = a + val;
            int hc = (a & 0x0F) + (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0x8E: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(iy() + d);
            uint16_t r = a + val + (f & 1);
            int hc = (a & 0x0F) + (val & 0x0F) + (f & 1);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0);
            if (hc > 0x0F) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0x96: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(iy() + d);
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0x9E: { int8_t d = (int8_t)mem_rb(pc++); val = mem_rb(iy() + d);
            int c = f & 1;
            uint16_t r = a - val - c;
            int hc = (a & 0x0F) - (val & 0x0F) - c;
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            a = r & 0xFF; f |= (a & 0x28);
            cycles = 19; break;
        }
        case 0xA6: { int8_t d = (int8_t)mem_rb(pc++); a &= mem_rb(iy() + d); f = flags_szp(a); f |= 0x10; cycles = 19; break; }
        case 0xAE: { int8_t d = (int8_t)mem_rb(pc++); a ^= mem_rb(iy() + d); f = flags_szp(a); cycles = 19; break; }
        case 0xB6: { int8_t d = (int8_t)mem_rb(pc++); a |= mem_rb(iy() + d); f = flags_szp(a); cycles = 19; break; }
        case 0xBE: { int8_t d = (int8_t)mem_rb(pc++);
            val = mem_rb(iy() + d);
            uint16_t r = a - val;
            int hc = (a & 0x0F) - (val & 0x0F);
            f = (r & 0x80) | (r == 0 ? 0x40 : 0) | 2;
            if (hc < 0) f |= 0x10; if (r > 0xFF) f |= 1;
            if (((a ^ val ^ r) & 0x80) && ((a ^ r) & 0x80)) f |= 4;
            f |= (val & 0x28);
            cycles = 19; break;
        }
        case 0xE1: set_iy(pop()); cycles = 14; break;
        case 0xE3: tmp16 = mem_rw(sp); mem_ww(sp, iy()); set_iy(tmp16); cycles = 23; break;
        case 0xE5: push(iy()); cycles = 15; break;
        case 0xE9: pc = iy(); cycles = 8; break;
        default: break;
    }
    return cycles;
}
