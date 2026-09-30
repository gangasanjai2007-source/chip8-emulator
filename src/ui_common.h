// Small drawing helpers shared by ui.cpp (developer panels), hub.cpp (library, details,
// settings, help, in-game HUD) and themes.cpp (the decorations around the game screen).
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "games.h"
#include "imgui.h"
#include <SDL2/SDL.h>

// Fonts (loaded in ui.cpp). The pixel fonts may be nullptr if the font file is missing,
// in which case the helpers fall back to ImGui's default font.
extern ImFont* font_small;  // Press Start 2P,  8 px
extern ImFont* font_title;  // Press Start 2P, 16 px
extern ImFont* font_mid;    // Press Start 2P, 24 px
extern ImFont* font_big;    // Press Start 2P, 32 px

inline ImU32 col(Rgb c, float a = 1.0f){
    return IM_COL32(c.r, c.g, c.b, (int)(a * 255));
}
inline Rgb mix_rgb(Rgb a, Rgb b, float t){
    return {(uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t)};
}
inline ImVec2 v2(float x, float y){ return ImVec2(x, y); }

// Text in one of the pixel fonts (or the default font if it is missing)
void pixel_text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 colour, const char* text);
ImVec2 pixel_text_size(ImFont* font, const char* text);
// Text with a soft glow behind it (drawn a few times, offset, at low opacity)
void glow_text(ImDrawList* dl, ImFont* font, ImVec2 pos, ImU32 colour, ImU32 glow, const char* text);

// Decorations for a game's visual theme (themes.cpp).
// background: fills the whole window behind everything; frame: drawn around the game screen.
// Neither ever draws inside `screen`, so the CHIP-8 picture is never covered.
void draw_theme_background(ImDrawList* dl, const GameTheme& t, ImVec2 size, double time);
void draw_theme_frame(ImDrawList* dl, const GameTheme& t, SDL_Rect screen, double time);

#endif
