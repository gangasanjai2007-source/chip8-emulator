#include "library.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static std::string lower(std::string s){
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

std::string rom_problem(const std::string& path){
    std::error_code err;
    if(!fs::exists(path, err)) return "The file does not exist.";
    if(!fs::is_regular_file(path, err)) return "This is not a file.";
    auto size = fs::file_size(path, err);
    if(err) return "The file could not be read.";
    if(size == 0) return "The file is empty.";
    if(size > 4096 - 0x200) return "The file is too big to be a CHIP-8 program (max 3584 bytes).";
    std::ifstream f(path, std::ios::binary);
    if(!f) return "The file could not be opened.";
    return "";
}

void library_scan(Library& lib){
    library_free(lib);
    std::vector<LibraryEntry> known, unknown, tests;
    std::error_code err;
    if(fs::exists("roms", err)){
        for(auto& item : fs::recursive_directory_iterator("roms", err)){
            std::string ext = lower(item.path().extension().string());
            if(!item.is_regular_file(err) || (ext != ".ch8" && ext != ".sc8")) continue;

            LibraryEntry e;
            e.path = item.path().generic_string();
            e.info = find_game(e.path);
            if(e.info){
                e.title = e.info->title;
                e.genre = e.info->genre;
                e.about = e.info->about;
                e.theme = e.info->theme;
            }
            else{
                e.title = item.path().stem().string();
                e.genre = "Unknown";
                e.about = "A CHIP-8 program with no built-in notes. Try the keys shown on the keypad.";
                e.theme = DEFAULT_THEME;
            }
            e.broken = !rom_problem(e.path).empty();
            if(e.info && std::string(e.info->genre) == "Test") tests.push_back(e);
            else if(e.info) known.push_back(e);
            else unknown.push_back(e);
        }
    }
    // Known games in the order they are listed in games.h (our curated order)
    auto order = [](const LibraryEntry& e){ return (int)(e.info - GAMES); };
    std::sort(known.begin(), known.end(), [&](auto& a, auto& b){ return order(a) < order(b); });
    std::sort(tests.begin(), tests.end(), [&](auto& a, auto& b){ return order(a) < order(b); });
    std::sort(unknown.begin(), unknown.end(), [](auto& a, auto& b){ return lower(a.title) < lower(b.title); });

    lib.entries = known;
    lib.entries.insert(lib.entries.end(), unknown.begin(), unknown.end());
    lib.entries.insert(lib.entries.end(), tests.begin(), tests.end());
    lib.scanned = true;
}

void chip8_to_texture(const Chip8& c, SDL_Texture* tex, Rgb on, Rgb off){
    static uint32_t pixels[128*64];
    int w = c.screen_width(), h = c.screen_height();
    for(int y = 0; y < 64; y++)
        for(int x = 0; x < 128; x++){
            // Low-resolution screens are doubled so every texture is 128x64
            int sx = (w == 128) ? x : x / 2, sy = (h == 64) ? y : y / 2;
            bool lit = c.display[sx + sy * w] != 0;
            Rgb col = lit ? on : off;
            pixels[x + y * 128] = 0xFF000000u | (col.r << 16) | (col.g << 8) | col.b;
        }
    SDL_UpdateTexture(tex, nullptr, pixels, 128 * sizeof(uint32_t));
}

// Runs one frame the way main.cpp does: N instructions (stopping after a draw if the game
// waits for the screen refresh), then the 60 Hz timers
static void run_one_frame(Chip8& c, int speed, bool vblank){
    c.drew_sprite = false;
    for(int i = 0; i < speed; i++){
        c.emulate_cycle();
        if(vblank && c.drew_sprite) break;
    }
    c.tick_timers();
}

static void settings_for(const LibraryEntry& e, Chip8& c, int& speed, bool& vblank){
    c.cosmac_quirks = e.info ? !e.info->needs_schip : true;
    speed  = e.info ? e.info->speed : 10;
    vblank = e.info ? e.info->vblank : true;
}

static SDL_Texture* new_texture(SDL_Renderer* r){
    return SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 128, 64);
}

void library_make_thumbnails(Library& lib, SDL_Renderer* renderer, int budget){
    for(LibraryEntry& e : lib.entries){
        if(budget <= 0) return;
        if(e.thumb_ready || e.broken) continue;
        if(!e.thumb) e.thumb = new_texture(renderer);

        // Run the game for 5 seconds and keep the busiest screen we saw (title screens often
        // blink, so the last frame alone could be empty)
        Chip8 c;
        int speed; bool vblank;
        settings_for(e, c, speed, vblank);
        c.load_rom(e.path);
        Chip8 best = c;
        int best_lit = -1;
        for(int f = 1; f <= 300; f++){
            run_one_frame(c, speed, vblank);
            if(f % 15 == 0){
                int lit = 0;
                for(int i = 0; i < c.screen_width() * c.screen_height(); i++) lit += c.display[i];
                if(lit > best_lit){ best_lit = lit; best = c; }
            }
        }
        chip8_to_texture(best, e.thumb, e.theme.on, e.theme.off);
        e.thumb_ready = true;
        budget--;
    }
}

bool library_all_ready(const Library& lib){
    for(const LibraryEntry& e : lib.entries) if(!e.thumb_ready && !e.broken) return false;
    return true;
}

void library_free(Library& lib){
    for(LibraryEntry& e : lib.entries) if(e.thumb) SDL_DestroyTexture(e.thumb);
    lib.entries.clear();
    lib.scanned = false;
}

void preview_update(LivePreview& p, const LibraryEntry& e, SDL_Renderer* renderer){
    if(!p.tex) p.tex = new_texture(renderer);
    int speed; bool vblank;
    if(p.path != e.path){ // a different game was selected: start it from the beginning
        p.chip8 = Chip8();
        settings_for(e, p.chip8, speed, vblank);
        p.chip8.load_rom(e.path);
        p.path = e.path;
        p.frames = 0;
    }
    Chip8 tmp; // only used to read the recommended settings
    settings_for(e, tmp, speed, vblank);
    run_one_frame(p.chip8, speed, vblank);
    p.frames++;
    // Loop the preview every 20 seconds so it never gets stuck on a "game over" screen
    if(p.frames > 20 * 60) p.path.clear();
    chip8_to_texture(p.chip8, p.tex, e.theme.on, e.theme.off);
}
