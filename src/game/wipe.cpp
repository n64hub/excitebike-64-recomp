// The fades across the whole window (RT64's Aspect Ratio Expand). screen_draw_wipe draws every fade, a race's (its
// intro's cut to the track, the race's start and end) and the menus': 64 black columns whose alpha ramps across the
// screen, fill rectangles under a scissor of the screen. A race's views and the menus' backdrop reach the window's
// sides (src/game/widescreen.cpp, src/game/menu_logo.cpp), so in a window wider than 4:3 the sides went on showing the
// scene while the 4:3 picture faded to black.
//
// So in a wider window us.toml hooks screen_draw_wipe's entry, noting where the display list stands ($a0), and its
// return, the list's end in $v0, and eb_wipe_end puts the commands it added in a buffer of this file's: the scissor
// becomes an RT64 extended one as wide as the window, and every column is spread across the window, the screen's left
// side at the window's left and its right side at the window's right, so the ramp sweeps the whole window. Stretching
// only the two outer columns to the window's sides left each side the alpha of one column: the left side, the column
// the right-to-left sweep darkens last, stayed lit as one flat strip while the ramp beside it went dark. The columns
// are untextured colour, so spreading them changes nothing but where the ramp stands. The first command the game wrote
// becomes a branch to the buffer, which branches back to where the game goes on, as src/game/sky.cpp does.

#include <cmath>
#include <cstdint>

#include "recomp.h"

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

#include "eb.h"

namespace {

constexpr uint32_t G_DL = 0xDE, G_DL_NOPUSH = 1, G_FILLRECT = 0xF6, G_SETSCISSOR = 0xED;
constexpr uint32_t G_SC_NON_INTERLACE = 0;

constexpr gpr EB_SCREEN_WIDTH = (gpr)(int32_t)0x800F1D8C;  // gScreenWidth
constexpr gpr EB_SCREEN_HEIGHT = (gpr)(int32_t)0x800F1D90; // gScreenHeight

// Four buffers in turn past src/game/sky.cpp's (0x80C44000, four of 0x4000), since the renderer may read a list a
// frame or more after the game sends it. A wipe is 64 columns of two commands and a few more.
constexpr uint32_t BUFFER_BASE = 0x80C54000;
constexpr uint32_t BUFFER_SIZE = 0x1000;
constexpr uint32_t BUFFER_COUNT = 4;

uint32_t g_start = 0;
uint32_t g_next_buffer = 0;

uint32_t rd(uint8_t* rdram, uint32_t addr) {
    return (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
}

void wr(uint8_t* rdram, uint32_t addr, uint32_t value) {
    MEM_W(0, (gpr)(int32_t)addr) = (int32_t)value;
}

} // namespace

// At screen_draw_wipe's entry, with the list in $a0.
extern "C" void eb_wipe_begin(uint8_t* rdram, uint32_t gdl) {
    g_start = gdl;
}

// At its return, with the list's end in $v0.
extern "C" void eb_wipe_end(uint8_t* rdram, uint32_t gdl) {
    const uint32_t start = g_start;
    g_start = 0;
    if (eb::window_wide() <= 1.0f || start == 0 || gdl <= start || (start & 7) != 0 ||
        gdl - start > BUFFER_SIZE - 0x100) {
        return;
    }
    const int32_t screen = MEM_W(0, EB_SCREEN_WIDTH);
    const int32_t height = MEM_W(0, EB_SCREEN_HEIGHT);
    if (screen <= 0 || height <= 0) {
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

    GfxCommand c[2];
    gEXEnable(c);
    put_cmds(c, 1);
    gEXPushScissor(c);
    put_cmds(c, 1);
    for (uint32_t at = start; at < gdl; at += 8) {
        const uint32_t w0 = rd(rdram, at);
        const uint32_t w1 = rd(rdram, at + 4);
        if ((w0 >> 24) == G_SETSCISSOR) {
            gEXSetScissor(c, G_SC_NON_INTERLACE, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, window_left, 0, window_right,
                          height);
            put_cmds(c, 2);
            continue;
        }
        if ((w0 >> 24) != G_FILLRECT) {
            put(w0, w1);
            continue;
        }
        // The screen's [0, screen] onto the window's [window_left, window_right], in the rectangle's 10.2 fixed point.
        auto spread = [&](int32_t x) {
            return int32_t(int64_t(window_left) * 4 + int64_t(x) * (window_right - window_left) / screen);
        };
        const int32_t ulx = spread((w1 >> 12) & 0xFFF), lrx = spread((w0 >> 12) & 0xFFF);
        const int32_t lry = w0 & 0xFFF, uly = w1 & 0xFFF;
        // RT64's extended fill rectangle (rt64_gbi_extended.cpp's fillrectV1; rt64_extended_gbi.h has no macro).
        put((uint32_t(RT64_EXTENDED_OPCODE) << 24) | G_EX_FILLRECT_V1,
            uint32_t(G_EX_ORIGIN_NONE) | (uint32_t(G_EX_ORIGIN_NONE) << 12));
        put(((uint32_t)(ulx & 0xFFFF) << 16) | (uint32_t)uly, ((uint32_t)(lrx & 0xFFFF) << 16) | (uint32_t)lry);
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
