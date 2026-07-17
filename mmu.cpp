#include "mmu.hpp"
#include "ppu.hpp"

uint8_t MMU::read(uint16_t addr) {
    if (addr < 0x8000) {
        uint8_t val = cart->read_rom(addr);
        int cheat = cart->apply_cheats(addr);
        return cheat >= 0 ? (uint8_t)cheat : val;
    }
    if (addr < 0xA000) return ppu ? ppu->read_vram(addr & 0x1FFF) : 0xFF;
    if (addr < 0xC000) {
        uint8_t val = cart->read_ram(addr);
        int cheat = cart->apply_cheats(addr);
        return cheat >= 0 ? (uint8_t)cheat : val;
    }
    if (addr < 0xE000) {
        if (addr < 0xD000) return wram[addr & 0x0FFF];
        return wram[wram_bank * 0x1000 + (addr & 0x0FFF)];
    }
    if (addr < 0xFE00) {
        uint16_t mir = 0xC000 + (addr & 0x1FFF);
        if (mir < 0xD000) return wram[mir & 0x0FFF];
        return wram[wram_bank * 0x1000 + (mir & 0x0FFF)];
    }
    if (addr < 0xFEA0) {
        if (dma_active) return 0xFF;
        return ppu ? ppu->read_oam(addr & 0xFF) : 0xFF;
    }
    if (addr < 0xFF00) return 0xFF;
    if (addr == 0xFF00) return io[0] & 0x30;
    if (addr >= 0xFF10 && addr <= 0xFF3F) return io[addr & 0x7F];
    if (addr >= 0xFF4C && addr <= 0xFF7F) return io[addr & 0x7F];
    if (addr == 0xFF4D) return (io[0x4D] & 0x80) | 0x7E;
    if (addr == 0xFF0F) return io[0x0F];
    if (addr == 0xFF40) return io[0x40];
    if (addr == 0xFF41) return (io[0x41] & 0x87) | 0x80;
    if (addr == 0xFF42) return io[0x42];
    if (addr == 0xFF43) return io[0x43];
    if (addr == 0xFF44) return io[0x44];
    if (addr == 0xFF45) return io[0x45];
    if (addr == 0xFF47) return io[0x47];
    if (addr == 0xFF48) return io[0x48];
    if (addr == 0xFF49) return io[0x49];
    if (addr == 0xFF4A) return io[0x4A];
    if (addr == 0xFF4B) return io[0x4B];
    if (addr == 0xFF04) return io[0x04];
    if (addr == 0xFF05) return io[0x05];
    if (addr == 0xFF06) return io[0x06];
    if (addr == 0xFF07) return io[0x07];
    if (addr == 0xFF50) return boot_rom_disable ? 0xFF : 0x00;
    if (addr == 0xFF70) return 0xF8 | (io[0x70] & 7);
    if (addr >= 0xFF80 && addr < 0xFFFF) return hram[addr & 0x7F];
    if (addr == 0xFFFF) return ie;
    return 0xFF;
}

void MMU::write(uint16_t addr, uint8_t val) {
    if (addr < 0x8000) { cart->write_mbc(addr, val); return; }
    if (addr < 0xA000) { if (ppu) ppu->write_vram(addr & 0x1FFF, val); return; }
    if (addr < 0xC000) { cart->write_ram(addr, val); return; }
    if (addr < 0xE000) {
        if (addr < 0xD000) { wram[addr & 0x0FFF] = val; return; }
        wram[wram_bank * 0x1000 + (addr & 0x0FFF)] = val; return;
    }
    if (addr < 0xFE00) {
        uint16_t mir = 0xC000 + (addr & 0x1FFF);
        if (mir < 0xD000) { wram[mir & 0x0FFF] = val; return; }
        wram[wram_bank * 0x1000 + (mir & 0x0FFF)] = val; return;
    }
    if (addr < 0xFEA0) { if (dma_active) return; if (ppu) ppu->write_oam(addr & 0xFF, val); return; }
    if (addr < 0xFF00) return;
    if (addr == 0xFF00) { io[0] = (io[0] & 0xCF) | (val & 0x30); return; }
    if (addr >= 0xFF10 && addr <= 0xFF3F) { io[addr & 0x7F] = val; return; }
    if (addr >= 0xFF4C && addr <= 0xFF7F) { io[addr & 0x7F] = val; return; }
    if (addr == 0xFF0F) { io[0x0F] = val; return; }
    if (addr == 0xFF40) { io[0x40] = val; return; }
    if (addr == 0xFF41) { io[0x41] = (io[0x41] & 0x78) | (val & 0x87); return; }
    if (addr == 0xFF42) { io[0x42] = val; return; }
    if (addr == 0xFF43) { io[0x43] = val; return; }
    if (addr == 0xFF44) { io[0x44] = 0; return; }
    if (addr == 0xFF45) { io[0x45] = val; return; }
    if (addr == 0xFF46) { do_dma(val); return; }
    if (addr == 0xFF47) { io[0x47] = val; return; }
    if (addr == 0xFF48) { io[0x48] = val; return; }
    if (addr == 0xFF49) { io[0x49] = val; return; }
    if (addr == 0xFF4A) { io[0x4A] = val; return; }
    if (addr == 0xFF4B) { io[0x4B] = val; return; }
    if (addr == 0xFF4D) { io[0x4D] = (io[0x4D] & 0x80) | (val & 1); return; }
    if (addr == 0xFF4F) { if (ppu && ppu->cgb_mode) { ppu->vram_bank = val & 1; io[0x4F] = val & 1; } return; }
    if (addr == 0xFF50) { boot_rom_disable = 0xFF; io[0x50] = val; return; }
    if (addr == 0xFF51) { io[0x51] = val; return; }
    if (addr == 0xFF52) { io[0x52] = val; return; }
    if (addr == 0xFF53) { io[0x53] = val; return; }
    if (addr == 0xFF54) { io[0x54] = val; return; }
    if (addr == 0xFF55) { io[0x55] = val; return; }
    if (addr == 0xFF68) { if (ppu) ppu->write_bgpi(val); io[0x68] = val; return; }
    if (addr == 0xFF69) { if (ppu) ppu->write_bgpd(val); io[0x69] = val; return; }
    if (addr == 0xFF6A) { if (ppu) ppu->write_obpi(val); io[0x6A] = val; return; }
    if (addr == 0xFF6B) { if (ppu) ppu->write_obpd(val); io[0x6B] = val; return; }
    if (addr == 0xFF70) {
        int bank = val & 7;
        if (bank == 0) bank = 1;
        wram_bank = bank;
        io[0x70] = bank;
        return;
    }
    if (addr == 0xFF04) { io[0x04] = 0; return; }
    if (addr == 0xFF05) { io[0x05] = val; return; }
    if (addr == 0xFF06) { io[0x06] = val; return; }
    if (addr == 0xFF07) { io[0x07] = val & 0x07; return; }
    if (addr >= 0xFF80 && addr < 0xFFFF) { hram[addr & 0x7F] = val; return; }
    if (addr == 0xFFFF) { ie = val; return; }
}

void MMU::do_dma(uint8_t start) {
    dma_src = start << 8;
    dma_remaining = 0xA0;
    dma_active = true;
}

void MMU::dma_step(int cycles) {
    if (!dma_active || !ppu) return;
    int bytes = cycles / 4;
    while (dma_remaining > 0 && bytes > 0) {
        ppu->oam[0xA0 - dma_remaining] = read(dma_src);
        dma_src = (dma_src + 1) & 0xFFFF;
        dma_remaining--;
        bytes--;
    }
    if (dma_remaining == 0) dma_active = false;
}
