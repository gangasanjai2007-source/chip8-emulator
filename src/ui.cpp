// On-screen control panels, drawn with Dear ImGui (https://github.com/ocornut/imgui, MIT licence).
//
// Dear ImGui is an "immediate mode" UI library: every frame we simply call functions like
// ImGui::Button("Save"), and the call returns true on the frame the button is clicked.
// There are no callbacks or widget objects to keep in sync with the emulator; the panels
// always show the current state because they are rebuilt from it 60 times per second.
//
// Layout (1280x720 window):
//   +--------------------------+------------+
//   |  game (960x480)          |  Controls  |
//   +--------------+-----------+  (320 wide)|
//   |  Debugger    | ROM       |            |
//   |              | browser   |            |
//   +--------------+-----------+------------+
#include "app.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// Places the next panel at a fixed spot; the panels cannot be moved or resized
static void fixed_window(const char* title, float x, float y, float w, float h){
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse);
}

// ---------------- Controls panel ----------------
static void controls_panel(App& app){
    Settings& s = app.s;
    fixed_window("Controls", UI_GAME_W, 0, UI_W - UI_GAME_W, UI_H);

    std::string name = app.rom_path.empty() ? "(none)" : fs::path(app.rom_path).filename().string();
    ImGui::Text("ROM: %s", name.c_str());
    ImGui::TextWrapped("%s", app.status.c_str());
    ImGui::Separator();

    // Run control
    if(ImGui::Button(s.paused ? "Resume (Space)" : "Pause (Space)")) app_toggle_pause(app);
    ImGui::SameLine();
    ImGui::BeginDisabled(!s.paused);
    if(ImGui::Button("Step (N)")) s.step_once = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Restart")) app_restart(app);

    // Rewind while the button is held down (same as holding Tab)
    static bool rewinding_by_mouse = false;
    ImGui::Button("Hold to rewind (Tab)");
    bool held = ImGui::IsItemActive();
    if(held != rewinding_by_mouse){
        s.rewinding = held;
        rewinding_by_mouse = held;
        app_update_title(app);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.1f s saved", app.history.size() / 60.0);

    if(ImGui::SliderInt("Speed", &s.cycles_per_frame, MIN_CYCLES, MAX_CYCLES, "%d ops/frame"))
        app_update_title(app);

    // Savestates
    if(ImGui::Button("Save state (K)")) app_save_state(app);
    ImGui::SameLine();
    if(ImGui::Button("Load state (L)")) app_load_state(app);

    // Quirks mode
    int mode = app.chip8.cosmac_quirks ? 0 : 1;
    if(ImGui::Combo("Mode (M)", &mode, "CHIP-8 (original)\0SUPER-CHIP\0")){
        app.chip8.cosmac_quirks = (mode == 0);
        app_update_title(app);
    }
    ImGui::TextDisabled("Resolution: %dx%d%s", app.chip8.screen_width(), app.chip8.screen_height(),
                        app.chip8.hires ? " (hi-res)" : "");

    ImGui::SeparatorText("Display");
    if(ImGui::BeginCombo("Palette (P)", PALETTES[s.palette].name)){
        for(int i = 0; i < NUM_PALETTES; i++)
            if(ImGui::Selectable(PALETTES[i].name, s.palette == i)){ s.palette = i; app_update_title(app); }
        ImGui::EndCombo();
    }
    // The "Custom" palette can be edited with colour pickers
    Palette& p = PALETTES[s.palette];
    if(std::string(p.name) == "Custom"){
        float on[3]  = {p.on_r / 255.f,  p.on_g / 255.f,  p.on_b / 255.f};
        float off[3] = {p.off_r / 255.f, p.off_g / 255.f, p.off_b / 255.f};
        if(ImGui::ColorEdit3("Pixels", on)){
            p.on_r = (uint8_t)(on[0]*255); p.on_g = (uint8_t)(on[1]*255); p.on_b = (uint8_t)(on[2]*255);
        }
        if(ImGui::ColorEdit3("Background", off)){
            p.off_r = (uint8_t)(off[0]*255); p.off_g = (uint8_t)(off[1]*255); p.off_b = (uint8_t)(off[2]*255);
        }
    }
    ImGui::Checkbox("Phosphor fade (G)", &s.phosphor);
    ImGui::SameLine();
    ImGui::Checkbox("Scanlines (H)", &s.scanlines);

    ImGui::SeparatorText("Sound");
    int wave = app.audio.waveform;
    float freq = (float)app.audio.frequency;
    bool changed = ImGui::Combo("Waveform (T)", &wave, "Square\0Sine\0Triangle\0Sawtooth\0");
    changed |= ImGui::SliderFloat("Pitch", &freq, 110.f, 1760.f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
    if(changed) app_set_sound(app, wave, freq);
    if(ImGui::Button("Test sound")) app.test_beep_frames = 30; // half a second

    // On-screen CHIP-8 keypad in its original layout. Buttons light up when a key is
    // pressed (keyboard or mouse); holding a button with the mouse presses that key.
    ImGui::SeparatorText("Keypad");
    static const int layout[16] = {0x1,0x2,0x3,0xC, 0x4,0x5,0x6,0xD, 0x7,0x8,0x9,0xE, 0xA,0x0,0xB,0xF};
    static const char* pc_keys[16] = {"X","1","2","3","Q","W","E","A","S","D","Z","C","4","R","F","V"};
    for(int i = 0; i < 16; i++){
        int k = layout[i];
        bool lit = app.chip8.key[k] || app.ui_keys[k];
        if(lit) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.6f, 0.1f, 1.0f));
        char label[16];
        std::snprintf(label, sizeof(label), "%X\n(%s)", k, pc_keys[k]);
        ImGui::Button(label, ImVec2(62, 40));
        app.ui_keys[k] = ImGui::IsItemActive();
        if(lit) ImGui::PopStyleColor();
        if(i % 4 != 3) ImGui::SameLine();
    }

    ImGui::Separator();
    ImGui::TextDisabled("F1 hides these panels");
    ImGui::End();
}

// ---------------- Debugger panel ----------------
static void debugger_panel(App& app){
    const Chip8& c = app.chip8;
    fixed_window("Debugger", 0, UI_GAME_H, 600, UI_H - UI_GAME_H);

    // Left column: registers
    ImGui::BeginChild("regs", ImVec2(250, 0));
    ImGui::Text("PC %03X   I %03X   SP %X", c.get_pc(), c.get_index(), c.get_sp());
    ImGui::Text("DT %02X    ST %02X", c.get_delay_timer(), c.get_sound_timer());
    ImGui::Separator();
    if(ImGui::BeginTable("v", 4, ImGuiTableFlags_BordersInnerV)){
        for(int i = 0; i < 16; i++){
            ImGui::TableNextColumn();
            ImGui::Text("V%X %02X", i, c.get_v(i));
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
    ImGui::Text("Stack:");
    for(int i = c.get_sp() - 1; i >= 0; i--) ImGui::Text("  [%d] %03X", i, c.get_stack(i));
    if(c.get_sp() == 0) ImGui::TextDisabled("  (empty)");
    ImGui::EndChild();

    ImGui::SameLine();

    // Right column: disassembly around PC. Click a line to set / clear a breakpoint.
    ImGui::BeginChild("disasm", ImVec2(0, 0), ImGuiChildFlags_Borders);

    // Breakpoint controls: type an address in hex, or click a line below
    static char bp_text[8] = "";
    ImGui::SetNextItemWidth(60);
    if(ImGui::InputTextWithHint("##bp", "addr", bp_text, sizeof(bp_text),
                                ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)
       || (ImGui::SameLine(), ImGui::SmallButton("Set breakpoint"))){
        unsigned addr = 0;
        if(std::sscanf(bp_text, "%x", &addr) == 1 && addr < 0x1000) app.breakpoint = (int)addr;
    }
    ImGui::SameLine();
    if(app.breakpoint >= 0){
        ImGui::Text("at %03X", app.breakpoint);
        ImGui::SameLine();
        if(ImGui::SmallButton("clear")) app.breakpoint = -1;
    }
    else ImGui::TextDisabled("(or click a line)");

    // The listing stays still while PC moves inside it, so lines can be clicked;
    // it only jumps when PC leaves the visible range.
    const int LINES = 24;
    static int view_start = 0x200;
    int pc = c.get_pc();
    if(pc < view_start + 4 || pc >= view_start + (LINES - 4) * 2) view_start = std::max(0, pc - 8) & ~1;
    for(int addr = view_start; addr < view_start + LINES * 2 && addr < 0xFFF; addr += 2){
        uint16_t op = (c.read_memory(addr) << 8) | c.read_memory(addr + 1);
        bool is_pc = (addr == pc), is_bp = (addr == app.breakpoint);
        char line[64];
        std::snprintf(line, sizeof(line), "%s %03X  %04X  %s", is_bp ? "*" : " ", addr, op,
                      Chip8::disassemble(op).c_str());
        if(is_pc) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
        if(is_bp) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        if(ImGui::Selectable(line, is_pc)) app.breakpoint = is_bp ? -1 : addr;
        if(is_bp) ImGui::PopStyleColor();
        if(is_pc) ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::End();
}

// ---------------- ROM browser panel ----------------
// Lists every .ch8 file under the roms/ folder; click one to load it.
static void rom_browser_panel(App& app){
    fixed_window("ROM browser", 600, UI_GAME_H, UI_GAME_W - 600, UI_H - UI_GAME_H);

    static std::vector<std::string> roms;
    static bool scanned = false;
    static char filter[64] = "";
    if(ImGui::Button("Refresh")) scanned = false;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "search...", filter, sizeof(filter));

    if(!scanned){
        roms.clear();
        std::error_code err; // no exceptions if the folder is missing
        if(fs::exists("roms", err)){
            for(auto& entry : fs::recursive_directory_iterator("roms", err)){
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if(entry.is_regular_file(err) && (ext == ".ch8" || ext == ".sc8"))
                    roms.push_back(entry.path().generic_string());
            }
        }
        std::sort(roms.begin(), roms.end());
        scanned = true;
    }

    ImGui::BeginChild("list", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if(roms.empty()) ImGui::TextDisabled("No .ch8 files found in roms/");
    std::string f = filter;
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
    for(const std::string& path : roms){
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        if(!f.empty() && lower.find(f) == std::string::npos) continue;
        if(ImGui::Selectable(path.c_str(), path == app.rom_path)) app_load_rom(app, path);
    }
    ImGui::EndChild();
    ImGui::End();
}

void ui_draw(App& app){
    controls_panel(app);
    debugger_panel(app);
    rom_browser_panel(app);
}
