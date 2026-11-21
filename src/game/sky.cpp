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
//
// And in every window the sky turns with the camera as smoothly as the track. sky_draw_strip draws each strip as two
// copies of the image side by side, the second starting at the image's left edge where the first ends, a seam that
// moves with the yaw, so both rectangles change width every frame. RT64_INTERPOLATE_RECT_SCROLLS pairs a rectangle with
// the frame before's by its place, size and texels, and slid these the wrong way in the frames between: the backdrop
// moved too far and stepped back at each of the game's own frames, against a track that moved evenly. So eb_sky_end
// makes each pair one rectangle across the view, the first reaching the second's right side with the tile's wrap
// (256 texels) carrying the image on past the seam, and the second's commands no-ops: its edges stay put, only its
// texture scrolls, and RT64 slides that within the tile's period. And sky_update_scroll takes the yaw a 32nd at a
// time (gCamYaw / 32, 256 steps to the image's width) and sky_draw_strip places the image at whole pixels of the
// screen (sSkyScrollX * (view's width / 2) / sSkyScaleX, cut down), so the image moves 1 or 2 of the screen's pixels a
// game frame, unevenly: eb_sky_end moves each strip's texture by what the two roundings cut off, from the yaw's own
// low bits, its texture coordinate going back by that many pixels' worth of texels (s, s10.5, runs ds, s5.10, a
// pixel).

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
constexpr gpr EB_MIRROR_MODE = (gpr)(int32_t)0x800C2CA4;   // gMirrorMode
constexpr uint32_t EB_CAM_YAW = 0x800E89A0;                // gCamYaw[4]
constexpr uint32_t EB_SKY_SCROLL_X = 0x800E9840;           // sSkyScrollX[4]
constexpr uint32_t EB_SKY_SCALE_X = 0x800E9860;            // sSkyScaleX[4]
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
    g_open = !fill || eb::window_wide() > 1.0f;
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
    // The view's left side and one past its right, in quarter pixels.
    auto view_sides = [&](int32_t view, int32_t& left, int32_t& right) {
        const uint32_t vr = EB_VIEW_RECTS + (uint32_t)(views - 1) * 64 + (uint32_t)view * 16;
        left = (int32_t)rd(rdram, vr) * 4;
        right = left + (int32_t)rd(rdram, vr + 8) * 4;
    };
    if (!g_fill) {
        // Each view's strips go on by the pixels sky_update_scroll and sky_draw_strip cut off (above).
        float frac[4] = {};
        for (int32_t view = 0; view < views; view++) {
            int32_t left, right;
            view_sides(view, left, right);
            const int32_t width = (right - left) / 4;
            const int32_t scroll_x = (int32_t)rd(rdram, EB_SKY_SCROLL_X + (uint32_t)view * 4);
            const int32_t scale_x = (int32_t)rd(rdram, EB_SKY_SCALE_X + (uint32_t)view * 4);
            int32_t yaw = (int32_t)rd(rdram, EB_CAM_YAW + (uint32_t)view * 4) & 0x1FFF;
            if (MEM_W(0, EB_MIRROR_MODE) != 0) {
                yaw = 0x2000 - yaw;
            }
            if (scale_x > 0 && width > 0) {
                const float exact = (float(scroll_x) + float(yaw & 31) / 32.0f) * float(width / 2) / float(scale_x);
                frac[view] = exact - float(scroll_x * (width / 2) / scale_x);
            }
        }
        int strip = -1;
        for (uint32_t at = start; at + 24 <= gdl; at += 8) {
            while (strip + 1 < g_strips && g_strip_at[strip + 1] <= at) {
                strip++;
            }
            const int32_t view = strip >= 0 ? g_strip_view[strip] : -1;
            if ((rd(rdram, at) >> 24) != G_TEXRECT || (rd(rdram, at + 8) >> 24) != G_RDPHALF_1 ||
                (rd(rdram, at + 16) >> 24) != G_RDPHALF_2 || view < 0 || view >= views) {
                continue;
            }
            const uint32_t st = rd(rdram, at + 12);
            const int32_t ds = (int16_t)(rd(rdram, at + 20) >> 16);
            const int32_t s = (int16_t)(st >> 16) - (int32_t)std::lround(frac[view] * float(ds) / 32.0f);
            wr(rdram, at + 12, ((uint32_t)(s & 0xFFFF) << 16) | (st & 0xFFFF));
            at += 16;
        }
        // Each pair of a strip's rectangles that meet at the image's seam becomes one: the first reaches the second's
        // right side, the tile's wrap carrying the image on past the seam, and the second's commands become no-ops.
        uint32_t first = 0;
        for (uint32_t at = start; at + 24 <= gdl; at += 8) {
            if ((rd(rdram, at) >> 24) != G_TEXRECT || (rd(rdram, at + 8) >> 24) != G_RDPHALF_1 ||
                (rd(rdram, at + 16) >> 24) != G_RDPHALF_2) {
                continue;
            }
            if (first != 0) {
                const uint32_t a0 = rd(rdram, first), a1 = rd(rdram, first + 4);
                const uint32_t b0 = rd(rdram, at), b1 = rd(rdram, at + 4);
                const uint32_t a_st = rd(rdram, first + 12), b_st = rd(rdram, at + 12);
                const uint32_t a_steps = rd(rdram, first + 20), b_steps = rd(rdram, at + 20);
                const int32_t a_ulx = (a1 >> 12) & 0xFFF, a_lrx = (a0 >> 12) & 0xFFF, b_ulx = (b1 >> 12) & 0xFFF;
                const int32_t ds = (int16_t)(a_steps >> 16);
                // s (s10.5) at the second's left side, carried on from the first's, against the second's own.
                const int32_t carried = (int32_t)(((int16_t)(a_st >> 16) + (int64_t)ds * (b_ulx - a_ulx) / 128) & 0x1FFF);
                const int32_t off = (carried - (int32_t)((b_st >> 16) & 0x1FFF)) & 0x1FFF;
                if ((a0 & 0xFFF) == (b0 & 0xFFF) && (a1 & 0x07000FFF) == (b1 & 0x07000FFF) && a_lrx == b_ulx &&
                    (a_st & 0xFFFF) == (b_st & 0xFFFF) && a_steps == b_steps && (off <= 32 || off >= 0x2000 - 32)) {
                    wr(rdram, first, (a0 & 0xFF000FFF) | (b0 & 0x00FFF000));
                    for (uint32_t i = 0; i < 24; i += 4) {
                        wr(rdram, at + i, 0);
                    }
                    first = 0;
                    at += 16;
                    continue;
                }
            }
            first = at;
            at += 16;
        }
        if (eb::window_wide() <= 1.0f) {
            return;
        }
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
