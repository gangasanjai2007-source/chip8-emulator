# CHIP-8 Emulator — TatHack '26

![Build and test](https://github.com/gangasanjai2007-source/chip8-emulator/actions/workflows/build.yml/badge.svg)

A CHIP-8 emulator in C++ with SDL2 graphics and audio. We started from the partially broken TatHack
codebase, found and fixed its bugs in the CPU, timing, rendering and audio, and added speed control,
savestates, colour palettes, rewind, a CRT effect, selectable sound waveforms, a switchable quirks mode
and a pause/step debugger with a disassembler. Automated tests run on Linux, macOS and Windows on every push.

**Team:** <!-- TODO: Name (GitHub @username) for all 4 members -->

**Demo video:** <!-- TODO: unlisted YouTube link -->

---

## Quick start

### Windows (MSYS2)
1. Install MSYS2 from https://www.msys2.org
2. Open **MSYS2 MINGW64** and install the tools:
   ```bash
   pacman -S --needed make mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2
   ```
3. Build and run:
   ```bash
   make
   ./chip8.exe roms/Pong.ch8
   ```

### Linux / macOS
```bash
sudo apt-get install libsdl2-dev   # Ubuntu/Debian  (Arch: sudo pacman -S sdl2, macOS: brew install sdl2)
make
./chip8 roms/Pong.ch8
```

---

## Controls

### CHIP-8 keypad
```
Chip-8 Keypad:          Keyboard:
┌─┬─┬─┬─┐               ┌─┬─┬─┬─┐
│1│2│3│C│               │1│2│3│4│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│4│5│6│D│               │Q│W│E│R│
├─┼─┼─┼─┤      =        ├─┼─┼─┼─┤
│7│8│9│E│               │A│S│D│F│
├─┼─┼─┼─┤               ├─┼─┼─┼─┤
│A│0│B│F│               │Z│X│C│V│
└─┴─┴─┴─┘               └─┴─┴─┴─┘
```

### Emulator controls (added by us)
| Key | Action |
|-----|--------|
| `=` / `-` | Increase / decrease emulation speed (1–100 instructions per frame) |
| `P` | Cycle colour palette |
| `M` | Toggle quirks mode: original CHIP-8 ↔ SUPER-CHIP |
| `Tab` (hold) | Rewind time (up to 10 seconds) |
| `G` / `H` | CRT phosphor fade on/off / scanlines on/off |
| `T` | Next sound waveform: square → sine → triangle → sawtooth |
| `[` / `]` | Beep pitch down / up by one musical semitone |
| `F5` or `K` | Save state (`K` works on Macs, where F-keys need `fn`) |
| `F9` or `L` | Load state |
| `Space` | Pause / resume (prints CPU state) |
| `N` | Step one instruction while paused (prints disassembly + registers) |
| `Backspace` | Restart the ROM |
| Drag & drop | Drop a `.ch8` file on the window to load it |
| `Esc` | Quit |

The window title always shows the ROM, speed, palette and mode.
Start in SUPER-CHIP mode with `./chip8 roms/Blinky.ch8 --schip` (Blinky needs it).

Game tips — **Pong:** left paddle `1`/`Q`, right paddle `4`/`R`. **Tetris:** `Q` rotate, `W` left, `E` right, `A` drop.

---

## Bugs found and fixed

| # | File | Bug | Symptom | Fix |
|---|------|-----|---------|-----|
| 1 | chip8.cpp | `00EE` read `stack[sp]` before decrementing `sp` | Games crash or jump to garbage after a subroutine | Decrement `sp` first, then read; added stack overflow/underflow checks |
| 2 | chip8.cpp | `8XY5`/`8XY7` used `>` for the no-borrow flag | Wrong VF when both values are equal | Use `>=` |
| 3 | chip8.cpp | `8XY4/5/6/7/E` wrote VF before the result | Wrong result when X = F (flags test) | Compute flag, write result, write VF last |
| 4 | chip8.cpp | `FX0A` advanced PC even with no key pressed | "Wait for key" didn't wait | Only advance when a key is pressed |
| 5 | chip8.cpp | `FX33` tens digit was `value/10` | Scores like 123 displayed wrong | `(value/10) % 10` |
| 6 | chip8.cpp | `FX55`/`FX65` loop used `i < x` | Last register not saved/loaded | `i <= x` |
| 7 | chip8.cpp / main.cpp | Timers decremented every instruction | Timers ran ~10× too fast | Decrement once per frame (60 Hz) |
| 8 | main.cpp | `SDL_Delay(16)` inside the 10-cycle loop | Everything ~10× too slow | Delay once per frame |
| 9 | main.cpp | Renderer used `(31 - y)` | Screen upside down | Use `y` |
| 10 | main.cpp | Square wave period gave ~220 Hz | Beep an octave too low | Correct the period for 440 Hz |
| 11 | Makefile | Hard-coded `-lSDL2` | Build failed on Windows | Use `sdl2-config --cflags/--libs` (portable) |
| 12 | chip8.cpp | No bounds checks on PC, I, key index, font digit | A bad ROM could read/write outside the 4 KB array | Mask addresses to 12 bits (`& 0xFFF`), keys/digits to 4 bits |
| 13 | main.cpp | `keymap` stored SDL key codes as `uint8_t` | Works only by luck for ASCII keys | Use `SDL_Keycode` |
| 14 | main.cpp | `beeping` flag shared with the audio thread without locking | Data race | `SDL_LockAudioDevice` around the update |

### How we found them
- Read each opcode against the CHIP-8 reference (Cowgod's technical reference).
- Ran the **Timendus CHIP-8 test suite** (`roms/test/`). `3-corax+` and `4-flags` show a ✔/✘ per opcode,
  so each fix could be verified.
- Watched the symptoms in the games (upside-down Pong, slow gameplay, Tetris crashing).

**Result:** `3-corax+` ✔ all opcodes, `4-flags` ✔ all flags (including the X = F cases),
`5-quirks` ✔ all six checks in CHIP-8 mode.
<!-- TODO: add before/after screenshots of 3-corax+ and 4-flags -->

---

## Features added

### 1. Configurable emulation speed
The main loop runs at a fixed 60 frames per second. Each frame executes `cycles_per_frame`
instructions (default 10 = 600 instructions/second, range 1–100), changed live with `=` / `-`.
Timers tick once per frame, **independent of CPU speed**, so games keep correct timing even when
the CPU is sped up. Frame pacing uses `SDL_GetPerformanceCounter` and only sleeps for the time left
in the frame.

### 2. Savestates
The whole machine state (memory, V0–VF, I, PC, stack, SP, timers, display) is written to a binary file
with a 4-byte magic header `C8SV` and a version byte. Loading reads into a temporary copy, checks the
header, file length and value ranges (PC, I, SP), and only then replaces the live state, so a corrupt
or edited file can't crash the emulator. Each ROM gets its own save file (`<rom>.c8s`).
Tested: save → load → both copies produce identical output; truncated, garbage and missing files are
rejected with a message and the running game is untouched.

### 3. Colour palettes
`P` cycles through a table of `{foreground RGB, background RGB}` palettes: **Classic** (white/black),
**Green Screen** (#33FF33), **Amber CRT** (#FFB000), **Neon** (hot pink on dark purple) and
**Game Boy** (dark green on light green). Adding a palette is one line in the `PALETTES` table.

### 4. Pause & step debugger with disassembler (bonus)
`Space` pauses and `N` executes one instruction. Each step prints the program counter, the next
opcode **disassembled into readable assembly**, and all registers:
```
PC=21A  OP=D015  DRW V0, V1, 5       I=2A0  SP=1  DT=00  ST=00
V0=0C V1=08 V2=00 V3=00 V4=00 V5=00 V6=00 V7=00
V8=00 V9=00 VA=00 VB=00 VC=00 VD=00 VE=00 VF=00
```

### 5. Switchable quirks mode (bonus)
The original 1977 COSMAC VIP CHIP-8 and the later SUPER-CHIP behave differently in a few
instructions, and games are written for one or the other. `M` switches between them:

| Behaviour | CHIP-8 (default) | SUPER-CHIP |
|---|---|---|
| `8XY1/2/3` reset VF | yes | no |
| `8XY6/E` shift source | VY | VX |
| `FX55/65` change I | I += X + 1 | unchanged |
| Draws per frame | 1 (waits for screen refresh) | unlimited |

With CHIP-8 mode the Timendus `5-quirks` test passes all six checks; Blinky needs SUPER-CHIP mode.

### 6. Rewind
Hold `Tab` to run time backwards. Every frame, a copy of the whole machine (the `Chip8` object, ~6 KB
of plain arrays) is pushed onto a history of the last 600 frames (10 seconds, ~4 MB). While `Tab` is
held, one state per frame is popped off and restored, so the game plays backwards at normal speed.
A `std::deque` is used so the oldest state can be dropped from the front in constant time. This
reuses the same idea as savestates, but in memory instead of a file.

### 7. CRT effect
- **Phosphor fade (`G`, on by default):** each pixel has a brightness that jumps to 1.0 when lit and
  decays by ×0.6 per frame when off; the colour is blended between the palette's background and
  foreground. Real CRT phosphor glows briefly the same way. CHIP-8 games erase and redraw sprites
  every frame, so this also removes most of the flicker.
- **Scanlines (`H`):** a semi-transparent black line over every other row of screen pixels.

### 8. Sound waveforms
The beep is generated with a **phase accumulator**: each audio sample advances a phase by
`frequency / sample_rate`, and the waveform function turns the phase into a sample
(square, sine, triangle or sawtooth). This gives the exact frequency at whatever sample rate the
sound card provides. `[` / `]` change the pitch by one semitone (×2^(1/12)), from 110 Hz to 1760 Hz.
The settings are shared with SDL's audio thread, so they are only changed while holding
`SDL_LockAudioDevice`.

### 9. Quality-of-life
Drag & drop ROM loading, `Backspace` restart, live status in the window title, software-renderer
fallback when GPU acceleration is unavailable.

---

## Automated tests

```bash
make test
```
[`tests/run_tests.cpp`](tests/run_tests.cpp) runs the CPU core without a window:
- runs the Timendus `corax+`, `flags` and `quirks` ROMs and compares the final screen with a
  known-good result (a hash of the screen, recorded after checking by eye that every result is ✔);
- checks that a savestate restores a game exactly, and that garbage/missing files are rejected
  without touching the running game.

We checked that the tests really catch bugs: putting the original `8XY5` borrow bug back makes the
flags and quirks tests fail.

**Continuous integration:** [GitHub Actions](.github/workflows/build.yml) builds the emulator and
runs `make test` on **Linux, macOS and Windows (MSYS2)** on every push.

---

## Architecture

```
main.cpp (SDL2 front-end)                      chip8.cpp (CPU core)
┌──────────────────────────────┐              ┌──────────────────────────────┐
│ loop at 60 fps:              │              │ memory[4096], V[16], I, PC   │
│  1. handle_input  ───────────┼── key[16] ──►│ stack[16], SP, timers        │
│  2. N × emulate_cycle ───────┼─────────────►│ emulate_cycle(): fetch →     │
│  3. tick_timers (60 Hz)      │              │   decode → execute           │
│  4. audio: beep if ST > 0    │◄─ display ───│ display[64*32]               │
│  5. draw_graphics (palette,  │              │ save_state / load_state      │
│     phosphor, scanlines)     │              │ print_state (disassembler)   │
│  6. push state for rewind    │              │                              │
│  7. sleep rest of the frame  │              │                              │
└──────────────────────────────┘              └──────────────────────────────┘
```
- **Core / front-end split:** `Chip8` knows nothing about SDL, so the CPU can be tested and reasoned
  about separately from graphics, input and audio.
- **Timing:** the CPU runs N instructions per frame (adjustable); timers tick once per frame at 60 Hz,
  independent of CPU speed, as the CHIP-8 spec requires.

---

## Team contributions
| Member | Worked on |
|--------|-----------|
| <!-- name --> | <!-- TODO: be accurate: what each person actually did, tested and presents --> |
| <!-- name --> | |
| <!-- name --> | |
| <!-- name --> | |

---

## AI usage disclosure
We used AI tools significantly and want to be transparent about it:
- **Claude (Anthropic)** reviewed the original code and identified the bugs, and wrote most of the
  final implementation of the fixes and features (speed control, savestates, palettes, quirks mode,
  debugger/disassembler, rewind, CRT effect, sound waveforms), the automated tests, the CI workflow
  and this README.
- **ChatGPT** was used by a team member for an earlier round of CPU fixes.
- We set up the build on Windows (MSYS2) and macOS, ran every change against the Timendus test suite
  and the game ROMs, and reviewed the code so each of us can explain how it works.
<!-- TODO: edit so this is exactly accurate for your team -->

---

## Credits
- Original codebase: TatHack '26 / Tathva, NIT Calicut
- Test ROMs: [Timendus/chip8-test-suite](https://github.com/Timendus/chip8-test-suite) (GPL-3.0)
- Game ROMs: [kripod/chip8-roms](https://github.com/kripod/chip8-roms)
- Reference: Cowgod's CHIP-8 Technical Reference
