@echo off
setlocal
set CXX=g++
set CXXFLAGS=-std=c++17 -O2 -mwindows

echo === Building Console Launcher ===
%CXX% %CXXFLAGS% launcher.cpp mmu.cpp -o console_launcher.exe -lgdi32 -lcomctl32 -lshell32 -lwinmm -lcomdlg32

if %ERRORLEVEL% EQU 0 (
    echo [OK] console_launcher.exe
    echo.
    echo Features:
    echo - Gameboy / GBC (.gb/.gbc) + Chip-8 (.ch8/.c8b)
    echo - Space Invaders (.inv/.rom/.bin) + Atari 2600 (.a26/.bin/.rom)
    echo - MBC2 support for early Gameboy games
    echo - MBC3 Real-Time Clock for Pokemon Crystal
    echo - MBC5 support for later Gameboy games
    echo - Battery saves: auto save/load SRAM from disk
    echo - GBC color support: full 15-bit RGB color palette
    echo - GBC double-speed mode (FF4D)
    echo - Audio: Gameboy pulse channels, Chip-8 beep, Space Invaders effects, Atari TIA audio
    echo - Atari TIA collision detection (proper CX registers)
    echo - Atari bankswitching: F8/F6/F4 for larger 2600 ROMs
    echo - Save states: F5=save, F7=load for all systems
    echo - Cheat support: Game Genie and GameShark codes
    echo.
    echo Cross-platform: sdl_launcher.cpp for Linux/macOS
    echo   Build: g++ -std=c++17 -O2 sdl_launcher.cpp mmu.cpp -o sdl_launcher -lSDL2
) else (
    echo [FAILED] Error code %ERRORLEVEL%
)
pause
