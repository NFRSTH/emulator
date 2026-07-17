#pragma once
#include <cstdint>
#include <cstring>
#include <cstdlib>

struct MMU;

static const uint32_t DMG_PALETTE[4] = {
    0xFFFFFFFF, 0xC0C0C0FF, 0x606060FF, 0x0F0F0FFF
};

static uint32_t gbc_to_rgba(uint16_t c) {
    uint8_t r = ((c >> 0) & 0x1F) << 3;
    uint8_t g = ((c >> 5) & 0x1F) << 3;
    uint8_t b = ((c >> 10) & 0x1F) << 3;
    return 0xFF000000 | (b << 16) | (g << 8) | r;
}

struct PPU {
    MMU* mmu;
    uint8_t vram[0x4000]; // 2 banks x 0x2000
    uint8_t oam[0xA0];
    uint32_t framebuffer[160 * 144];
    int mode, mode_clock, line;
    bool frame_complete;
    bool cgb_mode;

    // CGB state
    int vram_bank;
    uint16_t bg_palettes[8][4];
    uint16_t obj_palettes[8][4];
    uint8_t bgpi, bgpd_byte;
    uint8_t obpi, obpd_byte;

    PPU() {
        memset(vram, 0, sizeof(vram));
        memset(oam, 0, sizeof(oam));
        memset(framebuffer, 0xFF, sizeof(framebuffer));
        mode = 2;
        mode_clock = 0;
        line = 0;
        frame_complete = false;
        cgb_mode = false;
        vram_bank = 0;
        bgpi = obpi = 0;
        bgpd_byte = obpd_byte = 0;
        for (int p = 0; p < 8; p++)
            for (int c = 0; c < 4; c++)
                bg_palettes[p][c] = obj_palettes[p][c] = c;
    }

    uint8_t read_vram(uint16_t addr) {
        if (mode < 3) {
            if (cgb_mode && addr < 0x2000 && vram_bank)
                return vram[0x2000 + addr];
            return vram[addr];
        }
        return 0xFF;
    }

    void write_vram(uint16_t addr, uint8_t val) {
        if (mode < 3) {
            if (cgb_mode && addr < 0x2000 && vram_bank)
                vram[0x2000 + addr] = val;
            else
                vram[addr] = val;
        }
    }

    void write_bgpi(uint8_t v) {
        bgpi = v;
        bgpd_byte = 0;
    }

    void write_bgpd(uint8_t v) {
        int index = bgpi & 0x3F;
        int pal = index / 4;
        int col = index % 4;
        if (bgpd_byte == 0) {
            bg_palettes[pal][col] = (bg_palettes[pal][col] & 0xFF00) | v;
        } else {
            bg_palettes[pal][col] = (bg_palettes[pal][col] & 0x00FF) | (v << 8);
        }
        bgpd_byte ^= 1;
        if (bgpi & 0x80) bgpi = (bgpi & 0xC0) | ((index + 1) & 0x3F);
    }

    void write_obpi(uint8_t v) {
        obpi = v;
        obpd_byte = 0;
    }

    void write_obpd(uint8_t v) {
        int index = obpi & 0x3F;
        int pal = index / 4;
        int col = index % 4;
        if (obpd_byte == 0) {
            obj_palettes[pal][col] = (obj_palettes[pal][col] & 0xFF00) | v;
        } else {
            obj_palettes[pal][col] = (obj_palettes[pal][col] & 0x00FF) | (v << 8);
        }
        obpd_byte ^= 1;
        if (obpi & 0x80) obpi = (obpi & 0xC0) | ((index + 1) & 0x3F);
    }

    uint8_t read_oam(uint16_t addr) {
        if (mode < 3 && mode > 0) return oam[addr];
        return 0xFF;
    }

    void write_oam(uint16_t addr, uint8_t val) {
        if (mode < 3) oam[addr] = val;
    }

    void step(int cycles) {
        set_stat();
        mode_clock += cycles;
        switch (mode) {
            case 2:
                if (mode_clock >= 80) {
                    mode_clock -= 80;
                    mode = 3;
                }
                break;
            case 3:
                if (mode_clock >= 172) {
                    mode_clock -= 172;
                    mode = 0;
                    render_scanline();
                }
                break;
            case 0:
                if (mode_clock >= 204) {
                    mode_clock -= 204;
                    line++;
                    if (line == 144) {
                        mode = 1;
                        mmu->request_interrupt(0);
                        frame_complete = true;
                    } else {
                        mode = 2;
                    }
                }
                break;
            case 1:
                if (mode_clock >= 456) {
                    mode_clock -= 456;
                    line++;
                    if (line > 153) {
                        line = 0;
                        mode = 2;
                        frame_complete = true;
                    }
                }
                break;
        }
        uint8_t ly = (uint8_t)line;
        mmu->write(0xFF44, ly);
        if (ly == mmu->read(0xFF45)) {
            mmu->io[0x41] |= 0x04;
            if (mmu->io[0x41] & 0x40) mmu->request_interrupt(1);
        } else {
            mmu->io[0x41] &= ~0x04;
        }
    }

    void set_stat() {
        uint8_t stat = mmu->io[0x41] & 0xFC;
        stat |= (mode == 1) ? 1 : (mode == 2) ? 2 : (mode == 3) ? 3 : 0;
        mmu->io[0x41] = (mmu->io[0x41] & 0xFC) | (stat & 3);
    }

    void render_scanline() {
        uint8_t lcdc = mmu->io[0x40];
        if (!(lcdc & 0x80)) return;

        if (lcdc & 0x01) render_background();
        if (lcdc & 0x20) render_window();
        if (lcdc & 0x02) render_sprites();
    }

    // Return a 2-bit color index for a pixel in a tile
    int get_tile_pixel(int tile_num, int pixel_x, int pixel_y, int* bank_out, int* vflip_out) {
        int bank = 0;
        int vflip = 0;
        if (cgb_mode) {
            bank = *bank_out;
            vflip = *vflip_out;
        }

        int y = vflip ? (7 - pixel_y) : pixel_y;
        int byte_offset = y * 2;
        int bit = 7 - pixel_x;

        uint8_t b1 = vram[bank * 0x2000 + tile_num * 16 + byte_offset];
        uint8_t b2 = vram[bank * 0x2000 + tile_num * 16 + byte_offset + 1];
        return ((b2 >> bit) & 1) << 1 | ((b1 >> bit) & 1);
    }

    uint32_t get_bg_color(int pal, int color_index) {
        if (cgb_mode) return gbc_to_rgba(bg_palettes[pal][color_index]);
        return DMG_PALETTE[color_index];
    }

    uint32_t get_obj_color(int pal, int color_index) {
        if (cgb_mode) return gbc_to_rgba(obj_palettes[pal][color_index]);
        return DMG_PALETTE[color_index];
    }

    void render_background() {
        uint8_t lcdc = mmu->io[0x40];
        bool use_tile_map = lcdc & 0x08;
        bool unsigned_tiles = lcdc & 0x10;
        uint16_t tile_map_base = use_tile_map ? 0x1C00 : 0x1800;
        uint8_t scx = mmu->io[0x43];
        uint8_t scy = mmu->io[0x42];
        uint8_t bgp = mmu->io[0x47];

        int y = scy + line;
        int tile_row = (y / 8) & 31;

        for (int x = 0; x < 160; x++) {
            int x_pos = scx + x;
            int tile_col = (x_pos / 8) & 31;

            uint16_t tile_addr = tile_map_base + tile_row * 32 + tile_col;
            uint8_t tile_num = vram[tile_addr];

            int pixel_x = x_pos & 7;
            int pixel_y = y & 7;

            // CGB attributes from second byte of map entry
            int pal = 0;
            int tile_bank = 0;
            int hflip = 0, vflip = 0;
            if (cgb_mode) {
                uint8_t attr = vram[tile_addr + 1];
                pal = attr & 7;
                tile_bank = (attr >> 3) & 1;
                hflip = (attr >> 4) & 1;
                vflip = (attr >> 5) & 1;
                if (hflip) pixel_x = 7 - pixel_x;
            }

            int real_tile = unsigned_tiles ? tile_num : (0x1000 / 16) + (int8_t)tile_num;

            int ye = vflip ? (7 - pixel_y) : pixel_y;
            int byte_offset = ye * 2;
            int bit = 7 - pixel_x;

            uint8_t b1 = vram[tile_bank * 0x2000 + real_tile * 16 + byte_offset];
            uint8_t b2 = vram[tile_bank * 0x2000 + real_tile * 16 + byte_offset + 1];
            int color = ((b2 >> bit) & 1) << 1 | ((b1 >> bit) & 1);

            int shade;
            if (cgb_mode) {
                framebuffer[line * 160 + x] = gbc_to_rgba(bg_palettes[pal][color]);
            } else {
                shade = (bgp >> (color * 2)) & 3;
                framebuffer[line * 160 + x] = DMG_PALETTE[shade];
            }
        }
    }

    void render_window() {
        uint8_t lcdc = mmu->io[0x40];
        if (!(lcdc & 0x20)) return;
        bool use_tile_map = lcdc & 0x40;
        bool unsigned_tiles = lcdc & 0x10;
        uint16_t tile_map_base = use_tile_map ? 0x1C00 : 0x1800;
        uint8_t wy = mmu->io[0x4A];
        uint8_t wx = mmu->io[0x4B];
        uint8_t bgp = mmu->io[0x47];

        if (line < wy) return;
        if (wx > 166) return;

        int win_y = line - wy;
        int tile_row = win_y / 8;

        for (int x = 0; x < 160; x++) {
            if (x + 7 < wx) continue;
            int win_x = x + 7 - wx;
            if (win_x < 0) continue;
            int tile_col = win_x / 8;

            uint16_t tile_addr = tile_map_base + tile_row * 32 + tile_col;
            uint8_t tile_num = vram[tile_addr];

            int pixel_x = win_x & 7;
            int pixel_y = win_y & 7;

            int pal = 0;
            int tile_bank = 0;
            int hflip = 0, vflip = 0;
            if (cgb_mode) {
                uint8_t attr = vram[tile_addr + 1];
                pal = attr & 7;
                tile_bank = (attr >> 3) & 1;
                hflip = (attr >> 4) & 1;
                vflip = (attr >> 5) & 1;
                if (hflip) pixel_x = 7 - pixel_x;
            }

            int real_tile = unsigned_tiles ? tile_num : (0x1000 / 16) + (int8_t)tile_num;
            int ye = vflip ? (7 - pixel_y) : pixel_y;
            int byte_offset = ye * 2;
            int bit = 7 - pixel_x;

            uint8_t b1 = vram[tile_bank * 0x2000 + real_tile * 16 + byte_offset];
            uint8_t b2 = vram[tile_bank * 0x2000 + real_tile * 16 + byte_offset + 1];
            int color = ((b2 >> bit) & 1) << 1 | ((b1 >> bit) & 1);

            int shade;
            if (cgb_mode) {
                framebuffer[line * 160 + x] = gbc_to_rgba(bg_palettes[pal][color]);
            } else {
                shade = (bgp >> (color * 2)) & 3;
                framebuffer[line * 160 + x] = DMG_PALETTE[shade];
            }
        }
    }

    void render_sprites() {
        uint8_t lcdc = mmu->io[0x40];
        if (!(lcdc & 0x02)) return;
        bool tall = lcdc & 0x04;
        int sprite_height = tall ? 16 : 8;

        for (int i = 0; i < 40; i++) {
            int sprite_y = oam[i * 4 + 0] - 16;
            int sprite_x = oam[i * 4 + 1] - 8;
            uint8_t tile = oam[i * 4 + 2];
            uint8_t flags = oam[i * 4 + 3];

            if (sprite_y > line || sprite_y + sprite_height <= line) continue;

            if (tall) tile &= 0xFE;

            int sprite_line = line - sprite_y;
            if (flags & 0x40) sprite_line = sprite_height - 1 - sprite_line;

            int tile_bank = 0;
            int pal = 0;
            if (cgb_mode) {
                tile_bank = (flags >> 3) & 1;
                pal = flags & 7;
            }

            uint16_t tile_addr = tile * 16 + sprite_line * 2;
            uint8_t b1 = vram[tile_bank * 0x2000 + tile_addr];
            uint8_t b2 = vram[tile_bank * 0x2000 + tile_addr + 1];

            bool behind_bg = flags & 0x80;
            uint8_t palette_reg = (flags & 0x10) ? mmu->io[0x49] : mmu->io[0x48];

            for (int px = 0; px < 8; px++) {
                int bit = (flags & 0x20) ? px : 7 - px;
                int color = ((b2 >> bit) & 1) << 1 | ((b1 >> bit) & 1);
                if (color == 0) continue;
                int sx = sprite_x + px;
                if (sx < 0 || sx >= 160) continue;

                int idx = line * 160 + sx;

                if (cgb_mode) {
                    // Priority bit in CGB: bit 7 of flags = BG priority (if set, sprite behind BG color 1-3)
                    if (behind_bg) {
                        int existing_color = framebuffer[idx];
                        if (existing_color != DMG_PALETTE[0] && existing_color != 0xFFFFFFFF) continue;
                        // In CGB, priority = sprite behind BG when BG color index != 0
                        // We approximate by checking if it's not the white/transparent-ish color
                    }
                    framebuffer[idx] = gbc_to_rgba(obj_palettes[pal][color]);
                } else {
                    int shade = (palette_reg >> (color * 2)) & 3;
                    if (behind_bg) {
                        uint32_t existing = framebuffer[idx];
                        if (existing != DMG_PALETTE[0]) continue;
                    }
                    framebuffer[idx] = DMG_PALETTE[shade];
                }
            }
        }
    }
};
