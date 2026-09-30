// The game library: every ROM found under roms/, with its card information and a thumbnail.
//
// Thumbnails are not image files. Each ROM is run for a few seconds without a window (the
// same Chip8 class the emulator uses) and its screen is captured, so every card shows a real
// picture of that game. The selected game also gets a live, animated preview.
#ifndef LIBRARY_H
#define LIBRARY_H

#include "chip8.h"
#include "games.h"
#include <SDL2/SDL.h>
#include <string>
#include <vector>

struct LibraryEntry {
    std::string path;            // e.g. "roms/games/DinoRun.ch8"
    std::string title, genre, about;
    const GameInfo* info = nullptr; // notes from games.h (nullptr for unknown ROMs)
    GameTheme theme;             // visual identity (DEFAULT_THEME for unknown ROMs)
    SDL_Texture* thumb = nullptr;   // 128x64 texture, filled in by library_make_thumbnails
    bool thumb_ready = false;
    bool broken = false;         // file missing, empty or too big: shown as "NO SIGNAL"
};

struct Library {
    std::vector<LibraryEntry> entries;
    bool scanned = false;
};

// Finds all .ch8 / .sc8 files under roms/: known games first (in games.h order), then other
// ROMs alphabetically, then the test ROMs.
void library_scan(Library& lib);

// Makes at most `budget` thumbnails per call, so the library appears immediately and the
// pictures fill in over the first frames (a short "loading" state instead of a frozen window).
void library_make_thumbnails(Library& lib, SDL_Renderer* renderer, int budget);
bool library_all_ready(const Library& lib);
void library_free(Library& lib);

// Checks that a file can be loaded as a CHIP-8 ROM; returns "" if fine, else the reason
std::string rom_problem(const std::string& path);

// Copies a CHIP-8 screen into a 128x64 texture using two colours
void chip8_to_texture(const Chip8& c, SDL_Texture* tex, Rgb on, Rgb off);

// Live preview: runs the selected game (no input, no sound) so its card is animated
struct LivePreview {
    Chip8 chip8;
    std::string path;            // which ROM is running ("" = none)
    SDL_Texture* tex = nullptr;
    int frames = 0;
};
void preview_update(LivePreview& p, const LibraryEntry& e, SDL_Renderer* renderer);

#endif
