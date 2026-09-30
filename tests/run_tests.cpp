// Automated tests for the CHIP-8 core (no window or SDL needed).
// Run with:  make test
//
// 1. Runs the Timendus test-suite ROMs and compares the final screen against a
//    known-good "golden" screen (stored as a hash). If any opcode breaks, the
//    screen changes, the hash no longer matches, and the test prints the screen.
// 2. Checks that savestates round-trip exactly and that bad files are rejected.
#include "../src/chip8.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

// Known-good screen hashes, recorded after checking by eye that every result on
// these screens is a checkmark (corax+: all opcodes, flags: all flags, quirks: all 6 in
// both modes) and that the scrolling test shapes sit exactly inside their frames
static const uint32_t EXPECTED_CORAX  = 0x438FBB56;
static const uint32_t EXPECTED_FLAGS  = 0x5CE72FCE;
static const uint32_t EXPECTED_QUIRKS = 0x42B162E9;
static const uint32_t EXPECTED_SCHIP_QUIRKS = 0xBE0FD30F;
static const uint32_t EXPECTED_SCROLL_LORES = 0x117D5A89;
static const uint32_t EXPECTED_SCROLL_HIRES = 0x83F1015F;

static int failures = 0;

static void check(bool ok, const std::string& name){
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if(!ok) failures++;
}

// FNV-1a hash of the screen: a short fingerprint of what is displayed
static uint32_t screen_hash(const Chip8& c){
    uint32_t h = 2166136261u;
    for(int i = 0; i < c.screen_width() * c.screen_height(); i++){ h ^= c.display[i]; h *= 16777619u; }
    return h;
}

static void print_screen(const Chip8& c){
    int w = c.screen_width(), h = c.screen_height();
    for(int y = 0; y < h; y++){
        for(int x = 0; x < w; x++) std::putchar(c.display[y*w + x] ? '#' : '.');
        std::putchar('\n');
    }
}

// A key press: CHIP-8 key `key` is held for 8 frames starting at frame `frame`
struct Press { int key; int frame; };

// Runs a ROM for a number of frames the same way main.cpp does (10 cycles + 1 timer tick per frame).
// press_key >= 0 presses that CHIP-8 key twice (to get through the quirks test menus).
static Chip8 run_rom(const std::string& path, int frames, int press_key = -1,
                     bool cosmac = true, std::vector<Press> presses = {}){
    Chip8 c;
    c.cosmac_quirks = cosmac;
    c.load_rom(path);
    for(int f = 0; f < frames; f++){
        if(press_key >= 0) c.key[press_key] = ((f > 30 && f < 40) || (f > 200 && f < 210)) ? 1 : 0;
        for(const Press& p : presses) c.key[p.key] = (f >= p.frame && f < p.frame + 8) ? 1 : 0;
        c.drew_sprite = false;
        for(int i = 0; i < 15; i++){
            c.emulate_cycle();
            if(c.cosmac_quirks && c.drew_sprite) break;
        }
        c.tick_timers();
    }
    return c;
}

static void rom_test(const char* name, const std::string& path, int frames, int key, uint32_t expected,
                     bool cosmac = true, std::vector<Press> presses = {}){
    Chip8 c = run_rom(path, frames, key, cosmac, presses);
    uint32_t h = screen_hash(c);
    check(h == expected, std::string(name) + " screen matches known-good result");
    if(h != expected){
        std::printf("  expected hash %08X, got %08X. Screen:\n", expected, h);
        print_screen(c);
    }
}

int main(){
    const std::string R = "roms/test/";
    std::printf("CHIP-8 core tests\n\n");

    rom_test("corax+ (all opcodes)", R + "3-corax+.ch8", 300, -1, EXPECTED_CORAX);
    rom_test("flags (VF carry/borrow)", R + "4-flags.ch8", 1500, -1, EXPECTED_FLAGS);
    rom_test("quirks (CHIP-8 mode)", R + "5-quirks.ch8", 2500, 1, EXPECTED_QUIRKS);
    // SUPER-CHIP: menu choices are made by pressing keys at fixed frames
    rom_test("quirks (SUPER-CHIP mode)", R + "5-quirks.ch8", 2500, -1, EXPECTED_SCHIP_QUIRKS,
             false, {{2, 30}, {1, 150}});
    rom_test("scrolling (SUPER-CHIP low res)", R + "8-scrolling.ch8", 500, -1, EXPECTED_SCROLL_LORES,
             false, {{1, 30}, {1, 120}, {1, 220}});
    rom_test("scrolling (SUPER-CHIP high res)", R + "8-scrolling.ch8", 500, -1, EXPECTED_SCROLL_HIRES,
             false, {{1, 30}, {2, 120}});

    // Savestate round trip: save mid-game, load into a new machine, run both, compare
    Chip8 a = run_rom("roms/Tetris.ch8", 200);
    check(a.save_state("test_tmp.c8s"), "savestate: save succeeds");
    Chip8 b;
    check(b.load_state("test_tmp.c8s"), "savestate: load succeeds");
    for(int i = 0; i < 3000; i++){ a.emulate_cycle(); b.emulate_cycle(); }
    check(std::memcmp(a.display, b.display, sizeof(a.display)) == 0, "savestate: restored game runs identically");

    // Bad files must be rejected and must leave the machine untouched
    FILE* f = std::fopen("test_bad.c8s", "wb");
    std::fwrite("NOT A SAVESTATE", 1, 15, f);
    std::fclose(f);
    uint32_t before = screen_hash(b);
    check(!b.load_state("test_bad.c8s"), "savestate: garbage file rejected");
    check(!b.load_state("does_not_exist.c8s"), "savestate: missing file rejected");
    check(screen_hash(b) == before, "savestate: failed load leaves the game untouched");
    std::remove("test_tmp.c8s");
    std::remove("test_bad.c8s");

    std::printf("\n%s (%d failure%s)\n", failures ? "TESTS FAILED" : "ALL TESTS PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
