# Console Launcher

A multi-system emulator supporting **Gameboy / Gameboy Color**, **Chip-8**, **Space Invaders**, and **Atari 2600** — all in a single portable codebase.

## Systems

| System | Extension | Description |
|--------|-----------|-------------|
| **Gameboy / GBC** | `.gb`, `.gbc` | Full DMG + CGB emulation with MBC1/2/3/5 mapper support |
| **Chip-8** | `.ch8`, `.c8b` | Classic CHIP-8 interpreter with 64x32 display |
| **Space Invaders** | `.inv`, `.rom`, `.bin` | Intel 8080-based arcade emulation |
| **Atari 2600** | `.a26`, `.bin`, `.rom` | 6507-based VCS emulation with TIA |

## Features

### Gameboy / Gameboy Color
- **MBC1, MBC2, MBC3, MBC5** mapper support
- **MBC3 Real-Time Clock** — accurate RTC for Pokemon Crystal and other RTC titles
- **MBC2** — 512-nibble internal RAM with address-bit-8 banking
- **MBC5** — up to 512 ROM banks, 16 RAM banks
- **GBC Color** — full 15-bit RGB palette via CGB registers (FF68/FF69/FF6A/FF6B)
- **GBC Double-Speed** — correct FF4D read/write for speed switching
- **Battery saves** — auto save/load SRAM to `.sav` files
- **Audio** — dual pulse wave channels with sweep, envelope, and length counters

### Chip-8
- Full CHIP-8 instruction set including SCHIP extensions
- 60Hz timing with delay/sound timers
- Audio beep output

### Space Invaders (8080)
- Complete Intel 8080 CPU emulation
- Shift register hardware, port-mapped I/O
- Sound effects: UFO, shoot, explosion, invader hit
- 256×224 monochrome display

### Atari 2600
- 6507 CPU with simplified instruction set
- **TIA collision detection** — proper CXM0P/CXM1P/CXP0FB/CXP1FB/CXM0FB/CXM1FB/CXBLPF/CXPPMM registers
- **Bankswitching** — F8 (2K), F6 (4K), F4 (8K) schemes
- TIA audio with polynomial counters and volume control
- Player/missile/ball/playfield rendering

### General
- **Cheat support** — Game Genie (6/9-letter codes) and GameShark (8-hex codes)
- **Save states** — F5 save, F7 load for all systems (stored as `.st0` files)
- **Configurable key bindings** — via the built-in settings dialog (Windows)

## Building

### Windows (MinGW)

```bash
compile.bat
```

Or manually:

```bash
g++ -std=c++17 -O2 launcher.cpp mmu.cpp -o console_launcher.exe ^
    -lgdi32 -lcomctl32 -lshell32 -lwinmm -lcomdlg32
```

### Linux / macOS (SDL2)

```bash
g++ -std=c++17 -O2 sdl_launcher.cpp mmu.cpp -o sdl_launcher -lSDL2
```

## Usage

### Windows Launcher

Run `console_launcher.exe`, select a system from the list, and click **Launch** (or double-click). Browse for a ROM file and the emulator starts in its own window.

### SDL2 (Cross-platform)

Pass the ROM path as a command-line argument:

```bash
./sdl_launcher path/to/rom.gb
./sdl_launcher path/to/rom.ch8
./sdl_launcher path/to/rom.a26
```

The extension determines which system loads.

## Controls

### Gameboy

| Action    | Key          |
|-----------|--------------|
| D-Pad     | Arrow keys   |
| A         | X            |
| B         | Z            |
| Start     | Enter        |
| Select    | Backspace    |

### Chip-8

| Hex Key | Keyboard |
|---------|----------|
| 0       | X        |
| 1       | 1        |
| 2       | 2        |
| 3       | 3        |
| 4       | Q        |
| 5       | W        |
| 6       | E        |
| 7       | R        |
| 8       | A        |
| 9       | S        |
| A       | D        |
| B       | F        |
| C       | Z        |
| D       | C        |
| E       | V        |

### Space Invaders

| Action | Key       |
|--------|-----------|
| Move   | Left/Right arrows |
| Fire   | Z         |
| Coin   | 5         |
| 1P     | 1         |
| 2P     | 2         |

### Atari 2600

| Action | Key       |
|--------|-----------|
| Move   | Arrow keys|
| Fire   | Space     |
| Select | F1        |
| Reset  | F2        |

### All Systems

| Action      | Key |
|-------------|-----|
| Save state  | F5  |
| Load state  | F7  |

## Configuration

The Windows launcher reads `consoles.cfg` to populate the system list. You can add external emulators or modify key bindings through the **Settings** button.

## File Structure

```
emulator/
├── launcher.cpp       # Windows GUI launcher (Win32 API)
├── sdl_launcher.cpp   # Cross-platform SDL2 port
├── mmu.cpp / mmu.hpp  # Gameboy memory management unit
├── cartridge.hpp      # Cartridge loading, MBC mappers, RTC, cheats
├── cpu.hpp            # Gameboy SM83 CPU core
├── ppu.hpp            # Gameboy / GBC PPU with color support
├── audio.hpp          # Windows audio output (waveOut)
├── chip8.hpp          # Chip-8 interpreter
├── invaders.hpp       # Space Invaders (8080) emulator
├── atari2600.hpp      # Atari 2600 (TIA/6507) emulator
├── savestate.hpp      # Save state serialization
├── consoles.cfg       # Launcher system definitions
├── compile.bat        # Windows build script
└── .gitignore
```

## License

MIT
