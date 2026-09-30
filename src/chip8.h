#ifndef CHIP8_H
#define CHIP8_H

#include <cstdint>
#include <string>

class Chip8{
    public:
        Chip8();
        void load_rom(const std::string& filename); // To load a game file
        void emulate_cycle(); // To execute one instruction
        void tick_timers(); // Counts the timers down; called once per frame (60 Hz)
        bool draw_flag; // When we need to redraw the screen;

        // Screen buffer. Big enough for SUPER-CHIP high resolution (128x64).
        // Pixel (x, y) is display[x + y * screen_width()]; in normal resolution
        // only the first 64x32 = 2048 entries are used.
        uint8_t display[128*64];
        bool hires = false;   // SUPER-CHIP: true = 128x64 mode, false = 64x32
        bool halted = false;  // SUPER-CHIP 00FD: program asked the interpreter to exit
        int screen_width()  const { return hires ? 128 : 64; }
        int screen_height() const { return hires ? 64 : 32; }
        uint8_t key[16]; // Keyboard of 16 keys
        uint8_t get_sound_timer() const {return sound_timer;} // For getting the value of sound timer

        // Quirks mode. The original 1977 COSMAC VIP CHIP-8 and the later SUPER-CHIP
        // behave differently in a few instructions, and games are written for one or the other.
        // true  = original COSMAC VIP behaviour (default)
        // false = SUPER-CHIP / modern behaviour (needed by e.g. Blinky)
        bool cosmac_quirks = true;
        bool drew_sprite = false; // set by DXYN; lets main.cpp limit drawing to once per frame

        // Savestates: write / read the whole machine state to a file
        bool save_state(const std::string& filename) const;
        bool load_state(const std::string& filename);

        // Debugger: print PC, the next opcode and all registers to the terminal
        void print_state() const;
        // Turns a 2-byte opcode into readable assembly text, e.g. 0x6A05 -> "LD VA, 0x05"
        static std::string disassemble(uint16_t op);

        // Read-only access to the CPU state, used by the on-screen debugger
        uint16_t get_pc() const { return pc; }
        uint16_t get_index() const { return index; }
        uint8_t  get_sp() const { return sp; }
        uint8_t  get_v(int i) const { return v[i & 0xF]; }
        uint16_t get_stack(int i) const { return stack[i & 0xF]; }
        uint8_t  get_delay_timer() const { return delay_timer; }
        uint8_t  read_memory(uint16_t addr) const { return memory[addr & 0xFFF]; }
    private:
        uint8_t memory[4096]; // Memory of 4KB
        uint8_t v[16]; // 16 registers, V0 to VF
        uint16_t index; // Index register for memory addresses
        uint16_t pc; // Program counter
        uint16_t stack[16];
        uint8_t sp; // Stack pointer
        uint8_t delay_timer; // Counts down at 60Hz
        uint8_t sound_timer; // Beeps when greater than 0, counts down at 60Hz
        uint16_t opcode; // Current instruction
        uint8_t rpl[16];  // SUPER-CHIP "RPL user flags" (FX75 / FX85)
        void scroll_down(int n);   // SUPER-CHIP 00CN
        void scroll_right(int n);  // SUPER-CHIP 00FB
        void scroll_left(int n);   // SUPER-CHIP 00FC
        void initialise(); // Initialises everything
        void load_fonts(); // Loads font (0-9, A-F)
};

#endif
