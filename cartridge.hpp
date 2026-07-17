#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <ctime>
#include <string>
#include <vector>

static const char* save_ext(const char* path) {
    static char buf[1024];
    strcpy(buf, path);
    char* dot = strrchr(buf, '.');
    if (dot) strcpy(dot, ".sav");
    else strcat(buf, ".sav");
    return buf;
}

struct Cartridge {
    std::vector<uint8_t> rom;
    std::vector<uint8_t> ram;
    std::string title;
    uint8_t cgb_flag;
    uint8_t mbc_type;
    int rom_banks;
    int ram_banks;
    int current_rom_bank;
    int current_ram_bank;
    bool ram_enabled;
    bool battery;

    // MBC2 state
    bool mbc2_mode;

    // MBC3 RTC state
    int rtc_reg;
    int rtc_latch;
    uint8_t rtc_seconds, rtc_minutes, rtc_hours;
    uint16_t rtc_day;
    uint8_t rtc_ctrl; // bit 6 = halt, bit 7 = day carry
    uint8_t rtc_latched_seconds, rtc_latched_minutes, rtc_latched_hours;
    uint16_t rtc_latched_day;
    uint8_t rtc_latched_ctrl;
    time_t rtc_base;

    // Cheat support
    std::vector<std::pair<uint16_t, std::pair<uint8_t, uint8_t>>> gs_cheats; // GameShark addr->(value, compare) compare=0 means no compare
    std::vector<std::pair<uint16_t, std::pair<uint8_t, uint8_t>>> gg_cheats; // Game Genie decoded addr->(value, compare)

    // Game Genie alphabet: A=0, B=1, C=2, D=3, E=4, F=5, G=6, H=7, I=8, J=9, K=10, L=11, M=12, N=13, O=14, P=15
    static int gg_index(char c) {
        const char* alpha = "ABCDEFGHIJKLMNOP";
        const char* p = strchr(alpha, toupper(c));
        return p ? (int)(p - alpha) : -1;
    }

    // Decode Game Genie code (6 or 9 letter code)
    static bool decode_gg(const char* code, uint16_t& addr, uint8_t& value, uint8_t& compare, bool& has_compare) {
        char clean[12]; int ci = 0;
        for (int i = 0; code[i] && ci < 11; i++)
            if (code[i] != '-' && code[i] != ' ') clean[ci++] = toupper(code[i]);
        clean[ci] = 0;
        if (ci != 6 && ci != 9) return false;

        int t[12];
        for (int i = 0; i < ci; i++) {
            t[i] = gg_index(clean[i]);
            if (t[i] < 0) return false;
        }

        if (ci == 6) {
            addr = ((t[5] & 1) << 15) | ((t[3] & 7) << 12) |
                   ((t[4] & 7) << 8) | ((t[5] & 0x0E) << 7) |
                   ((t[2] & 7) << 4) | ((t[1] & 7) << 1);
            value = ((t[4] >> 3) << 4) | (t[0] & 0x0F);
            has_compare = false;
            compare = 0;
            return true;
        } else {
            addr = ((t[5] & 1) << 15) | ((t[3] & 7) << 12) |
                   ((t[4] & 7) << 8) | ((t[5] & 0x0E) << 7) |
                   ((t[2] & 7) << 4) | ((t[1] & 7) << 1);
            value = ((t[4] >> 3) << 4) | (t[0] & 0x0F);
            compare = ((t[8] & 1) << 7) | ((t[6] & 7) << 4) |
                      ((t[7] & 7) << 1) | ((t[8] >> 1) & 1);
            has_compare = true;
            return true;
        }
    }

    // Decode GameShark code (8 hex digits, format: VALUEADDRHIADDRLO)
    static bool decode_gs(const char* code, uint16_t& addr, uint8_t& value, uint8_t& compare, bool& has_compare) {
        char clean[12]; int ci = 0;
        for (int i = 0; code[i] && ci < 10; i++)
            if (code[i] != ' ' && code[i] != '-') clean[ci++] = toupper(code[i]);
        clean[ci] = 0;
        if (ci != 8) return false;

        unsigned long full = strtoul(clean, nullptr, 16);
        value = (full >> 24) & 0xFF;
        addr = (full >> 8) & 0xFFFF;
        has_compare = false;
        compare = 0;
        return true;
    }

    // Apply cheats: return the value if address is patched, -1 if not
    int apply_cheats(uint16_t addr) {
        // GameShark has priority, then Game Genie
        for (auto& c : gs_cheats)
            if (c.first == addr) return c.second.first;
        for (auto& c : gg_cheats)
            if (c.first == addr) return c.second.first;
        return -1;
    }

    // Add a Game Genie code
    bool add_gg_cheat(const char* code) {
        uint16_t addr; uint8_t value, compare; bool has_compare;
        if (!decode_gg(code, addr, value, compare, has_compare)) return false;
        gg_cheats.push_back({addr, {value, compare}});
        return true;
    }

    // Add a GameShark code
    bool add_gs_cheat(const char* code) {
        uint16_t addr; uint8_t value, compare; bool has_compare;
        if (!decode_gs(code, addr, value, compare, has_compare)) return false;
        gs_cheats.push_back({addr, {value, compare}});
        return true;
    }

    void clear_cheats() {
        gs_cheats.clear();
        gg_cheats.clear();
    }

    Cartridge() : cgb_flag(0), mbc_type(0), rom_banks(1), ram_banks(0),
                  current_rom_bank(1), current_ram_bank(0),
                  ram_enabled(false), battery(false),
                  mbc2_mode(false),
                  rtc_reg(0), rtc_latch(0),
                  rtc_seconds(0), rtc_minutes(0), rtc_hours(0),
                  rtc_day(0), rtc_ctrl(0),
                  rtc_latched_seconds(0), rtc_latched_minutes(0), rtc_latched_hours(0),
                  rtc_latched_day(0), rtc_latched_ctrl(0),
                  rtc_base(0) {}

    void rtc_update() {
        if (rtc_ctrl & 0x40) return;
        time_t now = time(nullptr);
        int elapsed = (int)(now - rtc_base);
        if (elapsed < 0) elapsed = 0;
        int total_sec = rtc_seconds + rtc_minutes * 60 + rtc_hours * 3600 + rtc_day * 86400 + elapsed;
        rtc_seconds = total_sec % 60;
        total_sec /= 60;
        rtc_minutes = total_sec % 60;
        total_sec /= 60;
        rtc_hours = total_sec % 24;
        total_sec /= 24;
        if (total_sec > 0x1FF) {
            rtc_ctrl |= 0x80;
            total_sec = 0x1FF;
        }
        rtc_day = total_sec;
        rtc_base = now;
    }

    void rtc_latch_time() {
        rtc_update();
        rtc_latched_seconds = rtc_seconds;
        rtc_latched_minutes = rtc_minutes;
        rtc_latched_hours = rtc_hours;
        rtc_latched_day = rtc_day;
        rtc_latched_ctrl = rtc_ctrl;
    }

    uint8_t read_rtc() {
        rtc_update();
        switch (rtc_reg) {
            case 0x08: return rtc_latched_seconds;
            case 0x09: return rtc_latched_minutes;
            case 0x0A: return rtc_latched_hours;
            case 0x0B: return rtc_latched_day & 0xFF;
            case 0x0C:
                return (rtc_latched_ctrl & 0xC0) | (rtc_latched_day >> 8);
            default: return 0xFF;
        }
    }

    void write_rtc(uint8_t val) {
        rtc_update();
        switch (rtc_reg) {
            case 0x08: rtc_seconds = val & 0x3F; break;
            case 0x09: rtc_minutes = val & 0x3F; break;
            case 0x0A: rtc_hours = val & 0x1F; break;
            case 0x0B: rtc_day = (rtc_day & 0x100) | val; break;
            case 0x0C:
                rtc_day = (rtc_day & 0xFF) | ((val & 1) << 8);
                rtc_ctrl = (rtc_ctrl & 0x3F) | (val & 0xC0);
                if (!(rtc_ctrl & 0x40)) rtc_base = time(nullptr);
                break;
        }
    }

    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        rom.resize(size);
        fread(rom.data(), 1, size, f);
        fclose(f);

        title.clear();
        for (int i = 0x134; i <= 0x143 && rom[i] != 0; i++)
            title += (char)rom[i];

        cgb_flag = rom[0x143];
        mbc_type = rom[0x147];
        mbc2_mode = (mbc_type == 0x05 || mbc_type == 0x06);

        switch (mbc_type) {
            case 0: case 0x08: case 0x09: rom_banks = 2; break;
            case 1: case 2: case 3: rom_banks = 64; break;
            case 5: case 6: rom_banks = 16; break;
            case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13: rom_banks = 128; break;
            case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: rom_banks = 512; break;
            default: rom_banks = 2; break;
        }

        int ram_size = rom[0x149];
        switch (ram_size) {
            case 0: ram_banks = (mbc_type == 0x09 || mbc_type == 0x13 || mbc_type == 0x1B || mbc_type == 0x1E || mbc_type == 0x1D || mbc_type == 0x06 || mbc_type == 0x10) ? 1 : 0; break;
            case 1: ram_banks = 1; break;
            case 2: ram_banks = 1; break;
            case 3: ram_banks = 4; break;
            case 4: ram_banks = 16; break;
            default: ram_banks = 0; break;
        }

        // MBC2 has 512 nibbles of internal RAM
        if (mbc2_mode) {
            ram.resize(512, 0);
            ram_banks = 1;
        } else {
            ram.resize(ram_banks > 0 ? ram_banks * 0x2000 : 0, 0);
        }

        battery = (mbc_type == 0x03 || mbc_type == 0x06 || mbc_type == 0x09 || mbc_type == 0x0F || mbc_type == 0x10 || mbc_type == 0x13 || mbc_type == 0x1B || mbc_type == 0x1E);

        if (battery && !ram.empty())
            load_battery(path);

        rtc_base = time(nullptr);
        return true;
    }

    void load_battery(const char* path) {
        const char* sp = save_ext(path);
        FILE* f = fopen(sp, "rb");
        if (!f) return;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && !ram.empty()) {
            if ((size_t)sz > ram.size()) sz = ram.size();
            fread(ram.data(), 1, sz, f);
        }
        fclose(f);
    }

    void save_battery(const char* path) {
        if (!battery || ram.empty()) return;
        const char* sp = save_ext(path);
        FILE* f = fopen(sp, "wb");
        if (!f) return;
        fwrite(ram.data(), 1, ram.size(), f);
        fclose(f);
    }

    uint8_t read_rom(uint16_t addr) {
        if (addr < 0x4000) {
            // MBC2 bank 0 at 0x0000-0x3FFF
            return rom[addr & 0x3FFF];
        }
        int bank = current_rom_bank % rom_banks;
        if (bank == 0) bank = 1;
        if (mbc_type >= 0x19 && mbc_type <= 0x1E) bank = current_rom_bank % rom_banks;
        if (mbc2_mode && bank == 0) bank = 1;
        return rom[(bank * 0x4000) + (addr & 0x3FFF)];
    }

    void write_mbc(uint16_t addr, uint8_t val) {
        switch (mbc_type) {
            case 0: break;
            case 1: case 2: case 3:
                if (addr < 0x2000) ram_enabled = ((val & 0x0F) == 0x0A);
                else if (addr < 0x4000) {
                    int bank = val & 0x1F;
                    if (bank == 0) bank = 1;
                    current_rom_bank = (current_rom_bank & 0x60) | bank;
                }
                else if (addr < 0x6000) {
                    if (rom_banks <= 32) {
                        current_ram_bank = val & 3;
                    } else {
                        current_rom_bank = ((val & 3) << 5) | (current_rom_bank & 0x1F);
                        if (current_rom_bank == 0) current_rom_bank = 1;
                    }
                }
                break;
            case 5: case 6:
                if (addr < 0x2000) {
                    ram_enabled = ((val & 0x0F) == 0x0A);
                } else if (addr < 0x4000) {
                    int bank = val & 0x0F;
                    if (bank == 0) bank = 1;
                    current_rom_bank = bank;
                }
                break;
            case 0x0F: case 0x10:
                if (addr < 0x2000) ram_enabled = ((val & 0x0F) == 0x0A);
                else if (addr < 0x4000) {
                    int bank = val & 0x7F;
                    if (bank == 0) bank = 1;
                    current_rom_bank = bank;
                }
                else if (addr < 0x6000) {
                    if (val >= 0x08 && val <= 0x0C) {
                        rtc_reg = val;
                        current_ram_bank = 0;
                    } else {
                        current_ram_bank = val & 3;
                        rtc_reg = 0;
                    }
                }
                else if (addr < 0x8000) {
                    if (val == 0x01) rtc_latch = 1;
                    else if (val == 0x00 && rtc_latch == 1) {
                        rtc_latch_time();
                        rtc_latch = 0;
                    } else {
                        rtc_latch = 0;
                    }
                }
                break;
            case 0x11: case 0x12: case 0x13:
                if (addr < 0x2000) ram_enabled = ((val & 0x0F) == 0x0A);
                else if (addr < 0x4000) {
                    int bank = val & 0x7F;
                    if (bank == 0) bank = 1;
                    current_rom_bank = bank;
                }
                else if (addr < 0x6000) {
                    if ((mbc_type == 0x13 || mbc_type == 0x10 || mbc_type == 0x0F) && val >= 0x08 && val <= 0x0C) {
                        rtc_reg = val;
                        current_ram_bank = 0;
                    } else {
                        current_ram_bank = val & 3;
                        rtc_reg = 0;
                    }
                }
                else if (addr < 0x8000) {
                    if ((mbc_type == 0x0F || mbc_type == 0x10 || mbc_type == 0x13)) {
                        if (val == 0x01) rtc_latch = 1;
                        else if (val == 0x00 && rtc_latch == 1) {
                            rtc_latch_time();
                            rtc_latch = 0;
                        } else {
                            rtc_latch = 0;
                        }
                    }
                }
                break;
            case 0x19: case 0x1A: case 0x1B:
            case 0x1C: case 0x1D: case 0x1E:
                if (addr < 0x2000) ram_enabled = ((val & 0x0F) == 0x0A);
                else if (addr < 0x3000) {
                    current_rom_bank = (current_rom_bank & 0x100) | val;
                }
                else if (addr < 0x4000) {
                    current_rom_bank = (current_rom_bank & 0xFF) | ((val & 1) << 8);
                }
                else if (addr < 0x6000) {
                    current_ram_bank = val & 0x0F;
                }
                break;
            default: break;
        }
    }

    uint8_t read_ram(uint16_t addr) {
        if (!ram_enabled || ram.empty()) return 0xFF;
        if (mbc2_mode) {
            return ram[addr & 0x1FF] | 0xF0;
        }
        // MBC3 RTC read
        if ((mbc_type == 0x0F || mbc_type == 0x10) && rtc_reg >= 0x08 && rtc_reg <= 0x0C) {
            return read_rtc();
        }
        int bank = 0;
        if (mbc_type == 0x09 || mbc_type == 0) bank = 0;
        else if (mbc_type >= 0x19 && mbc_type <= 0x1E) bank = current_ram_bank % ram_banks;
        else bank = current_ram_bank % ram_banks;
        return ram[(bank * 0x2000) + (addr & 0x1FFF)];
    }

    void write_ram(uint16_t addr, uint8_t val) {
        if (!ram_enabled || ram.empty()) return;
        if (mbc2_mode) {
            ram[addr & 0x1FF] = val & 0x0F;
            return;
        }
        // MBC3 RTC write
        if ((mbc_type == 0x0F || mbc_type == 0x10) && rtc_reg >= 0x08 && rtc_reg <= 0x0C) {
            write_rtc(val);
            return;
        }
        int bank = 0;
        if (mbc_type == 0x09 || mbc_type == 0) bank = 0;
        else if (mbc_type >= 0x19 && mbc_type <= 0x1E) bank = current_ram_bank % ram_banks;
        else bank = current_ram_bank % ram_banks;
        ram[(bank * 0x2000) + (addr & 0x1FFF)] = val;
    }
};
