#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

struct Chip8 {
    uint8_t memory[4096];
    uint8_t V[16];
    uint16_t I, pc;
    uint16_t stack[16];
    uint8_t sp;
    uint8_t delay_timer, sound_timer;
    uint32_t framebuffer[64 * 32];
    bool keys[16];
    bool draw_flag;

    void init() {
        static const uint8_t font[80] = {
            0xF0,0x90,0x90,0x90,0xF0, 0x20,0x60,0x20,0x20,0x70,
            0xF0,0x10,0xF0,0x80,0xF0, 0xF0,0x10,0xF0,0x10,0xF0,
            0x90,0x90,0xF0,0x10,0x10, 0xF0,0x80,0xF0,0x10,0xF0,
            0xF0,0x80,0xF0,0x90,0xF0, 0xF0,0x10,0x20,0x40,0x40,
            0xF0,0x90,0xF0,0x90,0xF0, 0xF0,0x90,0xF0,0x10,0xF0,
            0xF0,0x90,0xF0,0x90,0x90, 0xE0,0x90,0xE0,0x90,0xE0,
            0xF0,0x80,0x80,0x80,0xF0, 0xE0,0x90,0x90,0x90,0xE0,
            0xF0,0x80,0xF0,0x80,0xF0
        };
        memset(memory, 0, sizeof(memory));
        memcpy(memory, font, 80);
        memset(V, 0, sizeof(V));
        I = 0; pc = 0x200;
        memset(stack, 0, sizeof(stack));
        sp = 0;
        delay_timer = sound_timer = 0;
        memset(framebuffer, 0, sizeof(framebuffer));
        memset(keys, 0, sizeof(keys));
        draw_flag = false;
    }

    bool load(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (size > 4096 - 0x200) size = 4096 - 0x200;
        fread(memory + 0x200, 1, size, f);
        fclose(f);
        return true;
    }

    int step() {
        draw_flag = false;
        uint16_t op = (memory[pc] << 8) | memory[pc + 1];
        pc += 2;

        uint8_t x = (op >> 8) & 0x0F;
        uint8_t y = (op >> 4) & 0x0F;
        uint8_t n = op & 0x0F;
        uint8_t nn = op & 0xFF;
        uint16_t nnn = op & 0x0FFF;

        switch (op & 0xF000) {
            case 0x0000:
                if (op == 0x00E0) {
                    memset(framebuffer, 0, sizeof(framebuffer));
                    draw_flag = true;
                } else if (op == 0x00EE) {
                    if (sp > 0) { sp--; pc = stack[sp]; }
                }
                break;
            case 0x1000: pc = nnn; break;
            case 0x2000:
                if (sp < 16) { stack[sp] = pc; sp++; }
                pc = nnn;
                break;
            case 0x3000: if (V[x] == nn) pc += 2; break;
            case 0x4000: if (V[x] != nn) pc += 2; break;
            case 0x5000: if (V[x] == V[y]) pc += 2; break;
            case 0x6000: V[x] = nn; break;
            case 0x7000: V[x] += nn; break;
            case 0x8000:
                switch (n) {
                    case 0x00: V[x] = V[y]; break;
                    case 0x01: V[x] |= V[y]; break;
                    case 0x02: V[x] &= V[y]; break;
                    case 0x03: V[x] ^= V[y]; break;
                    case 0x04: {
                        uint16_t r = V[x] + V[y];
                        V[0xF] = r > 0xFF ? 1 : 0;
                        V[x] = r & 0xFF;
                        break;
                    }
                    case 0x05: V[0xF] = V[x] >= V[y] ? 1 : 0; V[x] -= V[y]; break;
                    case 0x06: V[0xF] = V[x] & 1; V[x] >>= 1; break;
                    case 0x07: V[0xF] = V[y] >= V[x] ? 1 : 0; V[x] = V[y] - V[x]; break;
                    case 0x0E: V[0xF] = V[x] >> 7; V[x] <<= 1; break;
                }
                break;
            case 0x9000: if (V[x] != V[y]) pc += 2; break;
            case 0xA000: I = nnn; break;
            case 0xB000: pc = nnn + V[0]; break;
            case 0xC000: V[x] = (rand() & 0xFF) & nn; break;
            case 0xD000: {
                uint8_t xp = V[x] & 63, yp = V[y] & 31;
                V[0xF] = 0;
                for (int row = 0; row < n && yp + row < 32; row++) {
                    uint8_t line = memory[I + row];
                    for (int col = 0; col < 8 && xp + col < 64; col++) {
                        if (line & (0x80 >> col)) {
                            int idx = (yp + row) * 64 + (xp + col);
                            if (framebuffer[idx]) V[0xF] = 1;
                            framebuffer[idx] ^= 0xFFFFFFFF;
                        }
                    }
                }
                draw_flag = true;
                break;
            }
            case 0xE000:
                if (nn == 0x9E && keys[V[x]]) pc += 2;
                if (nn == 0xA1 && !keys[V[x]]) pc += 2;
                break;
            case 0xF000:
                switch (nn) {
                    case 0x07: V[x] = delay_timer; break;
                    case 0x0A: {
                        bool pressed = false;
                        for (int i = 0; i < 16; i++) {
                            if (keys[i]) { V[x] = i; pressed = true; break; }
                        }
                        if (!pressed) pc -= 2;
                        break;
                    }
                    case 0x15: delay_timer = V[x]; break;
                    case 0x18: sound_timer = V[x]; break;
                    case 0x1E: I += V[x]; break;
                    case 0x29: I = V[x] * 5; break;
                    case 0x33:
                        memory[I] = V[x] / 100;
                        memory[I + 1] = (V[x] / 10) % 10;
                        memory[I + 2] = V[x] % 10;
                        break;
                    case 0x55: for (int i = 0; i <= x; i++) memory[I + i] = V[i]; break;
                    case 0x65: for (int i = 0; i <= x; i++) V[i] = memory[I + i]; break;
                }
                break;
        }

        if (delay_timer > 0) delay_timer--;
        if (sound_timer > 0) sound_timer--;
        return 2;
    }
};
