// Visual identity for each game: decorations drawn AROUND the CHIP-8 screen with ImGui's
// draw lists. Only rectangles, circles, lines and triangles are used, so there are no image
// files to ship. Positions that look random come from a small hash function, so they are the
// same every frame (no flickering) without storing anything.
#include "ui_common.h"
#include <cmath>
#include <cstdio>

static const float PI_F = 3.14159265f;

// Deterministic "random" number in [0, 1) for index i (same result every frame)
static float rnd(int i){
    unsigned int x = (unsigned int)i * 2654435761u;
    x ^= x >> 13; x *= 0x5bd1e995u; x ^= x >> 15;
    return (x & 0xFFFFFF) / 16777216.0f;
}

static void gradient(ImDrawList* dl, ImVec2 a, ImVec2 b, Rgb top, Rgb bottom){
    dl->AddRectFilledMultiColor(a, b, col(top), col(top), col(bottom), col(bottom));
}

// Rectangle outline drawn several times, growing and fading: looks like a neon glow
static void glow_rect(ImDrawList* dl, ImVec2 a, ImVec2 b, Rgb c, float thickness, int layers, float strength){
    for(int i = layers; i >= 1; i--){
        float g = i * 3.0f;
        dl->AddRect(v2(a.x - g, a.y - g), v2(b.x + g, b.y + g), col(c, strength / i), 4.0f, 0, thickness);
    }
    dl->AddRect(a, b, col(c), 2.0f, 0, thickness);
}

static void corner_brackets(ImDrawList* dl, ImVec2 a, ImVec2 b, float len, ImU32 c, float th){
    dl->AddLine(a, v2(a.x + len, a.y), c, th); dl->AddLine(a, v2(a.x, a.y + len), c, th);
    dl->AddLine(v2(b.x, a.y), v2(b.x - len, a.y), c, th); dl->AddLine(v2(b.x, a.y), v2(b.x, a.y + len), c, th);
    dl->AddLine(v2(a.x, b.y), v2(a.x + len, b.y), c, th); dl->AddLine(v2(a.x, b.y), v2(a.x, b.y - len), c, th);
    dl->AddLine(b, v2(b.x - len, b.y), c, th); dl->AddLine(b, v2(b.x, b.y - len), c, th);
}

// ---------------------------------------------------------------- backgrounds
void draw_theme_background(ImDrawList* dl, const GameTheme& t, ImVec2 size, double time){
    float W = size.x, H = size.y, tm = (float)time;
    gradient(dl, v2(0, 0), size, t.bg_top, t.bg_bottom);

    switch(t.style){
    case STYLE_NEON: {
        // Synthwave floor: horizontal lines rushing towards the viewer + converging lines
        float horizon = H * 0.62f;
        for(int i = 0; i < 14; i++){
            float p = fmodf(i / 14.0f + tm * 0.08f, 1.0f);
            float y = horizon + (H - horizon) * p * p;
            dl->AddLine(v2(0, y), v2(W, y), col(t.accent, 0.10f + 0.35f * p), 1.5f);
        }
        for(int i = -10; i <= 10; i++)
            dl->AddLine(v2(W / 2 + i * 40.0f, horizon), v2(W / 2 + i * 220.0f, H), col(t.accent, 0.18f), 1.0f);
        dl->AddLine(v2(0, horizon), v2(W, horizon), col(t.on, 0.5f), 2.0f);
        break;
    }
    case STYLE_BLOCKS: {
        // Slowly falling coloured tetromino-like blocks
        static const Rgb colours[7] = {{0,220,255},{255,214,60},{170,80,255},{60,220,90},{255,70,70},{40,90,255},{255,150,40}};
        for(int i = 0; i < 40; i++){
            float x = rnd(i) * W, speed = 12 + rnd(i + 99) * 25;
            float y = fmodf(rnd(i + 7) * H + tm * speed, H + 80) - 40;
            float s = 14;
            Rgb c = colours[i % 7];
            int shape = i % 4; // four little 4-square shapes
            static const int cells[4][4][2] = {{{0,0},{1,0},{2,0},{3,0}}, {{0,0},{1,0},{0,1},{1,1}},
                                               {{0,0},{1,0},{2,0},{1,1}}, {{0,0},{0,1},{1,1},{2,1}}};
            for(auto& cell : cells[shape]){
                ImVec2 a = v2(x + cell[0] * s, y + cell[1] * s);
                dl->AddRectFilled(a, v2(a.x + s - 2, a.y + s - 2), col(c, 0.16f));
            }
        }
        break;
    }
    case STYLE_TERMINAL: {
        // Scrolling columns of hex digits, like a memory dump on an old terminal
        char line[64];
        float row_h = 14;
        int rows = (int)(H / row_h) + 2;
        float scroll = fmodf(tm * 20.0f, row_h);
        for(int r = 0; r < rows; r++){
            int n = r + (int)(tm * 20.0f / row_h);
            std::snprintf(line, sizeof(line), "%03X: %02X%02X %02X%02X %02X%02X %02X%02X",
                          (0x200 + n * 8) & 0xFFF, (int)(rnd(n) * 255), (int)(rnd(n + 1) * 255), (int)(rnd(n + 2) * 255),
                          (int)(rnd(n + 3) * 255), (int)(rnd(n + 4) * 255), (int)(rnd(n + 5) * 255), (int)(rnd(n + 6) * 255),
                          (int)(rnd(n + 7) * 255));
            float y = r * row_h - scroll;
            dl->AddText(v2(18, y), col(t.accent, 0.13f), line);
            dl->AddText(v2(W - 250, y), col(t.accent, 0.13f), line);
        }
        break;
    }
    case STYLE_GHOST: {
        // Maze walls (rounded double lines) along the sides, with pellets
        for(int i = 0; i < 6; i++){
            float y = 40 + i * (H - 80) / 6.0f;
            dl->AddRect(v2(20, y), v2(150, y + 50), col(t.accent, 0.55f), 10, 0, 2);
            dl->AddRect(v2(28, y + 8), v2(142, y + 42), col(t.accent, 0.3f), 6, 0, 2);
            dl->AddRect(v2(W - 150, y), v2(W - 20, y + 50), col(t.accent, 0.55f), 10, 0, 2);
            dl->AddRect(v2(W - 142, y + 8), v2(W - 28, y + 42), col(t.accent, 0.3f), 6, 0, 2);
        }
        for(int i = 0; i < 12; i++){
            float y = 30 + i * (H - 60) / 12.0f;
            bool power = (i % 4 == 0);
            float pulse = power ? 4.0f + 1.5f * sinf(tm * 6) : 2.5f;
            dl->AddCircleFilled(v2(170, y), pulse, col({255,190,170}, 0.8f));
            dl->AddCircleFilled(v2(W - 170, y), pulse, col({255,190,170}, 0.8f));
        }
        break;
    }
    case STYLE_BRICKS: {
        float bw = 64, bh = 26;
        for(int r = 0; r * bh < H; r++)
            for(int c = -1; c * bw < W; c++){
                float x = c * bw + ((r % 2) ? bw / 2 : 0), y = r * bh;
                float shade = 0.18f + 0.12f * rnd(r * 97 + c);
                dl->AddRectFilled(v2(x + 2, y + 2), v2(x + bw - 2, y + bh - 2), col(t.accent, shade));
            }
        break;
    }
    case STYLE_SPACE: {
        // Twinkling stars moving slowly left, and a ringed planet
        for(int i = 0; i < 160; i++){
            float x = fmodf(rnd(i) * W - tm * (4 + rnd(i + 3) * 18) + W * 10, W);
            float y = rnd(i + 50) * H;
            float tw = 0.4f + 0.6f * (0.5f + 0.5f * sinf(tm * (1 + rnd(i + 9) * 3) + i));
            float r = rnd(i + 11) < 0.1f ? 2.0f : 1.0f;
            dl->AddCircleFilled(v2(x, y), r, col({230,235,255}, tw));
        }
        ImVec2 pc = v2(W - 120, 110);
        dl->AddCircleFilled(pc, 44, col({90,60,160}));
        dl->AddCircleFilled(v2(pc.x - 12, pc.y - 12), 30, col({120,90,200}, 0.6f));
        dl->AddEllipse(pc, ImVec2(80, 16), col({200,190,255}, 0.8f), -0.3f, 48, 3);
        break;
    }
    case STYLE_DESERT: {
        // Striped setting sun, dunes and cacti
        ImVec2 sun = v2(W * 0.5f, H * 0.62f);
        dl->AddCircleFilled(sun, 170, col({255,210,90}, 0.9f), 64);
        for(int i = 0; i < 6; i++){
            float y = sun.y + 20 + i * 22;
            dl->AddRectFilled(v2(sun.x - 180, y), v2(sun.x + 180, y + 4 + i * 1.5f), col(t.bg_top));
        }
        for(int d = 0; d < 3; d++){
            float base = H * (0.72f + d * 0.09f);
            Rgb dc = mix_rgb({200,110,60}, {90,40,40}, d / 2.0f);
            for(int x = 0; x < W; x += 8){
                float y1 = base + 18 * sinf(x * 0.006f + d * 2), y2 = base + 18 * sinf((x + 8) * 0.006f + d * 2);
                dl->AddQuadFilled(v2((float)x, y1), v2(x + 8.0f, y2), v2(x + 8.0f, H), v2((float)x, H), col(dc));
            }
        }
        for(int i = 0; i < 4; i++){
            float x = (i < 2) ? 60 + i * 70.0f : W - 100 - (i - 2) * 80.0f, y = H * 0.8f;
            dl->AddRectFilled(v2(x, y - 60), v2(x + 12, y), col({40,60,40}), 5);
            dl->AddRectFilled(v2(x - 14, y - 40), v2(x - 4, y - 24), col({40,60,40}), 4);
            dl->AddRectFilled(v2(x + 16, y - 48), v2(x + 26, y - 30), col({40,60,40}), 4);
        }
        break;
    }
    case STYLE_WESTERN: {
        // Vertical wooden planks with grain lines and nails
        float pw = 58;
        for(int i = 0; i * pw < W; i++){
            float x = i * pw;
            Rgb wood = mix_rgb({120,68,28}, {90,48,18}, rnd(i));
            dl->AddRectFilled(v2(x, 0), v2(x + pw - 3, H), col(wood));
            for(int g = 0; g < 4; g++){
                float gx = x + 8 + rnd(i * 7 + g) * (pw - 16);
                dl->AddLine(v2(gx, 0), v2(gx + 6, H), col({60,30,10}, 0.35f), 1);
            }
            for(int n = 0; n < 2; n++) dl->AddCircleFilled(v2(x + pw / 2, 40.0f + n * (H - 80)), 3, col({40,30,20}));
        }
        break;
    }
    case STYLE_CAVE: {
        // Stalactites hanging from the top and stalagmites rising from the bottom
        for(int i = 0; i < 30; i++){
            float x = rnd(i) * W, w = 20 + rnd(i + 1) * 40, h = 40 + rnd(i + 2) * 110;
            dl->AddTriangleFilled(v2(x - w / 2, 0), v2(x + w / 2, 0), v2(x, h), col({70,52,36}));
            float x2 = rnd(i + 40) * W, h2 = 30 + rnd(i + 41) * 90;
            dl->AddTriangleFilled(v2(x2 - w / 2, H), v2(x2 + w / 2, H), v2(x2, H - h2), col({60,45,30}));
        }
        for(int i = 0; i < 20; i++){ // glinting crystals
            float x = rnd(i + 80) * W, y = rnd(i + 81) * H;
            float a = 0.3f + 0.7f * (0.5f + 0.5f * sinf(tm * 2 + i));
            dl->AddCircleFilled(v2(x, y), 2, col(t.accent, a));
        }
        break;
    }
    case STYLE_SKY: {
        // Clouds made of overlapping circles, drifting to the left
        for(int i = 0; i < 9; i++){
            float x = fmodf(rnd(i) * (W + 300) - tm * (10 + rnd(i + 5) * 15) + (W + 300) * 10, W + 300) - 150;
            float y = 40 + rnd(i + 20) * (H - 120), s = 0.6f + rnd(i + 30) * 0.8f;
            for(int c = 0; c < 5; c++)
                dl->AddCircleFilled(v2(x + (c - 2) * 26 * s, y + (c % 2) * -14 * s), (26 + (c % 2) * 10) * s, col({255,255,255}, 0.75f));
        }
        break;
    }
    case STYLE_GRID: {
        // A grid of squares that light up in slow waves
        float cell = 40;
        for(int r = 0; r * cell < H; r++)
            for(int c = 0; c * cell < W; c++){
                float wave = 0.5f + 0.5f * sinf(tm * 1.5f + c * 0.35f + r * 0.5f);
                dl->AddRectFilled(v2(c * cell + 4, r * cell + 4), v2((c + 1) * cell - 4, (r + 1) * cell - 4),
                                  col(t.accent, 0.03f + 0.10f * wave * wave), 3);
            }
        break;
    }
    case STYLE_BEACH: {
        // Sun, sea with moving waves, and sand
        dl->AddCircleFilled(v2(W - 140, 90), 50, col({255,236,120}), 48);
        float sea = H * 0.55f, sand = H * 0.78f;
        dl->AddRectFilled(v2(0, sea), v2(W, sand), col({40,140,210}));
        for(int wv = 0; wv < 4; wv++){
            float y = sea + 12 + wv * 22;
            for(int x = 0; x < W; x += 6){
                float y1 = y + 5 * sinf(x * 0.03f + tm * 2 + wv), y2 = y + 5 * sinf((x + 6) * 0.03f + tm * 2 + wv);
                dl->AddLine(v2((float)x, y1), v2(x + 6.0f, y2), col({220,245,255}, 0.6f), 2);
            }
        }
        dl->AddRectFilled(v2(0, sand), v2(W, H), col({250,215,150}));
        break;
    }
    case STYLE_GRAVEYARD: {
        // Big moon, tombstones along the ground, a few floating wisps
        dl->AddCircleFilled(v2(W - 150, 120), 70, col({240,235,210}), 64);
        dl->AddCircleFilled(v2(W - 125, 105), 62, col(t.bg_top, 0.35f), 64);
        float ground = H * 0.86f;
        dl->AddRectFilled(v2(0, ground), v2(W, H), col({20,6,26}));
        for(int i = 0; i < 12; i++){
            float x = 30 + i * (W - 60) / 12.0f, h = 40 + rnd(i) * 30;
            dl->AddRectFilled(v2(x, ground - h), v2(x + 34, ground), col({70,60,90}), 16, ImDrawFlags_RoundCornersTop);
            dl->AddLine(v2(x + 17, ground - h + 12), v2(x + 17, ground - h + 28), col({40,30,55}), 3);
            dl->AddLine(v2(x + 10, ground - h + 18), v2(x + 24, ground - h + 18), col({40,30,55}), 3);
        }
        for(int i = 0; i < 6; i++){
            float x = rnd(i + 5) * W, y = H * 0.3f + rnd(i + 6) * H * 0.4f + 12 * sinf(tm + i);
            dl->AddCircleFilled(v2(x, y), 5, col(t.accent, 0.35f));
            dl->AddCircleFilled(v2(x, y), 2, col(t.on, 0.8f));
        }
        break;
    }
    case STYLE_LAB: {
        // Blueprint: fine and coarse grid lines
        for(float x = 0; x < W; x += 20) dl->AddLine(v2(x, 0), v2(x, H), col(t.accent, ((int)x % 100 == 0) ? 0.18f : 0.06f));
        for(float y = 0; y < H; y += 20) dl->AddLine(v2(0, y), v2(W, y), col(t.accent, ((int)y % 100 == 0) ? 0.18f : 0.06f));
        break;
    }
    case STYLE_TV:
    default:
        break;
    }
}

// ---------------------------------------------------------------- frames
void draw_theme_frame(ImDrawList* dl, const GameTheme& t, SDL_Rect s, double time){
    ImVec2 a = v2((float)s.x, (float)s.y), b = v2((float)(s.x + s.w), (float)(s.y + s.h));
    float tm = (float)time;
    // Dark edge right around the screen, in every theme
    dl->AddRect(v2(a.x - 3, a.y - 3), v2(b.x + 3, b.y + 3), IM_COL32(0, 0, 0, 255), 0, 0, 6);

    switch(t.style){
    case STYLE_NEON: {
        float pulse = 0.55f + 0.25f * sinf(tm * 2.5f);
        glow_rect(dl, v2(a.x - 12, a.y - 12), v2(b.x + 12, b.y + 12), t.accent, 3, 4, pulse * 0.5f);
        dl->AddRect(v2(a.x - 20, a.y - 20), v2(b.x + 20, b.y + 20), col(t.on, 0.6f), 4, 0, 2);
        break;
    }
    case STYLE_BLOCKS: {
        // Border made of coloured squares, like the walls of a well
        static const Rgb colours[7] = {{0,220,255},{255,214,60},{170,80,255},{60,220,90},{255,70,70},{40,90,255},{255,150,40}};
        float c = 16, gap = 2;
        int i = 0;
        for(float x = a.x - 24; x < b.x + 24; x += c + gap, i++){
            dl->AddRectFilled(v2(x, a.y - 24), v2(x + c, a.y - 24 + c), col(colours[i % 7]));
            dl->AddRectFilled(v2(x, b.y + 8), v2(x + c, b.y + 8 + c), col(colours[(i + 3) % 7]));
        }
        for(float y = a.y - 6; y < b.y + 6; y += c + gap, i++){
            dl->AddRectFilled(v2(a.x - 24, y), v2(a.x - 24 + c, y + c), col(colours[i % 7]));
            dl->AddRectFilled(v2(b.x + 8, y), v2(b.x + 8 + c, y + c), col(colours[(i + 2) % 7]));
        }
        break;
    }
    case STYLE_TERMINAL: {
        dl->AddRect(v2(a.x - 14, a.y - 34), v2(b.x + 14, b.y + 14), col(t.accent, 0.9f), 0, 0, 2);
        dl->AddRectFilled(v2(a.x - 14, a.y - 34), v2(b.x + 14, a.y - 12), col(t.accent, 0.25f));
        bool cursor = fmodf(tm, 1.0f) < 0.5f;
        char prompt[64];
        std::snprintf(prompt, sizeof(prompt), "C:\\CHIP8> RUN %s%s", t.name, cursor ? "_" : "");
        dl->AddText(v2(a.x - 6, a.y - 29), col(t.accent), prompt);
        break;
    }
    case STYLE_GHOST:
        dl->AddRect(v2(a.x - 12, a.y - 12), v2(b.x + 12, b.y + 12), col(t.accent), 14, 0, 3);
        dl->AddRect(v2(a.x - 20, a.y - 20), v2(b.x + 20, b.y + 20), col(t.accent, 0.6f), 18, 0, 3);
        for(int i = 0; i < 4; i++){ // little ghosts on the frame
            float gx = a.x + (b.x - a.x) * (0.2f + i * 0.2f), gy = b.y + 34;
            static const Rgb ghost[4] = {{255,60,60},{255,170,220},{80,220,255},{255,170,60}};
            float bob = 3 * sinf(tm * 3 + i);
            dl->AddCircleFilled(v2(gx, gy - 6 + bob), 10, col(ghost[i]));
            dl->AddRectFilled(v2(gx - 10, gy - 6 + bob), v2(gx + 10, gy + 8 + bob), col(ghost[i]));
            dl->AddCircleFilled(v2(gx - 4, gy - 8 + bob), 3, IM_COL32_WHITE);
            dl->AddCircleFilled(v2(gx + 4, gy - 8 + bob), 3, IM_COL32_WHITE);
        }
        break;
    case STYLE_BRICKS: {
        float bw = 32, bh = 14;
        for(int r = 0; r < 2; r++){
            for(float x = a.x - 28 + (r % 2) * bw / 2; x < b.x + 28; x += bw){
                dl->AddRectFilled(v2(x + 1, a.y - 32 + r * bh), v2(x + bw - 1, a.y - 32 + r * bh + bh - 2), col(t.accent));
                dl->AddRectFilled(v2(x + 1, b.y + 6 + r * bh), v2(x + bw - 1, b.y + 6 + r * bh + bh - 2), col(t.accent));
            }
        }
        for(float y = a.y - 4; y < b.y + 4; y += bh){
            dl->AddRectFilled(v2(a.x - 28, y + 1), v2(a.x - 6, y + bh - 1), col(t.accent));
            dl->AddRectFilled(v2(b.x + 6, y + 1), v2(b.x + 28, y + bh - 1), col(t.accent));
        }
        break;
    }
    case STYLE_SPACE:
        dl->AddRect(v2(a.x - 12, a.y - 12), v2(b.x + 12, b.y + 12), col(t.accent, 0.5f), 0, 0, 1);
        corner_brackets(dl, v2(a.x - 20, a.y - 20), v2(b.x + 20, b.y + 20), 40, col(t.accent), 3);
        break;
    case STYLE_DESERT:
    case STYLE_BEACH:
        dl->AddRect(v2(a.x - 14, a.y - 14), v2(b.x + 14, b.y + 14), col(t.accent), 10, 0, 8);
        dl->AddRect(v2(a.x - 20, a.y - 20), v2(b.x + 20, b.y + 20), col({255,245,220}, 0.7f), 14, 0, 2);
        break;
    case STYLE_WESTERN:
        dl->AddRect(v2(a.x - 16, a.y - 16), v2(b.x + 16, b.y + 16), col({70,36,10}), 0, 0, 20);
        for(ImVec2 n : {v2(a.x - 16, a.y - 16), v2(b.x + 16, a.y - 16), v2(a.x - 16, b.y + 16), v2(b.x + 16, b.y + 16)})
            dl->AddCircleFilled(n, 5, col({200,190,170}));
        break;
    case STYLE_CAVE:
        dl->AddRect(v2(a.x - 14, a.y - 14), v2(b.x + 14, b.y + 14), col({90,70,50}), 6, 0, 16);
        for(int i = 0; i < 30; i++){ // rough rocky edge
            float x = a.x + (b.x - a.x) * rnd(i + 200);
            dl->AddTriangleFilled(v2(x - 8, a.y - 22), v2(x + 8, a.y - 22), v2(x, a.y - 30), col({90,70,50}));
        }
        break;
    case STYLE_SKY:
        dl->AddRect(v2(a.x - 12, a.y - 12), v2(b.x + 12, b.y + 12), IM_COL32(255, 255, 255, 230), 16, 0, 6);
        break;
    case STYLE_GRID: {
        float pulse = 0.5f + 0.5f * sinf(tm * 2);
        glow_rect(dl, v2(a.x - 12, a.y - 12), v2(b.x + 12, b.y + 12), t.accent, 2, 3, 0.2f + 0.3f * pulse);
        break;
    }
    case STYLE_GRAVEYARD:
        glow_rect(dl, v2(a.x - 12, a.y - 12), v2(b.x + 12, b.y + 12), t.accent, 2, 4, 0.35f);
        break;
    case STYLE_LAB: {
        dl->AddRect(v2(a.x - 10, a.y - 10), v2(b.x + 10, b.y + 10), col(t.accent), 0, 0, 2);
        for(int i = 0; i <= 8; i++){ // ruler ticks
            float x = a.x + (b.x - a.x) * i / 8.0f, y = a.y + (b.y - a.y) * i / 8.0f;
            dl->AddLine(v2(x, a.y - 10), v2(x, a.y - 18), col(t.accent), 2);
            dl->AddLine(v2(a.x - 10, y), v2(a.x - 18, y), col(t.accent), 2);
        }
        break;
    }
    case STYLE_TV:
    default:
        dl->AddRect(v2(a.x - 20, a.y - 20), v2(b.x + 20, b.y + 20), IM_COL32(70, 64, 58, 255), 16, 0, 24);
        break;
    }
}
