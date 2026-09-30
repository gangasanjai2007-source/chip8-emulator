// Developer view panels (F1 in a game), fonts and the colour theme, drawn with
// Dear ImGui (https://github.com/ocornut/imgui, MIT licence).
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
#include "ui_common.h"
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
        {"F1",          "Developer view on / off"},
        {"F3 / F4",     "Help / Settings page"},
        {"F11",         "Fullscreen"},
        {"Esc or F2",   "Exit to the library"},
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
            if(ImGui::Button("Exit to library (Esc)")) app_exit_to_library(app);
            ImGui::SameLine();
            if(ImGui::Button("Fullscreen (F11)")) app_set_play_mode(app, true);

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
            std::string theme_label = std::string("Game theme (") + app_theme(app).name + ")";
            if(ImGui::BeginCombo("Palette (P)", s.palette < 0 ? theme_label.c_str() : PALETTES[s.palette].name)){
                if(ImGui::Selectable(theme_label.c_str(), s.palette < 0)){ s.palette = -1; app_update_title(app); }
                for(int i = 0; i < NUM_PALETTES; i++)
                    if(ImGui::Selectable(PALETTES[i].name, s.palette == i)){ s.palette = i; app_update_title(app); }
                ImGui::EndCombo();
            }
            // The "Custom" palette can be edited with colour pickers
            if(s.palette >= 0 && std::string(PALETTES[s.palette].name) == "Custom"){
                Palette& p = PALETTES[s.palette];
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
ImFont* font_small = nullptr; // Press Start 2P,  8 px (arcade pixel font)
ImFont* font_title = nullptr; // Press Start 2P, 16 px
ImFont* font_mid   = nullptr; // Press Start 2P, 24 px
ImFont* font_big   = nullptr; // Press Start 2P, 32 px

void ui_load_fonts(){
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault(); // ProggyClean: already a crisp pixel font, used for the panels
    // The pixel font is loaded from the assets folder; if it is missing we fall back to the default
    const char* path = "assets/fonts/PressStart2P-Regular.ttf";
    std::error_code err;
    if(fs::exists(path, err)){
        // Multiples of 8 px keep this pixel font perfectly sharp
        font_small = io.Fonts->AddFontFromFileTTF(path, 8.0f);
        font_title = io.Fonts->AddFontFromFileTTF(path, 16.0f);
        font_mid   = io.Fonts->AddFontFromFileTTF(path, 24.0f);
        font_big   = io.Fonts->AddFontFromFileTTF(path, 32.0f);
    }
}

// Retro theme: square corners, 1-pixel borders, and every colour derived from the
// currently selected palette, so the whole interface matches the game screen.
static void apply_theme(Rgb accent_rgb){
    float ar = accent_rgb.r / 255.f, ag = accent_rgb.g / 255.f, ab = accent_rgb.b / 255.f;
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

// Text in the pixel font (falls back to the normal font if it wasn't found)
void pixel_text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 col, const char* text){
    ImFont* f = font ? font : ImGui::GetFont();
    float size = font ? font->FontSize : ImGui::GetFontSize();
    dl->AddText(f, size, pos, col, text);
}

ImVec2 pixel_text_size(ImFont* font, const char* text){
    ImFont* f = font ? font : ImGui::GetFont();
    float size = font ? font->FontSize : ImGui::GetFontSize();
    return f->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
}

void glow_text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 colour, ImU32 glow, const char* text){
    for(int dx = -2; dx <= 2; dx += 2)
        for(int dy = -2; dy <= 2; dy += 2)
            if(dx || dy) pixel_text(dl, font, ImVec2(pos.x + dx, pos.y + dy), glow, text);
    pixel_text(dl, font, pos, colour, text);
}

// The TV case around the screen in the developer view, with its labels and lights
static void draw_bezel(App& app){
    SDL_Rect b = app.bezel_rect, sc = app.screen_rect;
    if(b.w == 0) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    dl->AddRectFilled(ImVec2(b.x, b.y), ImVec2(b.x + b.w, b.y + b.h), IM_COL32(46, 42, 38, 255), 14.0f); // plastic case

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
    if(hint.empty()) hint = "F1: back to the game view";
    ImVec2 ts = ImGui::CalcTextSize(hint.c_str());
    dl->AddText(ImVec2(sc.x + (sc.w - ts.x) / 2, label_y + 3), IM_COL32(220, 210, 190, 255), hint.c_str());
}

// Draws the frame's UI. The in-game developer view (F1) uses the panels in this file;
// everything else (library, details, settings, help, in-game HUD) is in hub.cpp.
void ui_draw(App& app){
    bool in_game = (app.screen == SCREEN_GAME);
    if(in_game && app.classic) return; // --classic: just the game, like the original
    // The whole interface takes its accent colour from the game (or a neutral arcade pink)
    bool game_page = in_game || app.screen == SCREEN_HELP ||
                     (app.screen == SCREEN_SETTINGS && app.back_to == SCREEN_GAME);
    Rgb accent = game_page ? app_theme(app).accent
               : (app.screen == SCREEN_SETTINGS ? Rgb{0, 220, 255} : Rgb{255, 70, 170});
    if(app.screen == SCREEN_DETAILS && !app.library.entries.empty())
        accent = app.library.entries[std::min(app.selected, (int)app.library.entries.size() - 1)].theme.accent;
    apply_theme(accent);

    if(in_game && app.dev_view && !app.play_mode){
        draw_bezel(app);
        side_panel(app);
        debugger_panel(app);
        rom_browser_panel(app);
        hub_overlays(app);
    }
    else hub_draw(app);
}
