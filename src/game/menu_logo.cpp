// The main menu's sponsor logo across the whole window (RT64's Aspect Ratio Expand). Behind the main menu's list the
// game draws a faint sponsor logo crossing the screen, down and to the right, the next one each time
// (shell_draw_screen's SCREEN_TYPE_LIST, with render_draw_image): from just past the screen's left edge to just past
// its right, its part left of the screen cut off by the game and its part right of it by the scissor. The backdrop
// behind it reaches the window's sides (RT64 stretches its rows, which span the screen), so in a window wider than 4:3
// the logo came in at the 4:3 frame's left edge and went out at its right, a straight edge across it on the way.
//
// So in a wider window it crosses the window instead, as far along its way as the game has it: us.toml hooks the call
// that draws it (shared with the track lists' right arrow, which draws with other flags). eb_logo_begin notes how far
// along its way the logo is and where the game puts its top, has the game draw all of it at the screen's left edge,
// and notes where the display list stands (gdl, $a3); eb_logo_end reads the commands the call added, its texture's
// loads and a rectangle for each band of rows, and puts them back in a buffer of this file's with each rectangle an
// RT64 extended one moved to the logo's place in the window, under a scissor as wide as the window. The first command
// the game wrote becomes a branch to the buffer, which branches back to where the game goes on, as src/game/hud.cpp
// does for the race's HUD. That scissor reaches past the framebuffer's sides, which RT64_SCISSOR_INSIDE_FRAMEBUFFER
// (CMakeLists.txt) keeps from stretching the rest of the menu.
//
// The game is given the screen's left edge, not a place of the logo's own, because a texture rectangle's corners are
// 10.2 bits: the logo drawn at its place moved right by its width ran past 1023 pixels over the last part of its way,
// and was then left to the game, which drew it at its 4:3 place, a jump back and a straight edge at the 4:3 frame's
// right side in the window's lower right. An extended rectangle's corners are signed 16-bit quarter pixels, so this
// file moves it from there to wherever the window has it. A band of rows the game puts above the screen has its
// corners wrap round below zero and draws nothing, so the logo came in at the top a band at a time, each band
// appearing whole as it came onto the screen. Each band's rows are read back from where the game put the logo's top,
// so a band crossing the window's top edge is drawn cut by it.

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "recomp.h"

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

#include "eb.h"

namespace {

constexpr uint32_t G_DL = 0xDE, G_DL_NOPUSH = 1, G_TEXRECT = 0xE4, G_RDPHALF_1 = 0xE1, G_RDPHALF_2 = 0xF1;
constexpr uint32_t G_SC_NON_INTERLACE = 0;

constexpr gpr EB_SCREEN_WIDTH = (gpr)(int32_t)0x800F1D8C;  // gScreenWidth
constexpr gpr EB_SCREEN_HEIGHT = (gpr)(int32_t)0x800F1D90; // gScreenHeight
// render_draw_image's flags for the logo (IMG_8BIT | IMG_IA); the arrow's are 3.
constexpr int32_t LOGO_FLAGS = 6;

// Four buffers in turn past src/game/hud.cpp's (0x80C00000, eight of 0x8000), since the renderer may read a list a
// frame or more after the game sends it, and the game keeps two lists.
constexpr uint32_t BUFFER_BASE = 0x80C40000;
constexpr uint32_t BUFFER_SIZE = 0x1000;
constexpr uint32_t BUFFER_COUNT = 4;

bool g_open = false;
uint32_t g_start = 0;
float g_along = 0.0f; // how far along its way the logo is, from touching the screen's left edge (0) to its right (1)
int32_t g_top = 0;    // the logo's top as the game places it, in quarter pixels
uint32_t g_next_buffer = 0;

uint32_t rd(uint8_t* rdram, uint32_t addr) {
    return (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
}

void wr(uint8_t* rdram, uint32_t addr, uint32_t value) {
    MEM_W(0, (gpr)(int32_t)addr) = (int32_t)value;
}

} // namespace

// Before the call, with its arguments: x in $a0 (in 512ths of the screen's width), y in $a1 (in 240ths of its height),
// the image in $a2, gdl in $a3, the flags at 0x1C($sp).
extern "C" void eb_logo_begin(uint8_t* rdram, recomp_context* ctx) {
    g_open = false;
    if (MEM_W(0x1C, ctx->r29) != LOGO_FLAGS) {
        return;
    }
    const int32_t screen = MEM_W(0, EB_SCREEN_WIDTH);
    const int32_t height = MEM_W(0, EB_SCREEN_HEIGHT);
    if (eb::window_wide() <= 1.0f || screen <= 0 || height <= 0) {
        return;
    }
    const int32_t logo_w = int32_t(MEM_BU(0, ctx->r6)) * 8; // Img.w, in 8s of pixels
    const int32_t x = (int32_t)ctx->r4;
    // From x = -logo_w, all of it left of the screen, which the game doesn't draw, to x = 512, all of it right of it.
    if (x <= -logo_w || x >= 512) {
        return;
    }
    g_along = float(x + logo_w) / float(512 + logo_w);
    g_top = ((int32_t)ctx->r5 * height / 240) * 4; // as render_draw_image places it
    ctx->r4 = 0;
    g_start = (uint32_t)ctx->r7;
    g_open = true;
}

// After it, with gdl where the game goes on ($v0).
extern "C" void eb_logo_end(uint8_t* rdram, uint32_t gdl) {
    if (!g_open) {
        return;
    }
    g_open = false;
    const uint32_t start = g_start;
    if (gdl <= start || (start & 7) != 0 || (gdl - start) / 8 > 200) {
        return;
    }
    // The logo's width as drawn (its rectangles from the screen's left edge, 5/4 of its texels on a 640-wide screen),
    // and its left edge's place on its way across the window, whose sides are (wide - 1) / 2 screens past the
    // screen's: from touching the window's left side to touching its right, as the game's goes from the screen's.
    int32_t drawn = 0; // in quarter pixels
    for (uint32_t at = start; at + 8 <= gdl; at += 8) {
        const uint32_t w0 = rd(rdram, at);
        if ((w0 >> 24) == G_TEXRECT) {
            const int32_t lrx = (w0 >> 12) & 0xFFF, ulx = (rd(rdram, at + 4) >> 12) & 0xFFF;
            drawn = std::max(drawn, lrx - ulx);
        }
    }
    if (drawn <= 0) {
        return;
    }
    const float wide = eb::window_wide();
    const int32_t screen = MEM_W(0, EB_SCREEN_WIDTH);
    const int32_t height = MEM_W(0, EB_SCREEN_HEIGHT);
    const float window_left = -float(screen) * (wide - 1.0f) * 0.5f * 4.0f;
    const float way = float(screen) * wide * 4.0f + float(drawn);
    const int32_t dx = (int32_t)std::lround(window_left - float(drawn) + g_along * way);

    const uint32_t buffer = BUFFER_BASE + g_next_buffer * BUFFER_SIZE;
    g_next_buffer = (g_next_buffer + 1) % BUFFER_COUNT;
    uint32_t pos = buffer;
    auto put = [&](uint32_t w0, uint32_t w1) {
        if (pos + 8 <= buffer + BUFFER_SIZE) {
            wr(rdram, pos, w0);
            wr(rdram, pos + 4, w1);
        }
        pos += 8;
    };
    auto put_cmds = [&](const GfxCommand* c, int n) {
        for (int i = 0; i < n; i++) {
            put(c[i].values.word0, c[i].values.word1);
        }
    };

    GfxCommand c[3];
    gEXEnable(c);
    put_cmds(c, 1);
    gEXPushScissor(c);
    put_cmds(c, 1);
    gEXSetScissor(c, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -4 * screen, 0, 5 * screen, height);
    put_cmds(c, 2);
    // A band's rows are at or below the logo's top and less than 1024 pixels from it, which the 12 bits they were
    // written in keep (a band above the screen wrapped round to near 1024); the margin takes in the game's rounding.
    const int32_t top = g_top - 64;
    for (uint32_t at = start; at < gdl; at += 8) {
        const uint32_t w0 = rd(rdram, at);
        const uint32_t w1 = rd(rdram, at + 4);
        if ((w0 >> 24) == G_TEXRECT && at + 24 <= gdl && (rd(rdram, at + 8) >> 24) == G_RDPHALF_1 &&
            (rd(rdram, at + 16) >> 24) == G_RDPHALF_2) {
            const int32_t lrx = (w0 >> 12) & 0xFFF, lry12 = w0 & 0xFFF;
            const int32_t tile = (w1 >> 24) & 7, ulx = (w1 >> 12) & 0xFFF, uly12 = w1 & 0xFFF;
            const int32_t uly = top + ((uly12 - top) & 0xFFF);
            const int32_t lry = uly + ((lry12 - uly12) & 0xFFF);
            const uint32_t st = rd(rdram, at + 12), steps = rd(rdram, at + 20);
            gEXTextureRectangle(c, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, ulx + dx, uly, lrx + dx, lry, tile,
                                st >> 16, st & 0xFFFF, steps >> 16, steps & 0xFFFF);
            put_cmds(c, 3);
            at += 16;
        }
        else {
            put(w0, w1);
        }
    }
    gEXPopScissor(c);
    put_cmds(c, 1);
    put((G_DL << 24) | (G_DL_NOPUSH << 16), gdl & 0x1FFFFFFF);
    if (pos > buffer + BUFFER_SIZE) {
        return;
    }
    // The game's first command becomes the branch to the buffer.
    wr(rdram, start, (G_DL << 24) | (G_DL_NOPUSH << 16));
    wr(rdram, start + 4, buffer & 0x1FFFFFFF);
}
