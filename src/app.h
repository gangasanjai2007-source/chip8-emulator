// Shared front-end state: everything main.cpp (window, sound, input, main loop),
// hub.cpp (library, details, settings, help, in-game HUD) and ui.cpp (developer panels) need.
#ifndef APP_H
#define APP_H

#include "chip8.h"
#include "games.h"
#include "library.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <deque>
#include <string>

const double FRAME_MS = 1000.0 / 60.0; // 60 frames per second

// Emulation speed limits (CPU instructions executed per frame)
const int MIN_CYCLES = 1;
const int MAX_CYCLES = 500; // some newer games were written for very fast interpreters
const int DEFAULT_CYCLES = 10; // 10 per frame * 60 fps = 600 instructions/second

// Rewind: remember the last 10 seconds of machine states (600 frames * ~10 KB = ~6 MB)
const int REWIND_FRAMES = 600;

// Window layout. The window is 1280x720 (--classic: the original plain 640x320).
// In the developer view (F1) the game is drawn in the top-left corner with panels around it.
const int CLASSIC_W = 640, CLASSIC_H = 320;
const int UI_W = 1280, UI_H = 720;
const int UI_GAME_W = 960, UI_GAME_H = 510; // game + TV bezel area; panels fill the rest

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
    int palette = -1;       // -1 = the game's own theme colours, 0.. = an entry of PALETTES
    bool paused = false;
    bool step_once = false; // run exactly one instruction while paused
    bool running = true;
    bool rewinding = false; // true while Tab (or the Rewind button) is held
    bool phosphor = true;   // CRT effect: pixels fade out instead of vanishing
    bool scanlines = false; // CRT effect: dark horizontal lines
    bool glow = true;       // CRT effect: lit pixels bleed light into their neighbours
    // Wait for the screen refresh after each sprite draw (at most one draw per frame), like the
    // original COSMAC VIP. Newer games written for Octo expect this off. Set per game (games.h).
    bool display_wait = true;
};

// ---------------- Screens ----------------
// HUB: the game library. DETAILS: one game's page. GAME: playing (in-game HUD, or the
// developer view with F1). SETTINGS and HELP: full pages; opening them pauses the game.
enum Screen { SCREEN_HUB, SCREEN_DETAILS, SCREEN_GAME, SCREEN_SETTINGS, SCREEN_HELP };

// ---------------- Everything the front end owns ----------------
struct App{
    Chip8 chip8;
    Settings s;
    AudioState audio;
    SDL_AudioDeviceID audio_device = 0;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    Screen screen = SCREEN_HUB;
    Screen back_to = SCREEN_HUB;    // where Back goes from Settings / Help
    double screen_since = 0;        // time the current screen was opened (for the fade-in)
    bool paused_before_page = false; // pause state to restore when leaving Settings / Help
    bool dev_view = false;          // F1 in a game: developer panels (debugger, ROM browser...)
    Library library;                // every ROM found under roms/
    LivePreview preview;            // animated preview of the selected game
    int selected = 0;               // selected card in the library (index into library.entries)
    std::string error_title, error_text; // shown as a message box when not empty
    bool confirm_quit = false;      // Esc on the library asks before quitting
    float fps = 60;
    std::deque<Chip8> history;   // rewind history, newest state at the back
    std::string rom_path;        // empty = no ROM loaded yet
    bool play_mode = false;      // F11: fullscreen "retro console" view, no developer panels
    bool classic = false;        // --classic: original plain 640x320 window, no bezel
    SDL_Rect screen_rect{0, 0, CLASSIC_W, CLASSIC_H}; // where the CHIP-8 screen is drawn
    SDL_Rect bezel_rect{0, 0, 0, 0};                  // the TV frame around it (w = 0: none)
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
bool app_load_rom(App& app, const std::string& path); // false (and an error message) if it can't
void app_play(App& app, const std::string& path);     // load a game and switch to the game screen
void app_go(App& app, Screen screen);                 // switch screens (with a short fade)
void app_exit_to_library(App& app);
const GameTheme& app_theme(const App& app);           // theme of the loaded game
Palette app_palette(const App& app);                  // the two colours the screen uses now
void app_restart(App& app);
void app_save_state(App& app);
void app_load_state(App& app);
void app_toggle_pause(App& app);
void app_set_sound(App& app, int waveform, double frequency);
void app_set_play_mode(App& app, bool on);
void app_eject(App& app);
void app_apply_recommended(App& app); // mode + speed from games.h for the loaded game
void app_update_title(App& app);

// The on-screen panels (ui.cpp) and pages (hub.cpp)
void ui_load_fonts();          // call once after ImGui is created
void ui_draw(App& app);        // draws whatever the current screen needs
void ui_dev_panels(App& app);  // developer view panels (ui.cpp)
void hub_draw(App& app);       // library, details, settings, help, in-game HUD (hub.cpp)
void hub_overlays(App& app);   // message boxes and toasts, over the developer view too

#endif
