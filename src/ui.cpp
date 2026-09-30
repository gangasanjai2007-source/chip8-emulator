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
#include "games.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>
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

// ---------------- Help tab ----------------
// Explains the controls for the loaded game (from games.h) and lists every emulator shortcut.
static void help_tab(App& app){
    const GameInfo* g = app.rom_path.empty() ? nullptr : find_game(app.rom_path);

    ImGui::SeparatorText("This game");
    if(app.rom_path.empty()){
        ImGui::TextWrapped("No game loaded. Pick one in the ROM browser below the game screen.");
    }
    else if(g){
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", g->title);
        ImGui::TextWrapped("%s", g->about);
        if(g->keys[0].action && ImGui::BeginTable("gamekeys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)){
            ImGui::TableSetupColumn("Action");
            ImGui::TableSetupColumn("Press");
            ImGui::TableSetupColumn("CHIP-8 key");
            ImGui::TableHeadersRow();
            for(const GameKey& k : g->keys){
                if(!k.action) break;
                ImGui::TableNextColumn(); ImGui::Text("%s", k.action);
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%s", PC_KEY_NAMES[k.key]);
                ImGui::TableNextColumn(); ImGui::TextDisabled("%X", k.key);
            }
            ImGui::EndTable();
        }
        if(g->tips[0]) ImGui::TextWrapped("Tip: %s", g->tips);

        // One click sets the mode and speed this game needs
        bool mode_ok = (app.chip8.cosmac_quirks == !g->needs_schip);
        if(!mode_ok || app.s.cycles_per_frame != g->speed || app.s.display_wait != g->vblank){
            char label[96];
            std::snprintf(label, sizeof(label), "Apply: %s mode, speed %d",
                          g->needs_schip ? "SUPER-CHIP" : "CHIP-8", g->speed);
            ImGui::TextDisabled("Recommended settings:");
            if(ImGui::Button(label)) app_apply_recommended(app);
        }
        else ImGui::TextDisabled("Recommended settings are active.");
    }
    else{
        ImGui::TextWrapped("No built-in notes for this ROM. CHIP-8 games use the 4x4 keypad; many use "
                           "2 / Q / E / S (CHIP-8 keys 2, 4, 6, 8) for up / left / right / down and "
                           "W (key 5) as the action button. Watch the keypad below while you try keys, "
                           "and use the debugger to see which keys the game checks (SKP / SKNP).");
    }

    ImGui::SeparatorText("Emulator shortcuts");
    static const char* shortcuts[][2] = {
        {"= / -",       "Faster / slower (instructions per frame)"},
        {"Space",       "Pause / resume"},
        {"N",           "Step one instruction (while paused)"},
        {"Tab (hold)",  "Rewind time, up to 10 seconds"},
        {"K or F5",     "Save state"},
        {"L or F9",     "Load state"},
        {"Backspace",   "Restart the game"},
        {"P",           "Next colour palette"},
        {"G",           "CRT phosphor fade on / off"},
        {"H",           "Scanlines on / off"},
        {"T",           "Next sound waveform"},
        {"[ / ]",       "Beep pitch down / up"},
        {"M",           "CHIP-8 / SUPER-CHIP mode"},
        {"F1",          "Show / hide these panels"},
        {"F11",         "Play mode: fullscreen retro console"},
        {"F2",          "Eject the cartridge (start screen)"},
        {"Drag & drop", "Drop a .ch8 file to load it"},
        {"Esc",         "Quit"},
    };
    if(ImGui::BeginTable("shortcuts", 2, ImGuiTableFlags_RowBg)){
        ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("What it does");
        for(auto& row : shortcuts){
            ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "%s", row[0]);
            ImGui::TableNextColumn(); ImGui::TextWrapped("%s", row[1]);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Keyboard = CHIP-8 keypad");
    ImGui::TextDisabled("  1 2 3 4        1 2 3 C");
    ImGui::TextDisabled("  Q W E R   =    4 5 6 D");
    ImGui::TextDisabled("  A S D F        7 8 9 E");
    ImGui::TextDisabled("  Z X C V        A 0 B F");
}

// ---------------- Side panel: Controls / Help tabs + keypad ----------------
static void side_panel(App& app){
    Settings& s = app.s;
    fixed_window("Controls", UI_GAME_W, 0, UI_W - UI_GAME_W, UI_H);

    std::string name = app.rom_path.empty() ? "(none)" : fs::path(app.rom_path).filename().string();
    ImGui::Text("ROM: %s", name.c_str());
    ImGui::TextWrapped("%s", app.status.c_str());

    const float KEYPAD_H = 250; // space kept at the bottom for the keypad
    ImGui::BeginChild("tabs", ImVec2(0, -KEYPAD_H));
    if(ImGui::BeginTabBar("sidetabs")){
        if(ImGui::BeginTabItem("Controls")){
            // Run control
            if(ImGui::Button(s.paused ? "Resume (Space)" : "Pause (Space)")) app_toggle_pause(app);
            ImGui::SameLine();
            ImGui::BeginDisabled(!s.paused);
            if(ImGui::Button("Step (N)")) s.step_once = true;
            ImGui::EndDisabled();
            ImGui::SameLine();
            if(ImGui::Button("Restart")) app_restart(app);
            if(ImGui::Button("Eject (F2)")) app_eject(app);
            ImGui::SameLine();
            if(ImGui::Button("Play mode: fullscreen (F11)")) app_set_play_mode(app, true);

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
                s.display_wait = (mode == 0); // the original CHIP-8 waits, SUPER-CHIP doesn't
                app_update_title(app);
            }
            ImGui::Checkbox("Wait for screen refresh", &s.display_wait);
            if(ImGui::IsItemHovered())
                ImGui::SetTooltip("At most one sprite draw per frame, like the original 1977 machine.\n"
                                  "Newer games (written for the Octo emulator) expect this off.");
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
            ImGui::Checkbox("Glow", &s.glow);

            ImGui::SeparatorText("Sound");
            int wave = app.audio.waveform;
            float freq = (float)app.audio.frequency;
            bool changed = ImGui::Combo("Waveform (T)", &wave, "Square\0Sine\0Triangle\0Sawtooth\0");
            changed |= ImGui::SliderFloat("Pitch", &freq, 110.f, 1760.f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
            if(changed) app_set_sound(app, wave, freq);
            if(ImGui::Button("Test sound")) app.test_beep_frames = 30; // half a second
            ImGui::EndTabItem();
        }
        // Switch to the Help tab automatically when a known game is loaded
        ImGuiTabItemFlags help_flags = app.open_help_tab ? ImGuiTabItemFlags_SetSelected : 0;
        app.open_help_tab = false;
        if(ImGui::BeginTabItem("Help", nullptr, help_flags)){
            help_tab(app);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    // On-screen CHIP-8 keypad in its original layout. Buttons light up when a key is
    // pressed (keyboard or mouse); holding a button with the mouse presses that key.
    // For known games, each key also shows what it does in that game.
    const GameInfo* g = app.rom_path.empty() ? nullptr : find_game(app.rom_path);
    ImGui::SeparatorText(g ? "Keypad (labels for this game)" : "Keypad");
    static const int layout[16] = {0x1,0x2,0x3,0xC, 0x4,0x5,0x6,0xD, 0x7,0x8,0x9,0xE, 0xA,0x0,0xB,0xF};
    for(int i = 0; i < 16; i++){
        int k = layout[i];
        const char* action = "";
        if(g) for(const GameKey& gk : g->keys) if(gk.action && gk.key == k) action = gk.action;

        bool lit = app.chip8.key[k] || app.ui_keys[k];
        int colours = 0;
        if(action[0]){ ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.45f, 0.35f, 1.0f)); colours++; }
        if(lit){ ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.6f, 0.1f, 1.0f)); colours++; }
        char label[48];
        // "##k%X" gives each button a fixed ID, so changing the label text doesn't break holding it
        std::snprintf(label, sizeof(label), "%X  (%s)\n%s##k%X", k, PC_KEY_NAMES[k], action, k);
        ImGui::Button(label, ImVec2(70, 44));
        app.ui_keys[k] = ImGui::IsItemActive();
        ImGui::PopStyleColor(colours);
        if(i % 4 != 3) ImGui::SameLine();
    }
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

// ================= Retro look =================
static ImFont* font_title = nullptr; // Press Start 2P, 16 px (arcade pixel font)
static ImFont* font_big   = nullptr; // Press Start 2P, 32 px

void ui_load_fonts(){
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault(); // ProggyClean: already a crisp pixel font, used for the panels
    // The pixel font is loaded from the assets folder; if it is missing we fall back to the default
    const char* path = "assets/fonts/PressStart2P-Regular.ttf";
    std::error_code err;
    if(fs::exists(path, err)){
        font_title = io.Fonts->AddFontFromFileTTF(path, 16.0f);
        font_big   = io.Fonts->AddFontFromFileTTF(path, 32.0f);
    }
}

// Retro theme: square corners, 1-pixel borders, and every colour derived from the
// currently selected palette, so the whole interface matches the game screen.
static void apply_theme(const Palette& pal){
    // Use the brighter of the palette's two colours as the accent (Game Boy's "on" colour is dark)
    int on_lum = pal.on_r + pal.on_g + pal.on_b, off_lum = pal.off_r + pal.off_g + pal.off_b;
    float ar, ag, ab;
    if(on_lum >= off_lum){ ar = pal.on_r / 255.f;  ag = pal.on_g / 255.f;  ab = pal.on_b / 255.f; }
    else                 { ar = pal.off_r / 255.f; ag = pal.off_g / 255.f; ab = pal.off_b / 255.f; }
    auto accent = [&](float k, float a = 1.0f){ return ImVec4(ar * k, ag * k, ab * k, a); };
    auto tint   = [&](float t){ return ImVec4(ar + (1 - ar) * t, ag + (1 - ag) * t, ab + (1 - ab) * t, 1.0f); };

    ImGuiStyle& st = ImGui::GetStyle();
    st.WindowRounding = st.FrameRounding = st.GrabRounding = st.TabRounding = 0.0f;
    st.ChildRounding = st.PopupRounding = st.ScrollbarRounding = 0.0f;
    st.WindowBorderSize = st.FrameBorderSize = 1.0f;
    st.WindowTitleAlign = ImVec2(0.5f, 0.5f);

    ImVec4* c = st.Colors;
    c[ImGuiCol_Text]                 = tint(0.55f);
    c[ImGuiCol_TextDisabled]         = accent(0.55f);
    c[ImGuiCol_WindowBg]             = ImVec4(0.035f, 0.035f, 0.04f, 1.0f);
    c[ImGuiCol_ChildBg]              = ImVec4(0.02f, 0.02f, 0.025f, 1.0f);
    c[ImGuiCol_PopupBg]              = ImVec4(0.05f, 0.05f, 0.06f, 0.98f);
    c[ImGuiCol_Border]               = accent(0.45f);
    c[ImGuiCol_FrameBg]              = accent(0.12f);
    c[ImGuiCol_FrameBgHovered]       = accent(0.22f);
    c[ImGuiCol_FrameBgActive]        = accent(0.32f);
    c[ImGuiCol_TitleBg]              = accent(0.18f);
    c[ImGuiCol_TitleBgActive]        = accent(0.30f);
    c[ImGuiCol_TitleBgCollapsed]     = accent(0.12f);
    c[ImGuiCol_Button]               = accent(0.20f);
    c[ImGuiCol_ButtonHovered]        = accent(0.38f);
    c[ImGuiCol_ButtonActive]         = accent(0.60f);
    c[ImGuiCol_Header]               = accent(0.22f);
    c[ImGuiCol_HeaderHovered]        = accent(0.36f);
    c[ImGuiCol_HeaderActive]         = accent(0.50f);
    c[ImGuiCol_CheckMark]            = accent(1.0f);
    c[ImGuiCol_SliderGrab]           = accent(0.80f);
    c[ImGuiCol_SliderGrabActive]     = accent(1.0f);
    c[ImGuiCol_Separator]            = accent(0.35f);
    c[ImGuiCol_Tab]                  = accent(0.16f);
    c[ImGuiCol_TabHovered]           = accent(0.40f);
    c[ImGuiCol_TabSelected]          = accent(0.34f);
    c[ImGuiCol_TableHeaderBg]        = accent(0.18f);
    c[ImGuiCol_TableRowBgAlt]        = accent(0.06f);
    c[ImGuiCol_ScrollbarGrab]        = accent(0.30f);
    c[ImGuiCol_ScrollbarGrabHovered] = accent(0.45f);
    c[ImGuiCol_ScrollbarGrabActive]  = accent(0.60f);
    c[ImGuiCol_TextSelectedBg]       = accent(0.40f, 0.6f);
}

// The palette's accent colour (set by apply_theme), as a packed colour for drawing
static ImU32 col_accent_u32(){ return ImGui::GetColorU32(ImGuiCol_CheckMark); }

// Blinks at about 2 Hz, for "INSERT CARTRIDGE" style text
static bool blink(){ return (int)(ImGui::GetTime() * 2.0) % 2 == 0; }

// Text in the pixel font (falls back to the normal font if it wasn't found)
static void pixel_text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 col, const char* text){
    ImFont* f = font ? font : ImGui::GetFont();
    float size = font ? font->FontSize : ImGui::GetFontSize();
    dl->AddText(f, size, pos, col, text);
}

// Labels and lights on the TV bezel, drawn over the plastic frame main.cpp painted
static void draw_bezel(App& app){
    SDL_Rect b = app.bezel_rect, sc = app.screen_rect;
    if(b.w == 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();

    // Rounded corners and a thin highlight make the flat rectangles look like a TV case and tube
    dl->AddRect(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), IM_COL32(90, 84, 76, 255), 14.0f, 0, 3.0f);
    dl->AddRect(ImVec2(sc.x - 7, sc.y - 7), ImVec2(sc.x + sc.w + 7, sc.y + sc.h + 7),
                IM_COL32(12, 12, 12, 255), 14.0f, 0, 10.0f);

    float label_y = sc.y + sc.h + 14;
    // Brand logo on the bottom-left of the case
    pixel_text(dl, font_title, ImVec2(sc.x, label_y), IM_COL32(200, 190, 170, 255), "CHIP-8");
    dl->AddText(ImVec2(sc.x + 112, label_y + 3), IM_COL32(150, 140, 125, 255),
                app.chip8.cosmac_quirks ? "COMPUTER SYSTEM" : "SUPER-CHIP SYSTEM");

    // Power light (always on) and sound light (on while the beep plays)
    float lx = sc.x + sc.w - 70, ly = label_y + 8;
    dl->AddCircleFilled(ImVec2(lx, ly), 5, IM_COL32(60, 230, 90, 255));
    dl->AddText(ImVec2(lx + 9, ly - 7), IM_COL32(150, 140, 125, 255), "PWR");
    bool beeping = app.chip8.get_sound_timer() > 0 || app.test_beep_frames > 0;
    dl->AddCircleFilled(ImVec2(lx + 42, ly), 5, beeping ? IM_COL32(255, 70, 50, 255) : IM_COL32(70, 25, 20, 255));
    dl->AddText(ImVec2(lx + 51, ly - 7), IM_COL32(150, 140, 125, 255), "SND");

    // Middle of the bezel: controls of the loaded game, or what to do next
    std::string hint;
    const GameInfo* g = app.rom_path.empty() ? nullptr : find_game(app.rom_path);
    if(g) hint = game_key_summary(*g);
    if(app.s.rewinding) hint = "<< REWINDING";
    else if(app.s.paused) hint = "PAUSED - Space to resume";
    if(app.play_mode) hint += hint.empty() ? "F11: exit play mode" : "   |   F11: exit";
    ImVec2 ts = ImGui::CalcTextSize(hint.c_str());
    dl->AddText(ImVec2(sc.x + (sc.w - ts.x) / 2, label_y + 3), IM_COL32(220, 210, 190, 255), hint.c_str());
}

// Start screen shown when no cartridge is inserted: pick a game like on an old console
static void start_screen(App& app){
    SDL_Rect sc = app.screen_rect;
    ImGui::SetNextWindowPos(ImVec2(sc.x, sc.y));
    ImGui::SetNextWindowSize(ImVec2(sc.w, sc.h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##start", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 col = ImGui::GetColorU32(ImGuiCol_CheckMark); // the palette's accent colour

    // Title
    const char* title = "CHIP-8";
    ImFont* big = font_big ? font_big : ImGui::GetFont();
    float big_size = font_big ? font_big->FontSize : ImGui::GetFontSize() * 3;
    ImVec2 tsz = big->CalcTextSizeA(big_size, FLT_MAX, 0, title);
    float cx = sc.x + sc.w / 2.0f, y = sc.y + sc.h * 0.10f;
    dl->AddText(big, big_size, ImVec2(cx - tsz.x / 2, y), col, title);
    if(blink()){
        const char* sub = "INSERT CARTRIDGE";
        ImFont* f = font_title ? font_title : ImGui::GetFont();
        float fs = font_title ? font_title->FontSize : ImGui::GetFontSize();
        ImVec2 ssz = f->CalcTextSizeA(fs, FLT_MAX, 0, sub);
        dl->AddText(f, fs, ImVec2(cx - ssz.x / 2, y + big_size + 18), col, sub);
    }

    // One "cartridge" card per built-in game that exists on disk, in a 3-column grid
    static const char* const carts[] = {
        "roms/Pong.ch8", "roms/Tetris.ch8", "roms/Blinky.ch8",
        "roms/games/DinoRun.ch8", "roms/games/Br8kout.ch8", "roms/games/SuperPong.ch8",
        "roms/games/Snek.ch8", "roms/games/Outlaw.ch8", "roms/games/CaveExplorer.ch8"};
    std::vector<const char*> found;
    std::error_code err;
    for(const char* c : carts) if(fs::exists(c, err)) found.push_back(c);
    if(!found.empty()){
        const int COLS = 3;
        int rows = ((int)found.size() + COLS - 1) / COLS;
        float gap = 12.0f;
        float top = sc.h * 0.30f, bottom = sc.h - 40.0f;   // space between the title and the footer
        float card_w = std::min(270.0f, (sc.w - 60.0f - (COLS - 1) * gap) / COLS);
        float card_h = std::min(120.0f, (bottom - top - (rows - 1) * gap) / rows);
        float total_w = COLS * card_w + (COLS - 1) * gap;
        float x0 = (sc.w - total_w) / 2.0f;
        for(size_t i = 0; i < found.size(); i++){
            const GameInfo* g = find_game(found[i]);
            int col = (int)i % COLS, row = (int)i / COLS;
            ImGui::SetCursorPos(ImVec2(x0 + col * (card_w + gap), top + row * (card_h + gap)));
            char id[16];
            std::snprintf(id, sizeof(id), "##cart%d", (int)i);
            bool pressed = ImGui::Button(id, ImVec2(card_w, card_h));
            pressed |= ImGui::IsKeyPressed((ImGuiKey)(ImGuiKey_1 + (int)i), false); // keys 1-9
            // Card contents drawn on top of the button: number, title, one line about it, controls
            ImVec2 p = ImGui::GetItemRectMin();
            ImVec4 clip(p.x + 6, p.y + 4, p.x + card_w - 6, p.y + card_h - 4);
            char num[16];
            std::snprintf(num, sizeof(num), "[%d]", (int)i + 1);
            pixel_text(dl, font_title, ImVec2(p.x + 8, p.y + 8), col_accent_u32(), num);
            pixel_text(dl, font_title, ImVec2(p.x + 58, p.y + 8), ImGui::GetColorU32(ImGuiCol_Text),
                       g ? g->title : found[i]);
            if(g){
                if(card_h >= 90)
                    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 8, p.y + 32),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), g->about, nullptr, card_w - 16, &clip);
                std::string keys = game_key_summary(*g);
                if(keys.empty()) keys = "All 16 keys";
                // Below the title; long control lists wrap onto a second or third line
                float keys_y = (card_h >= 90) ? p.y + card_h - 36 : p.y + 34;
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 8, keys_y),
                            col_accent_u32(), keys.c_str(), nullptr, card_w - 16, &clip);
            }
            if(pressed){
                app_load_rom(app, found[i]); // applies the game's recommended settings too
                break;
            }
        }
    }
    const char* foot = "Press 1-9 or click a cartridge  -  other ROMs: ROM browser (F1) or drag & drop";
    ImVec2 fsz = ImGui::CalcTextSize(foot);
    dl->AddText(ImVec2(cx - fsz.x / 2, sc.y + sc.h - 28), ImGui::GetColorU32(ImGuiCol_TextDisabled), foot);
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void ui_draw(App& app){
    apply_theme(PALETTES[app.s.palette]);
    draw_bezel(app);
    if(app.rom_path.empty()) start_screen(app);
    if(app.show_ui && !app.play_mode){
        side_panel(app);
        debugger_panel(app);
        rom_browser_panel(app);
    }
}
