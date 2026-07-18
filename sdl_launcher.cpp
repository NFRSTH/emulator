#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

// Virtual key codes for cross-platform key mapping
enum { VK_UP=0x100, VK_DOWN=0x101, VK_LEFT=0x102, VK_RIGHT=0x103,
       VK_SPACE=0x20, VK_RETURN=0x0D, VK_BACK=0x08,
       VK_F1=0x110, VK_F2=0x111, VK_F5=0x115, VK_F7=0x117 };

#include "cartridge.hpp"
#include "mmu.hpp"
#include "ppu.hpp"
#include "cpu.hpp"
#include "audio.hpp"
#include "savestate.hpp"
#include "invaders.hpp"
#include "atari2600.hpp"
#include "chip8.hpp"

// ========== Audio ==========
static SDL_AudioDeviceID audio_dev = 0;
static SDL_AudioSpec audio_spec;

static void audio_init() {
    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = AUDIO_SR;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = AUDIO_SAMPLES;
    want.callback = nullptr;
    audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &audio_spec, 0);
    if (audio_dev) SDL_PauseAudioDevice(audio_dev, 0);
}

static void audio_push(int16_t* buf, int samples) {
    if (audio_dev) SDL_QueueAudio(audio_dev, buf, samples * sizeof(int16_t));
}

static void audio_close() {
    if (audio_dev) { SDL_CloseAudioDevice(audio_dev); audio_dev = 0; }
}

// ========== Key state ==========
static int sdl_vk_from_scancode[512];
static bool sdl_keys[512];

static int vk_from_sdl(SDL_Scancode s) {
    if (s >= 512) return 0;
    return sdl_vk_from_scancode[s] ? sdl_vk_from_scancode[s] : 0;
}

static void init_keymap() {
    memset(sdl_vk_from_scancode, 0, sizeof(sdl_vk_from_scancode));
    memset(sdl_keys, 0, sizeof(sdl_keys));

    auto map = [](SDL_Scancode sc, int vk) { if (sc < 512) sdl_vk_from_scancode[sc] = vk; };
    map(SDL_SCANCODE_X, 'X');
    map(SDL_SCANCODE_Z, 'Z');
    map(SDL_SCANCODE_RETURN, VK_RETURN);
    map(SDL_SCANCODE_BACKSPACE, VK_BACK);
    map(SDL_SCANCODE_UP, VK_UP);
    map(SDL_SCANCODE_DOWN, VK_DOWN);
    map(SDL_SCANCODE_LEFT, VK_LEFT);
    map(SDL_SCANCODE_RIGHT, VK_RIGHT);
    map(SDL_SCANCODE_SPACE, VK_SPACE);
    map(SDL_SCANCODE_F1, VK_F1);
    map(SDL_SCANCODE_F2, VK_F2);
    map(SDL_SCANCODE_F5, VK_F5);
    map(SDL_SCANCODE_F7, VK_F7);
}

// ========== Gameboy ==========
static const int GB_W = 160, GB_H = 144, GB_SCALE = 3;
static Cartridge gb_cart; static MMU gb_mmu; static PPU gb_ppu; static CPU gb_cpu;
static bool gb_run;
static int gb_tc, gb_dc;
static const int GB_DCYC = 256;
static const int GB_TSPD[4] = {1024,16,64,256};
static int gb_k_up, gb_k_down, gb_k_left, gb_k_right;
static int gb_k_a, gb_k_b, gb_k_start, gb_k_select;
static char gb_rom_path[1024];

static const uint8_t GB_DUTY[4][8] = {
    {0,0,0,0,0,0,0,1}, {1,0,0,0,0,0,0,1},
    {1,0,0,0,0,1,1,1}, {0,1,1,1,1,1,1,0}
};

struct GbPulse {
    uint32_t phase;
    int freq, duty, volume, env_vol, env_dir, env_period, env_timer;
    int sweep_period, sweep_shift, sweep_dir, sweep_timer, shadow;
    bool on, len_enabled, sweep_enabled;
    int length;
    void reg_write(int addr, uint8_t v) {
        if (addr == 1) { duty = (v >> 6) & 3; length = (64 - (v & 63)) * 2; }
        else if (addr == 2) { env_vol = v >> 4; env_dir = (v >> 3) & 1; env_period = v & 7; volume = env_vol; env_timer = env_period; }
        else if (addr == 3) { freq = (freq & 0x700) | v; }
        else if (addr == 4) {
            freq = (freq & 0xFF) | ((v & 7) << 8);
            len_enabled = v & 0x40;
            if (v & 0x80) {
                on = env_vol > 0 || env_period > 0;
                phase = 0; volume = env_vol;
                env_timer = env_period; shadow = freq;
                if (sweep_period || sweep_shift) sweep_enabled = true;
                sweep_timer = sweep_period ? sweep_period : 8;
            }
        } else if (addr == 0) { sweep_period = (v >> 4) & 7; sweep_dir = (v >> 3) & 1; sweep_shift = v & 7; }
    }
    void sweep_tick() {
        if (!sweep_enabled || !sweep_period) return;
        sweep_timer--; if (sweep_timer > 0) return;
        sweep_timer = sweep_period ? sweep_period : 8;
        int inc = shadow >> sweep_shift;
        if (sweep_dir) shadow -= inc; else shadow += inc;
        if (shadow > 2047) { on = false; return; }
        freq = shadow;
        int inc2 = shadow >> sweep_shift;
        if (sweep_dir) { if (shadow - inc2 < 0) on = false; }
        else { if (shadow + inc2 > 2047) on = false; }
    }
};

struct GbWave {
    uint32_t phase;
    int freq, volume_shift;
    bool on, dac_enabled, len_enabled;
    int length;
    void reg_write(int addr, uint8_t v) {
        if (addr == 0) { dac_enabled = v & 0x80; if (!dac_enabled) on = false; }
        else if (addr == 1) { length = (256 - v); }
        else if (addr == 2) { volume_shift = ((v >> 5) & 3) ? 5 - ((v >> 5) & 3) : 4; }
        else if (addr == 3) { freq = (freq & 0x700) | v; }
        else if (addr == 4) {
            freq = (freq & 0xFF) | ((v & 7) << 8);
            len_enabled = v & 0x40;
            if (v & 0x80) { on = dac_enabled; phase = 0; }
        }
    }
};

struct GbNoise {
    uint32_t phase;
    uint16_t lfsr;
    int shift, r, width;
    int volume, env_vol, env_dir, env_period, env_timer;
    int length;
    bool on, dac_enabled, len_enabled;
    void reg_write(int addr, uint8_t v) {
        if (addr == 1) { length = (64 - (v & 63)) * 2; }
        else if (addr == 2) { env_vol = v >> 4; env_dir = (v >> 3) & 1; env_period = v & 7; volume = env_vol; env_timer = env_period; }
        else if (addr == 3) { shift = (v >> 4) & 0xF; width = (v >> 3) & 1; r = v & 7; }
        else if (addr == 4) {
            len_enabled = v & 0x40;
            if (v & 0x80) {
                dac_enabled = env_vol > 0 || env_period > 0;
                on = dac_enabled; lfsr = 0x7FFF; volume = env_vol; env_timer = env_period; phase = 0;
            }
        }
    }
};

static GbPulse gb_p1, gb_p2;
static GbWave gb_wave;
static GbNoise gb_noise;
static int gb_sweep_frame_count;

static void gb_gen_audio() {
    int16_t buf[AUDIO_SAMPLES];
    for (int i = 0; i < AUDIO_SAMPLES; i++) {
        int mix = 0;

        for (auto* ch : {&gb_p1, &gb_p2}) {
            if (!ch->on || ch->volume == 0) continue;
            int pat = GB_DUTY[ch->duty][(ch->phase >> 14) & 7];
            if (pat) mix += ch->volume * 600;
            int inc = (131072 / (2048 - ch->freq)) * 65536 / AUDIO_SR;
            if (inc < 1) inc = 1;
            ch->phase += inc;
        }

        if (gb_wave.on && gb_wave.dac_enabled) {
            int pos = (gb_wave.phase >> 14) & 0x1F;
            int nibble = (gb_mmu.io[0x30 + pos / 2] >> (4 * (1 - (pos & 1)))) & 0xF;
            int vol = gb_wave.volume_shift == 4 ? 0 : nibble >> gb_wave.volume_shift;
            mix += vol * 150;
            int inc = (131072 / (2048 - gb_wave.freq)) * 65536 / AUDIO_SR;
            if (inc < 1) inc = 1;
            gb_wave.phase += inc;
        }

        if (gb_noise.on && gb_noise.volume > 0) {
            int div = (gb_noise.r + 1) * (1 << (gb_noise.shift + 1));
            if (div < 1) div = 1;
            int inc = (131072 / div) * 65536 / AUDIO_SR;
            if (inc < 1) inc = 1;
            uint32_t old = gb_noise.phase;
            gb_noise.phase += inc;
            if (gb_noise.phase < old) {
                uint16_t bit = (gb_noise.lfsr & 1) ^ ((gb_noise.lfsr >> 1) & 1);
                gb_noise.lfsr >>= 1;
                gb_noise.lfsr |= bit << (gb_noise.width ? 6 : 14);
            }
            if (!(gb_noise.lfsr & 1)) mix += gb_noise.volume * 600;
        }

        buf[i] = (int16_t)(mix > 32767 ? 32767 : mix < -32768 ? -32768 : mix);
    }
    audio_push(buf, AUDIO_SAMPLES);
}

static void gb_apu_tick() {
    gb_p1.reg_write(0, gb_mmu.io[0x10]);
    gb_p1.reg_write(1, gb_mmu.io[0x11]);
    gb_p1.reg_write(2, gb_mmu.io[0x12]);
    gb_p1.reg_write(3, gb_mmu.io[0x13]);
    gb_p1.reg_write(4, gb_mmu.io[0x14]);
    gb_p2.reg_write(1, gb_mmu.io[0x16]);
    gb_p2.reg_write(2, gb_mmu.io[0x17]);
    gb_p2.reg_write(3, gb_mmu.io[0x18]);
    gb_p2.reg_write(4, gb_mmu.io[0x19]);
    gb_wave.reg_write(0, gb_mmu.io[0x1A]);
    gb_wave.reg_write(1, gb_mmu.io[0x1B]);
    gb_wave.reg_write(2, gb_mmu.io[0x1C]);
    gb_wave.reg_write(3, gb_mmu.io[0x1D]);
    gb_wave.reg_write(4, gb_mmu.io[0x1E]);
    gb_noise.reg_write(1, gb_mmu.io[0x21]);
    gb_noise.reg_write(2, gb_mmu.io[0x22]);
    gb_noise.reg_write(3, gb_mmu.io[0x23]);
    gb_noise.reg_write(4, gb_mmu.io[0x24]);
    if (gb_p1.len_enabled && ++gb_p1.length > 128) gb_p1.on = false;
    if (gb_p2.len_enabled && ++gb_p2.length > 128) gb_p2.on = false;
    if (gb_wave.len_enabled && ++gb_wave.length > 256) gb_wave.on = false;
    if (gb_noise.len_enabled && ++gb_noise.length > 128) gb_noise.on = false;
    auto env_tick = [](auto* ch) {
        if (ch->env_period && ch->env_timer-- <= 0) {
            ch->env_timer = ch->env_period;
            if (ch->env_dir) { if (ch->volume < 15) ch->volume++; }
            else { if (ch->volume > 0) ch->volume--; }
        }
    };
    env_tick(&gb_p1); env_tick(&gb_p2); env_tick(&gb_noise);
    if (++gb_sweep_frame_count >= 128) {
        gb_sweep_frame_count = 0;
        gb_p1.sweep_tick();
    }
}

static void gb_upd_timer(int c) {
    uint8_t tac = gb_mmu.io[0x07];
    if (tac & 0x04) {
        int sp = GB_TSPD[tac & 3]; gb_tc += c;
        while (gb_tc >= sp) { gb_tc -= sp;
            if (gb_mmu.io[0x05] == 0xFF) { gb_mmu.io[0x05] = gb_mmu.io[0x06]; gb_mmu.request_interrupt(2); }
            else gb_mmu.io[0x05]++; }
    }
    gb_dc += c;
    while (gb_dc >= GB_DCYC) { gb_dc -= GB_DCYC; gb_mmu.io[0x04]++; }
}

static void gb_upd_inp() {
    uint8_t p1 = gb_mmu.io[0]; p1 |= 0xCF;
    uint8_t col = (p1 >> 4) & 3;
    if (!(col & 1)) {
        if (sdl_keys[gb_k_a]) p1 &= ~1;
        if (sdl_keys[gb_k_b]) p1 &= ~2;
        if (sdl_keys[gb_k_select]) p1 &= ~4;
        if (sdl_keys[gb_k_start]) p1 &= ~8;
    }
    if (!(col & 2)) {
        if (sdl_keys[gb_k_right]) p1 &= ~1;
        if (sdl_keys[gb_k_left]) p1 &= ~2;
        if (sdl_keys[gb_k_up]) p1 &= ~4;
        if (sdl_keys[gb_k_down]) p1 &= ~8;
    }
    if ((p1 & 0x0F) != 0x0F) gb_mmu.request_interrupt(4);
    gb_mmu.io[0] = p1;
}

static void gb_run_fr() {
    int c = 0;
    while (c < 70224) { int c2 = gb_cpu.tick(); gb_mmu.dma_step(c2); for (int i = 0; i < c2; i += 4) gb_ppu.step(4); gb_upd_timer(c2); c += c2; }
    gb_ppu.frame_complete = false;
}

static bool run_gameboy(const char* path, SDL_Window* win) {
    if (!gb_cart.load(path)) return false;
    strcpy(gb_rom_path, path);
    gb_mmu = MMU(); gb_ppu = PPU(); gb_cpu = CPU();
    gb_mmu.ppu = &gb_ppu; gb_mmu.cpu = &gb_cpu; gb_cpu.mmu = &gb_mmu; gb_ppu.mmu = &gb_mmu;
    gb_ppu.cgb_mode = (gb_cart.cgb_flag == 0x80 || gb_cart.cgb_flag == 0xC0);
    gb_cpu.PC = 0x0100; gb_cpu.SP = 0xFFFE; gb_cpu.AF = 0x01B0;
    gb_cpu.BC = 0x0013; gb_cpu.DE = 0x00D8; gb_cpu.HL = 0x014D;
    memset(sdl_keys, 0, sizeof(sdl_keys)); gb_tc = gb_dc = 0; gb_run = true;
    gb_k_up = VK_UP; gb_k_down = VK_DOWN; gb_k_left = VK_LEFT; gb_k_right = VK_RIGHT;
    gb_k_a = 'X'; gb_k_b = 'Z'; gb_k_start = VK_RETURN; gb_k_select = VK_BACK;

    gb_p1 = GbPulse(); gb_p2 = GbPulse(); gb_wave = GbWave(); gb_noise = GbNoise(); gb_sweep_frame_count = 0;
    audio_init();

    SDL_Surface* surf = SDL_GetWindowSurface(win);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, GB_W, GB_H);

    const double ft = 1.0 / 59.73;
    uint32_t last_tick = SDL_GetTicks();

    while (gb_run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { gb_run = false; break; }
            if (e.type == SDL_KEYDOWN) {
                int k = vk_from_sdl(e.key.keysym.scancode);
                if (k > 0 && k < 512) sdl_keys[k] = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_F5) save_gb_state(state_path(gb_rom_path, 0), &gb_cpu, &gb_mmu, &gb_ppu, &gb_cart);
                if (e.key.keysym.scancode == SDL_SCANCODE_F7) load_gb_state(state_path(gb_rom_path, 0), &gb_cpu, &gb_mmu, &gb_ppu, &gb_cart);
            }
            if (e.type == SDL_KEYUP) {
                int k = vk_from_sdl(e.key.keysym.scancode);
                if (k > 0 && k < 512) sdl_keys[k] = false;
            }
        }
        if (!gb_run) break;

        gb_upd_inp(); gb_run_fr(); gb_apu_tick(); gb_gen_audio();

        SDL_UpdateTexture(tex, nullptr, gb_ppu.framebuffer, GB_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        uint32_t now = SDL_GetTicks();
        uint32_t frame_ms = (uint32_t)(ft * 1000.0);
        uint32_t elapsed = now - last_tick;
        if (elapsed < frame_ms) SDL_Delay(frame_ms - elapsed);
        last_tick = now;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    audio_close();
    gb_cart.save_battery(gb_rom_path);
    return true;
}

// ========== Chip-8 ==========
static const int C8_W = 64, C8_H = 32, C8_SCL = 12;
static const uint32_t C8_ON = 0x00CC44FF, C8_OFF = 0x001A0022;
static Chip8 c8; static bool c8_run;

static bool run_chip8(const char* path, SDL_Window* win) {
    c8.init(); if (!c8.load(path)) return false;
    memset(sdl_keys, 0, sizeof(sdl_keys)); c8_run = true; srand(SDL_GetTicks());
    audio_init();

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, C8_W, C8_H);

    const double ft = 1.0 / 60.0;
    uint32_t last_tick = SDL_GetTicks();
    static int c8_beep_phase = 0;

    while (c8_run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { c8_run = false; break; }
            if (e.type == SDL_KEYDOWN) {
                int k = e.key.keysym.scancode;
                if (k == SDL_SCANCODE_X) c8.keys[0] = true;
                else if (k == SDL_SCANCODE_1) c8.keys[1] = true;
                else if (k == SDL_SCANCODE_2) c8.keys[2] = true;
                else if (k == SDL_SCANCODE_3) c8.keys[3] = true;
                else if (k == SDL_SCANCODE_4) c8.keys[4] = true;
                else if (k == SDL_SCANCODE_Q) c8.keys[5] = true;
                else if (k == SDL_SCANCODE_W) c8.keys[6] = true;
                else if (k == SDL_SCANCODE_E) c8.keys[7] = true;
                else if (k == SDL_SCANCODE_R) c8.keys[8] = true;
                else if (k == SDL_SCANCODE_A) c8.keys[9] = true;
                else if (k == SDL_SCANCODE_S) c8.keys[0xA] = true;
                else if (k == SDL_SCANCODE_D) c8.keys[0xB] = true;
                else if (k == SDL_SCANCODE_F) c8.keys[0xC] = true;
                else if (k == SDL_SCANCODE_Z) c8.keys[0xD] = true;
                else if (k == SDL_SCANCODE_C) c8.keys[0xE] = true;
                else if (k == SDL_SCANCODE_V) c8.keys[0xF] = true;
                if (e.key.keysym.scancode == SDL_SCANCODE_F5) save_c8_state(state_path(path, 0), &c8);
                if (e.key.keysym.scancode == SDL_SCANCODE_F7) load_c8_state(state_path(path, 0), &c8);
            }
            if (e.type == SDL_KEYUP) {
                int k = e.key.keysym.scancode;
                if (k == SDL_SCANCODE_X) c8.keys[0] = false;
                else if (k == SDL_SCANCODE_1) c8.keys[1] = false;
                else if (k == SDL_SCANCODE_2) c8.keys[2] = false;
                else if (k == SDL_SCANCODE_3) c8.keys[3] = false;
                else if (k == SDL_SCANCODE_4) c8.keys[4] = false;
                else if (k == SDL_SCANCODE_Q) c8.keys[5] = false;
                else if (k == SDL_SCANCODE_W) c8.keys[6] = false;
                else if (k == SDL_SCANCODE_E) c8.keys[7] = false;
                else if (k == SDL_SCANCODE_R) c8.keys[8] = false;
                else if (k == SDL_SCANCODE_A) c8.keys[9] = false;
                else if (k == SDL_SCANCODE_S) c8.keys[0xA] = false;
                else if (k == SDL_SCANCODE_D) c8.keys[0xB] = false;
                else if (k == SDL_SCANCODE_F) c8.keys[0xC] = false;
                else if (k == SDL_SCANCODE_Z) c8.keys[0xD] = false;
                else if (k == SDL_SCANCODE_C) c8.keys[0xE] = false;
                else if (k == SDL_SCANCODE_V) c8.keys[0xF] = false;
            }
        }
        if (!c8_run) break;

        for (int i = 0; i < 600; i++) c8.step();

        SDL_UpdateTexture(tex, nullptr, c8.framebuffer, C8_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        int16_t abuf[AUDIO_SAMPLES];
        for (int i = 0; i < AUDIO_SAMPLES; i++) {
            if (c8.sound_timer > 0) {
                abuf[i] = (c8_beep_phase & 0x10000) ? 8000 : -8000;
                c8_beep_phase += 440 * 65536 / AUDIO_SR;
            } else {
                abuf[i] = 0;
            }
        }
        audio_push(abuf, AUDIO_SAMPLES);

        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last_tick;
        if (elapsed < (uint32_t)(ft * 1000.0)) SDL_Delay((uint32_t)(ft * 1000.0) - elapsed);
        last_tick = now;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    audio_close();
    return true;
}

// ========== Space Invaders ==========
static const int SI_W = Invaders8080::W, SI_H = Invaders8080::H, SI_SCL = 3;
static Invaders8080 si; static bool si_run;
static char si_rom_path[1024];

static bool run_invaders(const char* path, SDL_Window* win) {
    si.init(); if (!si.load(path)) return false;
    strcpy(si_rom_path, path);
    memset(&si.in0, 0, sizeof(si.in0)); memset(&si.in1, 0, sizeof(si.in1)); memset(&si.in2, 0, sizeof(si.in2));
    si.in0 = si.in1 = 0xFF; si.in2 = 0x00;
    si_run = true;
    audio_init();

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SI_W, SI_H);

    const double ft = 1.0 / 60.0;
    uint32_t last_tick = SDL_GetTicks();

    while (si_run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { si_run = false; break; }
            if (e.type == SDL_KEYDOWN) {
                auto sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_LEFT) si.in0 &= ~0x10;
                else if (sc == SDL_SCANCODE_RIGHT) si.in0 &= ~0x20;
                else if (sc == SDL_SCANCODE_Z) si.in0 &= ~0x08;
                else if (sc == SDL_SCANCODE_5) si.in0 &= ~0x01;
                else if (sc == SDL_SCANCODE_1) si.in0 &= ~0x04;
                else if (sc == SDL_SCANCODE_2) si.in0 &= ~0x02;
                if (e.key.keysym.scancode == SDL_SCANCODE_F5) save_si_state(state_path(si_rom_path, 0), &si);
                if (e.key.keysym.scancode == SDL_SCANCODE_F7) load_si_state(state_path(si_rom_path, 0), &si);
            }
            if (e.type == SDL_KEYUP) {
                auto sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_LEFT) si.in0 |= 0x10;
                else if (sc == SDL_SCANCODE_RIGHT) si.in0 |= 0x20;
                else if (sc == SDL_SCANCODE_Z) si.in0 |= 0x08;
                else if (sc == SDL_SCANCODE_5) si.in0 |= 0x01;
                else if (sc == SDL_SCANCODE_1) si.in0 |= 0x04;
                else if (sc == SDL_SCANCODE_2) si.in0 |= 0x02;
            }
        }
        if (!si_run) break;

        for (int i = 0; i < 30000; i++) si.step();
        si.int_pending = true;
        si.render();

        SDL_UpdateTexture(tex, nullptr, si.framebuffer, SI_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        int16_t abuf[AUDIO_SAMPLES]; si.gen_audio(abuf, AUDIO_SAMPLES, AUDIO_SR); audio_push(abuf, AUDIO_SAMPLES);

        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last_tick;
        if (elapsed < (uint32_t)(ft * 1000.0)) SDL_Delay((uint32_t)(ft * 1000.0) - elapsed);
        last_tick = now;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    audio_close();
    return true;
}

// ========== Atari 2600 ==========
static const int A26_W = 160, A26_H = 192, A26_SCL = 4;
static A2600 a26; static bool a26_run;
static char a26_rom_path[1024];

static bool run_a2600(const char* path, SDL_Window* win) {
    a26.init(); if (!a26.load(path)) return false;
    strcpy(a26_rom_path, path);
    a26_run = true;
    a26.swcha = 0xFF; a26.swchb = 0xFF; a26.inpt4 = a26.inpt5 = true;
    audio_init();

    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, A26_W, A26_H);

    const double ft = 1.0 / 60.0;
    uint32_t last_tick = SDL_GetTicks();

    while (a26_run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) { a26_run = false; break; }
            if (e.type == SDL_KEYDOWN) {
                auto sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_UP) a26.swcha &= ~0x08;
                else if (sc == SDL_SCANCODE_DOWN) a26.swcha &= ~0x04;
                else if (sc == SDL_SCANCODE_LEFT) a26.swcha &= ~0x02;
                else if (sc == SDL_SCANCODE_RIGHT) a26.swcha &= ~0x01;
                else if (sc == SDL_SCANCODE_SPACE) a26.inpt4 = false;
                else if (sc == SDL_SCANCODE_F1) a26.swchb |= 0x02;
                else if (sc == SDL_SCANCODE_F2) a26.swchb &= ~0x01;
                if (e.key.keysym.scancode == SDL_SCANCODE_F5) save_a26_state(state_path(a26_rom_path, 0), &a26);
                if (e.key.keysym.scancode == SDL_SCANCODE_F7) load_a26_state(state_path(a26_rom_path, 0), &a26);
            }
            if (e.type == SDL_KEYUP) {
                auto sc = e.key.keysym.scancode;
                if (sc == SDL_SCANCODE_UP) a26.swcha |= 0x08;
                else if (sc == SDL_SCANCODE_DOWN) a26.swcha |= 0x04;
                else if (sc == SDL_SCANCODE_LEFT) a26.swcha |= 0x02;
                else if (sc == SDL_SCANCODE_RIGHT) a26.swcha |= 0x01;
                else if (sc == SDL_SCANCODE_SPACE) a26.inpt4 = true;
                else if (sc == SDL_SCANCODE_F1) a26.swchb &= ~0x02;
                else if (sc == SDL_SCANCODE_F2) a26.swchb |= 0x01;
            }
        }
        if (!a26_run) break;

        a26.run_frame();

        SDL_UpdateTexture(tex, nullptr, a26.framebuffer, A26_W * sizeof(uint32_t));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);

        int16_t abuf[AUDIO_SAMPLES]; a26.gen_audio(abuf, AUDIO_SAMPLES, AUDIO_SR); audio_push(abuf, AUDIO_SAMPLES);

        uint32_t now = SDL_GetTicks();
        uint32_t elapsed = now - last_tick;
        if (elapsed < (uint32_t)(ft * 1000.0)) SDL_Delay((uint32_t)(ft * 1000.0) - elapsed);
        last_tick = now;
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    audio_close();
    return true;
}

// ========== Main ==========
static void print_usage() {
    printf("Console Launcher (SDL2)\n");
    printf("Usage: sdl_launcher <rom_path>\n");
    printf("Supported systems (auto-detected by extension):\n");
    printf("  .gb .gbc  - Gameboy / Gameboy Color\n");
    printf("  .ch8 .c8b - Chip-8\n");
    printf("  .inv .rom - Space Invaders\n");
    printf("  .a26 .bin - Atari 2600\n");
    printf("\nControls:\n");
    printf("  Gameboy: Arrow keys, X=A, Z=B, Enter=Start, Back=Select\n");
    printf("  Chip-8:  X,1,2,3,4,Q,W,E,R,A,S,D,F,Z,C,V = 0-F\n");
    printf("  SI:      Arrows, Z=Fire, 5=Coin, 1=1P, 2=2P\n");
    printf("  A2600:   Arrows, Space=Fire, F1=Select, F2=Reset\n");
    printf("  F5=Save State, F7=Load State\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char* path = argv[1];
    const char* ext = strrchr(path, '.');
    if (!ext) { printf("No extension found in path.\n"); return 1; }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    atexit(SDL_Quit);
    init_keymap();

    int console_type = 0; // 0=GB, 1=CHIP8, 2=INV, 3=A26
    int win_w = 160*3, win_h = 144*3;
    const char* title = "Emulator";

    if (strcmp(ext, ".gb") == 0 || strcmp(ext, ".gbc") == 0) {
        console_type = 0; win_w = 160*3; win_h = 144*3; title = "Gameboy";
    } else if (strcmp(ext, ".ch8") == 0 || strcmp(ext, ".c8b") == 0) {
        console_type = 1; win_w = 64*12; win_h = 32*12; title = "Chip-8";
    } else if (strcmp(ext, ".inv") == 0 || strcmp(ext, ".rom") == 0) {
        console_type = 2; win_w = 256*2; win_h = 224*2; title = "Space Invaders";
    } else if (strcmp(ext, ".a26") == 0 || strcmp(ext, ".bin") == 0) {
        console_type = 3; win_w = 160*3; win_h = 192*3; title = "Atari 2600";
    } else {
        printf("Unknown file extension: %s\nSupported: .gb, .gbc, .ch8, .c8b, .inv, .rom, .a26, .bin\n", ext);
        SDL_Quit();
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                         win_w, win_h, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool ok = false;
    switch (console_type) {
        case 0: ok = run_gameboy(path, win); break;
        case 1: ok = run_chip8(path, win); break;
        case 2: ok = run_invaders(path, win); break;
        case 3: ok = run_a2600(path, win); break;
    }

    SDL_DestroyWindow(win);
    SDL_Quit();
    return ok ? 0 : 1;
}
