// The finish's checkerboard over the whole view (RT64's Aspect Ratio Expand). After the first player finishes a race
// alone, event_draw_hud (the decomp's event.c) covers the winner's view with a translucent checkerboard whose columns
// fade in from left to right: 12 columns by 9 rows (fewer with more players) of fill rectangles over gViewRects, black
// squares and light grey ones, a column starting 10 frames after the one left of it, and every square at its last
// alpha once gEvent.finishTimer is -2. On the console that rectangle is the view itself, 472x208 inside the 512x240
// screen. Here the race's view fills the screen and the window's width (src/game/widescreen.cpp) while the HUD reads
// the game's box widened to the screen's width (512x208), so the board was stretched across the 4:3 frame only, with
// the track uncovered at the window's sides and in a band above and below it.
//
// So src/game/hud.cpp, which puts the race's HUD back in the frame's list, hands the board's fill rectangles to
// eb::draw_checkerboard, which draws the board again: the console's squares, as wide and tall as they are over the
// game's own view and at the same places, repeated out to the full view's top and bottom and, for a view RT64 widens,
// to the window's sides, with RT64's extended fill rectangle (signed coordinates) under an extended scissor as wide as
// the window. The columns fade in from the window's left to its right in the time the console's take from its first to
// its last, so the sweep crosses the whole window and is done as the game's timer ends. The alphas are the game's own
// (gCosTable, as event_draw_hud computes them).

#include <cmath>
#include <cstdint>

#include "recomp.h"

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

#include "eb.h"

namespace {

constexpr uint32_t G_SETPRIMCOLOR = 0xFA;
constexpr uint32_t G_SC_NON_INTERLACE = 0;

constexpr gpr EB_SCREEN_WIDTH = (gpr)(int32_t)0x800F1D8C; // gScreenWidth
constexpr gpr EB_NUM_PLAYERS = (gpr)(int32_t)0x800F074C;  // gGame.numPlayers (gGame 0x800F0710, +0x3C)
constexpr gpr EB_FINISH_TIMER = (gpr)(int32_t)0x800F0FD0; // gEvent.finishTimer (gEvent 0x800F0F48, +0x88)
constexpr gpr EB_COS_TABLE = (gpr)(int32_t)0x800C35A0;    // gCosTable, 0x1000 s16

int32_t floor_div(int32_t a, int32_t b) {
    return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0);
}

int32_t cos_at(uint8_t* rdram, int32_t i) {
    return MEM_H(0, EB_COS_TABLE + (i & 0xFFF) * 2);
}

// A square's alpha n frames after its column starts, as event_draw_hud computes it: black squares rise to 60 and
// settle at 35, the light ones rise to 255 and settle at 35.
int32_t alpha(uint8_t* rdram, int32_t n, bool white) {
    if (n < 0) {
        return 0;
    }
    if (n >= 30) {
        return 35;
    }
    if (n < 15) {
        const int32_t c = cos_at(rdram, (((n << 14) / 15) >> 4) - 0x400);
        return white ? (c * 255) >> 14 : (c * 15) >> 12;
    }
    const int32_t c = cos_at(rdram, ((((n - 15) << 14) / 15) >> 4));
    return white ? ((c * 55) >> 12) + 35 : ((c * 25) >> 14) + 35;
}

} // namespace

bool eb::draw_checkerboard(uint8_t* rdram, const int32_t box[4], void (*put)(void* ctx, uint32_t w0, uint32_t w1),
                           void* ctx) {
    int32_t game[4], full[4];
    if (!eb::view_of_box(rdram, box, game, full) || game[2] <= 0 || game[3] <= 0) {
        return false;
    }
    const int32_t players = MEM_W(0, EB_NUM_PLAYERS);
    const int32_t rows = players >= 2 ? 6 : 9, cols = players >= 3 ? 8 : 12;
    const int32_t timer = MEM_W(0, EB_FINISH_TIMER);
    const int32_t screen = MEM_W(0, EB_SCREEN_WIDTH);
    if ((timer < 0 && timer != -2) || screen <= 0) {
        return false;
    }

    // What the board covers: the full view, and past the screen's sides as far as the window shows for a view that
    // spans the screen's width (one player's, and two one above the other: those src/game/hud.cpp places).
    int32_t left = full[0], right = full[0] + full[2];
    const int32_t top = full[1], bottom = full[1] + full[3];
    if (full[0] == 0 && full[2] == screen && eb::window_wide() > 1.0f) {
        const int32_t margin = (int32_t)std::ceil(float(screen) * (eb::window_wide() - 1.0f) * 0.5f) + 1;
        left -= margin;
        right += margin;
    }
    // The console's grid over the game's view, column j's left edge and row i's top, extended past it both ways.
    auto col_x = [&](int32_t j) { return game[0] + floor_div(j * game[2], cols); };
    auto row_y = [&](int32_t i) { return game[1] + floor_div(i * game[3], rows); };
    int32_t j0 = 0, j1 = cols - 1, i0 = 0, i1 = rows - 1;
    while (j0 > -256 && col_x(j0) > left) j0--;
    while (j1 < 256 && col_x(j1 + 1) < right) j1++;
    while (i0 > -256 && row_y(i0) > top) i0--;
    while (i1 < 256 && row_y(i1 + 1) < bottom) i1++;

    GfxCommand c[2];
    gEXSetScissor(c, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, left, top, right, bottom);
    put(ctx, c[0].values.word0, c[0].values.word1);
    put(ctx, c[1].values.word0, c[1].values.word1);
    uint32_t prim = 0xFFFFFFFF;
    for (int32_t j = j0; j <= j1; j++) {
        // The column's start, its place from the first column to the last in the console's 0 to 10 * (cols - 1) frames.
        const int32_t start = j1 > j0 ? (10 * (cols - 1) * (j - j0) + (j1 - j0) / 2) / (j1 - j0) : 0;
        // Its black squares, then its light ones, each colour's primitive colour set once.
        for (int32_t shade = 0; shade < 2; shade++) {
            const bool white = shade != 0;
            const int32_t a = timer == -2 ? 35 : alpha(rdram, timer - start, white);
            const uint32_t w1 = (white ? 0xC8C8C800u : 0u) | uint32_t(a & 0xFF);
            if (w1 != prim) {
                put(ctx, G_SETPRIMCOLOR << 24, w1);
                prim = w1;
            }
            for (int32_t i = i0; i <= i1; i++) {
                if ((((i + j) & 1) != 0) != white) {
                    continue;
                }
                const int32_t ulx = col_x(j) * 4, lrx = col_x(j + 1) * 4, uly = row_y(i) * 4, lry = row_y(i + 1) * 4;
                // RT64's extended fill rectangle (rt64_gbi_extended.cpp's fillrectV1; rt64_extended_gbi.h has no
                // macro), the game's gDPFillRectangle with signed coordinates.
                put(ctx, (uint32_t(RT64_EXTENDED_OPCODE) << 24) | G_EX_FILLRECT_V1,
                    uint32_t(G_EX_ORIGIN_NONE) | (uint32_t(G_EX_ORIGIN_NONE) << 12));
                put(ctx, (uint32_t(ulx & 0xFFFF) << 16) | uint32_t(uly & 0xFFFF),
                    (uint32_t(lrx & 0xFFFF) << 16) | uint32_t(lry & 0xFFFF));
            }
        }
    }
    return true;
}
