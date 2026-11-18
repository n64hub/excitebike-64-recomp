// The boot's logos and notices kept at 4:3 in a wider window (RT64's Aspect Ratio Expand): the Left Field logo, the
// legal screen (with its Controller Pak, Rumble Pak and Expansion Pak lines), the N64 Sports logo, and the notices of
// the Controller Paks' free pages and of a lost backup. Each is the screen's background picture with the text in it,
// which shell_draw draws with render_draw_image as rectangles of rows that span the screen, and RT64 stretches a
// rectangle that spans the screen to the window's width, so in a 32:9 window the legal screen's text was drawn twice
// as wide as it is high. The menus' backgrounds, a pattern behind the menu, are left to reach the window's sides.
//
// So in a wider window us.toml hooks that call, noting where the display list stands ($a3) and the screen ($s7), and
// for those screens eb_splash_end puts the commands it added in a buffer of this file's after a black fill of the whole
// screen, which RT64 widens to the window, and between gEXSetRectAspect(G_EX_ASPECT_ADJUST) and back to AUTO, so the
// picture fills the window's height at 4:3, centred, black at the sides. The first command the game wrote becomes a
// branch to the buffer, which branches back to where the game goes on, as src/game/wipe.cpp does.

#include <cstdint>

#include "recomp.h"

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

#include "eb.h"

namespace {

constexpr uint32_t G_DL = 0xDE, G_DL_NOPUSH = 1;
constexpr uint32_t G_RDPPIPESYNC = 0xE7, G_SETOTHERMODE_H = 0xE3, G_SETFILLCOLOR = 0xF7, G_FILLRECT = 0xF6;
// gDPSetCycleType: G_SETOTHERMODE_H's shift 20 and length 2, as F3DEX2 packs them, and G_CYC_FILL and G_CYC_1CYCLE,
// which shell_draw has set before the background.
constexpr uint32_t SET_CYCLE_TYPE = (G_SETOTHERMODE_H << 24) | ((32 - 20 - 2) << 8) | (2 - 1);
constexpr uint32_t G_CYC_FILL = 3u << 20, G_CYC_1CYCLE = 0;

constexpr gpr EB_SCREEN_WIDTH = (gpr)(int32_t)0x800F1D8C;  // gScreenWidth
constexpr gpr EB_SCREEN_HEIGHT = (gpr)(int32_t)0x800F1D90; // gScreenHeight

// Screen's type (shell.h): SCREEN_TYPE_SPLASH to SCREEN_TYPE_BACKUP_LOST are the boot's logos and notices.
constexpr int32_t SCREEN_TYPE_OFFSET = 0x16;
constexpr int32_t SCREEN_TYPE_SPLASH = 21, SCREEN_TYPE_BACKUP_LOST = 25;

// Four buffers in turn past src/game/wipe.cpp's (0x80C54000, four of 0x1000), since the renderer may read a list a
// frame or more after the game sends it. A picture is a few dozen bands of rows, each a load and a rectangle.
constexpr uint32_t BUFFER_BASE = 0x80C58000;
constexpr uint32_t BUFFER_SIZE = 0x2000;
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

// Before shell_draw's render_draw_image call that draws the screen's background, with the list in $a3 and the screen
// in $s7.
extern "C" void eb_splash_begin(uint8_t* rdram, uint32_t gdl, uint32_t screen) {
    g_start = 0;
    if (eb::window_wide() <= 1.0f || screen == 0) {
        return;
    }
    const int32_t type = MEM_H(SCREEN_TYPE_OFFSET, (gpr)(int32_t)screen);
    if (type >= SCREEN_TYPE_SPLASH && type <= SCREEN_TYPE_BACKUP_LOST) {
        g_start = gdl;
    }
}

// After it, with the list's end in $v0.
extern "C" void eb_splash_end(uint8_t* rdram, uint32_t gdl) {
    const uint32_t start = g_start;
    g_start = 0;
    if (start == 0 || gdl <= start || (start & 7) != 0 || gdl - start > BUFFER_SIZE - 0x100) {
        return;
    }
    const int32_t width = MEM_W(0, EB_SCREEN_WIDTH);
    const int32_t height = MEM_W(0, EB_SCREEN_HEIGHT);
    if (width <= 0 || height <= 0) {
        return;
    }

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
    GfxCommand c[1];
    auto put_cmd = [&]() { put(c[0].values.word0, c[0].values.word1); };

    // Black over the whole screen, which spans it and so is widened to the window.
    put(G_RDPPIPESYNC << 24, 0);
    put(SET_CYCLE_TYPE, G_CYC_FILL);
    put(G_SETFILLCOLOR << 24, 0);
    put((G_FILLRECT << 24) | ((uint32_t)((width - 1) * 4) << 12) | (uint32_t)((height - 1) * 4), 0);
    put(G_RDPPIPESYNC << 24, 0);
    put(SET_CYCLE_TYPE, G_CYC_1CYCLE);
    gEXEnable(c);
    put_cmd();
    gEXSetRectAspect(c, G_EX_ASPECT_ADJUST);
    put_cmd();
    for (uint32_t at = start; at < gdl; at += 8) {
        put(rd(rdram, at), rd(rdram, at + 4));
    }
    gEXSetRectAspect(c, G_EX_ASPECT_AUTO);
    put_cmd();
    put((G_DL << 24) | (G_DL_NOPUSH << 16), gdl & 0x1FFFFFFF);
    if (pos > buffer + BUFFER_SIZE) {
        return;
    }
    // The game's first command becomes the branch to the buffer.
    wr(rdram, start, (G_DL << 24) | (G_DL_NOPUSH << 16));
    wr(rdram, start + 4, buffer & 0x1FFFFFFF);
}
