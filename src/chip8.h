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
        uint8_t display[64*32];
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
        void initialise(); // Initialises everything
        void load_fonts(); // Loads font (0-9, A-F)
};

#endif
