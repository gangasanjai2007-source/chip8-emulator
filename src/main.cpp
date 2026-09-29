#include "chip8.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64*SCALE;
const int HEIGHT = 32*SCALE;
const double FRAME_MS = 1000.0 / 60.0; // 60 frames per second

// Emulation speed limits (CPU instructions executed per frame)
const int MIN_CYCLES = 1;
const int MAX_CYCLES = 100;
const int DEFAULT_CYCLES = 10; // 10 per frame * 60 fps = 600 instructions/second

// Rewind: remember the last 10 seconds of machine states (600 frames * ~6 KB = ~4 MB)
const int REWIND_FRAMES = 600;

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
struct Palette{
    const char* name;
    uint8_t on_r, on_g, on_b;    // colour of lit pixels
    uint8_t off_r, off_g, off_b; // background colour
};

const Palette PALETTES[] = {
    {"Classic",      255, 255, 255,    0,   0,   0},
    {"Green Screen",  51, 255,  51,    0,  20,   0},
    {"Amber CRT",    255, 176,   0,   20,  10,   0},
    {"Neon",         255,  20, 147,   10,   0,  30},
    {"Game Boy",      15,  56,  15,  155, 188,  15},
};
const int NUM_PALETTES = sizeof(PALETTES) / sizeof(PALETTES[0]);

// ---------------- Emulator settings changed at runtime ----------------
struct Settings{
    int cycles_per_frame = DEFAULT_CYCLES;
    int palette = 0;
    bool paused = false;
    bool step_once = false; // run exactly one instruction while paused
    bool running = true;
    bool rewinding = false; // true while Tab is held
    bool phosphor = true;   // CRT effect: pixels fade out instead of vanishing
    bool scanlines = false; // CRT effect: dark horizontal lines
};

// ---------------- Sound ----------------
const double PI = 3.14159265358979323846; // (M_PI is not available on every compiler)
enum Waveform { SQUARE, SINE, TRIANGLE, SAWTOOTH, NUM_WAVEFORMS };
const char* WAVEFORM_NAMES[NUM_WAVEFORMS] = {"Square", "Sine", "Triangle", "Sawtooth"};

// Shared between the main thread and the audio thread (always changed under SDL_LockAudioDevice)
struct AudioState{
    bool beeping = false;
    int waveform = SQUARE;
    double frequency = 440.0; // Hz (the note A4)
    double phase = 0.0;       // position inside one wave cycle, from 0.0 to 1.0
    int sample_rate = 44100;
};

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

// Brightness of each pixel, from 0.0 (off) to 1.0 (fully lit). Used for the phosphor effect.
float glow[64*32] = {0};

// Mixes two colour values: t = 0 gives a, t = 1 gives b
uint8_t mix(uint8_t a, uint8_t b, float t){ return (uint8_t)(a + (b - a) * t); }

void draw_graphics(SDL_Renderer* renderer, Chip8& chip8, const Palette& pal, const Settings& s){
    // Clear screen with the palette's background colour
    SDL_SetRenderDrawColor(renderer, pal.off_r, pal.off_g, pal.off_b, 255);
    SDL_RenderClear(renderer);

    for(int y=0; y<32; y++){
        for(int x=0; x<64; x++){
            int i = x + (y*64);
            // Phosphor effect: like an old CRT screen, a pixel that switches off fades
            // out over a few frames instead of vanishing. CHIP-8 games erase and redraw
            // sprites every frame, so this also removes most of the flicker.
            if(chip8.display[i]) glow[i] = 1.0f;
            else glow[i] = s.phosphor ? glow[i] * 0.6f : 0.0f;
            if(glow[i] < 0.05f) continue;

            SDL_SetRenderDrawColor(renderer, mix(pal.off_r, pal.on_r, glow[i]),
                                             mix(pal.off_g, pal.on_g, glow[i]),
                                             mix(pal.off_b, pal.on_b, glow[i]), 255);
            // FIX: was (31-y)*SCALE, which drew the screen upside down
            SDL_Rect rect = {x*SCALE, y*SCALE, SCALE, SCALE};
            SDL_RenderFillRect(renderer, &rect);
        }
    }

    // Scanline effect: darken every other row of screen pixels
    if(s.scanlines){
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 110);
        for(int row = 0; row < HEIGHT; row += 2) SDL_RenderDrawLine(renderer, 0, row, WIDTH, row);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }
    SDL_RenderPresent(renderer);
}

// Shows the current settings in the window title bar
void update_title(SDL_Window* window, const std::string& rom, const Settings& s, const Chip8& chip8){
    std::string name = rom.substr(rom.find_last_of("/\\") + 1);
    std::string title = "CHIP-8 | " + name +
                        " | Speed: " + std::to_string(s.cycles_per_frame) + " ops/frame" +
                        " | Palette: " + PALETTES[s.palette].name +
                        " | Mode: " + (chip8.cosmac_quirks ? "CHIP-8" : "SCHIP");
    if(s.rewinding) title += " | << REWIND";
    else if(s.paused) title += " | PAUSED";
    SDL_SetWindowTitle(window, title.c_str());
}

void print_controls(){
    std::cout << "\nControls:\n"
              << "  CHIP-8 keypad : 1234 / QWER / ASDF / ZXCV\n"
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

void handle_input(Chip8& chip8, Settings& s, std::string& rom_path, SDL_Window* window,
                  AudioState& audio, SDL_AudioDeviceID audio_device, std::deque<Chip8>& history){
    SDL_Event event;
    std::string save_path = rom_path + ".c8s"; // one savestate file per ROM

    while(SDL_PollEvent(&event)){
        if(event.type == SDL_QUIT) s.running = false;

        // Load a new ROM by dragging a file onto the window
        if(event.type == SDL_DROPFILE){
            rom_path = event.drop.file;
            SDL_free(event.drop.file);
            bool mode = chip8.cosmac_quirks;
            chip8 = Chip8();
            chip8.cosmac_quirks = mode;
            chip8.load_rom(rom_path);
            history.clear(); // old rewind states belong to the previous game
            update_title(window, rom_path, s, chip8);
        }

        if(event.type == SDL_KEYDOWN){
            SDL_Keycode k = event.key.keysym.sym;
            bool repeat = event.key.repeat != 0; // true when a key is held down

            if(k == SDLK_ESCAPE) s.running = false;

            // ---- Emulation speed ----
            if(k == SDLK_EQUALS || k == SDLK_PLUS || k == SDLK_KP_PLUS){
                s.cycles_per_frame = std::min(MAX_CYCLES, s.cycles_per_frame + 1);
                update_title(window, rom_path, s, chip8);
            }
            if(k == SDLK_MINUS || k == SDLK_KP_MINUS){
                s.cycles_per_frame = std::max(MIN_CYCLES, s.cycles_per_frame - 1);
                update_title(window, rom_path, s, chip8);
            }

            if(!repeat){
                // ---- Colour palette ----
                if(k == SDLK_p){
                    s.palette = (s.palette + 1) % NUM_PALETTES;
                    update_title(window, rom_path, s, chip8);
                }
                // ---- Quirks mode ----
                if(k == SDLK_m){
                    chip8.cosmac_quirks = !chip8.cosmac_quirks;
                    update_title(window, rom_path, s, chip8);
                }
                // ---- Savestates (K/L also work on Macs, where F-keys need fn) ----
                if(k == SDLK_F5 || k == SDLK_k) chip8.save_state(save_path);
                if(k == SDLK_F9 || k == SDLK_l) chip8.load_state(save_path);

                // ---- Debugger ----
                if(k == SDLK_SPACE){
                    s.paused = !s.paused;
                    if(s.paused){
                        std::cout << "-- Paused. N = step, Space = resume --" << std::endl;
                        chip8.print_state();
                    }
                    update_title(window, rom_path, s, chip8);
                }
                if(k == SDLK_n && s.paused) s.step_once = true;

                // ---- Rewind (hold Tab) ----
                if(k == SDLK_TAB){
                    s.rewinding = true;
                    update_title(window, rom_path, s, chip8);
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
                    SDL_LockAudioDevice(audio_device); // the audio thread reads these values
                    if(k == SDLK_t) audio.waveform = (audio.waveform + 1) % NUM_WAVEFORMS;
                    // One semitone = frequency * 2^(1/12); limited to A2 (110 Hz) .. A6 (1760 Hz)
                    if(k == SDLK_RIGHTBRACKET) audio.frequency = std::min(1760.0, audio.frequency * std::pow(2.0, 1.0/12));
                    if(k == SDLK_LEFTBRACKET)  audio.frequency = std::max(110.0,  audio.frequency / std::pow(2.0, 1.0/12));
                    SDL_UnlockAudioDevice(audio_device);
                    std::cout << "Sound: " << WAVEFORM_NAMES[audio.waveform] << " wave, "
                              << (int)std::round(audio.frequency) << " Hz" << std::endl;
                }

                // ---- Restart ----
                if(k == SDLK_BACKSPACE){
                    bool mode = chip8.cosmac_quirks;
                    chip8 = Chip8();
                    chip8.cosmac_quirks = mode;
                    chip8.load_rom(rom_path);
                    history.clear();
                }
            }

            // Check which Chip-8 key was pressed
            for(int i=0; i<16; i++){
                if(k == keymap[i]) chip8.key[i] = 1;
            }
        }
        if(event.type == SDL_KEYUP){
            if(event.key.keysym.sym == SDLK_TAB){
                s.rewinding = false;
                update_title(window, rom_path, s, chip8);
            }
            for(int i=0; i<16; i++){
                if(event.key.keysym.sym == keymap[i]) chip8.key[i] = 0;
            }
        }
    }
}

int main(int argc, char** argv){
    if(argc < 2){
        std::cerr << "Usage: " << argv[0] << " <ROM file> [--schip]" << std::endl;
        return 1;
    }
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0){
        std::cerr << "SDL Error: " << SDL_GetError() << std::endl;
        return 1;
    }
    // Audio setup
    AudioState audio;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = &audio;

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(audio_device == 0) std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
    else{
        audio.sample_rate = have.freq; // use the rate the sound card actually gave us
        SDL_PauseAudioDevice(audio_device, 0);
    }

    SDL_Window* window = SDL_CreateWindow("Chip-8 Emulator", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    if(!window){
        std::cerr << "Window error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(!renderer){
        // No GPU acceleration available (e.g. some VMs): fall back to software rendering
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if(!renderer){
        std::cerr << "Renderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Chip8 chip8;
    std::string rom_path = argv[1];
    // Optional: start in SUPER-CHIP quirks mode (e.g. ./chip8 roms/Blinky.ch8 --schip)
    if(argc >= 3 && std::string(argv[2]) == "--schip") chip8.cosmac_quirks = false;
    chip8.load_rom(rom_path);

    Settings settings;
    update_title(window, rom_path, settings, chip8);
    print_controls();

    // Rewind history: one copy of the whole machine per frame, newest at the back.
    // Chip8 contains only plain arrays and numbers, so copying it is a simple memory copy.
    // A deque lets us drop the oldest state from the front cheaply.
    std::deque<Chip8> history;

    while(settings.running){
        Uint64 frame_start = SDL_GetPerformanceCounter();

        handle_input(chip8, settings, rom_path, window, audio, audio_device, history);

        if(settings.rewinding){
            // Step back one frame per frame, so time runs backwards at normal speed
            if(!history.empty()){
                chip8 = history.back();
                history.pop_back();
                std::memset(chip8.key, 0, sizeof(chip8.key)); // don't replay old key presses
            }
        }
        else if(!settings.paused){
            // Remember the state at the start of this frame (drop the oldest when full)
            if((int)history.size() == REWIND_FRAMES) history.pop_front();
            history.push_back(chip8);

            // Run N instructions this frame (N is adjustable with = / -)
            chip8.drew_sprite = false;
            for(int i=0; i<settings.cycles_per_frame; i++){
                chip8.emulate_cycle();
                // quirk: the original CHIP-8 waited for the screen refresh after each
                // sprite draw, so at most one draw happens per frame
                if(chip8.cosmac_quirks && chip8.drew_sprite) break;
            }
            // FIX: timers count down once per frame = 60 Hz, independent of CPU speed
            chip8.tick_timers();
        }
        else if(settings.step_once){
            chip8.emulate_cycle();
            chip8.print_state();
            settings.step_once = false;
        }

        // The audio callback runs on another thread, so lock while changing the flag
        bool should_beep = (chip8.get_sound_timer() > 0) && !settings.paused && !settings.rewinding;
        if(audio_device != 0){
            SDL_LockAudioDevice(audio_device);
            audio.beeping = should_beep;
            SDL_UnlockAudioDevice(audio_device);
        }

        draw_graphics(renderer, chip8, PALETTES[settings.palette], settings);

        // FIX: wait ONCE per frame (the old code waited 16 ms after EVERY
        // instruction, making everything ~10x too slow). We only wait for the
        // time left in this frame, so frames stay at 60 fps.
        double elapsed_ms = (SDL_GetPerformanceCounter() - frame_start) * 1000.0
                            / SDL_GetPerformanceFrequency();
        if(elapsed_ms < FRAME_MS) SDL_Delay((Uint32)(FRAME_MS - elapsed_ms));
    }
    if(audio_device != 0) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
