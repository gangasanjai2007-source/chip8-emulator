#include "chip8.h"
#include <SDL2/SDL.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <algorithm>

const int SCALE = 10; // Each pixel is 10x10 screen pixels
const int WIDTH = 64*SCALE;
const int HEIGHT = 32*SCALE;
const double FRAME_MS = 1000.0 / 60.0; // 60 frames per second

// Emulation speed limits (CPU instructions executed per frame)
const int MIN_CYCLES = 1;
const int MAX_CYCLES = 100;
const int DEFAULT_CYCLES = 10; // 10 per frame * 60 fps = 600 instructions/second

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
};

void audio_callback(void* userdata, uint8_t* stream, int len){
    static uint32_t sample_index = 0;
    int16_t* audio_buffer = (int16_t*) stream;
    int samples = len/2;

    bool* beeping = (bool*) userdata;
    for(int i=0; i<samples; i++){
        if(*beeping){
            // Generating 440Hz square wave.
            // FIX: at 44100 samples/s, one 440 Hz cycle = 100 samples, so the
            // wave must flip every 50 samples (the old /100 gave 220 Hz)
            int16_t value = ((sample_index++ / 50) % 2) ? 3000 : -3000;
            audio_buffer[i] = value;
        }
        else{
            audio_buffer[i] = 0; // Silence
            sample_index = 0;
        }
    }
}

void draw_graphics(SDL_Renderer* renderer, Chip8& chip8, const Palette& pal){
    // Clear screen with the palette's background colour
    SDL_SetRenderDrawColor(renderer, pal.off_r, pal.off_g, pal.off_b, 255);
    SDL_RenderClear(renderer);
    // Draw lit pixels in the palette's foreground colour
    SDL_SetRenderDrawColor(renderer, pal.on_r, pal.on_g, pal.on_b, 255);
    for(int y=0; y<32; y++){
        for(int x=0; x<64; x++){
            if(chip8.display[x + (y*64)] == 1){
                // FIX: was (31-y)*SCALE, which drew the screen upside down
                SDL_Rect rect = {x*SCALE, y*SCALE, SCALE, SCALE};
                SDL_RenderFillRect(renderer, &rect);
            }
        }
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
    if(s.paused) title += " | PAUSED";
    SDL_SetWindowTitle(window, title.c_str());
}

void print_controls(){
    std::cout << "\nControls:\n"
              << "  CHIP-8 keypad : 1234 / QWER / ASDF / ZXCV\n"
              << "  = / -         : faster / slower emulation\n"
              << "  P             : next colour palette\n"
              << "  M             : toggle quirks mode (original CHIP-8 / SUPER-CHIP)\n"
              << "  F5 or K       : save state\n"
              << "  F9 or L       : load state\n"
              << "  Space         : pause / resume (prints CPU state)\n"
              << "  N             : step one instruction while paused\n"
              << "  Backspace     : restart ROM\n"
              << "  Drag & drop   : a .ch8 file onto the window to load it\n"
              << "  Esc           : quit\n" << std::endl;
}

void handle_input(Chip8& chip8, Settings& s, std::string& rom_path, SDL_Window* window){
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

                // ---- Restart ----
                if(k == SDLK_BACKSPACE){
                    bool mode = chip8.cosmac_quirks;
                    chip8 = Chip8();
                    chip8.cosmac_quirks = mode;
                    chip8.load_rom(rom_path);
                }
            }

            // Check which Chip-8 key was pressed
            for(int i=0; i<16; i++){
                if(k == keymap[i]) chip8.key[i] = 1;
            }
        }
        if(event.type == SDL_KEYUP){
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
    bool beeping = false;
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 2048;
    want.callback = audio_callback;
    want.userdata = &beeping;

    SDL_AudioDeviceID audio_device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if(audio_device == 0) std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
    else SDL_PauseAudioDevice(audio_device, 0);

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

    while(settings.running){
        Uint64 frame_start = SDL_GetPerformanceCounter();

        handle_input(chip8, settings, rom_path, window);

        if(!settings.paused){
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
        bool should_beep = (chip8.get_sound_timer() > 0) && !settings.paused;
        if(audio_device != 0){
            SDL_LockAudioDevice(audio_device);
            beeping = should_beep;
            SDL_UnlockAudioDevice(audio_device);
        }

        draw_graphics(renderer, chip8, PALETTES[settings.palette]);

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
