#include "chip8.h"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <cstring>
#include <random>
#include <cstdio>

uint8_t chip8_fontset[80] = {
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80  // F
};

Chip8::Chip8(){
    initialise();
}

void Chip8::initialise(){
    pc = 0x200;
    opcode = 0;
    index = 0;
    sp = 0;

    memset(display, 0, sizeof(display));
    memset(stack, 0, sizeof(stack));
    memset(v, 0, sizeof(v));
    memset(memory, 0, sizeof(memory));
    memset(key, 0, sizeof(key));

    load_fonts();
    delay_timer = 0;
    sound_timer = 0;
    draw_flag = false;
}

void Chip8::load_fonts(){
    for(int i=0; i<80; i++) memory[i] = chip8_fontset[i];
}

void Chip8::load_rom(const std::string& filename){
    std::ifstream file(filename, std::ios::binary | std::ios::ate);

    if(!file.is_open()){
        std::cerr << "Failed to open ROM: " << filename << std::endl;
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    if(size > (4096-512)){ // 512 reserved for fonts/interpreter
        std::cerr << "ROM too large to fit in memory" << std::endl;
        return;
    }

    file.read((char*)(memory+512),size);
    file.close();

    std::cout << "Loaded ROM: " << filename << std::endl;
}

void Chip8::emulate_cycle(){
    pc &= 0xFFF; // FIX: keep PC inside 4KB so a bad jump can't read outside memory
    opcode = memory[pc] << 8 | memory[(pc+1) & 0xFFF]; // 16-bit instruction

    switch(opcode & 0xF000){ // Gets only the first 4 bits
        case 0x0000:
            switch(opcode & 0x00FF){
                case 0x00E0: // Clears the display
                    std::memset(display, 0, sizeof(display));
                    draw_flag = true;
                    pc += 2;
                    break;
                case 0x00EE: // Returns from subroutine
                    // FIX: sp points to the next FREE slot, so decrement first, then read
                    if(sp == 0){
                        std::cerr << "Stack underflow at 0x" << std::hex << pc << std::endl;
                        pc += 2;
                        break;
                    }
                    sp--;
                    pc = stack[sp];
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        case 0x1000: // 1XXX = Jump to address XXX
            pc = opcode & 0x0FFF;
            break;
        case 0x2000: // 2XXX = Call subroutine at XXX
            if(sp >= 16){ // FIX: guard against stack overflow (only 16 levels)
                std::cerr << "Stack overflow at 0x" << std::hex << pc << std::endl;
                pc += 2;
                break;
            }
            stack[sp] = pc;
            sp++;
            pc = opcode & 0x0FFF;
            break;
        case 0x3000: // 3XNN = Skip next instruction if v[x] = NN
            if(v[(opcode & 0x0F00) >> 8] == (opcode & 0x00FF)) pc += 4;
            else pc += 2;
            break;
        case 0x4000:  // 4XNN - Skip next instruction if v[x] != NN
            if (v[(opcode & 0x0F00) >> 8] != (opcode & 0x00FF)) pc += 4;
            else pc += 2;
            break;
        case 0x5000:  // 5XY0 - Skip next instruction if v[x] == v[y]
            if (v[(opcode & 0x0F00) >> 8] == v[(opcode & 0x00F0) >> 4]) pc += 4;
            else pc += 2;
            break;
        case 0x6000: // 6XNN = set v[n] = NN
            v[(opcode & 0x0F00) >> 8] = opcode & 0x00FF;
            pc += 2;
            break;
        case 0x7000: //7XNN = add NN to v[x]
            v[(opcode & 0x0F00) >> 8] += opcode & 0x00FF;
            pc += 2;
            break;
        case 0x8000: // Arithmetic operations
            switch(opcode & 0x000F){ // Look only at last 4 bits
                // Instruction is of the form 8XYN
                case 0x0000: // v[x] = v[y]
                    v[(opcode & 0x0F00) >> 8] = v[(opcode & 0x00F0) >> 4];
                    pc += 2;
                    break;
                case 0x0001: // v[x] = v[x] | v[y]
                    v[(opcode & 0x0F00) >> 8] |= v[(opcode & 0x00F0) >> 4];
                    if(cosmac_quirks) v[0xF] = 0; // quirk: original CHIP-8 resets VF
                    pc += 2;
                    break;
                case 0x0002: // v[x] = v[x] & v[y]
                    v[(opcode & 0x0F00) >> 8] &= v[(opcode & 0x00F0) >> 4];
                    if(cosmac_quirks) v[0xF] = 0; // quirk: original CHIP-8 resets VF
                    pc += 2;
                    break;
                case 0x0003: // v[x] = v[x] ^ v[y]
                    v[(opcode & 0x0F00) >> 8] ^= v[(opcode & 0x00F0) >> 4];
                    if(cosmac_quirks) v[0xF] = 0; // quirk: original CHIP-8 resets VF
                    pc += 2;
                    break;
                // FIX (4,5,6,7,E): compute the flag first, write the result, then write VF LAST.
                // Otherwise, when X is F, the result overwrites the flag.
                case 0x0004:{ // v[x] += v[y], v[F] = carry
                    uint16_t sum = v[(opcode & 0x0F00) >> 8] + v[(opcode & 0x00F0) >> 4];
                    uint8_t flag = (sum > 0xFF) ? 1 : 0;
                    v[(opcode & 0x0F00) >> 8] = sum & 0xFF;
                    v[0xF] = flag;
                    pc += 2;
                }
                    break;
                case 0x0005:{ // v[x] -= v[y], v[F] = NOT(borrow)
                    uint8_t vx = v[(opcode & 0x0F00) >> 8], vy = v[(opcode & 0x00F0) >> 4];
                    uint8_t flag = (vx >= vy) ? 1 : 0; // FIX: >= (equal values do not borrow)
                    v[(opcode & 0x0F00) >> 8] = vx - vy;
                    v[0xF] = flag;
                    pc += 2;
                }
                    break;
                case 0x0006:{ // v[x] >>= 1, v[F] = LSB
                    // quirk: original CHIP-8 shifts VY into VX; SUPER-CHIP shifts VX in place
                    uint8_t vx = cosmac_quirks ? v[(opcode & 0x00F0) >> 4] : v[(opcode & 0x0F00) >> 8];
                    v[(opcode & 0x0F00) >> 8] = vx >> 1;
                    v[0xF] = vx & 0x1;
                    pc += 2;
                }
                    break;
                case 0x0007:{ // v[x] = v[y] - v[x], v[F] = NOT(borrow)
                    uint8_t vx = v[(opcode & 0x0F00) >> 8], vy = v[(opcode & 0x00F0) >> 4];
                    uint8_t flag = (vy >= vx) ? 1 : 0; // FIX: >=
                    v[(opcode & 0x0F00) >> 8] = vy - vx;
                    v[0xF] = flag;
                    pc += 2;
                }
                    break;
                case 0x000E:{ // v[x] <<= 1, v[F] = MSB
                    uint8_t vx = cosmac_quirks ? v[(opcode & 0x00F0) >> 4] : v[(opcode & 0x0F00) >> 8];
                    v[(opcode & 0x0F00) >> 8] = vx << 1;
                    v[0xF] = vx >> 7;
                    pc += 2;
                }
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
                    break;
            }
            break;
        case 0x9000: // 9XY0 = Skip next instr. if v[x] != v[y]
            if (v[(opcode & 0x0F00) >> 8] != v[(opcode & 0x00F0) >> 4])
                pc += 4;
            else
                pc += 2;
            break;
        case 0xA000: // AXXX = set index to XXX
            index = opcode & 0x0FFF;
            pc += 2;
            break;
        case 0xB000: // BXXX = jump to address XXX + v[0]
            pc = (opcode & 0xFFF) + v[0];
            break;
        case 0xC000:{  // CXNN - v[x] = random_byte & NN
            static std::random_device rd;
            static std::mt19937 gen(rd());
            static std::uniform_int_distribution<> dist(0, 255);
            v[(opcode & 0x0F00) >> 8] = dist(gen) & (opcode & 0x00FF);
            pc += 2;
        }
            break;
        case 0xD000:{ // DXYN = draw sprite at (v[x], v[y]) with height N
            uint8_t x = v[(opcode & 0x0F00) >> 8], y = v[(opcode & 0x00F0) >> 4];
            uint8_t height = opcode & 0x000F, pixel;

            v[0xF] = 0; // Resetting collision flag
            // Looping through each row of the sprite
            for(int y_line=0; y_line<height; y_line++){
                pixel = memory[(index + y_line) & 0xFFF]; // One row of sprite data (FIX: stay inside 4KB)
                // Now looping through each pixel in the row (8)
                for(int x_line=0; x_line<8; x_line++){
                    // Check if current pixel is 1
                    if((pixel & (0x80 >> x_line)) != 0){
                        int screen_x = (x & 63) + x_line, screen_y = (y & 31) + y_line;
                        if (screen_x >= 64 || screen_y >= 32) continue;
                        int screen_index = screen_x + (screen_y*64); // 1D display array
                        // Checking for collision
                        if(display[screen_index] == 1) v[0xF] = 1; // Set collision flag
                        // We now flip the pixel
                        display[screen_index] ^= 1;


                    }
                }
            }
            draw_flag = true;
            drew_sprite = true;
            pc += 2;
        }
            break;
        case 0xE000: 
            switch(opcode & 0x00FF){
                case 0x009E: // EX9E = skip next instr. if key[v[x]] is pressed
                    if(key[v[(opcode & 0x0F00) >> 8] & 0xF] != 0) pc += 4;
                    else pc += 2;
                    break;
                case 0x00A1: // EXA1 = skip next instr. if key[v[x]] is not pressed
                    if(key[v[(opcode & 0x0F00) >> 8] & 0xF] == 0) pc += 4;
                    else pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        case 0xF000: // Timers and memory operations
            switch(opcode & 0x00FF){
                case 0x0007: // FX07 - v[x] = delay_timer
                    v[(opcode & 0x0F00) >> 8] = delay_timer;
                    pc += 2;
                    break;
                case 0x000A:{ // FX0A - wait for key press, store in v[x]
                    bool key_pressed = false;
                    for(int i=0; i<16; i++){
                        if(key[i] != 0){
                            v[(opcode & 0x0F00) >> 8] = i;
                            key_pressed = true;
                            break;
                        }
                    }
                    // FIX: only move on once a key is pressed; otherwise run this
                    // same instruction again next cycle (this is how "waiting" works)
                    if(key_pressed) pc += 2;
                }
                    break;
                case 0x0015: // FX15 - delay_timer = v[x]
                    delay_timer = v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x0018: // FX18 - sound_timer = v[x]
                    sound_timer = v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x001E: // FX1E - index += v[x]
                    index += v[(opcode & 0x0F00) >> 8];
                    pc += 2;
                    break;
                case 0x0029: // FX29 - index = location of sprite for digit v[x]
                    index = (v[(opcode & 0x0F00) >> 8] & 0xF) * 5; // each font digit is 5 bytes
                    pc += 2;
                    break;
                case 0x0033:{ // FX33 - store BCD representation of v[x] at index
                    uint8_t value = v[(opcode & 0x0F00) >> 8];
                    memory[index & 0xFFF] = value/100;           // hundreds
                    memory[(index+1) & 0xFFF] = (value/10)%10;   // FIX: tens (was value/10, e.g. 12 for 123)
                    memory[(index+2) & 0xFFF] = value%10;        // ones
                    pc += 2;
                }
                    break;
                case 0x0055: // FX55 - store v[0] to v[x] in memory starting from index
                    for(int i=0; i<=((opcode & 0x0F00) >> 8); i++){ // FIX: <= (V0 to VX includes VX)
                        memory[(index+i) & 0xFFF] = v[i];
                    }
                    // quirk: original CHIP-8 leaves I pointing after the last byte
                    if(cosmac_quirks) index += ((opcode & 0x0F00) >> 8) + 1;
                    pc += 2;
                    break;
                case 0x0065: // FX65 - Fill v[0] to v[x] from memory starting at index
                    for(int i=0; i<=((opcode & 0x0F00) >> 8); i++){ // FIX: <=
                        v[i] = memory[(index+i) & 0xFFF];
                    }
                    if(cosmac_quirks) index += ((opcode & 0x0F00) >> 8) + 1; // quirk (see FX55)
                    pc += 2;
                    break;
                default:
                    std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
                    pc += 2;
            }
            break;
        default:
            std::cerr << "Unknown opcode: 0x" << std::hex << opcode << std::endl;
            pc += 2;
            break;
    }
    // FIX: timers are no longer decremented here. They must count down at 60 Hz,
    // not once per instruction, so main.cpp calls tick_timers() once per frame.
}

void Chip8::tick_timers(){
    if(delay_timer > 0) delay_timer--;
    if(sound_timer > 0) sound_timer--;
}

// ---------------- Savestates ----------------
// File layout: 4-byte magic "C8SV", 1-byte version, then every part of the
// machine state in a fixed order. Binary is used because the state is a fixed
// ~6 KB block of bytes; no parsing library is needed.
static const char SAVE_MAGIC[4] = {'C','8','S','V'};
static const uint8_t SAVE_VERSION = 1;

bool Chip8::save_state(const std::string& filename) const{
    std::ofstream f(filename, std::ios::binary);
    if(!f){
        std::cerr << "Could not open " << filename << " for writing" << std::endl;
        return false;
    }
    f.write(SAVE_MAGIC, 4);
    f.write((const char*)&SAVE_VERSION, 1);
    f.write((const char*)memory, sizeof(memory));
    f.write((const char*)v, sizeof(v));
    f.write((const char*)&index, sizeof(index));
    f.write((const char*)&pc, sizeof(pc));
    f.write((const char*)stack, sizeof(stack));
    f.write((const char*)&sp, sizeof(sp));
    f.write((const char*)&delay_timer, sizeof(delay_timer));
    f.write((const char*)&sound_timer, sizeof(sound_timer));
    f.write((const char*)display, sizeof(display));
    if(!f){
        std::cerr << "Error while writing " << filename << std::endl;
        return false;
    }
    std::cout << "State saved to " << filename << std::endl;
    return true;
}

bool Chip8::load_state(const std::string& filename){
    std::ifstream f(filename, std::ios::binary);
    if(!f){
        std::cerr << "No savestate found: " << filename << std::endl;
        return false;
    }
    char magic[4];
    uint8_t version = 0;
    f.read(magic, 4);
    f.read((char*)&version, 1);
    if(!f || std::memcmp(magic, SAVE_MAGIC, 4) != 0 || version != SAVE_VERSION){
        std::cerr << "Invalid or incompatible savestate" << std::endl;
        return false;
    }

    // Read into a temporary copy first, so a broken file can never leave
    // the running emulator half-loaded.
    Chip8 tmp = *this;
    f.read((char*)tmp.memory, sizeof(tmp.memory));
    f.read((char*)tmp.v, sizeof(tmp.v));
    f.read((char*)&tmp.index, sizeof(tmp.index));
    f.read((char*)&tmp.pc, sizeof(tmp.pc));
    f.read((char*)tmp.stack, sizeof(tmp.stack));
    f.read((char*)&tmp.sp, sizeof(tmp.sp));
    f.read((char*)&tmp.delay_timer, sizeof(tmp.delay_timer));
    f.read((char*)&tmp.sound_timer, sizeof(tmp.sound_timer));
    f.read((char*)tmp.display, sizeof(tmp.display));
    if(!f){
        std::cerr << "Savestate file is truncated" << std::endl;
        return false;
    }
    // Validate values, so an edited file can't make the CPU read outside memory/stack
    if(tmp.pc >= 4096 || tmp.index >= 4096 || tmp.sp > 16){
        std::cerr << "Savestate has out-of-range values" << std::endl;
        return false;
    }

    *this = tmp;
    std::memset(key, 0, sizeof(key)); // don't restore keys that were held when saving
    draw_flag = true;
    std::cout << "State loaded from " << filename << std::endl;
    return true;
}

// ---------------- Debugger ----------------
// Turns a 2-byte opcode into readable assembly text, e.g. 0x6A05 -> "LD VA, 0x05"
static std::string disassemble(uint16_t op){
    char buf[32];
    unsigned x = (op >> 8) & 0xF, y = (op >> 4) & 0xF, n = op & 0xF, nn = op & 0xFF, nnn = op & 0xFFF;
    switch(op & 0xF000){
        case 0x0000:
            if(op == 0x00E0) return "CLS";
            if(op == 0x00EE) return "RET";
            std::snprintf(buf, sizeof(buf), "SYS 0x%03X", nnn); break;
        case 0x1000: std::snprintf(buf, sizeof(buf), "JP 0x%03X", nnn); break;
        case 0x2000: std::snprintf(buf, sizeof(buf), "CALL 0x%03X", nnn); break;
        case 0x3000: std::snprintf(buf, sizeof(buf), "SE V%X, 0x%02X", x, nn); break;
        case 0x4000: std::snprintf(buf, sizeof(buf), "SNE V%X, 0x%02X", x, nn); break;
        case 0x5000: std::snprintf(buf, sizeof(buf), "SE V%X, V%X", x, y); break;
        case 0x6000: std::snprintf(buf, sizeof(buf), "LD V%X, 0x%02X", x, nn); break;
        case 0x7000: std::snprintf(buf, sizeof(buf), "ADD V%X, 0x%02X", x, nn); break;
        case 0x8000: {
            static const char* ops[16] = {"LD","OR","AND","XOR","ADD","SUB","SHR","SUBN",
                                          "?","?","?","?","?","?","SHL","?"};
            std::snprintf(buf, sizeof(buf), "%s V%X, V%X", ops[n], x, y); break;
        }
        case 0x9000: std::snprintf(buf, sizeof(buf), "SNE V%X, V%X", x, y); break;
        case 0xA000: std::snprintf(buf, sizeof(buf), "LD I, 0x%03X", nnn); break;
        case 0xB000: std::snprintf(buf, sizeof(buf), "JP V0, 0x%03X", nnn); break;
        case 0xC000: std::snprintf(buf, sizeof(buf), "RND V%X, 0x%02X", x, nn); break;
        case 0xD000: std::snprintf(buf, sizeof(buf), "DRW V%X, V%X, %u", x, y, n); break;
        case 0xE000:
            if(nn == 0x9E){ std::snprintf(buf, sizeof(buf), "SKP V%X", x); break; }
            if(nn == 0xA1){ std::snprintf(buf, sizeof(buf), "SKNP V%X", x); break; }
            return "???";
        case 0xF000:
            switch(nn){
                case 0x07: std::snprintf(buf, sizeof(buf), "LD V%X, DT", x); break;
                case 0x0A: std::snprintf(buf, sizeof(buf), "LD V%X, K", x); break;
                case 0x15: std::snprintf(buf, sizeof(buf), "LD DT, V%X", x); break;
                case 0x18: std::snprintf(buf, sizeof(buf), "LD ST, V%X", x); break;
                case 0x1E: std::snprintf(buf, sizeof(buf), "ADD I, V%X", x); break;
                case 0x29: std::snprintf(buf, sizeof(buf), "LD F, V%X", x); break;
                case 0x33: std::snprintf(buf, sizeof(buf), "LD B, V%X", x); break;
                case 0x55: std::snprintf(buf, sizeof(buf), "LD [I], V0-V%X", x); break;
                case 0x65: std::snprintf(buf, sizeof(buf), "LD V0-V%X, [I]", x); break;
                default: return "???";
            }
            break;
        default: return "???";
    }
    return buf;
}

void Chip8::print_state() const{
    uint16_t next = (memory[pc & 0xFFF] << 8) | memory[(pc + 1) & 0xFFF];
    std::printf("PC=%03X  OP=%04X  %-18s I=%03X  SP=%X  DT=%02X  ST=%02X\n",
                pc, next, disassemble(next).c_str(), index, sp, delay_timer, sound_timer);
    for(int i = 0; i < 16; i++){
        std::printf("V%X=%02X%s", i, v[i], (i == 7 || i == 15) ? "\n" : " ");
    }
    std::fflush(stdout); // show it immediately (the Windows terminal buffers output)
}
