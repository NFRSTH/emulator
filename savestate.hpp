#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "cartridge.hpp"
#include "cpu.hpp"
#include "ppu.hpp"
#include "chip8.hpp"
#include "invaders.hpp"
#include "atari2600.hpp"

struct SaveState {
    FILE* f;
    bool writing;

    bool open_read(const char* path) {
        f = fopen(path, "rb");
        writing = false;
        return f != nullptr;
    }

    bool open_write(const char* path) {
        f = fopen(path, "wb");
        writing = true;
        return f != nullptr;
    }

    void close() {
        if (f) fclose(f);
    }

    void rw(void* data, int size) {
        if (f) {
            if (writing)
                fwrite(data, 1, size, f);
            else
                fread(data, 1, size, f);
        }
    }

    void wr(const void* data, int size) { if (f) fwrite(data, 1, size, f); }
    void rd(void* data, int size) { if (f) fread(data, 1, size, f); }
};

#define SS_BLOCK(m) rw(&m, sizeof(m))

static const uint32_t GB_SAVE_MAGIC = 0x47425356; // "GBSV"
static const uint32_t SI_SAVE_MAGIC = 0x53495356; // "SISV"
static const uint32_t A26_SAVE_MAGIC = 0x41323653; // "A26S"
static const uint32_t C8_SAVE_MAGIC = 0x43385356; // "C8SV"

static bool save_gb_state(const char* path, CPU* cpu, MMU* mmu, PPU* ppu, Cartridge* cart) {
    SaveState ss;
    if (!ss.open_write(path)) return false;
    uint32_t magic = GB_SAVE_MAGIC;
    ss.SS_BLOCK(magic);
    ss.SS_BLOCK(cpu->PC);
    ss.SS_BLOCK(cpu->SP);
    ss.SS_BLOCK(cpu->AF);
    ss.SS_BLOCK(cpu->BC);
    ss.SS_BLOCK(cpu->DE);
    ss.SS_BLOCK(cpu->HL);
    ss.SS_BLOCK(cpu->halted);
    ss.SS_BLOCK(cpu->ime);
    ss.SS_BLOCK(cpu->ime_scheduled);
    ss.SS_BLOCK(mmu->wram);
    ss.SS_BLOCK(mmu->hram);
    ss.SS_BLOCK(mmu->io);
    ss.SS_BLOCK(mmu->ie);
    ss.SS_BLOCK(ppu->vram);
    ss.SS_BLOCK(ppu->oam);
    ss.SS_BLOCK(ppu->mode);
    ss.SS_BLOCK(ppu->mode_clock);
    ss.SS_BLOCK(ppu->line);
    ss.SS_BLOCK(cart->current_rom_bank);
    ss.SS_BLOCK(cart->current_ram_bank);
    ss.SS_BLOCK(cart->ram_enabled);
    ss.SS_BLOCK(cart->ram);
    // MBC2 state
    ss.SS_BLOCK(cart->mbc2_mode);
    // MBC3 RTC state
    cart->rtc_update();
    ss.SS_BLOCK(cart->rtc_reg);
    ss.SS_BLOCK(cart->rtc_seconds);
    ss.SS_BLOCK(cart->rtc_minutes);
    ss.SS_BLOCK(cart->rtc_hours);
    ss.SS_BLOCK(cart->rtc_day);
    ss.SS_BLOCK(cart->rtc_ctrl);
    // GBC state
    ss.SS_BLOCK(ppu->vram_bank);
    ss.SS_BLOCK(ppu->bg_palettes);
    ss.SS_BLOCK(ppu->obj_palettes);
    ss.SS_BLOCK(ppu->bgpi);
    ss.SS_BLOCK(ppu->obpi);
    ss.SS_BLOCK(ppu->bgpd_byte);
    ss.SS_BLOCK(ppu->obpd_byte);
    ss.close();
    return true;
}

static bool load_gb_state(const char* path, CPU* cpu, MMU* mmu, PPU* ppu, Cartridge* cart) {
    SaveState ss;
    if (!ss.open_read(path)) return false;
    uint32_t magic;
    ss.SS_BLOCK(magic);
    if (magic != GB_SAVE_MAGIC) { ss.close(); return false; }
    ss.SS_BLOCK(cpu->PC);
    ss.SS_BLOCK(cpu->SP);
    ss.SS_BLOCK(cpu->AF);
    ss.SS_BLOCK(cpu->BC);
    ss.SS_BLOCK(cpu->DE);
    ss.SS_BLOCK(cpu->HL);
    ss.SS_BLOCK(cpu->halted);
    ss.SS_BLOCK(cpu->ime);
    ss.SS_BLOCK(cpu->ime_scheduled);
    ss.SS_BLOCK(mmu->wram);
    ss.SS_BLOCK(mmu->hram);
    ss.SS_BLOCK(mmu->io);
    ss.SS_BLOCK(mmu->ie);
    ss.SS_BLOCK(ppu->vram);
    ss.SS_BLOCK(ppu->oam);
    ss.SS_BLOCK(ppu->mode);
    ss.SS_BLOCK(ppu->mode_clock);
    ss.SS_BLOCK(ppu->line);
    ss.SS_BLOCK(cart->current_rom_bank);
    ss.SS_BLOCK(cart->current_ram_bank);
    ss.SS_BLOCK(cart->ram_enabled);
    ss.SS_BLOCK(cart->ram);
    // MBC2 state
    ss.SS_BLOCK(cart->mbc2_mode);
    // MBC3 RTC state
    ss.SS_BLOCK(cart->rtc_reg);
    ss.SS_BLOCK(cart->rtc_seconds);
    ss.SS_BLOCK(cart->rtc_minutes);
    ss.SS_BLOCK(cart->rtc_hours);
    ss.SS_BLOCK(cart->rtc_day);
    ss.SS_BLOCK(cart->rtc_ctrl);
    // GBC state
    ss.SS_BLOCK(ppu->vram_bank);
    ss.SS_BLOCK(ppu->bg_palettes);
    ss.SS_BLOCK(ppu->obj_palettes);
    ss.SS_BLOCK(ppu->bgpi);
    ss.SS_BLOCK(ppu->obpi);
    ss.SS_BLOCK(ppu->bgpd_byte);
    ss.SS_BLOCK(ppu->obpd_byte);
    cpu->mmu = mmu;
    ppu->mmu = mmu;
    ppu->cgb_mode = (cart->cgb_flag == 0x80 || cart->cgb_flag == 0xC0);
    cart->rtc_base = time(nullptr);
    ss.close();
    return true;
}

static bool save_si_state(const char* path, Invaders8080* si) {
    SaveState ss;
    if (!ss.open_write(path)) return false;
    uint32_t magic = SI_SAVE_MAGIC;
    ss.SS_BLOCK(magic);
    ss.wr(si, sizeof(Invaders8080));
    ss.close();
    return true;
}

static bool load_si_state(const char* path, Invaders8080* si) {
    SaveState ss;
    if (!ss.open_read(path)) return false;
    uint32_t magic;
    ss.SS_BLOCK(magic);
    if (magic != SI_SAVE_MAGIC) { ss.close(); return false; }
    ss.rd(si, sizeof(Invaders8080));
    ss.close();
    return true;
}

static bool save_a26_state(const char* path, A2600* a26) {
    SaveState ss;
    if (!ss.open_write(path)) return false;
    uint32_t magic = A26_SAVE_MAGIC;
    ss.SS_BLOCK(magic);
    ss.wr(a26, sizeof(A2600));
    ss.close();
    return true;
}

static bool load_a26_state(const char* path, A2600* a26) {
    SaveState ss;
    if (!ss.open_read(path)) return false;
    uint32_t magic;
    ss.SS_BLOCK(magic);
    if (magic != A26_SAVE_MAGIC) { ss.close(); return false; }
    ss.rd(a26, sizeof(A2600));
    ss.close();
    return true;
}

static bool save_c8_state(const char* path, Chip8* c8) {
    SaveState ss;
    if (!ss.open_write(path)) return false;
    uint32_t magic = C8_SAVE_MAGIC;
    ss.SS_BLOCK(magic);
    ss.wr(c8, sizeof(Chip8));
    ss.close();
    return true;
}

static bool load_c8_state(const char* path, Chip8* c8) {
    SaveState ss;
    if (!ss.open_read(path)) return false;
    uint32_t magic;
    ss.SS_BLOCK(magic);
    if (magic != C8_SAVE_MAGIC) { ss.close(); return false; }
    ss.rd(c8, sizeof(Chip8));
    ss.close();
    return true;
}

static const char* state_path(const char* rom_path, int slot) {
    static char buf[1024];
    strcpy(buf, rom_path);
    char* dot = strrchr(buf, '.');
    if (dot) *dot = 0;
    char slot_str[8];
    snprintf(slot_str, sizeof(slot_str), ".st%d", slot);
    strcat(buf, slot_str);
    return buf;
}
