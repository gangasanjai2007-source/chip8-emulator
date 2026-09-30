// The game library's built-in notes: for each ROM that comes with the emulator, its genre,
// author, description, controls, the settings it needs, and its visual theme.
//
// ADDING A GAME: put the .ch8 file anywhere under roms/ and it appears in the library straight
// away (with a generic card). To give it a proper card, add one entry to GAMES below.
//
// The key assignments were found from each game's own documentation ("howto" notes in the
// CHIP-8 Archive) or, for Pong/Tetris/Blinky, by reading the ROM's key-check instructions
// (EX9E / EXA1) in our disassembler and seeing what the code does next.
#ifndef GAMES_H
#define GAMES_H

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

// Keyboard key for each CHIP-8 key 0..F (matches keymap[] in main.cpp)
static const char* const PC_KEY_NAMES[16] = {"X","1","2","3","Q","W","E","A","S","D","Z","C","4","R","F","V"};

struct Rgb { uint8_t r, g, b; };

// How the area AROUND the CHIP-8 screen is decorated (the game pixels themselves are never
// touched; a theme only picks their two colours). Drawn in hub.cpp.
enum ThemeStyle {
    STYLE_TV,        // plain retro TV (unknown games)
    STYLE_NEON,      // neon arcade: glowing double outline
    STYLE_BLOCKS,    // falling-block frame made of coloured squares
    STYLE_TERMINAL,  // green-screen terminal with a command prompt
    STYLE_GHOST,     // dark maze walls with pellets
    STYLE_BRICKS,    // brick wall
    STYLE_SPACE,     // twinkling starfield
    STYLE_DESERT,    // sunset, sun and dunes
    STYLE_WESTERN,   // wooden planks
    STYLE_CAVE,      // rock walls and stalactites
    STYLE_SKY,       // blue sky and drifting clouds
    STYLE_GRID,      // glowing light grid
    STYLE_BEACH,     // sea, waves and sand
    STYLE_GRAVEYARD, // moon and tombstones
    STYLE_LAB        // blueprint grid, for the test ROMs
};

struct GameTheme {
    const char* name;      // shown in the HUD, e.g. "Neon Arcade"
    ThemeStyle style;
    Rgb on, off;           // the two CHIP-8 pixel colours
    Rgb bg_top, bg_bottom; // background gradient around the screen
    Rgb accent;            // borders, highlights and buttons
};

struct GameKey {
    int key;              // CHIP-8 key 0..F
    const char* action;   // short, fits on a keypad button
};

struct GameInfo {
    const char* match;    // lower-case text that appears in the ROM file name
    const char* title;
    const char* genre;    // Arcade, Action, Puzzle, Adventure or Test (used by the library filter)
    const char* author;   // author, year and licence
    const char* about;
    const char* tips;
    bool needs_schip;     // true = run in SUPER-CHIP mode
    int speed;            // recommended instructions per frame
    bool vblank;          // true = at most one sprite draw per frame (waits for the screen refresh,
                          // like the original COSMAC VIP); games written for Octo expect false
    GameTheme theme;
    GameKey keys[6];      // unused entries have action == nullptr
};

// Shared theme for the test ROMs
#define LAB_THEME {"Test Lab", STYLE_LAB, {255,255,255}, {6,14,28}, {12,28,52}, {4,10,20}, {90,170,255}}

static const GameInfo GAMES[] = {
    // ---- Games, in library order. "superpong" comes before "pong" so it matches first.
    {"superpong", "Super Pong", "Arcade", "offstatic, 2021 (CC0)",
     "A fast one-player pong: keep the ball in play as long as you can.",
     "E serves the ball.",
     false, 30, false,
     {"Hot Neon", STYLE_NEON, {255,70,200}, {12,0,28}, {40,0,60}, {4,0,12}, {0,230,255}},
     {{0x6, "Serve"}, {0x7, "Left"}, {0x9, "Right"}}},
    {"pong", "Pong", "Arcade", "classic CHIP-8 version (public domain)",
     "The original video game: two players, two paddles, one ball.",
     "Two people can share one keyboard: left player on 1/Q, right player on 4/R.",
     false, 10, true,
     {"Neon Arcade", STYLE_NEON, {0,255,220}, {4,0,22}, {26,0,48}, {2,0,8}, {255,0,200}},
     {{0x1, "L up"}, {0x4, "L down"}, {0xC, "R up"}, {0xD, "R down"}}},
    {"tetris", "Tetris", "Puzzle", "Fran Dachille, 1991 (classic CHIP-8 version)",
     "Rotate and move the falling pieces to fill complete rows.",
     "Hold A to drop faster. Press K before a risky move and L to undo it, or hold Tab to rewind.",
     false, 10, true,
     {"Block Party", STYLE_BLOCKS, {255,214,60}, {10,10,34}, {22,22,70}, {6,6,18}, {0,200,255}},
     {{0x4, "Rotate"}, {0x5, "Left"}, {0x6, "Right"}, {0x7, "Drop"}}},
    {"blinky", "Blinky", "Arcade", "Hans Christian Egeberg, 1991 (classic SUPER-CHIP game)",
     "Pac-Man style maze game: eat all the dots and avoid the ghosts.",
     "Written for SUPER-CHIP; the emulator switches mode and speed for you.",
     true, 30, false,
     {"Ghost Maze", STYLE_GHOST, {255,226,0}, {0,0,18}, {4,6,40}, {0,0,6}, {40,90,255}},
     {{0x3, "Up"}, {0x6, "Down"}, {0x7, "Left"}, {0x8, "Right"}}},
    {"dinorun", "Dino Run", "Action", "Andrezinrc (CC0)",
     "Endless runner: jump over the cacti for as long as you can.",
     "The only goal is to keep going. Hold Tab to rewind a crash!",
     false, 15, true,
     {"Desert Sunset", STYLE_DESERT, {0,64,32}, {199,240,216}, {255,176,96}, {120,50,40}, {255,140,40}},
     {{0x5, "Jump"}}},
    {"br8kout", "Br8kout", "Arcade", "SharpenedSpoon, 2014 (CC0)",
     "Breakout clone: bounce the ball off your paddle to smash every brick.",
     "Don't let the ball get past your paddle.",
     false, 7, false,
     {"Brick Wall", STYLE_BRICKS, {255,150,250}, {150,24,64}, {70,14,24}, {18,2,6}, {255,120,90}},
     {{0x7, "Left"}, {0x9, "Right"}}},
    {"snek", "Snek", "Arcade", "John Earnest, 2021 (CC0)",
     "Minimalist Snake in just 65 bytes: eat, grow, don't bite yourself.",
     "Written for a very fast CPU, so its speed is set high.",
     false, 200, false,
     {"Green Terminal", STYLE_TERMINAL, {90,255,90}, {0,14,0}, {0,26,4}, {0,6,0}, {90,255,90}},
     {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}}},
    {"spacejam", "Spacejam!", "Action", "WilliamDonnelly, 2016 (CC0)",
     "Fly your ship through an endless twisting space tunnel.",
     "Any key starts the game.",
     false, 100, false,
     {"Deep Space", STYLE_SPACE, {200,225,255}, {2,2,16}, {8,6,36}, {0,0,4}, {120,160,255}},
     {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}}},
    {"outlaw", "Outlaw", "Action", "John Earnest, 2014 (CC0)",
     "Wild-west duel adapted from the Atari 2600: outdraw the outlaw.",
     "Move your gunman and shoot the outlaw across the screen.",
     false, 15, false,
     {"Wild West", STYLE_WESTERN, {255,214,150}, {70,38,12}, {110,60,24}, {36,18,6}, {230,160,60}},
     {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}, {0x6, "Fire"}}},
    {"caveexplorer", "Cave Explorer", "Adventure", "John Earnest, 2014 (CC0)",
     "Explore a 16-screen cave world, solve puzzles and push crates.",
     "In platform levels E picks up / drops crates and Q resets the level.",
     false, 20, false,
     {"Deep Cave", STYLE_CAVE, {255,204,0}, {70,42,0}, {52,38,26}, {12,9,6}, {255,190,40}},
     {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}, {0x6, "Crate"}, {0x4, "Reset"}}},
    {"glitchghost", "Glitch Ghost", "Adventure", "JackieKircher (CC0)",
     "A lonely ghost in the cemetery turns trespassers into new ghost friends.",
     "Any key starts the game.",
     false, 100, false,
     {"Haunted Night", STYLE_GRAVEYARD, {210,255,225}, {22,8,34}, {48,12,70}, {6,0,12}, {170,90,255}},
     {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}, {0x6, "Haunt"}}},
    {"piper", "Piper", "Action", "Aeris, JordanMecom, LillianWang (CC0)",
     "A little bird collects goodies on the beach and dodges the waves.",
     "Any key starts the game. Hide in the sand (E) when a wave comes.",
     false, 100, false,
     {"Beach Day", STYLE_BEACH, {30,50,110}, {255,232,176}, {110,200,255}, {255,214,150}, {0,150,200}},
     {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}, {0x6, "Hide"}}},
    {"flightrunner", "Flight Runner", "Action", "TodPunk, 2014 (CC0)",
     "Fly through the obstacles without crashing.",
     "",
     false, 15, false,
     {"Blue Sky", STYLE_SKY, {255,214,0}, {20,40,200}, {70,150,255}, {190,225,255}, {255,214,0}},
     {{0x7, "Left"}, {0x9, "Right"}}},
    {"minilightsout", "Mini Lights Out", "Puzzle", "tobiasvl, 2019 (CC0)",
     "The 16 keys are a 4x4 grid of lights. Turn them all off.",
     "Each key toggles its light and its neighbours, laid out like the keypad (1 2 3 4 / Q W E R / A S D F / Z X C V).",
     false, 7, false,
     {"Light Grid", STYLE_GRID, {255,236,120}, {18,16,6}, {26,22,40}, {4,4,8}, {255,220,80}},
     {}},

    // ---- Test ROMs (Timendus CHIP-8 test suite)
    {"1-chip8-logo", "Test: CHIP-8 logo", "Test", "Timendus test suite (GPL-3.0)",
     "Draws the CHIP-8 logo using only the most basic instructions. No input needed.",
     "If the logo looks right, clear screen, jump, set register and draw all work.",
     false, 10, true, LAB_THEME, {}},
    {"2-ibm-logo", "Test: IBM logo", "Test", "Timendus test suite (GPL-3.0)",
     "The classic first test program: draws the IBM logo. No input needed.",
     "", false, 10, true, LAB_THEME, {}},
    {"3-corax", "Test: corax+ opcodes", "Test", "Timendus test suite (GPL-3.0)",
     "Runs every CHIP-8 instruction and shows a check mark or an X next to each opcode name.",
     "All check marks = every instruction behaves correctly. No input needed.",
     false, 10, true, LAB_THEME, {}},
    {"4-flags", "Test: flags", "Test", "Timendus test suite (GPL-3.0)",
     "Checks the VF flag after every maths instruction (HAPPY = no overflow, CARRY = with overflow).",
     "The last mark on each line checks the case where VF itself is the target register.",
     false, 10, true, LAB_THEME, {}},
    {"5-quirks", "Test: quirks", "Test", "Timendus test suite (GPL-3.0)",
     "Checks the behaviours that differ between CHIP-8 versions. First pick the platform.",
     "Set Mode to match your choice: CHIP-8 for 1, SUPER-CHIP for 2 (then 1 = modern).",
     false, 10, true, LAB_THEME, {{0x1, "CHIP-8"}, {0x2, "SCHIP"}, {0x3, "XO-CHIP"}}},
    {"6-keypad", "Test: keypad", "Test", "Timendus test suite (GPL-3.0)",
     "Tests the keypad instructions. Pick a test, then press keys: they light up on screen.",
     "1 = key down/up checks (EX9E/EXA1), 2 and 3 = 'wait for a key' (FX0A).",
     false, 10, true, LAB_THEME, {{0x1, "Test 1"}, {0x2, "Test 2"}, {0x3, "Test 3"}}},
    {"7-beep", "Test: beep", "Test", "Timendus test suite (GPL-3.0)",
     "Plays the beep while you hold the CHIP-8 'B' key.",
     "Hold C (CHIP-8 key B). Try different waveforms and pitches in Settings.",
     false, 10, true, LAB_THEME, {{0xB, "Beep"}}},
    {"8-scrolling", "Test: SUPER-CHIP scrolling", "Test", "Timendus test suite (GPL-3.0)",
     "Tests the SUPER-CHIP scroll instructions. Pick SUPER-CHIP, then low or high resolution.",
     "Correct result: every arrow sits neatly inside its box.",
     true, 10, false, LAB_THEME, {{0x1, "SCHIP/Lo"}, {0x2, "Hi-res"}}},
};
static const int NUM_GAMES = sizeof(GAMES) / sizeof(GAMES[0]);

// Theme for ROMs we have no notes for
static const GameTheme DEFAULT_THEME =
    {"Retro TV", STYLE_TV, {255,255,255}, {0,0,0}, {30,28,34}, {10,9,12}, {200,190,170}};

// Finds the notes for a ROM by its file name (nullptr if it is not a known ROM)
inline const GameInfo* find_game(const std::string& path){
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    for(int i = 0; i < NUM_GAMES; i++)
        if(name.find(GAMES[i].match) != std::string::npos) return &GAMES[i];
    return nullptr;
}

// Short summary, e.g. "Q Rotate, W Left, E Right, A Drop"
inline std::string game_key_summary(const GameInfo& g){
    std::string s;
    for(const GameKey& k : g.keys){
        if(!k.action) break;
        if(!s.empty()) s += ", ";
        s += std::string(PC_KEY_NAMES[k.key]) + " " + k.action;
    }
    return s;
}

#endif
