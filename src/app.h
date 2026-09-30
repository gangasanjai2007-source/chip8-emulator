// Shared front-end state: everything main.cpp (window, sound, input, main loop)
// and ui.cpp (the on-screen control panels) both need to see.
#ifndef APP_H
#define APP_H

#include "chip8.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <deque>
#include <string>

const double FRAME_MS = 1000.0 / 60.0; // 60 frames per second

// Emulation speed limits (CPU instructions executed per frame)
const int MIN_CYCLES = 1;
const int MAX_CYCLES = 100;
const int DEFAULT_CYCLES = 10; // 10 per frame * 60 fps = 600 instructions/second

// Rewind: remember the last 10 seconds of machine states (600 frames * ~10 KB = ~6 MB)
const int REWIND_FRAMES = 600;

// Window layout. Without the panels the window is the classic 640x320.
// With the panels (F1) the game is drawn larger in the top-left corner.
const int CLASSIC_W = 640, CLASSIC_H = 320;
const int UI_W = 1280, UI_H = 720;
const int UI_GAME_W = 960, UI_GAME_H = 480;

// ---------------- Colour palettes ----------------
struct Palette{
    const char* name;
    uint8_t on_r, on_g, on_b;    // colour of lit pixels
    uint8_t off_r, off_g, off_b; // background colour
};
extern Palette PALETTES[];   // the last entry, "Custom", can be edited in the panel
extern const int NUM_PALETTES;

// ---------------- Sound ----------------
enum Waveform { SQUARE, SINE, TRIANGLE, SAWTOOTH, NUM_WAVEFORMS };
extern const char* WAVEFORM_NAMES[NUM_WAVEFORMS];

// Shared between the main thread and the audio thread (always changed under SDL_LockAudioDevice)
struct AudioState{
    bool beeping = false;
    int waveform = SQUARE;
    double frequency = 440.0; // Hz (the note A4)
    double phase = 0.0;       // position inside one wave cycle, from 0.0 to 1.0
    int sample_rate = 44100;
};

// ---------------- Emulator settings changed at runtime ----------------
struct Settings{
    int cycles_per_frame = DEFAULT_CYCLES;
    int palette = 0;
    bool paused = false;
    bool step_once = false; // run exactly one instruction while paused
    bool running = true;
    bool rewinding = false; // true while Tab (or the Rewind button) is held
    bool phosphor = true;   // CRT effect: pixels fade out instead of vanishing
    bool scanlines = false; // CRT effect: dark horizontal lines
};

// ---------------- Everything the front end owns ----------------
struct App{
    Chip8 chip8;
    Settings s;
    AudioState audio;
    SDL_AudioDeviceID audio_device = 0;
    SDL_Window* window = nullptr;
    std::deque<Chip8> history;   // rewind history, newest state at the back
    std::string rom_path;        // empty = no ROM loaded yet
    bool show_ui = true;         // F1 shows/hides the panels
    int breakpoint = -1;         // pause when PC reaches this address (-1 = none)
    bool ui_keys[16] = {false};  // keypad buttons held with the mouse in the panel
    // A key pressed AND released within the same frame would never be seen by the game,
    // so such releases are delayed until the frame has run.
    bool key_went_down[16] = {false};
    bool release_pending[16] = {false};
    int test_beep_frames = 0;    // "Test sound" button: beep for this many frames
    bool open_help_tab = false;  // switch the side panel to the Help tab (after loading a game)
    std::string status;          // last message, shown in the panel
};

// Actions used by both the keyboard (main.cpp) and the panel buttons (ui.cpp)
void app_load_rom(App& app, const std::string& path);
void app_restart(App& app);
void app_save_state(App& app);
void app_load_state(App& app);
void app_toggle_pause(App& app);
void app_set_sound(App& app, int waveform, double frequency);
void app_set_ui_visible(App& app, bool visible);
void app_update_title(App& app);

// The on-screen panels (ui.cpp)
void ui_draw(App& app);

#endif
