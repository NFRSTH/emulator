#pragma once
#include <cstdint>
#include <cstring>
#include "cartridge.hpp"

struct PPU; struct CPU;

struct MMU {
    Cartridge* cart;
    PPU* ppu;
    CPU* cpu;
    uint8_t wram[0x8000];       // 32KB WRAM (CGB has 8 banks)
    uint8_t hram[0x80];
    uint8_t io[0x80];
    uint8_t ie;

    // DMA state
    bool dma_active;
    uint16_t dma_src;
    int dma_remaining;

    // CGB WRAM bank
    int wram_bank;

    // Boot ROM disable
    uint8_t boot_rom_disable;

    MMU() {
        memset(wram, 0, sizeof(wram));
        memset(hram, 0, sizeof(hram));
        memset(io, 0, sizeof(io));
        ie = 0;
        ppu = nullptr;
        cpu = nullptr;
        dma_active = false;
        dma_src = 0;
        dma_remaining = 0;
        wram_bank = 1;
        boot_rom_disable = 0;
    }

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t val);
    void do_dma(uint8_t start);
    void dma_step(int cycles);

    uint16_t read_word(uint16_t addr) {
        return read(addr) | (read(addr + 1) << 8);
    }

    void write_word(uint16_t addr, uint16_t val) {
        write(addr, val & 0xFF);
        write(addr + 1, val >> 8);
    }

    uint8_t read_unmapped(uint16_t addr) { return 0xFF; }

    void request_interrupt(int bit) {
        io[0x0F] |= (1 << bit);
    }
};
