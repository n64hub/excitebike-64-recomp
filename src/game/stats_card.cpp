// Rider Select's stats card out to the window's sides (RT64's Aspect Ratio Expand). Over the race's scene, Rider Select
// draws each player's name, the stats card (its frame, the labels Landing, Cornering, Jumping and Turbo, and their
// bars) and, with one player, the bio, under a scissor of the player's view (shell_draw_screen's
// SCREEN_TYPE_RIDER_SELECT). With one player, holding Z slides the bio down and the card right by up to 235 of the
// game's units as the bio opens, past the screen's right edge, which the scissor cuts it at; releasing Z slides it back.
// The scene behind reaches the window's sides (src/game/widescreen.cpp), so in a window wider than 4:3 the card was cut
// off at the 4:3 picture's right edge with the scene going on beside it, instead of sliding on toward the window's.
//
// So in a wider window us.toml hooks shell_draw_screen just after it writes that scissor ($a2 its command), and
// eb_stats_card_scissor makes a scissor that spans the screen's width (one player's view, or two players' one above the
// other: views RT64 widens to the window) an RT64 extended one out to the window's sides: the game's command becomes a
// call of a buffer of this file's, which sets it and returns. A view narrower than the screen (two players side by
// side, three or four) keeps the game's, whose sides part one view from the next, and so does every view in a window
// no wider than 4:3. Nothing else under it reaches past the screen's sides: the bio and its panels, the name and the
// arrows lie inside it. The game sets the whole screen's scissor again after its players (for the C buttons), and
// RT64_SCISSOR_INSIDE_FRAMEBUFFER (CMakeLists.txt) keeps a scissor past the framebuffer's sides from stretching what is
// drawn under it.

#include <cmath>
#include <cstdint>

#include "recomp.h"

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

#include "eb.h"

namespace {

constexpr uint32_t G_DL = 0xDE, G_ENDDL = 0xDF, G_SETSCISSOR = 0xED;

constexpr gpr EB_SCREEN_WIDTH = (gpr)(int32_t)0x800F1D8C; // gScreenWidth

// Buffers in turn past src/game/editor.cpp's (0x80C60000, eight of 0x18000), since the renderer may read a list a
// frame or more after the game sends it: up to four players' a frame, and the game keeps two lists.
constexpr uint32_t BUFFER_BASE = 0x80D20000;
constexpr uint32_t BUFFER_SIZE = 0x20;
constexpr uint32_t BUFFER_COUNT = 16;

uint32_t g_next_buffer = 0;

void wr(uint8_t* rdram, uint32_t addr, uint32_t value) {
    MEM_W(0, (gpr)(int32_t)addr) = (int32_t)value;
}

} // namespace

// After the store of Rider Select's scissor for a player's view, its command at `cmd` ($a2).
extern "C" void eb_stats_card_scissor(uint8_t* rdram, uint32_t cmd) {
    if (eb::window_wide() <= 1.0f || (cmd & 7) != 0) {
        return;
    }
    const uint32_t w0 = (uint32_t)MEM_W(0, (gpr)(int32_t)cmd);
    const uint32_t w1 = (uint32_t)MEM_W(4, (gpr)(int32_t)cmd);
    const int32_t screen = MEM_W(0, EB_SCREEN_WIDTH);
    if ((w0 >> 24) != G_SETSCISSOR || screen <= 0) {
        return;
    }
    // Its corners in quarter pixels; the view spans the screen when it runs from its left edge to its last pixel.
    const int32_t ulx = (w0 >> 12) & 0xFFF, uly = w0 & 0xFFF, lrx = (w1 >> 12) & 0xFFF, lry = w1 & 0xFFF;
    if (ulx > 4 || lrx + 8 < screen * 4) {
        return;
    }
    // The window's sides, in the screen's pixels: (wide - 1) / 2 screens past the screen's, a pixel further out.
    const int32_t margin = (int32_t)std::ceil(float(screen) * (eb::window_wide() - 1.0f) * 0.5f) + 1;

    const uint32_t buffer = BUFFER_BASE + g_next_buffer * BUFFER_SIZE;
    g_next_buffer = (g_next_buffer + 1) % BUFFER_COUNT;
    GfxCommand c[3];
    gEXEnable(&c[0]);
    gEXSetScissor(&c[1], (w1 >> 24) & 3, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -margin, uly / 4, screen + margin,
                  lry / 4);
    uint32_t pos = buffer;
    for (const GfxCommand& one : c) {
        wr(rdram, pos, one.values.word0);
        wr(rdram, pos + 4, one.values.word1);
        pos += 8;
    }
    wr(rdram, pos, G_ENDDL << 24);
    wr(rdram, pos + 4, 0);
    // The game's command becomes the call.
    wr(rdram, cmd, G_DL << 24);
    wr(rdram, cmd + 4, buffer & 0x1FFFFFFF);
}
