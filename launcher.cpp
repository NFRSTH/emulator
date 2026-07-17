#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

struct KeyBind { std::string action; int key; };

// ===================== Console Entry =====================
struct ConsoleEntry {
    std::string name;
    std::string emulator;
    std::string extensions;
    std::string filter;
    std::vector<KeyBind> key_bindings;

    int get_key(const char* act, int def) const {
        for (auto& k : key_bindings) if (k.action == act) return k.key;
        return def;
    }
    void set_key(const char* act, int k) {
        for (auto& b : key_bindings) if (b.action == act) { b.key = k; return; }
        key_bindings.push_back({act, k});
    }
    bool has_action(const char* act) const {
        for (auto& k : key_bindings) if (k.action == act) return true;
        return false;
    }
};

static const char* GB_ACTIONS[] = {"Up","Down","Left","Right","A","B","Start","Select",nullptr};
static const char* C8_ACTIONS[] = {"0","1","2","3","4","5","6","7","8","9","A","B","C","D","E","F",nullptr};
static int GB_DEFAULT_KEYS[] = {VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'X','Z',VK_RETURN,VK_BACK};
static int C8_DEFAULT_KEYS[] = {'X','1','2','3','4','Q','W','E','R','A','S','D','F','Z','C','V'};
static const char* NES_ACTIONS[] = {"Up","Down","Left","Right","A","B","Start","Select",nullptr};
static int NES_DEFAULT_KEYS[] = {VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,'X','Z',VK_RETURN,VK_BACK};
// C8 hex key 0->'X', 1->'1', 2->'2', 3->'3', 4->'4', 5->'Q', 6->'W', 7->'E',
// 8->'R', 9->'A', A->'S', B->'D', C->'F', D->'Z', E->'C', F->'V'
static const char* SI_ACTIONS[] = {"Left","Right","Fire","Coin","1P","2P",nullptr};
static int SI_DEFAULT_KEYS[] = {VK_LEFT,VK_RIGHT,'Z','5','1','2'};
static const char* A26_ACTIONS[] = {"Up","Down","Left","Right","Fire","Select","Reset",nullptr};
static int A26_DEFAULT_KEYS[] = {VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_SPACE,VK_F1,VK_F2};

static void set_default_bindings(ConsoleEntry& c, const char** acts, int* keys) {
    for (int i = 0; acts[i]; i++) {
        if (!c.has_action(acts[i]))
            c.key_bindings.push_back({acts[i], keys[i]});
    }
}

// ===================== Gameboy Emulator =====================
#include "cartridge.hpp"
#include "mmu.hpp"
#include "ppu.hpp"
#include "cpu.hpp"
#include "audio.hpp"
#include "savestate.hpp"
#include "invaders.hpp"
#include "atari2600.hpp"
#include "nes.hpp"

static const int GB_W = 160, GB_H = 144, GB_SCALE = 3;
static Cartridge gb_cart; static MMU gb_mmu; static PPU gb_ppu; static CPU gb_cpu;
static HBITMAP gb_bmp; static BITMAPINFO gb_bmi;
static bool gb_run, gb_keys[256];
static int gb_tc, gb_dc;
static const int GB_DCYC = 256;
static const int GB_TSPD[4] = {1024,16,64,256};
// custom key bindings for current session
static int gb_k_up, gb_k_down, gb_k_left, gb_k_right;
static int gb_k_a, gb_k_b, gb_k_start, gb_k_select;
static AudioOut gb_audio;
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
    int freq, volume_shift; // volume: 0=mute, 1=100%, 2=50%, 3=25%
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
            if (v & 0x80) {
                on = dac_enabled;
                phase = 0;
            }
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
                on = dac_enabled;
                lfsr = 0x7FFF;
                volume = env_vol;
                env_timer = env_period;
                phase = 0;
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

        // Pulse 1 & 2
        for (auto* ch : {&gb_p1, &gb_p2}) {
            if (!ch->on || ch->volume == 0) continue;
            int pat = GB_DUTY[ch->duty][(ch->phase >> 14) & 7];
            if (pat) mix += ch->volume * 600;
            int inc = (131072 / (2048 - ch->freq)) * 65536 / AUDIO_SR;
            if (inc < 1) inc = 1;
            ch->phase += inc;
        }

        // Wave channel
        if (gb_wave.on && gb_wave.dac_enabled) {
            int pos = (gb_wave.phase >> 14) & 0x1F;
            int nibble = (gb_mmu.io[0x30 + pos / 2] >> (4 * (1 - (pos & 1)))) & 0xF;
            int vol = gb_wave.volume_shift == 4 ? 0 : nibble >> gb_wave.volume_shift;
            mix += vol * 150;
            int inc = (131072 / (2048 - gb_wave.freq)) * 65536 / AUDIO_SR;
            if (inc < 1) inc = 1;
            gb_wave.phase += inc;
        }

        // Noise channel
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
    gb_audio.push(buf, AUDIO_SAMPLES);
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
    // Length counters
    if (gb_p1.len_enabled && ++gb_p1.length > 128) gb_p1.on = false;
    if (gb_p2.len_enabled && ++gb_p2.length > 128) gb_p2.on = false;
    if (gb_wave.len_enabled && ++gb_wave.length > 256) gb_wave.on = false;
    if (gb_noise.len_enabled && ++gb_noise.length > 128) gb_noise.on = false;
    // Envelope
    auto env_tick = [](auto* ch) {
        if (ch->env_period && ch->env_timer-- <= 0) {
            ch->env_timer = ch->env_period;
            if (ch->env_dir) { if (ch->volume < 15) ch->volume++; }
            else { if (ch->volume > 0) ch->volume--; }
        }
    };
    env_tick(&gb_p1); env_tick(&gb_p2); env_tick(&gb_noise);
    // Sweep per 128 frames (every ~2 seconds at 60fps = 128 frames)
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
        if (gb_keys[gb_k_a]) p1 &= ~1;
        if (gb_keys[gb_k_b]) p1 &= ~2;
        if (gb_keys[gb_k_select]) p1 &= ~4;
        if (gb_keys[gb_k_start]) p1 &= ~8;
    }
    if (!(col & 2)) {
        if (gb_keys[gb_k_right]) p1 &= ~1;
        if (gb_keys[gb_k_left]) p1 &= ~2;
        if (gb_keys[gb_k_up]) p1 &= ~4;
        if (gb_keys[gb_k_down]) p1 &= ~8;
    }
    if ((p1 & 0x0F) != 0x0F) gb_mmu.request_interrupt(4);
    gb_mmu.io[0] = p1;
}

static void gb_run_fr() {
    int c = 0;
    while (c < 70224) { int c2 = gb_cpu.tick(); gb_mmu.dma_step(c2); for (int i = 0; i < c2; i += 4) gb_ppu.step(4); gb_upd_timer(c2); c += c2; }
    gb_ppu.frame_complete = false;
}

static void gb_ren(HWND hwnd, HDC hdc) {
    HDC m = CreateCompatibleDC(hdc);
    HBITMAP o = (HBITMAP)SelectObject(m, gb_bmp);
    SetDIBits(hdc, gb_bmp, 0, GB_H, gb_ppu.framebuffer, &gb_bmi, DIB_RGB_COLORS);
    RECT r; GetClientRect(hwnd, &r);
    StretchBlt(hdc, 0, 0, r.right, r.bottom, m, 0, 0, GB_W, GB_H, SRCCOPY);
    SelectObject(m, o); DeleteDC(m);
}

static LRESULT CALLBACK gb_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN:
            gb_keys[w & 0xFF] = true;
            if (w == VK_F5) save_gb_state(state_path(gb_rom_path, 0), &gb_cpu, &gb_mmu, &gb_ppu, &gb_cart);
            if (w == VK_F7) load_gb_state(state_path(gb_rom_path, 0), &gb_cpu, &gb_mmu, &gb_ppu, &gb_cart);
            return 0;
        case WM_KEYUP: gb_keys[w & 0xFF] = false; return 0;
        case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); gb_ren(h, dc); EndPaint(h, &ps); return 0; }
        case WM_DESTROY: { gb_run = false; gb_cart.save_battery(gb_rom_path); return 0; }
    }
    return DefWindowProcA(h, m, w, l);
}

static bool run_gameboy(const char* path, HINSTANCE hi, ConsoleEntry& ce) {
    static bool cr = false;
    if (!cr) {
        WNDCLASSA wc = {0}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = gb_wp;
        wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "GBClass";
        RegisterClassA(&wc); cr = true;
    }
    if (!gb_cart.load(path)) return false;
    strcpy(gb_rom_path, path);
    gb_mmu = MMU(); gb_ppu = PPU(); gb_cpu = CPU();
    gb_mmu.ppu = &gb_ppu; gb_mmu.cpu = &gb_cpu; gb_cpu.mmu = &gb_mmu; gb_ppu.mmu = &gb_mmu;
    gb_ppu.cgb_mode = (gb_cart.cgb_flag == 0x80 || gb_cart.cgb_flag == 0xC0);
    gb_cpu.PC = 0x0100; gb_cpu.SP = 0xFFFE; gb_cpu.AF = 0x01B0;
    gb_cpu.BC = 0x0013; gb_cpu.DE = 0x00D8; gb_cpu.HL = 0x014D;
    memset(gb_keys, 0, sizeof(gb_keys)); gb_tc = gb_dc = 0; gb_run = true;

    gb_k_up = ce.get_key("Up", VK_UP); gb_k_down = ce.get_key("Down", VK_DOWN);
    gb_k_left = ce.get_key("Left", VK_LEFT); gb_k_right = ce.get_key("Right", VK_RIGHT);
    gb_k_a = ce.get_key("A", 'X'); gb_k_b = ce.get_key("B", 'Z');
    gb_k_start = ce.get_key("Start", VK_RETURN); gb_k_select = ce.get_key("Select", VK_BACK);

    gb_p1 = GbPulse(); gb_p2 = GbPulse(); gb_wave = GbWave(); gb_noise = GbNoise(); gb_sweep_frame_count = 0;
    gb_audio = AudioOut(); gb_audio.open();

    memset(&gb_bmi, 0, sizeof(gb_bmi));
    gb_bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    gb_bmi.bmiHeader.biWidth = GB_W; gb_bmi.bmiHeader.biHeight = -GB_H;
    gb_bmi.bmiHeader.biPlanes = 1; gb_bmi.bmiHeader.biBitCount = 32; gb_bmi.bmiHeader.biCompression = BI_RGB;
    HDC dc = GetDC(NULL); gb_bmp = CreateDIBSection(dc, &gb_bmi, DIB_RGB_COLORS, NULL, NULL, 0); ReleaseDC(NULL, dc);

    RECT wr = {0,0,GB_W*GB_SCALE,GB_H*GB_SCALE};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    char t[512]; snprintf(t, sizeof(t), "%s - Gameboy", gb_cart.title.c_str());
    HWND hw = CreateWindowExA(0, "GBClass", t, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right-wr.left, wr.bottom-wr.top, NULL, NULL, hi, NULL);
    if (!hw) return false;
    ShowWindow(hw, SW_SHOW); UpdateWindow(hw);

    LARGE_INTEGER f, l; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&l);
    const double ft = 1.0/59.73; MSG msg;
    while (gb_run && GetMessageA(&msg, NULL, 0, 0)) {
        do { TranslateMessage(&msg); DispatchMessageA(&msg); } while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
        if (!gb_run || !IsWindow(hw)) break;
        gb_upd_inp(); gb_run_fr(); gb_apu_tick(); gb_gen_audio();
        HDC hdc = GetDC(hw); if (hdc) { gb_ren(hw, hdc); ReleaseDC(hw, hdc); }
        LARGE_INTEGER n; QueryPerformanceCounter(&n);
        double e = (double)(n.QuadPart - l.QuadPart) / f.QuadPart;
        double sl = (ft - e) * 1000.0; if (sl > 0) Sleep((DWORD)sl);
        QueryPerformanceCounter(&l);
    }
    gb_audio.close(); DestroyWindow(hw); DeleteObject(gb_bmp); return true;
}

// ===================== Chip-8 Emulator =====================
#include "chip8.hpp"
static const int C8_W = 64, C8_H = 32, C8_SCL = 12;
static const uint32_t C8_ON = 0x00CC44FF, C8_OFF = 0x001A0022;
static Chip8 c8; static bool c8_run, c8_keys[256]; static int c8_steps = 600;
static int c8_kmap[16]; // Chip-8 hex key -> VK code
static char c8_rom_path[1024];

static void c8_setup_keys(ConsoleEntry& ce) {
    const char* hex = "0123456789ABCDEF";
    int defaults[16] = {'X','1','2','3','4','Q','W','E','R','A','S','D','F','Z','C','V'};
    for (int i = 0; i < 16; i++)
        c8_kmap[i] = ce.get_key(&hex[i], defaults[i]);
}

static LRESULT CALLBACK c8_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN: {
            c8_keys[w & 0xFF] = true; int k = w & 0xFF; for (int i=0;i<16;i++) if (c8_kmap[i]==k) c8.keys[i]=true;
            if (w == VK_F5) save_c8_state(state_path(c8_rom_path, 0), &c8);
            if (w == VK_F7) load_c8_state(state_path(c8_rom_path, 0), &c8);
            return 0;
        }
        case WM_KEYUP: { c8_keys[w & 0xFF] = false; int k = w & 0xFF; for (int i=0;i<16;i++) if (c8_kmap[i]==k) c8.keys[i]=false; return 0; }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT r; GetClientRect(h, &r); int ww=r.right, hh=r.bottom;
            int sc = ww/C8_W < hh/C8_H ? ww/C8_W : hh/C8_H;
            int ox = (ww - C8_W * sc) / 2;
            int oy = (hh - C8_H * sc) / 2;
            for (int py=0;py<C8_H;py++) for (int px=0;px<C8_W;px++) {
                uint32_t cl = c8.framebuffer[py*C8_W+px] ? C8_ON : C8_OFF;
                HBRUSH br = CreateSolidBrush(RGB(cl>>24,(cl>>16)&0xFF,(cl>>8)&0xFF));
                RECT cr = {ox+px*sc, oy+py*sc, ox+(px+1)*sc, oy+(py+1)*sc}; FillRect(dc, &cr, br); DeleteObject(br);
            }
            EndPaint(h, &ps); return 0;
        }
        case WM_DESTROY: c8_run = false; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static bool run_chip8(const char* path, HINSTANCE hi, ConsoleEntry& ce) {
    static bool cr = false;
    if (!cr) {
        WNDCLASSA wc = {0}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = c8_wp;
        wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "C8Class";
        RegisterClassA(&wc); cr = true;
    }
    c8.init(); if (!c8.load(path)) return false;
    strcpy(c8_rom_path, path);
    memset(c8_keys, 0, sizeof(c8_keys)); c8_run = true; c8_setup_keys(ce); srand(GetTickCount());
    AudioOut c8_audio; c8_audio.open();

    RECT wr = {0,0,C8_W*C8_SCL,C8_H*C8_SCL};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    const char* p = strrchr(path, '\\'); char t[256]; snprintf(t, sizeof(t), "Chip-8 - %s", p ? p+1 : path);
    HWND hw = CreateWindowExA(0, "C8Class", t, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right-wr.left, wr.bottom-wr.top, NULL, NULL, hi, NULL);
    if (!hw) return false;
    ShowWindow(hw, SW_SHOW); UpdateWindow(hw);

    LARGE_INTEGER f, l; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&l);
    const double ft = 1.0/60.0; MSG msg;
    while (c8_run && GetMessageA(&msg, NULL, 0, 0)) {
        do { TranslateMessage(&msg); DispatchMessageA(&msg); } while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
        if (!c8_run || !IsWindow(hw)) break;
        for (int i = 0; i < c8_steps; i++) c8.step();
        InvalidateRect(hw, NULL, FALSE); UpdateWindow(hw);

        {   // Generate beep if sound timer active
            int16_t abuf[AUDIO_SAMPLES];
            static int c8_beep_phase = 0;
            for (int i = 0; i < AUDIO_SAMPLES; i++) {
                if (c8.sound_timer > 0) {
                    abuf[i] = (c8_beep_phase & 0x10000) ? 8000 : -8000;
                    c8_beep_phase += 440 * 65536 / AUDIO_SR;
                } else {
                    abuf[i] = 0;
                }
            }
            c8_audio.push(abuf, AUDIO_SAMPLES);
        }

        LARGE_INTEGER n; QueryPerformanceCounter(&n);
        double e = (double)(n.QuadPart - l.QuadPart) / f.QuadPart;
        double sl = (ft - e) * 1000.0; if (sl > 5) Sleep((DWORD)sl);
        QueryPerformanceCounter(&l);
    }
    c8_audio.close(); DestroyWindow(hw); return true;
}

// ===================== Space Invaders Emulator =====================
static const int SI_W = Invaders8080::W, SI_H = Invaders8080::H, SI_SCL = 3;
static Invaders8080 si; static bool si_run;
static int si_k_left, si_k_right, si_k_fire, si_k_coin, si_k_1p, si_k_2p;
static char si_rom_path[1024];
static AudioOut si_audio;

static LRESULT CALLBACK si_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN: {
            int k = w & 0xFF;
            if (k == si_k_left) si.in0 &= ~0x10;
            else if (k == si_k_right) si.in0 &= ~0x20;
            else if (k == si_k_fire) si.in0 &= ~0x08;
            else if (k == si_k_coin) si.in0 &= ~0x01;
            else if (k == si_k_1p) si.in0 &= ~0x04;
            else if (k == si_k_2p) si.in0 &= ~0x02;
            if (w == VK_F5) save_si_state(state_path(si_rom_path, 0), &si);
            if (w == VK_F7) load_si_state(state_path(si_rom_path, 0), &si);
            return 0;
        }
        case WM_KEYUP: {
            int k = w & 0xFF;
            if (k == si_k_left) si.in0 |= 0x10;
            else if (k == si_k_right) si.in0 |= 0x20;
            else if (k == si_k_fire) si.in0 |= 0x08;
            else if (k == si_k_coin) si.in0 |= 0x01;
            else if (k == si_k_1p) si.in0 |= 0x04;
            else if (k == si_k_2p) si.in0 |= 0x02;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT r; GetClientRect(h, &r); int ww = r.right, hh = r.bottom;
            int sc = ww / SI_W < hh / SI_H ? ww / SI_W : hh / SI_H;
            int ox = (ww - SI_W * sc) / 2, oy = (hh - SI_H * sc) / 2;
            for (int py = 0; py < SI_H; py++) for (int px = 0; px < SI_W; px++) {
                uint32_t cl = si.framebuffer[py * SI_W + px];
                HBRUSH br = CreateSolidBrush(RGB(cl >> 16 & 0xFF, cl >> 8 & 0xFF, cl & 0xFF));
                RECT cr = {ox + px * sc, oy + py * sc, ox + (px + 1) * sc, oy + (py + 1) * sc};
                FillRect(dc, &cr, br); DeleteObject(br);
            }
            EndPaint(h, &ps); return 0;
        }
        case WM_DESTROY: si_run = false; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static bool run_invaders(const char* path, HINSTANCE hi, ConsoleEntry& ce) {
    static bool cr = false;
    if (!cr) {
        WNDCLASSA wc = {0}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = si_wp;
        wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "SIClass";
        RegisterClassA(&wc); cr = true;
    }
    si.init(); if (!si.load(path)) return false;
    strcpy(si_rom_path, path);
    memset(&si.in0, 0, sizeof(si.in0)); memset(&si.in1, 0, sizeof(si.in1)); memset(&si.in2, 0, sizeof(si.in2));
    si.in0 = si.in1 = 0xFF; si.in2 = 0x00;
    si_run = true;
    si_k_left = ce.get_key("Left", VK_LEFT); si_k_right = ce.get_key("Right", VK_RIGHT);
    si_k_fire = ce.get_key("Fire", 'Z'); si_k_coin = ce.get_key("Coin", '5');
    si_k_1p = ce.get_key("1P", '1'); si_k_2p = ce.get_key("2P", '2');
    si_audio.open();

    RECT wr = {0, 0, SI_W * SI_SCL, SI_H * SI_SCL};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    const char* p = strrchr(path, '\\');
    char t[256]; snprintf(t, sizeof(t), "Space Invaders - %s", p ? p + 1 : path);
    HWND hw = CreateWindowExA(0, "SIClass", t, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hi, NULL);
    if (!hw) return false;
    ShowWindow(hw, SW_SHOW); UpdateWindow(hw);

    LARGE_INTEGER f, l; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&l);
    const double ft = 1.0 / 60.0; MSG msg;
    while (si_run && GetMessageA(&msg, NULL, 0, 0)) {
        do { TranslateMessage(&msg); DispatchMessageA(&msg); } while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
        if (!si_run || !IsWindow(hw)) break;
        for (int i = 0; i < 30000; i++) si.step();
        si.int_pending = true;
        si.render();
        {   int16_t abuf[AUDIO_SAMPLES]; si.gen_audio(abuf, AUDIO_SAMPLES, AUDIO_SR); si_audio.push(abuf, AUDIO_SAMPLES); }
        InvalidateRect(hw, NULL, FALSE); UpdateWindow(hw);
        LARGE_INTEGER n; QueryPerformanceCounter(&n);
        double e = (double)(n.QuadPart - l.QuadPart) / f.QuadPart;
        double sl = (ft - e) * 1000.0; if (sl > 0) Sleep((DWORD)sl);
        QueryPerformanceCounter(&l);
    }
    si_audio.close(); DestroyWindow(hw); return true;
}

// ===================== Atari 2600 Emulator =====================
static const int A26_W = 160, A26_H = 192, A26_SCL = 4;
static A2600 a26; static bool a26_run;
static int a26_k_up, a26_k_down, a26_k_left, a26_k_right, a26_k_fire, a26_k_sel, a26_k_res;
static char a26_rom_path[1024];
static AudioOut a26_audio;

static LRESULT CALLBACK a26_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN: {
            int k = w & 0xFF;
            if (k == a26_k_up) a26.swcha &= ~0x08;
            else if (k == a26_k_down) a26.swcha &= ~0x04;
            else if (k == a26_k_left) a26.swcha &= ~0x02;
            else if (k == a26_k_right) a26.swcha &= ~0x01;
            else if (k == a26_k_fire) a26.inpt4 = false;
            else if (k == a26_k_sel) a26.swchb |= 0x02;
            else if (k == a26_k_res) a26.swchb &= ~0x01;
            if (w == VK_F5) save_a26_state(state_path(a26_rom_path, 0), &a26);
            if (w == VK_F7) load_a26_state(state_path(a26_rom_path, 0), &a26);
            return 0;
        }
        case WM_KEYUP: {
            int k = w & 0xFF;
            if (k == a26_k_up) a26.swcha |= 0x08;
            else if (k == a26_k_down) a26.swcha |= 0x04;
            else if (k == a26_k_left) a26.swcha |= 0x02;
            else if (k == a26_k_right) a26.swcha |= 0x01;
            else if (k == a26_k_fire) a26.inpt4 = true;
            else if (k == a26_k_sel) a26.swchb &= ~0x02;
            else if (k == a26_k_res) a26.swchb |= 0x01;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT r; GetClientRect(h, &r); int ww = r.right, hh = r.bottom;
            int sc = ww / A26_W < hh / A26_H ? ww / A26_W : hh / A26_H;
            int ox = (ww - A26_W * sc) / 2, oy = (hh - A26_H * sc) / 2;
            for (int py = 0; py < A26_H; py++) for (int px = 0; px < A26_W; px++) {
                uint32_t cl = a26.framebuffer[py * A26_W + px];
                HBRUSH br = CreateSolidBrush(RGB(cl >> 16 & 0xFF, cl >> 8 & 0xFF, cl & 0xFF));
                RECT cr = {ox + px * sc, oy + py * sc, ox + (px + 1) * sc, oy + (py + 1) * sc};
                FillRect(dc, &cr, br); DeleteObject(br);
            }
            EndPaint(h, &ps); return 0;
        }
        case WM_DESTROY: a26_run = false; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static bool run_a2600(const char* path, HINSTANCE hi, ConsoleEntry& ce) {
    static bool cr = false;
    if (!cr) {
        WNDCLASSA wc = {0}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = a26_wp;
        wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "A26Class";
        RegisterClassA(&wc); cr = true;
    }
    a26.init(); if (!a26.load(path)) return false;
    strcpy(a26_rom_path, path);
    a26_run = true;
    a26.swcha = 0xFF; a26.swchb = 0xFF; a26.inpt4 = a26.inpt5 = true;
    a26_k_up = ce.get_key("Up", VK_UP); a26_k_down = ce.get_key("Down", VK_DOWN);
    a26_k_left = ce.get_key("Left", VK_LEFT); a26_k_right = ce.get_key("Right", VK_RIGHT);
    a26_k_fire = ce.get_key("Fire", VK_SPACE); a26_k_sel = ce.get_key("Select", VK_F1); a26_k_res = ce.get_key("Reset", VK_F2);
    a26_audio.open();

    RECT wr = {0, 0, A26_W * A26_SCL, A26_H * A26_SCL};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    const char* p = strrchr(path, '\\');
    char t[256]; snprintf(t, sizeof(t), "Atari 2600 - %s", p ? p + 1 : path);
    HWND hw = CreateWindowExA(0, "A26Class", t, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hi, NULL);
    if (!hw) return false;
    ShowWindow(hw, SW_SHOW); UpdateWindow(hw);

    LARGE_INTEGER f, l; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&l);
    const double ft = 1.0 / 60.0; MSG msg;
    while (a26_run && GetMessageA(&msg, NULL, 0, 0)) {
        do { TranslateMessage(&msg); DispatchMessageA(&msg); } while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
        if (!a26_run || !IsWindow(hw)) break;
        a26.run_frame();
        {   int16_t abuf[AUDIO_SAMPLES]; a26.gen_audio(abuf, AUDIO_SAMPLES, AUDIO_SR); a26_audio.push(abuf, AUDIO_SAMPLES); }
        InvalidateRect(hw, NULL, FALSE); UpdateWindow(hw);
        LARGE_INTEGER n; QueryPerformanceCounter(&n);
        double e = (double)(n.QuadPart - l.QuadPart) / f.QuadPart;
        double sl = (ft - e) * 1000.0; if (sl > 0) Sleep((DWORD)sl);
        QueryPerformanceCounter(&l);
    }
    a26_audio.close(); DestroyWindow(hw); return true;
}

// ===================== NES Emulator =====================
static const int NES_W = 256, NES_H = 240, NES_SCL = 3;
static NES nes; static bool nes_run;
static int nes_k_up, nes_k_down, nes_k_left, nes_k_right, nes_k_a, nes_k_b, nes_k_start, nes_k_select;
static char nes_rom_path[1024];
static AudioOut nes_audio;

static LRESULT CALLBACK nes_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN: {
            int k = w & 0xFF;
            if (k == nes_k_up) nes.joy1[0] = true;
            else if (k == nes_k_down) nes.joy1[1] = true;
            else if (k == nes_k_left) nes.joy1[2] = true;
            else if (k == nes_k_right) nes.joy1[3] = true;
            else if (k == nes_k_a) nes.joy1[4] = true;
            else if (k == nes_k_b) nes.joy1[5] = true;
            else if (k == nes_k_start) nes.joy1[6] = true;
            else if (k == nes_k_select) nes.joy1[7] = true;
            if (w == VK_F5) /* save state not implemented */;
            if (w == VK_F7) /* load state not implemented */;
            return 0;
        }
        case WM_KEYUP: {
            int k = w & 0xFF;
            if (k == nes_k_up) nes.joy1[0] = false;
            else if (k == nes_k_down) nes.joy1[1] = false;
            else if (k == nes_k_left) nes.joy1[2] = false;
            else if (k == nes_k_right) nes.joy1[3] = false;
            else if (k == nes_k_a) nes.joy1[4] = false;
            else if (k == nes_k_b) nes.joy1[5] = false;
            else if (k == nes_k_start) nes.joy1[6] = false;
            else if (k == nes_k_select) nes.joy1[7] = false;
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
            RECT r; GetClientRect(h, &r); int ww = r.right, hh = r.bottom;
            int sc = ww / NES_W < hh / NES_H ? ww / NES_W : hh / NES_H;
            int ox = (ww - NES_W * sc) / 2, oy = (hh - NES_H * sc) / 2;
            for (int py = 0; py < NES_H; py++) for (int px = 0; px < NES_W; px++) {
                uint32_t cl = nes.framebuffer[py * NES_W + px];
                HBRUSH br = CreateSolidBrush(RGB(cl >> 16 & 0xFF, cl >> 8 & 0xFF, cl & 0xFF));
                RECT cr = {ox + px * sc, oy + py * sc, ox + (px + 1) * sc, oy + (py + 1) * sc};
                FillRect(dc, &cr, br); DeleteObject(br);
            }
            EndPaint(h, &ps); return 0;
        }
        case WM_DESTROY: nes_run = false; return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static bool run_nes(const char* path, HINSTANCE hi, ConsoleEntry& ce) {
    static bool cr = false;
    if (!cr) {
        WNDCLASSA wc = {0}; wc.style = CS_HREDRAW | CS_VREDRAW; wc.lpfnWndProc = nes_wp;
        wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = "NESClass";
        RegisterClassA(&wc); cr = true;
    }
    nes.init(); if (!nes.load(path)) return false;
    strcpy(nes_rom_path, path);
    nes_run = true;
    for (int i = 0; i < 8; i++) nes.joy1[i] = false;
    nes_k_up = ce.get_key("Up", VK_UP); nes_k_down = ce.get_key("Down", VK_DOWN);
    nes_k_left = ce.get_key("Left", VK_LEFT); nes_k_right = ce.get_key("Right", VK_RIGHT);
    nes_k_a = ce.get_key("A", 'X'); nes_k_b = ce.get_key("B", 'Z');
    nes_k_start = ce.get_key("Start", VK_RETURN); nes_k_select = ce.get_key("Select", VK_BACK);

    // Wait for PPU to stabilize for a few frames
    for (int i = 0; i < 60; i++) nes.run_frame();
    nes.frame_done = false;

    nes_audio.open();

    RECT wr = {0, 0, NES_W * NES_SCL, NES_H * NES_SCL};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME, FALSE);
    const char* p = strrchr(path, '\\');
    char t[256]; snprintf(t, sizeof(t), "NES - %s", p ? p + 1 : path);
    HWND hw = CreateWindowExA(0, "NESClass", t, WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right - wr.left, wr.bottom - wr.top, NULL, NULL, hi, NULL);
    if (!hw) return false;
    ShowWindow(hw, SW_SHOW); UpdateWindow(hw);

    LARGE_INTEGER f, l; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&l);
    const double ft = 1.0 / 60.0; MSG msg;
    while (nes_run && GetMessageA(&msg, NULL, 0, 0)) {
        do { TranslateMessage(&msg); DispatchMessageA(&msg); } while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE));
        if (!nes_run || !IsWindow(hw)) break;
        nes.run_frame();
        { int16_t abuf[AUDIO_SAMPLES]; nes.gen_audio(abuf, AUDIO_SAMPLES, AUDIO_SR); nes_audio.push(abuf, AUDIO_SAMPLES); }
        InvalidateRect(hw, NULL, FALSE); UpdateWindow(hw);
        LARGE_INTEGER n; QueryPerformanceCounter(&n);
        double e = (double)(n.QuadPart - l.QuadPart) / f.QuadPart;
        double sl = (ft - e) * 1000.0; if (sl > 0) Sleep((DWORD)sl);
        QueryPerformanceCounter(&l);
    }
    nes_audio.close(); DestroyWindow(hw); return true;
}

// ===================== Config =====================
static const char* CFG_FILE = "consoles.cfg";
static const char* LAUNCHER_TITLE = "Console Launcher";
static std::vector<ConsoleEntry> consoles;
static HWND hList, hBtnLaunch, hBtnAdd, hBtnRemove, hBtnSettings;
static char cur_path[1024];

static std::string cfg_path() {
    GetModuleFileNameA(NULL, cur_path, sizeof(cur_path));
    char* p = strrchr(cur_path, '\\'); if (p) *p = 0;
    return std::string(cur_path) + "\\" + CFG_FILE;
}

static void save_cfg() {
    FILE* f = fopen(cfg_path().c_str(), "w");
    if (!f) return;
    fprintf(f, "# Console Launcher Configuration\ncount=%zu\n\n", consoles.size());
    for (size_t i = 0; i < consoles.size(); i++) {
        auto& c = consoles[i];
        fprintf(f, "[%zu]\nname=%s\nemulator=%s\nextensions=%s\nfilter=%s\n", i, c.name.c_str(), c.emulator.c_str(), c.extensions.c_str(), c.filter.c_str());
        fprintf(f, "key_count=%zu\n", c.key_bindings.size());
        for (size_t j = 0; j < c.key_bindings.size(); j++)
            fprintf(f, "key_%zu_name=%s\nkey_%zu_val=%d\n", j, c.key_bindings[j].action.c_str(), j, c.key_bindings[j].key);
        fprintf(f, "\n");
    }
    fclose(f);
}

static void load_cfg() {
    consoles.clear();
    FILE* f = fopen(cfg_path().c_str(), "r");
    if (!f) return;
    char line[512]; ConsoleEntry cur; bool in = false;
    while (fgets(line, sizeof(line), f)) {
        char* nl = strchr(line, '\n'); if (nl) *nl = 0;
        nl = strchr(line, '\r'); if (nl) *nl = 0;
        if (line[0] == '#' || line[0] == 0) continue;
        if (line[0] == '[') {
            if (in && !cur.name.empty()) consoles.push_back(cur);
            cur = ConsoleEntry(); in = true; continue;
        }
        char k[64], v[448];
        if (sscanf(line, "%63[^=]=%447[^\n]", k, v) == 2) {
            if (!strcmp(k, "name")) cur.name = v;
            else if (!strcmp(k, "emulator")) cur.emulator = v;
            else if (!strcmp(k, "extensions")) cur.extensions = v;
            else if (!strcmp(k, "filter")) cur.filter = v;
            else {
                char pfx[32]; int idx;
                if (sscanf(k, "key_%d_name", &idx) == 1) {
                    if ((int)cur.key_bindings.size() <= idx) cur.key_bindings.resize(idx + 1);
                    cur.key_bindings[idx].action = v;
                }
                if (sscanf(k, "key_%d_val", &idx) == 1) {
                    if ((int)cur.key_bindings.size() <= idx) cur.key_bindings.resize(idx + 1);
                    cur.key_bindings[idx].key = atoi(v);
                }
            }
        }
    }
    if (in && !cur.name.empty()) consoles.push_back(cur);
    fclose(f);
}

static void refr_list() {
    ListView_DeleteAllItems(hList);
    for (size_t i = 0; i < consoles.size(); i++) {
        LVITEMA it = {0}; it.mask = LVIF_TEXT; it.pszText = (char*)consoles[i].name.c_str(); it.iItem = i;
        ListView_InsertItem(hList, &it);
        ListView_SetItemText(hList, i, 1, (char*)consoles[i].emulator.c_str());
        ListView_SetItemText(hList, i, 2, (char*)consoles[i].extensions.c_str());
    }
}

static int sel_idx() { return ListView_GetNextItem(hList, -1, LVNI_SELECTED); }

static bool is_internal(ConsoleEntry& c) { return c.emulator.empty() || c.emulator == "internal"; }

static void launch(int idx, HINSTANCE hi) {
    if (idx < 0 || idx >= (int)consoles.size()) return;
    ConsoleEntry& c = consoles[idx];
    char flt[512];
    if (c.filter.empty())
        snprintf(flt, sizeof(flt), "ROMs (*%s)\0*%s\0All\0*.*\0", c.extensions.c_str(), c.extensions.c_str());
    else
        snprintf(flt, sizeof(flt), "%s\0*%s\0All\0*.*\0", c.filter.c_str(), c.extensions.c_str());

    OPENFILENAMEA of = {0}; char rom[1024] = {0};
    of.lStructSize = sizeof(of); of.hwndOwner = GetParent(hList); of.lpstrFilter = flt;
    of.lpstrFile = rom; of.nMaxFile = sizeof(rom); of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameA(&of)) return;

    if (is_internal(c)) {
        EnableWindow(GetParent(hList), FALSE); bool ok = false;
        if (c.name == "Gameboy") ok = run_gameboy(rom, hi, c);
        else if (c.name == "Chip-8") ok = run_chip8(rom, hi, c);
        else if (c.name == "Space Invaders") ok = run_invaders(rom, hi, c);
        else if (c.name == "Atari 2600") ok = run_a2600(rom, hi, c);
        else if (c.name == "NES") ok = run_nes(rom, hi, c);
        EnableWindow(GetParent(hList), TRUE); SetForegroundWindow(GetParent(hList));
        if (!ok) MessageBoxA(GetParent(hList), "Failed to start emulator.", "Error", MB_OK | MB_ICONERROR);
    } else {
        char ep[1024]; snprintf(ep, sizeof(ep), "%s\\%s", cur_path, c.emulator.c_str());
        if (GetFileAttributesA(ep) == INVALID_FILE_ATTRIBUTES) snprintf(ep, sizeof(ep), "%s", c.emulator.c_str());
        char cmd[2048]; snprintf(cmd, sizeof(cmd), "\"%s\" \"%s\"", ep, rom);
        STARTUPINFOA si = {sizeof(si)}; PROCESS_INFORMATION pi;
        if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, cur_path, &si, &pi)) {
            char buf[512]; snprintf(buf, sizeof(buf), "Failed to launch:\n%s", ep);
            MessageBoxA(GetParent(hList), buf, "Error", MB_OK | MB_ICONERROR);
        } else { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    }
}

static void rem(int idx) {
    if (idx < 0 || idx >= (int)consoles.size()) return;
    char buf[256]; snprintf(buf, sizeof(buf), "Remove \"%s\"?", consoles[idx].name.c_str());
    if (MessageBoxA(GetParent(hList), buf, "Confirm", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        consoles.erase(consoles.begin() + idx); refr_list(); save_cfg();
    }
}

// ===================== Add Console Dialog =====================
static bool add_ok; static char add_name[256], add_emu[256], add_ext[256];
static HWND an, ae, ax;

static LRESULT CALLBACK add_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            HFONT hf = CreateFontA(14,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,0,0,"Segoe UI");
            CreateWindowExA(0,"STATIC","Console Name:",WS_CHILD|WS_VISIBLE,15,15,120,22,h,0,GetModuleHandle(NULL),0);
            an = CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,140,12,250,24,h,0,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"STATIC","Emulator Path:",WS_CHILD|WS_VISIBLE,15,50,120,22,h,0,GetModuleHandle(NULL),0);
            ae = CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,140,47,200,24,h,0,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"BUTTON","Browse...",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,348,47,50,24,h,(HMENU)104,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"STATIC","ROM Ext:",WS_CHILD|WS_VISIBLE,15,85,120,22,h,0,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"STATIC","(blank emulator = built-in)",WS_CHILD|WS_VISIBLE,140,105,250,16,h,0,GetModuleHandle(NULL),0);
            ax = CreateWindowExA(WS_EX_CLIENTEDGE,"EDIT","",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,140,82,250,24,h,0,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"BUTTON","OK",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,190,140,80,32,h,(HMENU)IDOK,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"BUTTON","Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,290,140,80,32,h,(HMENU)IDCANCEL,GetModuleHandle(NULL),0);
            if (hf) { SendMessage(an,WM_SETFONT,(WPARAM)hf,1); SendMessage(ae,WM_SETFONT,(WPARAM)hf,1); SendMessage(ax,WM_SETFONT,(WPARAM)hf,1); }
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK) {
                GetWindowTextA(an, add_name, 256); GetWindowTextA(ae, add_emu, 256); GetWindowTextA(ax, add_ext, 256);
                if (!add_name[0] || !add_ext[0]) { MessageBoxA(h, "Name and extensions required.", "Error", MB_OK|MB_ICONWARNING); return 0; }
                add_ok = true; DestroyWindow(h);
            } else if (LOWORD(w) == IDCANCEL) { add_ok = false; DestroyWindow(h); }
            else if (LOWORD(w) == 104) {
                char buf[256] = {0}; OPENFILENAMEA of = {0};
                of.lStructSize = sizeof(of); of.hwndOwner = h; of.lpstrFilter = "*.exe\0*.exe\0All\0*.*\0";
                of.lpstrFile = buf; of.nMaxFile = sizeof(buf); of.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                if (GetOpenFileNameA(&of)) SetWindowTextA(ae, buf);
            }
            return 0;
        case WM_CLOSE: add_ok = false; DestroyWindow(h); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void show_add(HWND p) {
    add_ok = false; add_name[0] = add_emu[0] = add_ext[0] = 0;
    WNDCLASSA ac = {0}; ac.lpfnWndProc = add_wp; ac.hInstance = GetModuleHandle(NULL);
    ac.hCursor = LoadCursor(NULL, IDC_ARROW); ac.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    ac.lpszClassName = "AddCls"; RegisterClassA(&ac);
    RECT pr; GetWindowRect(p, &pr);
    HWND h = CreateWindowExA(0, "AddCls", "Add Console", WS_CAPTION|WS_SYSMENU, pr.left+50, pr.top+50, 420, 220, p, 0, GetModuleHandle(NULL), 0);
    if (!h) return;
    EnableWindow(p, FALSE); ShowWindow(h, SW_SHOW);
    MSG msg; while (GetMessageA(&msg, NULL, 0, 0)) { if (!IsWindow(h)) break; if (!IsDialogMessage(h,&msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); } }
    EnableWindow(p, TRUE); SetForegroundWindow(p); UnregisterClassA("AddCls", GetModuleHandle(NULL));
    if (add_ok) {
        ConsoleEntry c; c.name = add_name; c.emulator = add_emu[0] ? add_emu : "internal"; c.extensions = add_ext;
        std::string ef; char t[256]; strcpy(t, add_ext); char* tok = strtok(t, ";");
        while (tok) { if (!ef.empty()) ef += ";"; ef += "*" + std::string(tok); tok = strtok(NULL, ";"); }
        c.filter = c.name + " ROMs (" + ef + ")";
        if (c.name == "Gameboy") set_default_bindings(c, GB_ACTIONS, GB_DEFAULT_KEYS);
        else if (c.name == "Chip-8") set_default_bindings(c, C8_ACTIONS, C8_DEFAULT_KEYS);
        else if (c.name == "Space Invaders") set_default_bindings(c, SI_ACTIONS, SI_DEFAULT_KEYS);
        else if (c.name == "Atari 2600") set_default_bindings(c, A26_ACTIONS, A26_DEFAULT_KEYS);
        else if (c.name == "NES") set_default_bindings(c, NES_ACTIONS, NES_DEFAULT_KEYS);
        consoles.push_back(c); refr_list(); save_cfg();
    }
}

// ===================== Settings Dialog =====================
static ConsoleEntry* set_entry = nullptr;
static int set_capturing = -1;
static HWND set_hwnd, set_list;
static int set_result;

static const char* key_display(int k) {
    static char b[32];
    if (k >= 'A' && k <= 'Z') { b[0]=k; b[1]=0; return b; }
    if (k >= '0' && k <= '9') { b[0]=k; b[1]=0; return b; }
    switch (k) {
        case VK_UP: return "Up"; case VK_DOWN: return "Down";
        case VK_LEFT: return "Left"; case VK_RIGHT: return "Right";
        case VK_RETURN: return "Enter"; case VK_BACK: return "Backspace";
        case VK_SPACE: return "Space"; case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl"; case VK_MENU: return "Alt";
        case VK_TAB: return "Tab"; case VK_ESCAPE: return "Esc";
        case VK_DELETE: return "Del"; case VK_HOME: return "Home";
        case VK_END: return "End"; case VK_PRIOR: return "PgUp";
        case VK_NEXT: return "PgDn"; case VK_INSERT: return "Ins";
        case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
        case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
        case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
        case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
        default: snprintf(b,sizeof(b),"Key%d",k); return b;
    }
}

static void refr_set_list() {
    ListView_DeleteAllItems(set_list);
    if (!set_entry) return;
    for (size_t i = 0; i < set_entry->key_bindings.size(); i++) {
        auto& kb = set_entry->key_bindings[i];
        char act[64], kname[32];
        strcpy(act, kb.action.c_str());
        if ((int)i == set_capturing) strcpy(kname, "[ Press Key... ]");
        else strcpy(kname, key_display(kb.key));
        LVITEMA it = {0}; it.mask = LVIF_TEXT; it.pszText = act; it.iItem = i;
        ListView_InsertItem(set_list, &it);
        ListView_SetItemText(set_list, i, 1, kname);
    }
}

static LRESULT CALLBACK set_wp(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            set_capturing = -1;
            CreateWindowExA(0,"STATIC","Click a row, then press the key to bind.",WS_CHILD|WS_VISIBLE,10,10,380,20,h,0,GetModuleHandle(NULL),0);
            set_list = CreateWindowExA(0, WC_LISTVIEWA, "", WS_CHILD|WS_VISIBLE|LVS_REPORT, 10, 35, 380, 220, h, 0, GetModuleHandle(NULL), 0);
            LVCOLUMNA col = {0}; col.mask = LVCF_TEXT|LVCF_WIDTH;
            col.cx = 150; col.pszText = (char*)"Action"; ListView_InsertColumn(set_list, 0, &col);
            col.cx = 200; col.pszText = (char*)"Key"; ListView_InsertColumn(set_list, 1, &col);
            ListView_SetExtendedListViewStyle(set_list, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
            CreateWindowExA(0,"BUTTON","OK",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,120,270,80,32,h,(HMENU)IDOK,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"BUTTON","Cancel",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,220,270,80,32,h,(HMENU)IDCANCEL,GetModuleHandle(NULL),0);
            CreateWindowExA(0,"BUTTON","Reset to Default",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10,270,100,32,h,(HMENU)100,GetModuleHandle(NULL),0);

            HFONT hf = CreateFontA(14,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,0,0,"Segoe UI");
            if (hf) SendMessage(set_list, WM_SETFONT, (WPARAM)hf, 1);
            refr_set_list();
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(w) == IDOK) { set_result = 1; DestroyWindow(h); }
            else if (LOWORD(w) == IDCANCEL) { set_result = 0; DestroyWindow(h); }
            else if (LOWORD(w) == 100) {
                // Reset defaults
                if (set_entry) {
                    set_entry->key_bindings.clear();
                    if (set_entry->name == "Gameboy") set_default_bindings(*set_entry, GB_ACTIONS, GB_DEFAULT_KEYS);
                    else if (set_entry->name == "Chip-8") set_default_bindings(*set_entry, C8_ACTIONS, C8_DEFAULT_KEYS);
                    else if (set_entry->name == "Space Invaders") set_default_bindings(*set_entry, SI_ACTIONS, SI_DEFAULT_KEYS);
                    else if (set_entry->name == "Atari 2600") set_default_bindings(*set_entry, A26_ACTIONS, A26_DEFAULT_KEYS);
                    else if (set_entry->name == "NES") set_default_bindings(*set_entry, NES_ACTIONS, NES_DEFAULT_KEYS);
                    set_capturing = -1;
                    refr_set_list();
                }
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)l;
            if (nm->hwndFrom == set_list && nm->code == NM_DBLCLK) {
                int idx = ListView_GetNextItem(set_list, -1, LVNI_SELECTED);
                if (idx >= 0 && idx < (int)set_entry->key_bindings.size()) {
                    set_capturing = idx;
                    refr_set_list();
                    SetFocus(set_list);
                }
            }
            return 0;
        }
        case WM_KEYDOWN: {
            if (set_capturing >= 0 && set_entry && set_capturing < (int)set_entry->key_bindings.size()) {
                int vk = w & 0xFF;
                if (vk == VK_ESCAPE) { set_capturing = -1; refr_set_list(); return 0; }

                // Check if another action already uses this key
                int conflict = -1;
                for (size_t i = 0; i < set_entry->key_bindings.size(); i++) {
                    if ((int)i != set_capturing && set_entry->key_bindings[i].key == vk) {
                        conflict = i; break;
                    }
                }

                if (conflict >= 0) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "'%s' already uses '%s'.\n\nStill rebind '%s' to '%s'?",
                             set_entry->key_bindings[conflict].action.c_str(),
                             key_display(vk),
                             set_entry->key_bindings[set_capturing].action.c_str(),
                             key_display(vk));

                    if (MessageBoxA(h, buf, "Key Conflict", MB_YESNO | MB_ICONWARNING) == IDYES) {
                        set_entry->key_bindings[set_capturing].key = vk;

                        char buf2[512];
                        snprintf(buf2, sizeof(buf2), "Would you like to change '%s' key?",
                                 set_entry->key_bindings[conflict].action.c_str());

                        if (MessageBoxA(h, buf2, "Change Conflicting Key", MB_YESNO | MB_ICONQUESTION) == IDYES)
                            set_capturing = conflict;
                        else
                            set_capturing = -1;
                    }
                } else {
                    set_entry->key_bindings[set_capturing].key = vk;
                    set_capturing = -1;
                }

                refr_set_list();
                return 0;
            }
            return DefWindowProcA(h, m, w, l);
        }
        case WM_CLOSE: set_result = 0; DestroyWindow(h); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void show_settings(HWND p, int idx) {
    if (idx < 0 || idx >= (int)consoles.size()) return;
    set_entry = &consoles[idx]; set_result = 0;
    WNDCLASSA sc = {0}; sc.lpfnWndProc = set_wp; sc.hInstance = GetModuleHandle(NULL);
    sc.hCursor = LoadCursor(NULL, IDC_ARROW); sc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    sc.lpszClassName = "SetCls"; RegisterClassA(&sc);
    RECT pr; GetWindowRect(p, &pr);
    char title[256]; snprintf(title, sizeof(title), "Controls - %s", set_entry->name.c_str());
    HWND h = CreateWindowExA(0, "SetCls", title, WS_CAPTION|WS_SYSMENU, pr.left+30, pr.top+30, 420, 350, p, 0, GetModuleHandle(NULL), 0);
    if (!h) return;
    EnableWindow(p, FALSE); ShowWindow(h, SW_SHOW);
    MSG msg; while (GetMessageA(&msg, NULL, 0, 0)) {
        if (!IsWindow(h)) break;
        // Skip IsDialogMessage while capturing keys so WM_KEYDOWN reaches our handler
        if (set_capturing >= 0 || !IsDialogMessage(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
    EnableWindow(p, TRUE); SetForegroundWindow(p); UnregisterClassA("SetCls", GetModuleHandle(NULL));
    if (set_result) save_cfg();
}

// ===================== Main Window =====================
static void cr_cols(HWND hl) {
    LVCOLUMNA col = {0}; col.mask = LVCF_TEXT|LVCF_WIDTH;
    col.cx = 180; col.pszText = (char*)"Console"; ListView_InsertColumn(hl, 0, &col);
    col.cx = 160; col.pszText = (char*)"Emulator"; ListView_InsertColumn(hl, 1, &col);
    col.cx = 120; col.pszText = (char*)"Extensions"; ListView_InsertColumn(hl, 2, &col);
    ListView_SetExtendedListViewStyle(hl, LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
}

LRESULT CALLBACK main_wp(HWND hw, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_CREATE: {
            hList = CreateWindowExA(0, WC_LISTVIEWA, "", WS_CHILD|WS_VISIBLE|LVS_REPORT, 10, 10, 460, 280, hw, 0, GetModuleHandle(NULL), 0);
            cr_cols(hList);
            hBtnLaunch = CreateWindowExA(0,"BUTTON","Launch",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,10,300,100,36,hw,(HMENU)1,GetModuleHandle(NULL),0);
            hBtnSettings = CreateWindowExA(0,"BUTTON","Settings",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,120,300,100,36,hw,(HMENU)4,GetModuleHandle(NULL),0);
            hBtnAdd = CreateWindowExA(0,"BUTTON","Add Console",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,230,300,120,36,hw,(HMENU)2,GetModuleHandle(NULL),0);
            hBtnRemove = CreateWindowExA(0,"BUTTON","Remove",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,360,300,100,36,hw,(HMENU)3,GetModuleHandle(NULL),0);
            HFONT hf = CreateFontA(15,0,0,0,FW_NORMAL,0,0,0,ANSI_CHARSET,0,0,0,0,"Segoe UI");
            if (hf) { SendMessage(hList,WM_SETFONT,(WPARAM)hf,1); SendMessage(hBtnLaunch,WM_SETFONT,(WPARAM)hf,1); SendMessage(hBtnSettings,WM_SETFONT,(WPARAM)hf,1); SendMessage(hBtnAdd,WM_SETFONT,(WPARAM)hf,1); SendMessage(hBtnRemove,WM_SETFONT,(WPARAM)hf,1); }
            load_cfg(); refr_list();
            if (consoles.empty()) {
                ConsoleEntry d;
                d.name="Gameboy"; d.emulator="internal"; d.extensions=".gb;.gbc"; d.filter="Gameboy ROMs (*.gb;*.gbc)";
                set_default_bindings(d, GB_ACTIONS, GB_DEFAULT_KEYS); consoles.push_back(d);
                d.name="Chip-8"; d.emulator="internal"; d.extensions=".ch8;.c8b"; d.filter="Chip-8 ROMs (*.ch8;*.c8b)";
                set_default_bindings(d, C8_ACTIONS, C8_DEFAULT_KEYS); consoles.push_back(d);
                d.name="Space Invaders"; d.emulator="internal"; d.extensions=".inv;.rom;.bin"; d.filter="Space Invaders ROMs (*.inv;*.rom;*.bin)";
                set_default_bindings(d, SI_ACTIONS, SI_DEFAULT_KEYS); consoles.push_back(d);
                d.name="Atari 2600"; d.emulator="internal"; d.extensions=".a26;.bin;.rom"; d.filter="Atari 2600 ROMs (*.a26;*.bin;*.rom)";
                set_default_bindings(d, A26_ACTIONS, A26_DEFAULT_KEYS); consoles.push_back(d);
                d.name="NES"; d.emulator="internal"; d.extensions=".nes;.rom"; d.filter="NES ROMs (*.nes;*.rom)";
                set_default_bindings(d, NES_ACTIONS, NES_DEFAULT_KEYS); consoles.push_back(d);
                save_cfg(); refr_list();
            }
            return 0;
        }
        case WM_SIZE: {
            int ww = LOWORD(l), hh = HIWORD(l);
            SetWindowPos(hList, NULL, 10, 10, ww-20, hh-90, SWP_NOZORDER);
            int y = hh - 50;
            SetWindowPos(hBtnLaunch, NULL, 10, y, 100, 36, SWP_NOZORDER);
            SetWindowPos(hBtnSettings, NULL, 120, y, 100, 36, SWP_NOZORDER);
            SetWindowPos(hBtnAdd, NULL, 230, y, 120, 36, SWP_NOZORDER);
            SetWindowPos(hBtnRemove, NULL, 360, y, 100, 36, SWP_NOZORDER);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(w)) {
                case 1: launch(sel_idx(), GetModuleHandle(NULL)); break;
                case 2: show_add(hw); break;
                case 3: rem(sel_idx()); break;
                case 4: {
                    int i = sel_idx();
                    if (i >= 0) show_settings(hw, i);
                    else MessageBoxA(hw, "Select a console first.", "Settings", MB_OK|MB_ICONINFORMATION);
                    break;
                }
            }
            return 0;
        case WM_NOTIFY: {
            NMHDR* nm = (NMHDR*)l;
            if (nm->idFrom == 0 && nm->code == NM_DBLCLK) launch(sel_idx(), GetModuleHandle(NULL));
            return 0;
        }
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hw, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int ns) {
    INITCOMMONCONTROLSEX ic = {sizeof(ic), ICC_LISTVIEW_CLASSES}; InitCommonControlsEx(&ic);
    WNDCLASSA wc = {0}; wc.style = CS_HREDRAW|CS_VREDRAW; wc.lpfnWndProc = main_wp;
    wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); wc.lpszClassName = "CLClass";
    RegisterClassA(&wc);
    RECT wr = {0,0,500,390}; AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX&~WS_THICKFRAME, FALSE);
    HWND hw = CreateWindowExA(0, "CLClass", LAUNCHER_TITLE, WS_OVERLAPPEDWINDOW&~WS_MAXIMIZEBOX&~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, wr.right-wr.left, wr.bottom-wr.top, NULL, NULL, hi, NULL);
    if (!hw) return 1; ShowWindow(hw, ns); UpdateWindow(hw);
    MSG msg; while (GetMessageA(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    return 0;
}
