#include "app.h"
#include "games.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

// Keyboard mapping (index = CHIP-8 key, value = PC keyboard key)
// FIX: SDL key codes are SDL_Keycode (32-bit), not uint8_t
SDL_Keycode keymap[16] = {
    SDLK_x, // 0
    SDLK_1, // 1
    SDLK_2, // 2
    SDLK_3, // 3
    SDLK_q, // 4
    SDLK_w, // 5
    SDLK_e, // 6
    SDLK_a, // 7
    SDLK_s, // 8
    SDLK_d, // 9
    SDLK_z, // A
    SDLK_c, // B
    SDLK_4, // C
    SDLK_r, // D
    SDLK_f, // E
    SDLK_v  // F
};

// ---------------- Colour palettes ----------------
Palette PALETTES[] = {
    {"Classic",      255, 255, 255,    0,   0,   0},
    {"Green Screen",  51, 255,  51,    0,  20,   0},
    {"Amber CRT",    255, 176,   0,   20,  10,   0},
    {"Neon",         255,  20, 147,   10,   0,  30},
    {"Game Boy",      15,  56,  15,  155, 188,  15},
    {"Custom",         0, 200, 255,   10,  10,  40}, // editable with colour pickers in the panel
};
const int NUM_PALETTES = sizeof(PALETTES) / sizeof(PALETTES[0]);

// ---------------- Sound ----------------
const double PI = 3.14159265358979323846; // (M_PI is not available on every compiler)
const char* WAVEFORM_NAMES[NUM_WAVEFORMS] = {"Square", "Sine", "Triangle", "Sawtooth"};

// SDL calls this on its own thread whenever the sound card needs more samples.
// FIX: the original square wave flipped every 100 samples = 220 Hz instead of 440 Hz.
// We now use a "phase accumulator": each sample advances the phase by frequency/sample_rate,
// which gives the exact frequency for any sample rate and lets us draw any wave shape.
void audio_callback(void* userdata, uint8_t* stream, int len){
    AudioState* a = (AudioState*) userdata;
    int16_t* audio_buffer = (int16_t*) stream;
    int samples = len/2;
    const double AMPLITUDE = 3000.0; // volume

    for(int i=0; i<samples; i++){
        if(!a->beeping){
            audio_buffer[i] = 0; // Silence
            a->phase = 0.0;
            continue;
        }
        double p = a->phase, value = 0.0;
        switch(a->waveform){
            case SQUARE:   value = (p < 0.5) ? 1.0 : -1.0;               break;
            case SINE:     value = std::sin(2.0 * PI * p);              break;
            case TRIANGLE: value = (p < 0.5) ? (4.0*p - 1.0) : (3.0 - 4.0*p); break;
            case SAWTOOTH: value = 2.0*p - 1.0;                           break;
        }
        audio_buffer[i] = (int16_t)(value * AMPLITUDE);
        a->phase += a->frequency / a->sample_rate;
        if(a->phase >= 1.0) a->phase -= 1.0;
    }
}

// ---------------- Drawing the CHIP-8 screen ----------------
// Brightness of each pixel, from 0.0 (off) to 1.0 (fully lit). Used for the phosphor effect.
float glow[128*64] = {0};
int glow_width = 64; // resolution the glow buffer belongs to

// Mixes two colour values: t = 0 gives a, t = 1 gives b
uint8_t mix(uint8_t a, uint8_t b, float t){ return (uint8_t)(a + (b - a) * t); }

// The CHIP-8 screen is first drawn into a small texture (64x32 or 128x64 pixels, one texture
// pixel per CHIP-8 pixel), and the GPU then stretches it onto the window. This works for any
// window size and for the SUPER-CHIP high-resolution mode.
void draw_graphics(SDL_Renderer* renderer, SDL_Texture* texture, Chip8& chip8,
                   const Palette& pal, const Settings& s, SDL_Rect dest){
    int w = chip8.screen_width(), h = chip8.screen_height();
    if(w != glow_width){ // resolution changed (SUPER-CHIP 00FE/00FF): start fresh
        std::memset(glow, 0, sizeof(glow));
        glow_width = w;
    }

    static uint32_t pixels[128*64];
    for(int y=0; y<h; y++){
        for(int x=0; x<w; x++){
            int i = x + (y*w);
            // Phosphor effect: like an old CRT screen, a pixel that switches off fades
            // out over a few frames instead of vanishing. CHIP-8 games erase and redraw
            // sprites every frame, so this also removes most of the flicker.
            if(chip8.display[i]) glow[i] = 1.0f;
            else glow[i] = s.phosphor ? glow[i] * 0.6f : 0.0f;
            float t = (glow[i] < 0.05f) ? 0.0f : glow[i];

            uint8_t r = mix(pal.off_r, pal.on_r, t), g = mix(pal.off_g, pal.on_g, t), b = mix(pal.off_b, pal.on_b, t);
            // FIX: the original code drew row y at (31-y), which put the screen upside down.
            // Here row y is simply stored at row y.
            pixels[x + y*128] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
    SDL_Rect src = {0, 0, w, h};
    SDL_UpdateTexture(texture, &src, pixels, 128 * sizeof(uint32_t));
    SDL_RenderCopy(renderer, texture, &src, &dest);

    // Glow effect: draw the screen 4 more times, slightly shifted, with "additive" blending
    // at low strength. Bright pixels then bleed a little light onto their neighbours, like
    // phosphor on a CRT. Only on dark backgrounds (on light ones it would wash out the image).
    int bg_brightness = (pal.off_r + pal.off_g + pal.off_b) / 3;
    if(s.glow && bg_brightness < 80){
        int spread = std::max(2, dest.w / 320);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
        SDL_SetTextureAlphaMod(texture, 40);
        const int offsets[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
        for(auto& o : offsets){
            SDL_Rect d = {dest.x + o[0]*spread, dest.y + o[1]*spread, dest.w, dest.h};
            SDL_RenderCopy(renderer, texture, &src, &d);
        }
        SDL_SetTextureAlphaMod(texture, 255);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
    }

    // Scanline effect: darken every other row of screen pixels
    if(s.scanlines){
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);
        for(int row = dest.y; row < dest.y + dest.h; row += 2)
            SDL_RenderDrawLine(renderer, dest.x, row, dest.x + dest.w - 1, row);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
}

// ---------------- Actions (used by keyboard shortcuts and panel buttons) ----------------
void app_update_title(App& app){
    std::string name = app.rom_path.empty() ? "no ROM loaded"
                     : app.rom_path.substr(app.rom_path.find_last_of("/\\") + 1);
    std::string title = "CHIP-8 | " + name +
                        " | Speed: " + std::to_string(app.s.cycles_per_frame) + " ops/frame" +
                        " | Palette: " + PALETTES[app.s.palette].name +
                        " | Mode: " + (app.chip8.cosmac_quirks ? "CHIP-8" : "SCHIP");
    if(app.s.rewinding) title += " | << REWIND";
    else if(app.s.paused) title += " | PAUSED";
    SDL_SetWindowTitle(app.window, title.c_str());
}

void app_load_rom(App& app, const std::string& path){
    // Known games (games.h) get their recommended mode, speed and screen-refresh setting
    if(const GameInfo* g = find_game(path)){
        app.chip8.cosmac_quirks = !g->needs_schip;
        app.s.cycles_per_frame = g->speed;
        app.s.display_wait = g->vblank;
    }
    bool mode = app.chip8.cosmac_quirks; // keep the chosen quirks mode
    app.chip8 = Chip8();
    app.chip8.cosmac_quirks = mode;
    app.chip8.load_rom(path);
    app.rom_path = path;
    app.history.clear(); // old rewind states belong to the previous game
    app.s.paused = false;
    // For the ROMs we know, show their controls right away (see games.h)
    if(const GameInfo* g = find_game(path)){
        std::string keys = game_key_summary(*g);
        app.status = std::string("Loaded ") + g->title + (keys.empty() ? "" : ": " + keys) + "  (see Help tab)";
        app.open_help_tab = true;
        std::cout << app.status << std::endl;
    }
    else app.status = "Loaded " + path;
    app_update_title(app);
}

void app_restart(App& app){
    if(!app.rom_path.empty()) app_load_rom(app, app.rom_path);
}

void app_save_state(App& app){
    if(app.rom_path.empty()) return;
    std::string file = app.rom_path + ".c8s"; // one savestate file per ROM
    app.status = app.chip8.save_state(file) ? "State saved to " + file : "Save failed";
}

void app_load_state(App& app){
    if(app.rom_path.empty()) return;
    std::string file = app.rom_path + ".c8s";
    app.status = app.chip8.load_state(file) ? "State loaded from " + file : "Load failed (see terminal)";
}

void app_toggle_pause(App& app){
    app.s.paused = !app.s.paused;
    if(app.s.paused){
        std::cout << "-- Paused. N = step, Space = resume --" << std::endl;
        app.chip8.print_state();
    }
    app_update_title(app);
}

void app_set_sound(App& app, int waveform, double frequency){
    SDL_LockAudioDevice(app.audio_device); // the audio thread reads these values
    app.audio.waveform = waveform;
    app.audio.frequency = std::clamp(frequency, 110.0, 1760.0);
    SDL_UnlockAudioDevice(app.audio_device);
}

void app_set_ui_visible(App& app, bool visible){
    app.show_ui = visible;
    if(visible) SDL_SetWindowSize(app.window, UI_W, UI_H);
    else        SDL_SetWindowSize(app.window, CLASSIC_W, CLASSIC_H);
}

// F11: fullscreen "retro console" view (TV bezel only, no developer panels)
void app_set_play_mode(App& app, bool on){
    app.play_mode = on;
    SDL_SetWindowFullscreen(app.window, on ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if(!on) app_set_ui_visible(app, app.show_ui); // restore the previous window size
    app.status = on ? "Play mode: F11 to return to the developer view" : "Developer view (F11 for play mode)";
}

// Remove the cartridge: back to the start screen
void app_eject(App& app){
    bool mode = app.chip8.cosmac_quirks;
    app.chip8 = Chip8();
    app.chip8.cosmac_quirks = mode;
    app.rom_path.clear();
    app.history.clear();
    app.s.paused = false;
    app.status = "Cartridge ejected. Pick a game.";
    app_update_title(app);
}

// Mode and speed the loaded game needs (from games.h), e.g. Blinky: SUPER-CHIP, speed 30
void app_apply_recommended(App& app){
    const GameInfo* g = app.rom_path.empty() ? nullptr : find_game(app.rom_path);
    if(!g) return;
    bool mode_ok = (app.chip8.cosmac_quirks == !g->needs_schip);
    app.chip8.cosmac_quirks = !g->needs_schip;
    app.s.cycles_per_frame = g->speed;
    app.s.display_wait = g->vblank;
    if(!mode_ok) app_restart(app); // the game must start again in the right mode
    app_update_title(app);
}

// Works out where the CHIP-8 screen and the TV bezel go for the current view
void update_layout(App& app){
    int ww, wh;
    // Size of the area we actually draw into (after going fullscreen this is the whole screen)
    SDL_GetRendererOutputSize(SDL_GetRenderer(app.window), &ww, &wh);
    if(app.play_mode){
        // Biggest whole-number scale that fits, so every CHIP-8 pixel is the same size
        int scale = std::max(1, std::min((ww - 160) / 64, (wh - 240) / 32));
        int sw = 64 * scale, sh = 32 * scale;
        int sx = (ww - sw) / 2, sy = (wh - sh - 60) / 2;
        app.screen_rect = {sx, sy, sw, sh};
        app.bezel_rect  = {sx - 48, sy - 40, sw + 96, sh + 120};
    }
    else if(app.show_ui){
        app.screen_rect = {32, 14, 896, 448}; // 14 screen pixels per CHIP-8 pixel (7 in hi-res)
        app.bezel_rect  = {10, 2, 940, 504};
    }
    else{
        app.screen_rect = {0, 0, CLASSIC_W, CLASSIC_H}; // the original plain window
        app.bezel_rect  = {0, 0, 0, 0};
    }
}

void print_controls(){
    std::cout << "\nControls:\n"
              << "  CHIP-8 keypad : 1234 / QWER / ASDF / ZXCV\n"
              << "  F1            : show / hide the control panels (mouse-driven)\n"
              << "  F11           : play mode (fullscreen retro console view)\n"
              << "  F2            : eject the cartridge (back to the start screen)\n"
              << "  = / -         : faster / slower emulation\n"
              << "  P             : next colour palette\n"
              << "  M             : toggle quirks mode (original CHIP-8 / SUPER-CHIP)\n"
              << "  Tab (hold)    : rewind time (up to 10 seconds)\n"
              << "  G / H         : CRT phosphor fade / scanlines on-off\n"
              << "  T             : next sound waveform (square, sine, triangle, sawtooth)\n"
              << "  [ / ]         : beep pitch down / up (one musical semitone)\n"
              << "  F5 or K       : save state\n"
              << "  F9 or L       : load state\n"
              << "  Space         : pause / resume (prints CPU state)\n"
              << "  N             : step one instruction while paused\n"
              << "  Backspace     : restart ROM\n"
              << "  Drag & drop   : a .ch8 file onto the window to load it\n"
              << "  Esc           : quit\n" << std::endl;
}

// ---------------- Keyboard and window events ----------------
void handle_input(App& app){
    Settings& s = app.s;
    Chip8& chip8 = app.chip8;
    SDL_Event event;

    while(SDL_PollEvent(&event)){
        ImGui_ImplSDL2_ProcessEvent(&event); // let the panels see mouse/keyboard too
        if(event.type == SDL_QUIT) s.running = false;

        // Load a new ROM by dragging a file onto the window
        if(event.type == SDL_DROPFILE){
            std::string path = event.drop.file;
            SDL_free(event.drop.file);
            app_load_rom(app, path);
        }

        // While typing in a text box in the panel, keys must not reach the game
        if(ImGui::GetIO().WantTextInput) continue;

        if(event.type == SDL_KEYDOWN){
            SDL_Keycode k = event.key.keysym.sym;
            bool repeat = event.key.repeat != 0; // true when a key is held down

            if(k == SDLK_ESCAPE) s.running = false;

            // ---- Emulation speed ----
            if(k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS){
                s.cycles_per_frame = std::min(MAX_CYCLES, s.cycles_per_frame + 1);
                app_update_title(app);
            }
            if(k == SDLK_MINUS || k == SDLK_KP_MINUS){
                s.cycles_per_frame = std::max(MIN_CYCLES, s.cycles_per_frame - 1);
                app_update_title(app);
            }

            if(!repeat){
                if(k == SDLK_F1 && !app.play_mode) app_set_ui_visible(app, !app.show_ui);
                if(k == SDLK_F11) app_set_play_mode(app, !app.play_mode);
                if(k == SDLK_F2) app_eject(app);

                // ---- Colour palette ----
                if(k == SDLK_p){
                    s.palette = (s.palette + 1) % NUM_PALETTES;
                    app_update_title(app);
                }
                // ---- Quirks mode ----
                if(k == SDLK_m){
                    chip8.cosmac_quirks = !chip8.cosmac_quirks;
                    s.display_wait = chip8.cosmac_quirks; // the original CHIP-8 waits, SUPER-CHIP doesn't
                    app_update_title(app);
                }
                // ---- Savestates (K/L also work on Macs, where F-keys need fn) ----
                if(k == SDLK_F5 || k == SDLK_k) app_save_state(app);
                if(k == SDLK_F9 || k == SDLK_l) app_load_state(app);

                // ---- Debugger ----
                if(k == SDLK_SPACE) app_toggle_pause(app);
                if(k == SDLK_n && s.paused) s.step_once = true;

                // ---- Rewind (hold Tab) ----
                if(k == SDLK_TAB){
                    s.rewinding = true;
                    app_update_title(app);
                }

                // ---- CRT effects ----
                if(k == SDLK_g){
                    s.phosphor = !s.phosphor;
                    std::cout << "Phosphor fade: " << (s.phosphor ? "on" : "off") << std::endl;
                }
                if(k == SDLK_h){
                    s.scanlines = !s.scanlines;
                    std::cout << "Scanlines: " << (s.scanlines ? "on" : "off") << std::endl;
                }

                // ---- Sound ----
                if(k == SDLK_t || k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET){
                    int wave = app.audio.waveform;
                    double freq = app.audio.frequency;
                    if(k == SDLK_t) wave = (wave + 1) % NUM_WAVEFORMS;
                    // One semitone = frequency * 2^(1/12); limited to A2 (110 Hz) .. A6 (1760 Hz)
                    if(k == SDLK_RIGHTBRACKET) freq *= std::pow(2.0, 1.0/12);
                    if(k == SDLK_LEFTBRACKET)  freq /= std::pow(2.0, 1.0/12);
                    app_set_sound(app, wave, freq);
                    std::cout << "Sound: " << WAVEFORM_NAMES[app.audio.waveform] << " wave, "
                              << (int)std::round(app.audio.frequency) << " Hz" << std::endl;
                }

                // ---- Restart ----
                if(k == SDLK_BACKSPACE) app_restart(app);
            }

            // Check which Chip-8 key was pressed
            for(int i=0; i<16; i++){
                if(k == keymap[i]){ chip8.key[i] = 1; app.key_went_down[i] = true; }
            }
        }
        if(event.type == SDL_KEYUP){
            if(event.key.keysym.sym == SDLK_TAB){
                s.rewinding = false;
                app_update_title(app);
            }
            for(int i=0; i<16; i++){
                if(event.key.keysym.sym != keymap[i]) continue;
                // Quick tap (down and up in the same frame): release after this frame runs
                if(app.key_went_down[i]) app.release_pending[i] = true;
                else chip8.key[i] = 0;
            }
        }
    }
}

// Runs one frame of emulation: N instructions, then the 60 Hz timers
void run_frame(App& app){
    Settings& s = app.s;
    Chip8& chip8 = app.chip8;
    if(app.rom_path.empty()) return; // nothing loaded yet

    if(s.rewinding){
        // Step back one frame per frame, so time runs backwards at normal speed
        if(!app.history.empty()){
            chip8 = app.history.back();
            app.history.pop_back();
            std::memset(chip8.key, 0, sizeof(chip8.key)); // don't replay old key presses
        }
        return;
    }
    if(s.paused){
        if(s.step_once){
            chip8.emulate_cycle();
            chip8.print_state();
            s.step_once = false;
        }
        return;
    }

    // Remember the state at the start of this frame (drop the oldest when full)
    if((int)app.history.size() == REWIND_FRAMES) app.history.pop_front();
    app.history.push_back(chip8);

    // Keys held with the mouse on the on-screen keypad count as pressed too
    uint8_t keyboard_keys[16];
    std::memcpy(keyboard_keys, chip8.key, 16);
    for(int i=0; i<16; i++) if(app.ui_keys[i]) chip8.key[i] = 1;

    // Run N instructions this frame (N is adjustable with = / -)
    chip8.drew_sprite = false;
    for(int i=0; i<s.cycles_per_frame; i++){
        chip8.emulate_cycle();
        // Breakpoint set in the debugger panel: stop when PC reaches it
        if(app.breakpoint >= 0 && chip8.get_pc() == app.breakpoint){
            s.paused = true;
            char msg[48];
            std::snprintf(msg, sizeof(msg), "Breakpoint hit at 0x%03X", app.breakpoint);
            app.status = msg;
            app_update_title(app);
            break;
        }
        // quirk: the original CHIP-8 waited for the screen refresh after each
        // sprite draw, so at most one draw happens per frame
        if(s.display_wait && chip8.drew_sprite) break;
    }
    // FIX: timers count down once per frame = 60 Hz, independent of CPU speed
    chip8.tick_timers();

    std::memcpy(chip8.key, keyboard_keys, 16); // mouse presses last only while held
}

int main(int argc, char** argv){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
        return 1;
    }
    App app;

    // Audio setup
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = &app.audio;

    app.audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(app.audio_device == 0) std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
    else{
        app.audio.sample_rate = have.freq; // use the rate the sound card actually gave us
        SDL_PauseAudioDevice(app.audio_device, 0);
    }

    app.window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  UI_W, UI_H, SDL_WINDOW_SHOWN);
    if(!app.window){
        std::cerr << "Window error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer){
        // No GPU acceleration available (e.g. some VMs): fall back to software rendering
        renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
    }
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(app.window);
        SDL_Quit();
        return 1;
    }
    // Texture holding one texel per CHIP-8 pixel; "0" = nearest-neighbour scaling (sharp pixels)
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture* screen_tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                SDL_TEXTUREACCESS_STREAMING, 128, 64);

    // Dear ImGui setup (the on-screen panels)
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // don't write an imgui.ini file
    ImGui::StyleColorsDark();
    ui_load_fonts();
    ImGui_ImplSDL2_InitForSDLRenderer(app.window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    // Command line: chip8 [ROM file] [--schip] [--classic]
    for(int i = 1; i < argc; i++){
        std::string arg = argv[i];
        // Optional: start in SUPER-CHIP quirks mode (e.g. ./chip8 roms/Blinky.ch8 --schip)
        if(arg == "--schip"){ app.chip8.cosmac_quirks = false; app.s.display_wait = false; }
        else if(arg == "--classic") app.classic = true; // start without the panels
        else app_load_rom(app, arg);
    }
    if(app.rom_path.empty()){
        app.status = "Pick a game in the ROM browser (or drag a .ch8 file onto the window)";
        app.classic = false; // the ROM browser is in the panels, so they must be visible
    }
    app_set_ui_visible(app, !app.classic);
    app_update_title(app);
    print_controls();

    while(app.s.running){
        Uint64 frame_start = SDL_GetPerformanceCounter();

        handle_input(app);
        run_frame(app);
        for(int i=0; i<16; i++){ // finish quick taps now that the game has seen them
            if(app.release_pending[i]) app.chip8.key[i] = 0;
            app.release_pending[i] = false;
            app.key_went_down[i] = false;
        }

        // The audio callback runs on another thread, so lock while changing the flag
        bool should_beep = (app.chip8.get_sound_timer() > 0 && !app.s.paused && !app.s.rewinding)
                           || app.test_beep_frames > 0;
        if(app.test_beep_frames > 0) app.test_beep_frames--;
        if(app.audio_device != 0){
            SDL_LockAudioDevice(app.audio_device);
            app.audio.beeping = should_beep;
            SDL_UnlockAudioDevice(app.audio_device);
        }

        // Draw: background, TV bezel, game screen, then the panels on top
        update_layout(app);
        SDL_SetRenderDrawColor(renderer, 16, 15, 18, 255);
        SDL_RenderClear(renderer);
        if(app.bezel_rect.w > 0){
            SDL_SetRenderDrawColor(renderer, 46, 42, 38, 255);  // plastic case
            SDL_RenderFillRect(renderer, &app.bezel_rect);
            SDL_Rect recess = {app.screen_rect.x - 8, app.screen_rect.y - 8,
                               app.screen_rect.w + 16, app.screen_rect.h + 16};
            SDL_SetRenderDrawColor(renderer, 12, 12, 12, 255);  // dark edge around the tube
            SDL_RenderFillRect(renderer, &recess);
        }
        draw_graphics(renderer, screen_tex, app.chip8, PALETTES[app.s.palette], app.s, app.screen_rect);

        // The panels, bezel labels and start screen are drawn with ImGui (ui.cpp)
        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ui_draw(app);
        ImGui::Render();
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        // FIX: wait ONCE per frame (the old code waited 16 ms after EVERY
        // instruction, making everything ~10x too slow). We only wait for the
        // time left in this frame, so frames stay at 60 fps.
        double elapsed_ms = (SDL_GetPerformanceCounter() - frame_start) * 1000.0
                            / SDL_GetPerformanceFrequency();
        if(elapsed_ms < FRAME_MS) SDL_Delay((Uint32)(FRAME_MS - elapsed_ms));
    }

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if(app.audio_device != 0) SDL_CloseAudioDevice(app.audio_device);
    SDL_DestroyTexture(screen_tex);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(app.window);
    SDL_Quit();

    return 0;
}
