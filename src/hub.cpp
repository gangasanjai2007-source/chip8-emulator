// The "console" side of the emulator, drawn with Dear ImGui:
//   - Library (hub): a grid of game cards with thumbnails, filters and search
//   - Details: one game's page with a live preview, controls and a Play button
//   - In-game HUD: themed frame around the screen, top bar and buttons (never over the screen)
//   - Settings and Help pages
// The developer view (F1: debugger, ROM browser, panels) is in ui.cpp.
//
// Most of the look is drawn by hand with ImGui draw lists (rectangles, text, images), with
// invisible ImGui buttons on top to handle the mouse. Keyboard navigation uses
// ImGui::IsKeyPressed().
#include "app.h"
#include "games.h"
#include "library.h"
#include "ui_common.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ------------------------------------------------------------------ helpers
static const Rgb HUB_BG_TOP = {24, 12, 44}, HUB_BG_BOTTOM = {4, 4, 12};
static const Rgb HUB_PINK = {255, 70, 170}, HUB_CYAN = {0, 220, 255};
static const Rgb CARD_BG = {20, 16, 34}, TEXT_DIM = {150, 140, 180}, WHITE = {255, 255, 255};

static float screen_age(const App& app){ return (float)(SDL_GetTicks64() / 1000.0 - app.screen_since); }
static float ease_out(float t){ t = std::clamp(t, 0.0f, 1.0f); return 1 - (1 - t) * (1 - t) * (1 - t); }
static ImTextureID tex_id(SDL_Texture* t){ return (ImTextureID)(intptr_t)t; }

static Rgb genre_colour(const std::string& g){
    if(g == "Arcade")    return {255, 70, 170};
    if(g == "Action")    return {255, 140, 40};
    if(g == "Puzzle")    return {0, 220, 255};
    if(g == "Adventure") return {90, 220, 110};
    if(g == "Test")      return {120, 150, 255};
    return {170, 170, 170};
}

// A full-window, invisible ImGui window to put a page's widgets in
static void begin_page(const char* id){
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
}

// Small coloured label, e.g. the genre on a card. Returns its width.
static float chip(ImDrawList* dl, ImVec2 pos, const char* text, Rgb c, bool filled = false){
    ImVec2 ts = pixel_text_size(font_small, text);
    ImVec2 b = v2(pos.x + ts.x + 12, pos.y + ts.y + 8);
    dl->AddRectFilled(pos, b, col(c, filled ? 0.9f : 0.18f), 3);
    dl->AddRect(pos, b, col(c, 0.9f), 3);
    pixel_text(dl, font_small, v2(pos.x + 6, pos.y + 4), filled ? IM_COL32(10, 8, 20, 255) : col(c), text);
    return b.x - pos.x;
}

// A keyboard key cap with the action it does next to it, e.g. [W] Jump. Returns its width.
static float key_cap(ImDrawList* dl, ImVec2 pos, const char* key, const char* action, Rgb accent){
    ImVec2 ks = pixel_text_size(font_small, key);
    float w = std::max(22.0f, ks.x + 12);
    dl->AddRectFilled(v2(pos.x, pos.y + 2), v2(pos.x + w, pos.y + 24), IM_COL32(0, 0, 0, 160), 4);   // shadow
    dl->AddRectFilled(pos, v2(pos.x + w, pos.y + 22), IM_COL32(235, 232, 245, 255), 4);                // key
    dl->AddRect(pos, v2(pos.x + w, pos.y + 22), col(accent), 4, 0, 1.5f);
    pixel_text(dl, font_small, v2(pos.x + (w - ks.x) / 2, pos.y + 7), IM_COL32(20, 16, 34, 255), key);
    float x = pos.x + w + 6;
    if(action && action[0]){
        dl->AddText(v2(x, pos.y + 4), IM_COL32(235, 232, 245, 255), action);
        x += ImGui::CalcTextSize(action).x;
    }
    return x - pos.x;
}

// A big hand-drawn button: pixel-font label and an optional key hint underneath.
// primary = filled with the accent colour. Returns true when clicked.
static bool arcade_button(const char* id, const char* label, const char* hint, ImVec2 pos, ImVec2 size,
                          Rgb accent, bool primary = false, bool* held = nullptr){
    ImGui::SetCursorScreenPos(pos);
    bool clicked = ImGui::InvisibleButton(id, size);
    bool hover = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    if(held) *held = active;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b = v2(pos.x + size.x, pos.y + size.y);
    float lift = active ? 0.0f : (hover ? -2.0f : 0.0f);
    if(hover) dl->AddRect(v2(pos.x - 3, pos.y - 3 + lift), v2(b.x + 3, b.y + 3 + lift), col(accent, 0.35f), 6, 0, 4);
    Rgb fill = primary ? accent : Rgb{28, 22, 46};
    if(!primary && hover) fill = mix_rgb(fill, accent, 0.25f);
    dl->AddRectFilled(v2(pos.x, pos.y + lift), v2(b.x, b.y + lift), col(fill), 4);
    dl->AddRect(v2(pos.x, pos.y + lift), v2(b.x, b.y + lift), col(accent, primary ? 1.0f : 0.8f), 4, 0, 2);
    ImFont* f = (size.y >= 50) ? font_title : font_small;
    ImVec2 ls = pixel_text_size(f, label);
    float ly = hint ? pos.y + size.y * 0.30f - ls.y / 2 : pos.y + (size.y - ls.y) / 2;
    ImU32 tc = primary ? IM_COL32(12, 8, 24, 255) : IM_COL32(240, 236, 255, 255);
    pixel_text(dl, f, v2(pos.x + (size.x - ls.x) / 2, ly + lift), tc, label);
    if(hint){
        ImVec2 hs = ImGui::CalcTextSize(hint);
        dl->AddText(v2(pos.x + (size.x - hs.x) / 2, pos.y + size.y * 0.66f - hs.y / 2 + lift),
                    primary ? IM_COL32(12, 8, 24, 200) : col(TEXT_DIM), hint);
    }
    return clicked;
}

// Draws a texture (thumbnail / preview) with a "loading" or "no signal" state
static void draw_screen_image(ImDrawList* dl, SDL_Texture* tex, bool ready, bool broken, ImVec2 a, ImVec2 b,
                              const GameTheme& theme, float time){
    if(broken){
        // TV static: random grey dots, different every frame
        dl->AddRectFilled(a, b, IM_COL32(20, 20, 20, 255));
        int seed = (int)(time * 30);
        for(int i = 0; i < 300; i++){
            unsigned int h = (unsigned int)(i * 7919 + seed * 104729);
            h ^= h >> 11; h *= 2654435761u;
            float x = a.x + (h % 1000) / 1000.0f * (b.x - a.x), y = a.y + ((h >> 10) % 1000) / 1000.0f * (b.y - a.y);
            int g = 80 + (h >> 20) % 150;
            dl->AddRectFilled(v2(x, y), v2(x + 3, y + 2), IM_COL32(g, g, g, 255));
        }
        ImVec2 ts = pixel_text_size(font_small, "NO SIGNAL");
        dl->AddRectFilled(v2((a.x + b.x - ts.x) / 2 - 6, (a.y + b.y) / 2 - 10), v2((a.x + b.x + ts.x) / 2 + 6, (a.y + b.y) / 2 + 10), IM_COL32(0, 0, 0, 220));
        pixel_text(dl, font_small, v2((a.x + b.x - ts.x) / 2, (a.y + b.y) / 2 - 4), IM_COL32(255, 80, 80, 255), "NO SIGNAL");
        return;
    }
    if(!ready || !tex){
        // Loading: the theme's background colour with a moving shine
        dl->AddRectFilled(a, b, col(theme.off));
        float p = fmodf(time * 0.8f, 1.4f) - 0.2f, w = (b.x - a.x);
        float x0 = a.x + w * p;
        dl->AddRectFilledMultiColor(v2(std::max(a.x, x0 - 40), a.y), v2(std::min(b.x, x0 + 40), b.y),
                                    col(theme.on, 0.0f), col(theme.on, 0.25f), col(theme.on, 0.25f), col(theme.on, 0.0f));
        ImVec2 ts = pixel_text_size(font_small, "LOADING");
        pixel_text(dl, font_small, v2((a.x + b.x - ts.x) / 2, (a.y + b.y) / 2 - 4), col(theme.on, 0.8f), "LOADING");
        return;
    }
    dl->AddImage(tex_id(tex), a, b);
}

// Background used by the library and settings: dark gradient, neon floor grid, scanlines
static void hub_background(ImDrawList* dl, float time){
    ImVec2 size = ImGui::GetIO().DisplaySize;
    dl->AddRectFilledMultiColor(v2(0, 0), size, col(HUB_BG_TOP), col(HUB_BG_TOP), col(HUB_BG_BOTTOM), col(HUB_BG_BOTTOM));
    float horizon = size.y * 0.55f;
    for(int i = 0; i < 12; i++){
        float p = fmodf(i / 12.0f + time * 0.05f, 1.0f);
        float y = horizon + (size.y - horizon) * p * p;
        dl->AddLine(v2(0, y), v2(size.x, y), col(HUB_PINK, 0.03f + 0.10f * p), 1.0f);
    }
    for(int i = -12; i <= 12; i++)
        dl->AddLine(v2(size.x / 2 + i * 50.0f, horizon), v2(size.x / 2 + i * 260.0f, size.y), col(HUB_PINK, 0.06f));
}
static void scanlines_overlay(ImDrawList* dl){
    ImVec2 size = ImGui::GetIO().DisplaySize;
    for(float y = 0; y < size.y; y += 3) dl->AddLine(v2(0, y), v2(size.x, y), IM_COL32(0, 0, 0, 28));
}

// ------------------------------------------------------------------ message boxes and toasts
// Darkens everything behind a message box. It is a real (input-blocking) window so the box,
// opened right after it and given focus, stays on top of it.
static void modal_backdrop(const char* id){
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::End();
    ImGui::SetNextWindowFocus();
}
// A centred box with a dark backdrop. Returns true while it is open.
static bool message_box(App& app){
    if(app.error_text.empty()) return false;
    ImGuiIO& io = ImGui::GetIO();
    modal_backdrop("##errdim");
    ImVec2 size = v2(560, 220), a = v2((io.DisplaySize.x - size.x) / 2, (io.DisplaySize.y - size.y) / 2);
    ImGui::SetNextWindowPos(a);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##error", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    wl->AddRectFilled(a, v2(a.x + size.x, a.y + 36), IM_COL32(200, 40, 60, 255));
    pixel_text(wl, font_title, v2(a.x + 16, a.y + 10), IM_COL32_WHITE, app.error_title.c_str());
    ImGui::SetCursorScreenPos(v2(a.x + 16, a.y + 52));
    ImGui::PushTextWrapPos(a.x + size.x - 16);
    ImGui::TextUnformatted(app.error_text.c_str());
    ImGui::PopTextWrapPos();
    if(arcade_button("##errok", "OK", "Enter / Esc", v2(a.x + size.x - 136, a.y + size.y - 60), v2(120, 44), HUB_PINK, true)
       || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
        app.error_text.clear();
    ImGui::End();
    return true;
}

// Short messages ("State saved", "Breakpoint hit"...) slide in at the top for a few seconds
static void toast(App& app, float y){
    static std::string last;
    static double shown_at = -100;
    if(app.status != last){ last = app.status; shown_at = ImGui::GetTime(); }
    float age = (float)(ImGui::GetTime() - shown_at);
    if(last.empty() || age > 3.0f) return;
    float a = std::min(1.0f, std::min(age * 6, (3.0f - age) * 3));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    ImVec2 ts = ImGui::CalcTextSize(last.c_str());
    float W = ImGui::GetIO().DisplaySize.x, x = (W - ts.x) / 2;
    float yy = y - (1 - ease_out(age * 4)) * 12;
    dl->AddRectFilled(v2(x - 14, yy), v2(x + ts.x + 14, yy + ts.y + 10), IM_COL32(10, 8, 20, (int)(230 * a)), 12);
    dl->AddRect(v2(x - 14, yy), v2(x + ts.x + 14, yy + ts.y + 10), col(HUB_CYAN, a), 12, 0, 1.5f);
    dl->AddText(v2(x, yy + 5), IM_COL32(240, 240, 255, (int)(255 * a)), last.c_str());
}

// ------------------------------------------------------------------ library (hub)
static const char* CATEGORIES[] = {"All", "Arcade", "Action", "Puzzle", "Adventure", "Unknown", "Test"};
static const char* CATEGORY_LABELS[] = {"ALL", "ARCADE", "ACTION", "PUZZLE", "ADVENTURE", "OTHER", "TESTS"};
static const int NUM_CATEGORIES = 7;
static int category = 0;
static char search[64] = "";
static std::vector<int> visible; // indices into library.entries that pass the filter

static bool matches(const LibraryEntry& e){
    // "All" includes the test ROMs too (they are listed last)
    if(category != 0 && e.genre != CATEGORIES[category]) return false;
    if(search[0]){
        std::string hay = e.title + " " + e.genre + " " + e.about + " " + e.path, needle = search;
        std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        if(hay.find(needle) == std::string::npos) return false;
    }
    return true;
}
static void update_visible(App& app){
    visible.clear();
    for(int i = 0; i < (int)app.library.entries.size(); i++)
        if(matches(app.library.entries[i])) visible.push_back(i);
    if(!visible.empty() && std::find(visible.begin(), visible.end(), app.selected) == visible.end())
        app.selected = visible[0];
}

static void open_settings(App& app, Screen from){
    app.back_to = from;
    if(from == SCREEN_GAME){ app.paused_before_page = app.s.paused; app.s.paused = true; }
    app_go(app, SCREEN_SETTINGS);
}

static void hub_screen(App& app){
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y, t = (float)ImGui::GetTime(), age = screen_age(app);
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    hub_background(bg, t);
    update_visible(app);
    bool modal = !app.error_text.empty() || app.confirm_quit;

    begin_page("##hub");
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- Header: logo, counter, search, settings
    glow_text(dl, font_big, v2(40, 26), col(WHITE), col(HUB_PINK, 0.35f), "CHIP-8");
    pixel_text(dl, font_mid, v2(250, 32), col(HUB_CYAN), "ARCADE");
    int games = 0;
    for(auto& e : app.library.entries) if(e.genre != "Test") games++;
    char sub[96];
    std::snprintf(sub, sizeof(sub), "%d GAMES  +  %d TEST ROMS", games, (int)app.library.entries.size() - games);
    pixel_text(dl, font_small, v2(42, 72), col(TEXT_DIM), sub);
    if(!library_all_ready(app.library)){
        int ready = 0;
        for(auto& e : app.library.entries) if(e.thumb_ready || e.broken) ready++;
        float p = app.library.entries.empty() ? 1 : ready / (float)app.library.entries.size();
        dl->AddRectFilled(v2(42, 86), v2(242, 90), IM_COL32(60, 50, 90, 255));
        dl->AddRectFilled(v2(42, 86), v2(42 + 200 * p, 90), col(HUB_CYAN));
    }

    ImGui::SetCursorScreenPos(v2(W - 520, 34));
    ImGui::SetNextItemWidth(300);
    if(!modal && ImGui::IsKeyPressed(ImGuiKey_Slash, false) && !io.WantTextInput) ImGui::SetKeyboardFocusHere();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
    // NoTabStop: Tab switches categories, so it must not also jump into the search box
    ImGui::PushItemFlag(ImGuiItemFlags_NoTabStop, true);
    ImGui::InputTextWithHint("##search", "search games...  ( / )", search, sizeof(search));
    ImGui::PopItemFlag();
    ImGui::PopStyleVar();
    // The '/' that opened the search box shouldn't end up in it
    std::string cleaned;
    for(const char* p = search; *p; p++) if(*p != '/') cleaned += *p;
    std::snprintf(search, sizeof(search), "%s", cleaned.c_str());
    if(arcade_button("##settings", "SETTINGS", "S", v2(W - 200, 26), v2(160, 44), HUB_CYAN)) open_settings(app, SCREEN_HUB);

    // ---- Category tabs
    float x = 40;
    for(int c = 0; c < NUM_CATEGORIES; c++){
        int count = 0;
        for(auto& e : app.library.entries) if(c == 0 || e.genre == CATEGORIES[c]) count++;
        if(count == 0) continue;
        char label[32];
        std::snprintf(label, sizeof(label), "%s %d", CATEGORY_LABELS[c], count);
        ImVec2 ls = pixel_text_size(font_small, label);
        ImVec2 a = v2(x, 106), b = v2(x + ls.x + 24, 132);
        ImGui::SetCursorScreenPos(a);
        char id[16]; std::snprintf(id, sizeof(id), "##cat%d", c);
        if(ImGui::InvisibleButton(id, v2(b.x - a.x, b.y - a.y))) category = c;
        bool on = (category == c), hover = ImGui::IsItemHovered();
        Rgb cc = c == 0 ? HUB_PINK : genre_colour(CATEGORIES[c]);
        dl->AddRectFilled(a, b, col(cc, on ? 0.95f : (hover ? 0.3f : 0.12f)), 13);
        dl->AddRect(a, b, col(cc, 0.9f), 13, 0, 1.5f);
        pixel_text(dl, font_small, v2(a.x + 12, a.y + 9), on ? IM_COL32(12, 8, 24, 255) : col(cc), label);
        x = b.x + 10;
    }
    // Tab: next category that has at least one game
    if(!modal && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Tab, false)){
        for(int step = 0; step < NUM_CATEGORIES; step++){
            category = (category + 1) % NUM_CATEGORIES;
            bool any = (category == 0);
            for(auto& e : app.library.entries) if(e.genre == CATEGORIES[category]) any = true;
            if(any) break;
        }
    }
    update_visible(app);

    // ---- Keyboard navigation of the card grid
    const int COLS = 4;
    static bool scroll_to_selected = false;
    int pos = (int)(std::find(visible.begin(), visible.end(), app.selected) - visible.begin());
    if(!modal && !io.WantTextInput && !visible.empty()){
        int move = 0;
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) move = 1;
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  move = -1;
        if(ImGui::IsKeyPressed(ImGuiKey_DownArrow))  move = COLS;
        if(ImGui::IsKeyPressed(ImGuiKey_UpArrow))    move = -COLS;
        if(move){
            pos = std::clamp(pos + move, 0, (int)visible.size() - 1);
            app.selected = visible[pos];
            scroll_to_selected = true;
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
            app_go(app, SCREEN_DETAILS);
        if(ImGui::IsKeyPressed(ImGuiKey_Space, false)) app_play(app, app.library.entries[app.selected].path);
        if(ImGui::IsKeyPressed(ImGuiKey_S, false)) open_settings(app, SCREEN_HUB);
    }

    // ---- Card grid (scrolls if there are more than two rows)
    const float CARD_W = 280, CARD_H = 250, GAP = 20;
    float grid_w = COLS * CARD_W + (COLS - 1) * GAP;
    ImGui::SetCursorScreenPos(v2((W - grid_w) / 2 - 10, 148));
    ImGui::BeginChild("grid", v2(grid_w + 20, H - 148 - 56), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* gl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x += 10; origin.y += 8;

    if(app.library.entries.empty() || visible.empty()){
        // Empty states
        const char* big = app.library.entries.empty() ? "NO CARTRIDGES FOUND" : "NO GAMES MATCH";
        const char* small = app.library.entries.empty()
            ? "Put .ch8 files in the roms/ folder next to the program, or drag one onto this window."
            : "Try a different search, or pick another category.";
        ImVec2 bs = pixel_text_size(font_mid, big);
        pixel_text(gl, font_mid, v2(origin.x + (grid_w - bs.x) / 2, origin.y + 120), col(HUB_PINK), big);
        ImVec2 ss = ImGui::CalcTextSize(small);
        gl->AddText(v2(origin.x + (grid_w - ss.x) / 2, origin.y + 170), col(TEXT_DIM), small);
        if(!app.library.entries.empty()){
            if(arcade_button("##clear", "CLEAR FILTERS", nullptr, v2(origin.x + grid_w / 2 - 110, origin.y + 210), v2(220, 40), HUB_CYAN)){
                search[0] = 0; category = 0;
            }
        }
    }

    for(int n = 0; n < (int)visible.size(); n++){
        int idx = visible[n];
        LibraryEntry& e = app.library.entries[idx];
        int c = n % COLS, r = n / COLS;
        // Cards slide up and fade in one after another when the library opens
        float appear = ease_out((age - n * 0.035f) / 0.35f);
        ImVec2 a = v2(origin.x + c * (CARD_W + GAP), origin.y + r * (CARD_H + GAP) + (1 - appear) * 30);
        ImVec2 b = v2(a.x + CARD_W, a.y + CARD_H);

        ImGui::SetCursorScreenPos(a);
        char id[24]; std::snprintf(id, sizeof(id), "##card%d", idx);
        bool clicked = ImGui::InvisibleButton(id, v2(CARD_W, CARD_H));
        bool hover = ImGui::IsItemHovered();
        if(hover && !modal && io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y > 0) app.selected = idx;
        if(clicked && !modal){ app.selected = idx; app_go(app, SCREEN_DETAILS); }
        bool sel = (app.selected == idx);
        if(sel && scroll_to_selected){ ImGui::SetScrollHereY(0.5f); scroll_to_selected = false; }

        Rgb acc = e.theme.accent;
        float alpha = appear;
        // Selected card: pulsing glow in the game's own accent colour, and lifted a little
        float lift = sel ? -4.0f : 0.0f;
        a.y += lift; b.y += lift;
        if(sel){
            float pulse = 0.5f + 0.5f * sinf(t * 4);
            for(int g = 3; g >= 1; g--)
                gl->AddRect(v2(a.x - g * 3, a.y - g * 3), v2(b.x + g * 3, b.y + g * 3), col(acc, (0.15f + 0.15f * pulse) / g * alpha), 10, 0, 3);
        }
        gl->AddRectFilled(v2(a.x + 4, a.y + 6), v2(b.x + 4, b.y + 6), IM_COL32(0, 0, 0, (int)(120 * alpha)), 8); // shadow
        gl->AddRectFilled(a, b, col(sel ? mix_rgb(CARD_BG, acc, 0.12f) : CARD_BG, alpha), 8);
        gl->AddRect(a, b, col(sel ? acc : Rgb{70, 60, 100}, alpha), 8, 0, sel ? 2.5f : 1.5f);

        // Thumbnail in a little frame of the game's theme colours (live preview when selected)
        ImVec2 ta = v2(a.x + 10, a.y + 10), tb = v2(b.x - 10, a.y + 10 + (CARD_W - 20) / 2);
        gl->AddRectFilled(v2(ta.x - 3, ta.y - 3), v2(tb.x + 3, tb.y + 3), col(e.theme.bg_top, alpha), 4);
        if(sel && !e.broken && app.preview.path == e.path)
            draw_screen_image(gl, app.preview.tex, true, false, ta, tb, e.theme, t);
        else draw_screen_image(gl, e.thumb, e.thumb_ready, e.broken, ta, tb, e.theme, t);
        if(sel && !e.broken){ // "PLAY" badge
            ImVec2 ps = pixel_text_size(font_small, "ENTER");
            gl->AddRectFilled(v2(tb.x - ps.x - 16, tb.y - 22), v2(tb.x - 4, tb.y - 4), col(acc, 0.95f), 3);
            pixel_text(gl, font_small, v2(tb.x - ps.x - 10, tb.y - 17), IM_COL32(10, 8, 20, 255), "ENTER");
        }

        // Title (smaller font if it is long), genre chip, one-line description
        float ty = tb.y + 12;
        ImFont* tf = pixel_text_size(font_title, e.title.c_str()).x <= CARD_W - 24 ? font_title : font_small;
        pixel_text(gl, tf, v2(a.x + 12, ty), col(WHITE, alpha), e.title.c_str());
        chip(gl, v2(a.x + 12, ty + 24), e.genre == "Unknown" ? "OTHER" : e.genre.c_str(), genre_colour(e.genre));
        ImVec4 clip(a.x + 10, ty + 44, b.x - 10, b.y - 6);
        gl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), v2(a.x + 12, ty + 48), col(TEXT_DIM, alpha),
                    e.about.c_str(), nullptr, CARD_W - 24, &clip);
    }
    int rows = ((int)visible.size() + COLS - 1) / COLS;
    ImGui::SetCursorScreenPos(v2(origin.x, origin.y + rows * (CARD_H + GAP)));
    ImGui::Dummy(v2(1, 1));
    ImGui::EndChild();

    // ---- Footer: keyboard help
    float fx = 40, fy = H - 40;
    Rgb acc = HUB_PINK;
    fx += key_cap(dl, v2(fx, fy), "ARROWS", "Select", acc) + 22;
    fx += key_cap(dl, v2(fx, fy), "ENTER", "Details", acc) + 22;
    fx += key_cap(dl, v2(fx, fy), "SPACE", "Play", acc) + 22;
    fx += key_cap(dl, v2(fx, fy), "TAB", "Category", acc) + 22;
    fx += key_cap(dl, v2(fx, fy), "/", "Search", acc) + 22;
    fx += key_cap(dl, v2(fx, fy), "S", "Settings", acc) + 22;
    fx += key_cap(dl, v2(fx, fy), "ESC", "Quit", acc) + 22;
    ImGui::End();

    // ---- "Quit?" box
    if(app.confirm_quit){
        modal_backdrop("##quitdim");
        ImVec2 size = v2(460, 170), qa = v2((W - size.x) / 2, (H - size.y) / 2);
        ImGui::SetNextWindowPos(qa);
        ImGui::SetNextWindowSize(size);
        ImGui::Begin("##quit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* ql = ImGui::GetWindowDrawList();
        ImVec2 ts = pixel_text_size(font_title, "QUIT THE ARCADE?");
        pixel_text(ql, font_title, v2(qa.x + (size.x - ts.x) / 2, qa.y + 28), col(WHITE), "QUIT THE ARCADE?");
        if(arcade_button("##qyes", "QUIT", "Enter", v2(qa.x + 50, qa.y + 90), v2(160, 48), HUB_PINK, true)
           || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) app.s.running = false;
        if(arcade_button("##qno", "STAY", "Esc", v2(qa.x + 250, qa.y + 90), v2(160, 48), HUB_CYAN)) app.confirm_quit = false;
        ImGui::End();
    }
    scanlines_overlay(ImGui::GetForegroundDrawList());
}

// ------------------------------------------------------------------ game details
static void details_screen(App& app){
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y, t = (float)ImGui::GetTime();
    if(app.library.entries.empty()){ app_go(app, SCREEN_HUB); return; }
    update_visible(app);
    LibraryEntry& e = app.library.entries[app.selected];
    const GameTheme& th = e.theme;
    bool modal = !app.error_text.empty();

    // Left / Right: previous / next game in the current library view
    if(!modal && !visible.empty()){
        int pos = (int)(std::find(visible.begin(), visible.end(), app.selected) - visible.begin());
        if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) app.selected = visible[(pos + 1) % visible.size()];
        if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  app.selected = visible[(pos + visible.size() - 1) % visible.size()];
        if(ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Space, false)){
            app_play(app, e.path);
            return;
        }
    }

    // The game's own world as the background, and its frame around the big preview
    draw_theme_background(ImGui::GetBackgroundDrawList(), th, io.DisplaySize, t);
    begin_page("##details");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float slide = (1 - ease_out(screen_age(app) / 0.3f)) * 40;

    SDL_Rect prev = {70, 150, 640, 320};
    prev.x -= (int)slide;
    draw_theme_frame(dl, th, prev, t);
    draw_screen_image(dl, app.preview.tex, app.preview.path == e.path, e.broken,
                      v2((float)prev.x, (float)prev.y), v2((float)(prev.x + prev.w), (float)(prev.y + prev.h)), th, t);
    // "LIVE PREVIEW" tag
    if(!e.broken){
        float blink = 0.5f + 0.5f * sinf(t * 5);
        dl->AddCircleFilled(v2(prev.x + 12.0f, prev.y + prev.h + 42.0f), 5, IM_COL32(255, 60, 60, (int)(120 + 135 * blink)));
        pixel_text(dl, font_small, v2(prev.x + 24.0f, prev.y + prev.h + 38.0f), IM_COL32(255, 255, 255, 220), "LIVE PREVIEW  (attract mode)");
    }

    // Info panel on the right
    ImVec2 pa = v2(770 + slide, 60), pb = v2(W - 40 + slide, H - 60);
    dl->AddRectFilled(pa, pb, IM_COL32(8, 6, 16, 225), 10);
    dl->AddRect(pa, pb, col(th.accent, 0.9f), 10, 0, 2);
    float x = pa.x + 24, y = pa.y + 24, w = pb.x - pa.x - 48;
    ImFont* tf = pixel_text_size(font_mid, e.title.c_str()).x <= w ? font_mid : font_title;
    glow_text(dl, tf, v2(x, y), col(WHITE), col(th.accent, 0.35f), e.title.c_str());
    y += 40;
    float cw = chip(dl, v2(x, y), e.genre == "Unknown" ? "OTHER" : e.genre.c_str(), genre_colour(e.genre), true);
    chip(dl, v2(x + cw + 8, y), th.name, th.accent);
    y += 30;
    if(e.info) { dl->AddText(v2(x, y), col(TEXT_DIM), e.info->author); y += 22; }
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), v2(x, y), col(WHITE), e.about.c_str(), nullptr, w);
    y += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, w, e.about.c_str()).y + 18;

    pixel_text(dl, font_small, v2(x, y), col(th.accent), "CONTROLS");
    y += 18;
    if(e.info && e.info->keys[0].action){
        float kx = x;
        for(const GameKey& k : e.info->keys){
            if(!k.action) break;
            float kw = ImGui::CalcTextSize(k.action).x + 40;
            if(kx + kw > x + w){ kx = x; y += 30; }
            kx += key_cap(dl, v2(kx, y), PC_KEY_NAMES[k.key], k.action, th.accent) + 16;
        }
        y += 34;
    }
    else{
        const char* none = (e.info && std::string(e.info->match) == "minilightsout")
            ? "All 16 keys: 1234 / QWER / ASDF / ZXCV = the 4x4 grid"
            : (e.info ? "No input needed." : "Unknown game: try W A S D, Q E, and 1-4.");
        dl->AddText(v2(x, y), col(WHITE), none);
        y += 26;
    }
    if(e.info && e.info->tips[0]){
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), v2(x, y), col(TEXT_DIM), e.info->tips, nullptr, w);
        y += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, w, e.info->tips).y + 14;
    }
    if(e.info){
        char rec[128];
        std::snprintf(rec, sizeof(rec), "Runs in %s mode at %d instructions/frame%s",
                      e.info->needs_schip ? "SUPER-CHIP" : "CHIP-8", e.info->speed,
                      e.info->vblank ? ", waiting for screen refresh" : "");
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), v2(x, y), col(TEXT_DIM), rec, nullptr, w);
    }

    // Buttons
    bool play = arcade_button("##play", "PLAY", "Enter", v2(x, pb.y - 84), v2(w * 0.62f, 60), th.accent, true);
    bool back = arcade_button("##back", "BACK", "Esc", v2(x + w * 0.66f, pb.y - 84), v2(w * 0.34f, 60), th.accent);
    if(!modal && play) app_play(app, e.path);
    if(!modal && back) app_go(app, SCREEN_HUB);

    // Previous / next hints
    pixel_text(dl, font_small, v2(70, H - 44), IM_COL32(255, 255, 255, 180), "<  >  browse games      ESC  back to library");
    ImGui::End();
}

// ------------------------------------------------------------------ in-game HUD
static void game_hud(App& app){
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y, t = (float)ImGui::GetTime();
    const GameTheme& th = app_theme(app);
    const GameInfo* g = find_game(app.rom_path);
    Settings& s = app.s;

    draw_theme_background(ImGui::GetBackgroundDrawList(), th, io.DisplaySize, t);
    draw_theme_frame(ImGui::GetBackgroundDrawList(), th, app.screen_rect, t);

    if(app.play_mode){
        // Fullscreen: just the game and a hint that fades away
        float age = screen_age(app);
        if(age < 4){
            float a = std::min(1.0f, 4 - age);
            const char* hint = "F11  leave fullscreen      ESC  library      SPACE  pause";
            ImVec2 hs = pixel_text_size(font_small, hint);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            fg->AddRectFilled(v2((W - hs.x) / 2 - 12, H - 40), v2((W + hs.x) / 2 + 12, H - 16), IM_COL32(0, 0, 0, (int)(180 * a)), 8);
            pixel_text(fg, font_small, v2((W - hs.x) / 2, H - 32), IM_COL32(255, 255, 255, (int)(255 * a)), hint);
        }
        toast(app, 20);
        return;
    }

    begin_page("##hud");
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // ---- Top bar: game, state, speed, theme, mode
    dl->AddRectFilled(v2(0, 0), v2(W, 56), IM_COL32(6, 4, 12, 215));
    dl->AddLine(v2(0, 56), v2(W, 56), col(th.accent, 0.8f), 2);
    std::string title = g ? g->title : fs::path(app.rom_path).stem().string();
    glow_text(dl, font_title, v2(24, 14), col(WHITE), col(th.accent, 0.3f), title.c_str());
    float tw = pixel_text_size(font_title, title.c_str()).x;
    chip(dl, v2(24, 36), g ? g->genre : "OTHER", genre_colour(g ? g->genre : "Unknown"));
    (void)tw;

    // State: RUNNING / PAUSED / REWIND with a coloured light
    const char* state = s.rewinding ? "REWIND" : (s.paused ? "PAUSED" : "RUNNING");
    Rgb sc = s.rewinding ? Rgb{0, 220, 255} : (s.paused ? Rgb{255, 190, 40} : Rgb{80, 240, 110});
    ImVec2 ss = pixel_text_size(font_title, state);
    float sx = (W - ss.x) / 2;
    float blink = (s.paused || s.rewinding) ? 0.5f + 0.5f * sinf(t * 6) : 1.0f;
    dl->AddRectFilled(v2(sx - 34, 12), v2(sx + ss.x + 16, 44), col(sc, 0.15f), 16);
    dl->AddRect(v2(sx - 34, 12), v2(sx + ss.x + 16, 44), col(sc, 0.8f), 16, 0, 1.5f);
    dl->AddCircleFilled(v2(sx - 18, 28), 6, col(sc, blink));
    pixel_text(dl, font_title, v2(sx, 20), col(sc), state);

    // Right: labelled values
    auto stat = [&](float x, const char* label, const std::string& value){
        pixel_text(dl, font_small, v2(x, 12), col(TEXT_DIM), label);
        dl->AddText(v2(x, 26), col(WHITE), value.c_str());
    };
    Palette pal = app_palette(app);
    stat(W - 470, "SPEED", std::to_string(s.cycles_per_frame) + " ops/frame");
    stat(W - 350, "THEME", pal.name);
    stat(W - 210, "MODE", app.chip8.cosmac_quirks ? "CHIP-8" : "SUPER-CHIP");
    char fps[16]; std::snprintf(fps, sizeof(fps), "%.0f", app.fps);
    stat(W - 90, "FPS", fps);

    // ---- Bottom bar: buttons
    float by = 572;
    dl->AddRectFilled(v2(0, by - 10), v2(W, H), IM_COL32(6, 4, 12, 215));
    dl->AddLine(v2(0, by - 10), v2(W, by - 10), col(th.accent, 0.8f), 2);
    struct Btn { const char* id; const char* label; const char* key; };
    const Btn buttons[] = {
        {"##lib", "LIBRARY", "Esc"}, {"##pause", s.paused ? "RESUME" : "PAUSE", "Space"},
        {"##save", "SAVE", "K / F5"}, {"##load", "LOAD", "L / F9"}, {"##rew", "REWIND", "hold Tab"},
        {"##restart", "RESTART", "Backspace"}, {"##help", "HELP", "F3"}, {"##set", "SETTINGS", "F4"},
        {"##dev", "DEV VIEW", "F1"}};
    const int NB = sizeof(buttons) / sizeof(buttons[0]);
    float bw = 124, gap = 10, bx = (W - (NB * bw + (NB - 1) * gap)) / 2;
    static bool rewinding_by_mouse = false;
    for(int i = 0; i < NB; i++){
        bool held = false;
        bool primary = (i == 1 && s.paused);
        bool clicked = arcade_button(buttons[i].id, buttons[i].label, buttons[i].key,
                                     v2(bx + i * (bw + gap), by), v2(bw, 46), th.accent, primary, i == 4 ? &held : nullptr);
        if(i == 4 && held != rewinding_by_mouse){ s.rewinding = held; rewinding_by_mouse = held; app_update_title(app); }
        if(!clicked) continue;
        switch(i){
            case 0: app_exit_to_library(app); ImGui::End(); return;
            case 1: app_toggle_pause(app); break;
            case 2: app_save_state(app); break;
            case 3: app_load_state(app); break;
            case 5: app_restart(app); break;
            case 6: app.back_to = SCREEN_GAME; app.paused_before_page = s.paused; s.paused = true; app_go(app, SCREEN_HELP); break;
            case 7: open_settings(app, SCREEN_GAME); break;
            case 8: app.dev_view = true; break;
        }
    }

    // ---- Controls for this game, as key caps
    float cy = by + 64, cx = 40;
    pixel_text(dl, font_small, v2(cx, cy + 7), col(th.accent), "CONTROLS");
    cx += 90;
    if(g && g->keys[0].action){
        for(const GameKey& k : g->keys){
            if(!k.action) break;
            cx += key_cap(dl, v2(cx, cy), PC_KEY_NAMES[k.key], k.action, th.accent) + 18;
        }
    }
    else dl->AddText(v2(cx, cy + 4), col(WHITE), g ? "No input needed (or all 16 keys: see Help)" : "Unknown game: try W A S D, Q, E and 1-4");
    const char* extra = "=/-  speed    P  palette    F11  fullscreen";
    ImVec2 es = ImGui::CalcTextSize(extra);
    dl->AddText(v2(W - es.x - 40, cy + 4), col(TEXT_DIM), extra);
    ImGui::End();
    toast(app, H - 44); // below the controls, never over the game
}

// ------------------------------------------------------------------ settings
static void settings_screen(App& app){
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y, t = (float)ImGui::GetTime();
    Settings& s = app.s;
    bool from_game = (app.back_to == SCREEN_GAME);
    Rgb acc = from_game ? app_theme(app).accent : HUB_CYAN;
    if(from_game) draw_theme_background(ImGui::GetBackgroundDrawList(), app_theme(app), io.DisplaySize, t);
    else hub_background(ImGui::GetBackgroundDrawList(), t);

    begin_page("##settings");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(v2(30, 20), v2(W - 30, H - 20), IM_COL32(8, 6, 16, 235), 12);
    dl->AddRect(v2(30, 20), v2(W - 30, H - 20), col(acc, 0.9f), 12, 0, 2);
    glow_text(dl, font_big, v2(60, 44), col(WHITE), col(acc, 0.3f), "SETTINGS");
    if(from_game){
        std::string sub = "Game paused: " + std::string(find_game(app.rom_path) ? find_game(app.rom_path)->title : app.rom_path.c_str());
        dl->AddText(v2(62, 86), col(TEXT_DIM), sub.c_str());
    }
    if(arcade_button("##back", from_game ? "RESUME" : "BACK", "Esc", v2(W - 230, 40), v2(170, 48), acc, true)){
        if(from_game) s.paused = app.paused_before_page;
        app_go(app, app.back_to);
        ImGui::End();
        return;
    }

    // Left: sections
    static int section = 0;
    const char* sections[] = {"EMULATION", "DISPLAY", "SOUND", "SAVESTATES", "CONTROLS", "ABOUT"};
    for(int i = 0; i < 6; i++){
        ImVec2 a = v2(60, 130 + i * 58.0f), b = v2(300, 130 + i * 58.0f + 46);
        ImGui::SetCursorScreenPos(a);
        char id[16]; std::snprintf(id, sizeof(id), "##sec%d", i);
        if(ImGui::InvisibleButton(id, v2(b.x - a.x, b.y - a.y))) section = i;
        bool on = section == i, hover = ImGui::IsItemHovered();
        dl->AddRectFilled(a, b, col(acc, on ? 0.9f : (hover ? 0.2f : 0.06f)), 6);
        if(on) dl->AddTriangleFilled(v2(b.x + 4, a.y + 14), v2(b.x + 4, b.y - 14), v2(b.x + 14, (a.y + b.y) / 2), col(acc));
        pixel_text(dl, font_title, v2(a.x + 16, a.y + 15), on ? IM_COL32(10, 8, 20, 255) : col(WHITE), sections[i]);
    }
    if(ImGui::IsKeyPressed(ImGuiKey_DownArrow) && !io.WantTextInput) section = (section + 1) % 6;
    if(ImGui::IsKeyPressed(ImGuiKey_UpArrow) && !io.WantTextInput) section = (section + 5) % 6;

    // Right: the chosen section, with normal ImGui widgets
    ImGui::SetCursorScreenPos(v2(340, 130));
    ImGui::BeginChild("section", v2(W - 340 - 60, H - 130 - 50), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    auto heading = [&](const char* text){
        ImVec2 p = ImGui::GetCursorScreenPos();
        pixel_text(cl, font_title, p, col(acc), text);
        ImGui::Dummy(v2(1, 26));
    };
    ImGui::PushItemWidth(420);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    const GameInfo* g = app.rom_path.empty() ? nullptr : find_game(app.rom_path);

    if(section == 0){
        heading("EMULATION SPEED");
        ImGui::SliderInt("Instructions per frame", &s.cycles_per_frame, MIN_CYCLES, MAX_CYCLES, "%d", ImGuiSliderFlags_Logarithmic);
        ImGui::TextDisabled("= %d instructions per second (the screen always runs at 60 frames per second).", s.cycles_per_frame * 60);
        const int presets[] = {5, 10, 20, 50, 200};
        const char* names[] = {"Slow 5", "Normal 10", "Fast 20", "Turbo 50", "Max 200"};
        for(int i = 0; i < 5; i++){ if(i) ImGui::SameLine(); if(ImGui::Button(names[i])) s.cycles_per_frame = presets[i]; }
        ImGui::Dummy(v2(1, 16));
        heading("COMPATIBILITY");
        int mode = app.chip8.cosmac_quirks ? 0 : 1;
        if(ImGui::Combo("Mode", &mode, "CHIP-8 (original 1977 COSMAC VIP)\0SUPER-CHIP (1991)\0")){
            app.chip8.cosmac_quirks = (mode == 0);
            s.display_wait = (mode == 0);
        }
        ImGui::Checkbox("Wait for screen refresh (at most one sprite draw per frame)", &s.display_wait);
        ImGui::TextDisabled("Games in the library set these for you when they start.");
        if(g && ImGui::Button("Use the recommended settings for this game")) app_apply_recommended(app);
    }
    else if(section == 1){
        heading("SCREEN COLOURS");
        // Clickable swatches: the game's own colours, then every palette
        for(int i = -1; i < NUM_PALETTES; i++){
            // -1 = the loaded game's own theme colours
            const GameTheme& th = app_theme(app);
            Palette p = (i < 0) ? Palette{th.name, th.on.r, th.on.g, th.on.b, th.off.r, th.off.g, th.off.b}
                                : PALETTES[i];
            std::string name = (i < 0) ? "Game theme" : PALETTES[i].name;
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::PushID(i);
            bool clicked = ImGui::InvisibleButton("sw", v2(118, 86));
            bool hover = ImGui::IsItemHovered();
            ImGui::PopID();
            bool on = (s.palette == i);
            cl->AddRectFilled(a, v2(a.x + 118, a.y + 56), IM_COL32(p.off_r, p.off_g, p.off_b, 255), 4);
            // a tiny "sprite" in the lit colour
            for(int k = 0; k < 5; k++) cl->AddRectFilled(v2(a.x + 30 + k * 12, a.y + 16 + (k % 2) * 12), v2(a.x + 40 + k * 12, a.y + 26 + (k % 2) * 12),
                                                        IM_COL32(p.on_r, p.on_g, p.on_b, 255));
            cl->AddRect(a, v2(a.x + 118, a.y + 56), on ? col(acc) : (hover ? col(acc, 0.5f) : IM_COL32(70, 60, 100, 255)), 4, 0, on ? 3.0f : 1.5f);
            cl->AddText(v2(a.x + 2, a.y + 62), on ? col(acc) : col(WHITE), name.c_str());
            if(clicked) s.palette = i;
            if((i + 2) % 6 != 0) ImGui::SameLine(0, 12);
        }
        if(s.palette >= 0 && std::string(PALETTES[s.palette].name) == "Custom"){
            Palette& p = PALETTES[s.palette];
            float on[3]  = {p.on_r / 255.f,  p.on_g / 255.f,  p.on_b / 255.f};
            float off[3] = {p.off_r / 255.f, p.off_g / 255.f, p.off_b / 255.f};
            if(ImGui::ColorEdit3("Pixels", on)){ p.on_r = (uint8_t)(on[0]*255); p.on_g = (uint8_t)(on[1]*255); p.on_b = (uint8_t)(on[2]*255); }
            if(ImGui::ColorEdit3("Background", off)){ p.off_r = (uint8_t)(off[0]*255); p.off_g = (uint8_t)(off[1]*255); p.off_b = (uint8_t)(off[2]*255); }
        }
        ImGui::Dummy(v2(1, 16));
        heading("CRT EFFECTS");
        ImGui::Checkbox("Phosphor fade: pixels fade out like an old TV (removes flicker)", &s.phosphor);
        ImGui::Checkbox("Scanlines", &s.scanlines);
        ImGui::Checkbox("Glow: lit pixels bleed a little light", &s.glow);
        ImGui::Dummy(v2(1, 8));
        if(ImGui::Button(app.play_mode ? "Leave fullscreen (F11)" : "Fullscreen (F11)")) app_set_play_mode(app, !app.play_mode);
    }
    else if(section == 2){
        heading("BEEP SOUND");
        int wave = app.audio.waveform;
        float freq = (float)app.audio.frequency;
        bool changed = false;
        // One button per waveform, each showing its shape
        for(int i = 0; i < NUM_WAVEFORMS; i++){
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::PushID(100 + i);
            if(ImGui::InvisibleButton("wave", v2(130, 90))){ wave = i; changed = true; }
            bool hover = ImGui::IsItemHovered();
            ImGui::PopID();
            bool on = (wave == i);
            cl->AddRectFilled(a, v2(a.x + 130, a.y + 90), col(acc, on ? 0.25f : (hover ? 0.12f : 0.05f)), 6);
            cl->AddRect(a, v2(a.x + 130, a.y + 90), on ? col(acc) : IM_COL32(70, 60, 100, 255), 6, 0, on ? 3.0f : 1.5f);
            ImVec2 prevp;
            for(int k = 0; k <= 100; k++){
                float ph = fmodf(k / 50.0f, 1.0f), v = 0;
                switch(i){
                    case SQUARE: v = ph < 0.5f ? 1.0f : -1.0f; break;
                    case SINE: v = sinf(2 * 3.14159265f * ph); break;
                    case TRIANGLE: v = ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph; break;
                    case SAWTOOTH: v = 2 * ph - 1; break;
                }
                ImVec2 p = v2(a.x + 15 + k, a.y + 38 - v * 20);
                if(k) cl->AddLine(prevp, p, on ? col(acc) : col(WHITE, 0.7f), 2);
                prevp = p;
            }
            cl->AddText(v2(a.x + 10, a.y + 68), col(WHITE), WAVEFORM_NAMES[i]);
            if(i < NUM_WAVEFORMS - 1) ImGui::SameLine(0, 14);
        }
        changed |= ImGui::SliderFloat("Pitch", &freq, 110.f, 1760.f, "%.0f Hz", ImGuiSliderFlags_Logarithmic);
        if(changed) app_set_sound(app, wave, freq);
        ImGui::TextDisabled("Keys: T = next waveform, [ and ] = one musical semitone down / up.");
        if(ImGui::Button("Play a test beep")) app.test_beep_frames = 30;
        if(app.audio_device == 0) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "No sound device was found, so the beep can't be heard.");
    }
    else if(section == 3){
        heading("SAVESTATES");
        ImGui::TextWrapped("A savestate is a snapshot of the whole machine: memory, registers, program counter, stack, "
                           "timers and screen. It is written to a small file next to the ROM (<rom>.c8s), with a "
                           "header and version number, and checked when loading, so a damaged file is rejected.");
        ImGui::Dummy(v2(1, 10));
        if(app.rom_path.empty()){
            ImGui::TextDisabled("Start a game to save or load its state.");
        }
        else{
            std::string file = app.rom_path + ".c8s";
            std::error_code err;
            bool exists = fs::exists(file, err);
            ImGui::Text("File: %s", file.c_str());
            ImGui::Text("Saved state: %s", exists ? "yes" : "none yet");
            if(ImGui::Button("Save now (K)")) app_save_state(app);
            ImGui::SameLine();
            ImGui::BeginDisabled(!exists);
            if(ImGui::Button("Load (L)")) app_load_state(app);
            ImGui::EndDisabled();
        }
        ImGui::Dummy(v2(1, 16));
        heading("REWIND");
        ImGui::TextWrapped("Hold Tab in a game to run time backwards (up to 10 seconds). The last 600 frames of machine "
                           "states are kept in memory.");
    }
    else if(section == 4){
        heading("KEYPAD");
        // The 16 CHIP-8 keys and the keyboard keys they are on
        static const int layout[16] = {0x1,0x2,0x3,0xC, 0x4,0x5,0x6,0xD, 0x7,0x8,0x9,0xE, 0xA,0x0,0xB,0xF};
        ImVec2 o = ImGui::GetCursorScreenPos();
        for(int i = 0; i < 16; i++){
            ImVec2 a = v2(o.x + (i % 4) * 64, o.y + (i / 4) * 52);
            cl->AddRectFilled(a, v2(a.x + 56, a.y + 44), IM_COL32(28, 22, 46, 255), 6);
            cl->AddRect(a, v2(a.x + 56, a.y + 44), col(acc, 0.8f), 6, 0, 1.5f);
            char k[4]; std::snprintf(k, sizeof(k), "%X", layout[i]);
            pixel_text(cl, font_title, v2(a.x + 8, a.y + 8), col(WHITE), k);
            pixel_text(cl, font_small, v2(a.x + 38, a.y + 30), col(acc), PC_KEY_NAMES[layout[i]]);
        }
        cl->AddText(v2(o.x + 280, o.y + 10), col(WHITE), "Big: CHIP-8 key.  Small: your keyboard key.");
        cl->AddText(v2(o.x + 280, o.y + 32), col(TEXT_DIM), "The keyboard's left block 1234 / QWER / ASDF / ZXCV");
        cl->AddText(v2(o.x + 280, o.y + 50), col(TEXT_DIM), "has the same shape as the original 4x4 keypad.");
        ImGui::Dummy(v2(1, 4 * 52 + 12));
        heading("SHORTCUTS");
        static const char* rows[][2] = {
            {"Space", "Pause / resume"}, {"N", "Step one instruction (paused)"}, {"Tab (hold)", "Rewind"},
            {"K or F5 / L or F9", "Save / load state"}, {"Backspace", "Restart"}, {"= / -", "Faster / slower"},
            {"P", "Next palette"}, {"G / H", "Phosphor / scanlines"}, {"T, [ ]", "Waveform, pitch"},
            {"M", "CHIP-8 / SUPER-CHIP"}, {"F1", "Developer view (debugger, ROM browser)"},
            {"F3 / F4", "Help / Settings"}, {"F11", "Fullscreen"}, {"Esc / F2", "Exit to library"}};
        if(ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg)){
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, 170);
            for(auto& r : rows){
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(acc.r / 255.f, acc.g / 255.f, acc.b / 255.f, 1), "%s", r[0]);
                ImGui::TableNextColumn(); ImGui::TextUnformatted(r[1]);
            }
            ImGui::EndTable();
        }
    }
    else{
        heading("CHIP-8 ARCADE");
        ImGui::TextWrapped("A CHIP-8 / SUPER-CHIP emulator in C++17 with SDL2 and Dear ImGui, built for TatHack '26 from "
                           "a deliberately broken starting codebase.");
        ImGui::Dummy(v2(1, 8));
        ImGui::BulletText("CPU core: src/chip8.cpp (no SDL, tested by make test)");
        ImGui::BulletText("Library and pages: src/hub.cpp, src/library.cpp, src/themes.cpp");
        ImGui::BulletText("Developer view with debugger, breakpoints and ROM browser: F1 in a game");
        ImGui::BulletText("Game notes, controls and themes: src/games.h");
        ImGui::Dummy(v2(1, 8));
        heading("CREDITS");
        ImGui::BulletText("Games 4+: CHIP-8 Archive by John Earnest and contributors (CC0)");
        ImGui::BulletText("Test ROMs: Timendus chip8-test-suite (GPL-3.0)");
        ImGui::BulletText("Dear ImGui by Omar Cornut (MIT); Press Start 2P font by CodeMan38 (OFL)");
    }
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------ help (in game)
static void help_screen(App& app){
    ImGuiIO& io = ImGui::GetIO();
    float W = io.DisplaySize.x, H = io.DisplaySize.y, t = (float)ImGui::GetTime();
    const GameTheme& th = app_theme(app);
    const GameInfo* g = find_game(app.rom_path);
    draw_theme_background(ImGui::GetBackgroundDrawList(), th, io.DisplaySize, t);

    begin_page("##help");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(v2(30, 20), v2(W - 30, H - 20), IM_COL32(8, 6, 16, 235), 12);
    dl->AddRect(v2(30, 20), v2(W - 30, H - 20), col(th.accent, 0.9f), 12, 0, 2);
    std::string title = "HOW TO PLAY: " + std::string(g ? g->title : fs::path(app.rom_path).stem().string());
    ImFont* tf = pixel_text_size(font_mid, title.c_str()).x < W - 340 ? font_mid : font_title;
    glow_text(dl, tf, v2(60, 48), col(WHITE), col(th.accent, 0.3f), title.c_str());
    if(arcade_button("##resume", "RESUME", "Esc / F3", v2(W - 230, 40), v2(170, 48), th.accent, true)
       || (ImGui::IsKeyPressed(ImGuiKey_F3, false) && screen_age(app) > 0.15f)){ // not the F3 that opened it
        app.s.paused = app.paused_before_page;
        app_go(app, SCREEN_GAME);
        ImGui::End();
        return;
    }

    float y = 120;
    if(g){
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), v2(60, y), col(WHITE), g->about, nullptr, 560);
        y += 30;
        if(g->tips[0]){ dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), v2(60, y), col(TEXT_DIM), g->tips, nullptr, 560); y += 40; }
    }
    // Big key caps for this game's controls
    pixel_text(dl, font_small, v2(60, y), col(th.accent), "CONTROLS");
    y += 20;
    if(g && g->keys[0].action){
        for(const GameKey& k : g->keys){
            if(!k.action) break;
            key_cap(dl, v2(60, y), PC_KEY_NAMES[k.key], k.action, th.accent);
            y += 32;
        }
    }
    else{ dl->AddText(v2(60, y), col(WHITE), g ? "No keys listed: see the notes above." : "Unknown game: try W A S D, Q, E and 1-4."); y += 30; }

    // Right: the keypad with this game's actions on it
    static const int layout[16] = {0x1,0x2,0x3,0xC, 0x4,0x5,0x6,0xD, 0x7,0x8,0x9,0xE, 0xA,0x0,0xB,0xF};
    ImVec2 o = v2(700, 130);
    pixel_text(dl, font_small, v2(o.x, o.y - 18), col(th.accent), "KEYPAD FOR THIS GAME");
    for(int i = 0; i < 16; i++){
        int k = layout[i];
        const char* action = "";
        if(g) for(const GameKey& gk : g->keys) if(gk.action && gk.key == k) action = gk.action;
        ImVec2 a = v2(o.x + (i % 4) * 120, o.y + (i / 4) * 70);
        bool used = action[0] != 0, down = app.chip8.key[k] != 0;
        dl->AddRectFilled(a, v2(a.x + 110, a.y + 60), down ? col(th.accent) : (used ? col(th.accent, 0.25f) : IM_COL32(28, 22, 46, 255)), 6);
        dl->AddRect(a, v2(a.x + 110, a.y + 60), col(th.accent, used ? 1.0f : 0.4f), 6, 0, used ? 2.0f : 1.0f);
        char kk[24]; std::snprintf(kk, sizeof(kk), "%s", PC_KEY_NAMES[k]);
        pixel_text(dl, font_title, v2(a.x + 10, a.y + 10), col(WHITE), kk);
        char ck[8]; std::snprintf(ck, sizeof(ck), "%X", k);
        pixel_text(dl, font_small, v2(a.x + 92, a.y + 8), col(TEXT_DIM), ck);
        if(used) dl->AddText(v2(a.x + 10, a.y + 36), col(WHITE), action);
    }
    // Shortcuts along the bottom
    float sx = 60, sy = H - 90;
    pixel_text(dl, font_small, v2(sx, sy - 22), col(th.accent), "EMULATOR");
    sx += key_cap(dl, v2(sx, sy), "SPACE", "Pause", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "TAB", "Rewind (hold)", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "K", "Save", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "L", "Load", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "BKSP", "Restart", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "P", "Palette", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "=/-", "Speed", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "F11", "Fullscreen", th.accent) + 20;
    sx += key_cap(dl, v2(sx, sy), "ESC", "Library", th.accent) + 20;
    ImGui::End();
}

// ------------------------------------------------------------------ entry point
void hub_draw(App& app){
    // Keep the live preview running for the selected card (library and details pages)
    if((app.screen == SCREEN_HUB || app.screen == SCREEN_DETAILS) && !app.library.entries.empty()){
        app.selected = std::clamp(app.selected, 0, (int)app.library.entries.size() - 1);
        LibraryEntry& e = app.library.entries[app.selected];
        if(!e.broken) preview_update(app.preview, e, app.renderer);
    }
    switch(app.screen){
        case SCREEN_HUB:      hub_screen(app); break;
        case SCREEN_DETAILS:  details_screen(app); break;
        case SCREEN_GAME:     game_hud(app); break;
        case SCREEN_SETTINGS: settings_screen(app); break;
        case SCREEN_HELP:     help_screen(app); break;
    }
    if(app.screen != SCREEN_GAME) toast(app, 8);
    message_box(app);
}

// Message boxes and short messages also appear over the developer view
void hub_overlays(App& app){
    toast(app, 8);
    message_box(app);
}
