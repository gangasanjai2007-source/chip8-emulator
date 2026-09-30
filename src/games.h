// Built-in notes for the ROMs that come with the emulator: what each key does in each game,
// and the settings the game needs. Used by the Help tab and the on-screen keypad labels.
//
// The key assignments were found by reading each ROM's key-check instructions (EX9E / EXA1)
// in the disassembler and seeing what the code does next (e.g. in Pong, key 1 subtracts 2
// from the left paddle's Y position = "up").
#ifndef GAMES_H
#define GAMES_H

#include <algorithm>
#include <cctype>
#include <string>

// Keyboard key for each CHIP-8 key 0..F (matches keymap[] in main.cpp)
static const char* const PC_KEY_NAMES[16] = {"X","1","2","3","Q","W","E","A","S","D","Z","C","4","R","F","V"};

struct GameKey {
    int key;              // CHIP-8 key 0..F
    const char* action;   // short, fits on a keypad button
};

struct GameInfo {
    const char* match;    // lower-case text that appears in the ROM file name
    const char* title;
    const char* about;
    const char* tips;
    bool needs_schip;     // true = run in SUPER-CHIP mode
    int speed;            // recommended instructions per frame
    bool vblank;          // true = at most one sprite draw per frame (waits for the screen refresh,
                          // like the original COSMAC VIP); games written for Octo expect false
    GameKey keys[6];      // unused entries have action == nullptr
};

static const GameInfo GAMES[] = {
    // ---- Games from the CHIP-8 Archive (CC0, see roms/games/CREDITS.md). Listed before Pong so
    // that "superpong" is matched before "pong". Controls from each game's "howto" notes.
    {"superpong", "Super Pong",
     "A fast one-player pong: keep the ball in play. By offstatic (2021).",
     "E serves the ball.",
     false, 30, false, {{0x6, "Serve"}, {0x7, "Left"}, {0x9, "Right"}}},
    {"br8kout", "Br8kout",
     "Breakout clone: bounce the ball to clear all the bricks. By SharpenedSpoon (2014).",
     "Don't let the ball get past your paddle.",
     false, 7, false, {{0x7, "Left"}, {0x9, "Right"}}},
    {"dinorun", "Dino Run",
     "Endless runner: jump over the cacti for as long as you can. By Andrezinrc.",
     "The only goal is to keep going. Hold Tab to rewind a crash!",
     false, 15, true, {{0x5, "Jump"}}},
    {"snek", "Snek",
     "Minimalist Snake in just 65 bytes. By John Earnest (2021).",
     "Eat to grow; don't run into yourself. Written for a very fast CPU, so speed is set to the maximum.",
     false, 100, false, {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}}},
    {"outlaw", "Outlaw",
     "Wild-west duel, adapted from the Atari 2600 game. By John Earnest (2014).",
     "Move your gunman and shoot the outlaw across the screen.",
     false, 15, false, {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}, {0x6, "Fire"}}},
    {"caveexplorer", "Cave Explorer",
     "Explore a 16-screen cave world, solve puzzles and push crates. By John Earnest (2014).",
     "In platform levels E picks up / drops crates and Q resets the level.",
     false, 20, false, {{0x5, "Up"}, {0x7, "Left"}, {0x8, "Down"}, {0x9, "Right"}, {0x6, "Crate"}, {0x4, "Reset"}}},
    {"flightrunner", "Flight Runner",
     "Fly through the obstacles without crashing. By TodPunk (2014).",
     "",
     false, 15, false, {{0x7, "Left"}, {0x9, "Right"}}},
    {"minilightsout", "Mini Lights Out",
     "Puzzle: the 16 keys are a 4x4 grid of lights. Turn them all off. By tobiasvl (2019).",
     "Each key toggles its light and its neighbours, laid out exactly like the keypad (1 2 3 4 / Q W E R / A S D F / Z X C V).",
     false, 7, false, {}},
    {"pong", "Pong",
     "Two-player tennis. Each player moves a paddle up and down to return the ball.",
     "Two people can share one keyboard: left player on 1/Q, right player on 4/R.",
     false, 10, true, {{0x1, "L up"}, {0x4, "L down"}, {0xC, "R up"}, {0xD, "R down"}}},
    {"tetris", "Tetris",
     "Rotate and move the falling pieces to fill complete rows.",
     "Hold A to drop faster. Press K before a risky move and L to undo it, or hold Tab to rewind.",
     false, 10, true, {{0x4, "Rotate"}, {0x5, "Left"}, {0x6, "Right"}, {0x7, "Drop"}}},
    {"blinky", "Blinky",
     "Pac-Man style maze game: eat the dots and avoid the ghosts.",
     "Written for SUPER-CHIP: click the Apply button below (SUPER-CHIP mode, speed 30).",
     true, 30, false, {{0x3, "Up"}, {0x6, "Down"}, {0x7, "Left"}, {0x8, "Right"}}},
    {"1-chip8-logo", "Test: CHIP-8 logo",
     "Draws the CHIP-8 logo using only the most basic instructions. No input needed.",
     "If the logo looks right, clear screen, jump, set register and draw all work.",
     false, 10, true, {}},
    {"2-ibm-logo", "Test: IBM logo",
     "The classic first test program: draws the IBM logo. No input needed.",
     "", false, 10, true, {}},
    {"3-corax", "Test: corax+ opcodes",
     "Runs every CHIP-8 instruction and shows a check mark or an X next to each opcode name.",
     "All check marks = every instruction behaves correctly. No input needed.",
     false, 10, true, {}},
    {"4-flags", "Test: flags",
     "Checks the VF flag after every maths instruction (HAPPY = no overflow, CARRY = with overflow).",
     "The last mark on each line checks the case where VF itself is the target register.",
     false, 10, true, {}},
    {"5-quirks", "Test: quirks",
     "Checks the behaviours that differ between CHIP-8 versions. First pick the platform.",
     "Set Mode to match your choice: CHIP-8 for 1, SUPER-CHIP for 2 (then 1 = modern).",
     false, 10, true, {{0x1, "CHIP-8"}, {0x2, "SCHIP"}, {0x3, "XO-CHIP"}}},
    {"6-keypad", "Test: keypad",
     "Tests the keypad instructions. Pick a test, then press keys: they light up on screen.",
     "1 = key down/up checks (EX9E/EXA1), 2 and 3 = 'wait for a key' (FX0A).",
     false, 10, true, {{0x1, "Test 1"}, {0x2, "Test 2"}, {0x3, "Test 3"}}},
    {"7-beep", "Test: beep",
     "Plays the beep while you hold the CHIP-8 'B' key.",
     "Hold C (CHIP-8 key B). Try different waveforms and pitches in the Sound settings.",
     false, 10, true, {{0xB, "Beep"}}},
    {"8-scrolling", "Test: SUPER-CHIP scrolling",
     "Tests the SUPER-CHIP scroll instructions. Pick SUPER-CHIP, then low or high resolution.",
     "Correct result: every arrow sits neatly inside its box.",
     true, 10, false, {{0x1, "SCHIP/Lo"}, {0x2, "Hi-res"}}},
};
static const int NUM_GAMES = sizeof(GAMES) / sizeof(GAMES[0]);

// Finds the notes for a ROM by its file name (nullptr if it is not a known ROM)
inline const GameInfo* find_game(const std::string& path){
    std::string name = path.substr(path.find_last_of("/\\") + 1);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    for(int i = 0; i < NUM_GAMES; i++)
        if(name.find(GAMES[i].match) != std::string::npos) return &GAMES[i];
    return nullptr;
}

// Short summary for the status line, e.g. "Q Rotate, W Left, E Right, A Drop"
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
