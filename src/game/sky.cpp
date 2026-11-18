// The race's sky across the whole window (RT64's Aspect Ratio Expand). Behind the track render_draw_race draws one of
// two skies in 2D, each into each view's rectangle: a track's sky image (sky_draw), 256x256, for each strip of 8 rows
// drawn twice side by side by sky_draw_strip, scrolled with the camera's yaw, as texture rectangles clipped to the
// view, the image wrapping across it once; or, on tracks without one (Mountain Quarry), the fog colour
// (render_clear_viewports), a fill rectangle a view. The track reaches the window's sides (src/game/widescreen.cpp),
// but these rectangles end at the screen's sides, so in a window wider than 4:3 the sides above the track kept what
// earlier frames drew there, smeared terrain beside the 4:3 frame's sky.
//
// So in a wider window the views that widen to it (eb::view_widened) draw their sky to its sides: us.toml hooks each
// call, noting where the display list stands before it, and sky_draw_strip's entry, noting each strip's view and where
// its commands start ($a1, $a3); render_clear_viewports fills view n with its n-th rectangle. After the call,
// eb_sky_end puts the commands it added in a buffer of this file's, under a scissor as wide as the window, with each
// rectangle of a widened view that touches the view's left side an RT64 extended one reaching the window's left side
// (a texture rectangle's coordinate moved back by as many pixels) and each that touches its right side one reaching
// the window's right side; the image wraps (its tiles wrap 256 texels across), so it carries on past the screen as the
// yaw turns. The first command the game wrote becomes a branch to the buffer, which branches back to where the game
// goes on, as src/game/menu_logo.cpp does.

#include <cmath>
#include <cstdint>

#include "recomp.h"

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

#include "eb.h"

namespace {

constexpr uint32_t G_DL = 0xDE, G_DL_NOPUSH = 1, G_TEXRECT = 0xE4, G_RDPHALF_1 = 0xE1, G_RDPHALF_2 = 0xF1;
constexpr uint32_t G_FILLRECT = 0xF6;
constexpr uint32_t G_SC_NON_INTERLACE = 0;

constexpr gpr EB_SCREEN_WIDTH = (gpr)(int32_t)0x800F1D8C;  // gScreenWidth
constexpr gpr EB_SCREEN_HEIGHT = (gpr)(int32_t)0x800F1D90; // gScreenHeight
constexpr gpr EB_NUM_VIEWS = (gpr)(int32_t)0x800C2ED4;     // gNumViews
constexpr uint32_t EB_VIEW_RECTS = 0x800E2038;             // gViewRects[4][4]: x, y, w, h each

// Four buffers in turn past src/game/menu_logo.cpp's (0x80C40000, four of 0x1000), since the renderer may read a list
// a frame or more after the game sends it, and the game keeps two lists. One holds two views' sky with room to spare.
constexpr uint32_t BUFFER_BASE = 0x80C44000;
constexpr uint32_t BUFFER_SIZE = 0x4000;
constexpr uint32_t BUFFER_COUNT = 4;

// The strips sky_draw_strip began: where in the list and for which view. 256 rows in strips of 8, for up to 3 views.
constexpr int MAX_STRIPS = 128;
uint32_t g_strip_at[MAX_STRIPS];
int32_t g_strip_view[MAX_STRIPS];
int g_strips = 0;

bool g_open = false;
bool g_fill = false;
uint32_t g_start = 0;
uint32_t g_next_buffer = 0;

uint32_t rd(uint8_t* rdram, uint32_t addr) {
    return (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
}

void wr(uint8_t* rdram, uint32_t addr, uint32_t value) {
    MEM_W(0, (gpr)(int32_t)addr) = (int32_t)value;
}

} // namespace

// Before the call to sky_draw (fill 0) or render_clear_viewports (fill 1), with gdl.
extern "C" void eb_sky_begin(uint8_t* rdram, uint32_t gdl, int32_t fill) {
    g_open = eb::window_wide() > 1.0f;
    g_fill = fill != 0;
    g_start = gdl;
    g_strips = 0;
}

// At sky_draw_strip's entry, with the view in $a1 and gdl in $a3.
extern "C" void eb_sky_strip(uint8_t* rdram, int32_t view, uint32_t gdl) {
    if (g_open && g_strips < MAX_STRIPS) {
        g_strip_at[g_strips] = gdl;
        g_strip_view[g_strips] = view;
        g_strips++;
    }
}

// After it, with gdl where the game goes on ($v0).
extern "C" void eb_sky_end(uint8_t* rdram, uint32_t gdl) {
    if (!g_open) {
        return;
    }
    g_open = false;
    const uint32_t start = g_start;
    if (gdl <= start || (start & 7) != 0 || (!g_fill && (g_strips == 0 || g_strips >= MAX_STRIPS)) ||
        gdl - start > BUFFER_SIZE - 0x100) {
        return;
    }
    const int32_t screen = MEM_W(0, EB_SCREEN_WIDTH);
    const int32_t height = MEM_W(0, EB_SCREEN_HEIGHT);
    const int32_t views = MEM_W(0, EB_NUM_VIEWS);
    if (screen <= 0 || views < 1 || views > 4) {
        return;
    }
    // The window's sides, in the screen's pixels: (wide - 1) / 2 screens past the screen's, a pixel further out.
    const int32_t margin = (int32_t)std::ceil(float(screen) * (eb::window_wide() - 1.0f) * 0.5f) + 1;
    const int32_t window_left = -margin, window_right = screen + margin;

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
    gEXSetScissor(c, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, window_left, 0, window_right, height);
    put_cmds(c, 2);
    int strip = -1;
    int32_t fills = 0;
    // The view's left side and one past its right, in quarter pixels.
    auto view_sides = [&](int32_t view, int32_t& left, int32_t& right) {
        const uint32_t vr = EB_VIEW_RECTS + (uint32_t)(views - 1) * 64 + (uint32_t)view * 16;
        left = (int32_t)rd(rdram, vr) * 4;
        right = left + (int32_t)rd(rdram, vr + 8) * 4;
    };
    for (uint32_t at = start; at < gdl; at += 8) {
        while (strip + 1 < g_strips && g_strip_at[strip + 1] <= at) {
            strip++;
        }
        const uint32_t w0 = rd(rdram, at);
        const uint32_t w1 = rd(rdram, at + 4);
        if (g_fill) {
            if ((w0 >> 24) != G_FILLRECT) {
                put(w0, w1);
                continue;
            }
            const int32_t view = fills++;
            if (view >= views || !eb::view_widened(rdram, view)) {
                put(w0, w1);
                continue;
            }
            int32_t left, right;
            view_sides(view, left, right);
            int32_t lrx = (w0 >> 12) & 0xFFF, ulx = (w1 >> 12) & 0xFFF;
            const int32_t lry = w0 & 0xFFF, uly = w1 & 0xFFF;
            // Its right corner is the view's last pixel, which fill mode fills.
            if (ulx <= left) {
                ulx = window_left * 4;
            }
            if (lrx >= right - 4) {
                lrx = window_right * 4;
            }
            // RT64's extended fill rectangle (rt64_gbi_extended.cpp's fillrectV1; rt64_extended_gbi.h has no macro).
            put((uint32_t(RT64_EXTENDED_OPCODE) << 24) | G_EX_FILLRECT_V1,
                uint32_t(G_EX_ORIGIN_NONE) | (uint32_t(G_EX_ORIGIN_NONE) << 12));
            put(((uint32_t)(ulx & 0xFFFF) << 16) | (uint32_t)uly, ((uint32_t)(lrx & 0xFFFF) << 16) | (uint32_t)lry);
            continue;
        }
        const bool rect = (w0 >> 24) == G_TEXRECT && at + 24 <= gdl && (rd(rdram, at + 8) >> 24) == G_RDPHALF_1 &&
                          (rd(rdram, at + 16) >> 24) == G_RDPHALF_2;
        const int32_t view = strip >= 0 ? g_strip_view[strip] : -1;
        if (!rect || view < 0 || view >= views || !eb::view_widened(rdram, view)) {
            put(w0, w1);
            continue;
        }
        int32_t left, right;
        view_sides(view, left, right);
        int32_t lrx = (w0 >> 12) & 0xFFF, ulx = (w1 >> 12) & 0xFFF;
        const int32_t lry = w0 & 0xFFF, uly = w1 & 0xFFF, tile = (w1 >> 24) & 7;
        const uint32_t st = rd(rdram, at + 12), steps = rd(rdram, at + 20);
        int32_t s = (int32_t)(st >> 16);
        const int32_t ds = (int16_t)(steps >> 16);
        // sky_draw_strip clips a strip's rectangles to the view's sides, and those it repeats to the top and bottom a
        // pixel short of the right one.
        if (ulx <= left) {
            const int32_t past = ulx - window_left * 4; // in quarter pixels
            // s (s10.5) runs ds (s5.10) a pixel: back by as many, kept within the 256 texels the tile wraps at.
            s = (int32_t)((s - (int64_t)ds * past / 128) & 0x1FFF);
            ulx = window_left * 4;
        }
        if (lrx >= right - 4) {
            lrx = window_right * 4;
        }
        gEXTextureRectangle(c, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, ulx, uly, lrx, lry, tile, s, st & 0xFFFF,
                            steps >> 16, steps & 0xFFFF);
        put_cmds(c, 3);
        at += 16;
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
