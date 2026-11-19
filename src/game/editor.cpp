// The track editor out to the window's edges (RT64's Aspect Ratio Expand). The game draws it inside a border of its
// 512x240 screen, 26 pixels at the left and the right and 16 at the top (sEditViewRects: the screen 26, 16, 460x209;
// its menu panel at the left, out to 162; the 3D track view 162 to 484, with a bar above and below it), so even a 4:3
// window showed black down both sides, and a wider one the whole editor in the middle.
//
// So the panel is drawn from the window's left edge and the track view out to its right edge, in every window, 4:3
// too. As src/game/hud.cpp does for the race's HUD, us.toml hooks edit_draw at its entry (the list's end, *pgdl) and
// before it returns (the list's end again), and the commands it added are put back with RT64's alignments between them:
// - what lies left of the track view (the panel's fill, its buttons and their text) moved left by the screen's border
//   and placed from the window's left (gEXSetRectAlign);
// - the track view's own fill and the first piece of each bar from the panel's edge to the window's right edge, the
//   bar's texture stretched with it (its pattern runs only up and down; the game's other pieces stay over it);
// - the screen's scissor from the window's left edge to its right, and the track view's from the panel's edge to the
//   window's right (gEXSetScissor);
// - the track view's viewport replaced by one far wider than the screen (VIEWPORT_HALF either side of its middle), its
//   projection narrowed by as much and moved by where the game's viewport was, so the picture lands where the game
//   draws it and goes on past it: RT64 widens a 3D view to the window only when it spans the whole width the frame's
//   scissors do, which the track view beside the panel never does, and places one that doesn't at the 4:3 frame's
//   scale, which past the frame shows what the wider viewport has there.
// What lies over the track view (its name, the piece's, the warnings, name entry) stays where the game draws it: the
// track view's middle is where it was in every window, since it widens by as much on both sides.
//
// When the panel slides out (slideOffset, up to MAX_SLIDE), the track view's left edge goes from the panel's edge to
// the window's, as the game's goes from the panel's to the border. The commands go to buffers of this file's past
// src/game/splash.cpp's (0x80C58000, four of 0x2000), eight in turn as hud.cpp's, each with room at its end for the
// viewports and projections written in it; a list longer than a buffer is left as the game drew it.

#include <cstdint>
#include <cstring>

#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

namespace {

// F3DEX2's commands and flags this reads (the decomp's include/PR/gbi.h).
constexpr uint32_t G_MTX = 0xDA, G_MOVEMEM = 0xDC, G_DL = 0xDE;
constexpr uint32_t G_TEXRECT = 0xE4, G_FILLRECT = 0xF6, G_SETSCISSOR = 0xED;
constexpr uint32_t G_MV_VIEWPORT = 8, G_DL_NOPUSH = 1;
constexpr uint32_t G_MTX_PUSH = 1, G_MTX_LOAD = 2, G_MTX_PROJECTION = 4;

// The game's data this reads (USA Rev 1).
constexpr uint32_t EB_SCREEN_WIDTH = 0x800F1D8C;      // gScreenWidth
constexpr uint32_t EB_EDIT_VIEW_RECTS = 0x800CF300;   // sEditViewRects: x, y, w, h (words) for the screen, the track view, ...
constexpr uint32_t EB_EDIT_VIEW_BASE = 0x800CF340;    // sEditViewRectsBase: the same with the panel in
constexpr uint32_t VIEW_SCREEN = 0, VIEW_TRACK = 1;
constexpr int MAX_SLIDE = 120;                        // 30 frames, 4 pixels each
constexpr int VIEWPORT_HALF = 1024;                   // the track view's viewport's half width: past a 32:9 window's sides

constexpr uint32_t BUFFER_BASE = 0x80C60000;
constexpr uint32_t BUFFER_SIZE = 0x18000;
constexpr uint32_t BUFFER_COUNT = 8;
constexpr uint32_t DATA_SIZE = 0x800; // at each buffer's end: viewports and projections
constexpr uint32_t MAX_COMMANDS = (BUFFER_SIZE - DATA_SIZE) / 8 - 64;

uint8_t* g_rdram;
uint32_t g_start;
bool g_open = false;
uint32_t g_next_buffer = 0;

uint32_t rd_w(uint32_t addr) {
    return *reinterpret_cast<uint32_t*>(g_rdram + ((addr & 0xFFFFFF) & ~3u));
}

int16_t rd_h(uint32_t addr) {
    uint32_t w = rd_w(addr);
    return int16_t((addr & 2) ? (w & 0xFFFF) : (w >> 16));
}

void wr_w(uint32_t addr, uint32_t value) {
    *reinterpret_cast<uint32_t*>(g_rdram + ((addr & 0xFFFFFF) & ~3u)) = value;
}

void wr_h(uint32_t addr, int16_t value) {
    uint32_t w = rd_w(addr);
    w = (addr & 2) ? ((w & 0xFFFF0000u) | uint16_t(value)) : ((w & 0xFFFFu) | (uint32_t(uint16_t(value)) << 16));
    wr_w(addr, w);
}

// A KSEG0 or segment 0 address in RDRAM; 0 for any other segment.
uint32_t resolve(uint32_t seg_addr) {
    if ((seg_addr & 0x0F000000) != 0) {
        return 0;
    }
    return 0x80000000u | (seg_addr & 0xFFFFFF);
}

struct Rect {
    int x, y, w, h;
};

Rect read_rect(uint32_t base, uint32_t index) {
    uint32_t a = base + index * 16;
    return { int(rd_w(a)), int(rd_w(a + 4)), int(rd_w(a + 8)), int(rd_w(a + 12)) };
}

struct Writer {
    uint32_t pos, end;
    bool ok = true;
    void put(uint32_t w0, uint32_t w1) {
        if (pos + 8 > end) {
            ok = false;
            return;
        }
        wr_w(pos, w0);
        wr_w(pos + 4, w1);
        pos += 8;
    }
    void put(const GfxCommand* cmds, int n) {
        for (int i = 0; i < n; i++) {
            put(cmds[i].values.word0, cmds[i].values.word1);
        }
    }
};

// An N64 fixed-point Mtx's element: its integer half in the first 32 bytes, its fraction in the next.
int32_t mtx_get(uint32_t m, int i, int j) {
    int idx = i * 4 + j;
    return int32_t((uint32_t(uint16_t(rd_h(m + idx * 2))) << 16) | uint16_t(rd_h(m + 32 + idx * 2)));
}

void mtx_set(uint32_t m, int i, int j, int32_t v) {
    int idx = i * 4 + j;
    wr_h(m + idx * 2, int16_t(uint32_t(v) >> 16));
    wr_h(m + 32 + idx * 2, int16_t(uint32_t(v) & 0xFFFF));
}

// The alignment rectangles are drawn with, as gEXSetRectAlign takes it.
struct Align {
    uint32_t lorigin = G_EX_ORIGIN_NONE, rorigin = G_EX_ORIGIN_NONE;
    int ulx = 0, lrx = 0;
    bool operator!=(const Align& o) const {
        return lorigin != o.lorigin || rorigin != o.rorigin || ulx != o.ulx || lrx != o.lrx;
    }
};

void rewrite(uint32_t start, uint32_t end) {
    const uint32_t count = (end - start) / 8;
    if (end <= start || count > MAX_COMMANDS || (start & 7) != 0) {
        return;
    }
    const int width = int(rd_w(EB_SCREEN_WIDTH));
    const Rect screen = read_rect(EB_EDIT_VIEW_RECTS, VIEW_SCREEN);
    const Rect track = read_rect(EB_EDIT_VIEW_RECTS, VIEW_TRACK);
    const Rect base = read_rect(EB_EDIT_VIEW_BASE, VIEW_TRACK);
    if (width < 320 || screen.x <= 0 || screen.x >= width / 4 || track.w <= 0 || base.x <= screen.x) {
        return;
    }
    // The panel moves left by the screen's border, and the track view's left edge with it, out to the window's as
    // the panel slides out.
    const int shift = screen.x;
    int slide = base.x - track.x;
    slide = slide < 0 ? 0 : (slide > MAX_SLIDE ? MAX_SLIDE : slide);
    const int track_left = (base.x - shift) * (MAX_SLIDE - slide) / MAX_SLIDE;
    const int track_right = track.x + track.w - 1;

    const uint32_t buffer = BUFFER_BASE + BUFFER_SIZE * g_next_buffer;
    g_next_buffer = (g_next_buffer + 1) % BUFFER_COUNT;
    Writer out{ buffer, buffer + BUFFER_SIZE - DATA_SIZE };
    uint32_t data = buffer + BUFFER_SIZE - DATA_SIZE;
    const uint32_t data_end = buffer + BUFFER_SIZE;

    GfxCommand c[2];
    gEXEnable(c);
    out.put(c, 1);
    gEXPushScissor(c);
    out.put(c, 1);

    // The track view's viewport while one is in effect (its middle and half width, in pixels), and the projection
    // loaded last.
    bool track_vp = false;
    float vp_mid = 0.0f, vp_half = 1.0f;
    uint32_t proj_w0 = 0, proj_w1 = 0;
    bool proj_narrowed = false;
    auto narrowed_projection = [&]() {
        uint32_t src = resolve(proj_w1);
        if (src == 0 || data + 64 > data_end) {
            out.put(proj_w0, proj_w1);
            proj_narrowed = false;
            return;
        }
        // x' = (mid - width/2 + half * x) / VIEWPORT_HALF, in the clip space: x's column scaled, plus w's.
        const double a = double(vp_half) / VIEWPORT_HALF;
        const double b = double(vp_mid - width * 0.5f) / VIEWPORT_HALF;
        uint32_t dst = data;
        data += 64;
        for (int k = 0; k < 64; k += 4) {
            wr_w(dst + k, rd_w(src + k));
        }
        for (int i = 0; i < 4; i++) {
            double x = mtx_get(src, i, 0), w = mtx_get(src, i, 3);
            mtx_set(dst, i, 0, int32_t(a * x + b * w));
        }
        out.put(proj_w0, dst & 0x1FFFFFFF);
        proj_narrowed = true;
    };

    Align align;
    const uint32_t* last_scissor = nullptr;
    uint32_t last_scissor_w[2] = {};
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t w0 = rd_w(start + i * 8), w1 = rd_w(start + i * 8 + 4);
        const uint32_t op = w0 >> 24;
        if (op == G_SETSCISSOR) {
            const uint32_t mode = (w1 >> 24) & 0x3;
            const int ulx = int((w0 >> 12) & 0xFFF) / 4, uly = int(w0 & 0xFFF) / 4;
            const int lrx = int((w1 >> 12) & 0xFFF) / 4, lry = int(w1 & 0xFFF) / 4;
            last_scissor_w[0] = w0;
            last_scissor_w[1] = w1;
            last_scissor = last_scissor_w;
            if (ulx == screen.x && lrx >= screen.x + screen.w - 1) {
                gEXSetScissor(c, mode, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, uly, 0, lry);
                out.put(c, 2);
            }
            else if (ulx == track.x && lrx >= track_right) {
                gEXSetScissor(c, mode, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, track_left, uly, 0, lry);
                out.put(c, 2);
            }
            else {
                out.put(w0, w1);
            }
            continue;
        }
        if (op == G_MOVEMEM && (w0 & 0xFF) == G_MV_VIEWPORT && resolve(w1) != 0) {
            const uint32_t vp = resolve(w1);
            const float half = rd_h(vp + 0) / 4.0f, mid = rd_h(vp + 8) / 4.0f;
            const bool is_track = mid + half >= track_right - 2 && mid - half >= track.x - 2 && half > 0.0f;
            if (is_track && data + 16 <= data_end) {
                vp_mid = mid;
                vp_half = half;
                uint32_t dst = data;
                data += 16;
                for (int k = 0; k < 16; k += 4) {
                    wr_w(dst + k, rd_w(vp + k));
                }
                wr_h(dst + 0, int16_t(VIEWPORT_HALF * 4));
                wr_h(dst + 8, int16_t(width * 2));
                out.put(w0, dst & 0x1FFFFFFF);
                track_vp = true;
                if (proj_w0 != 0) {
                    narrowed_projection();
                }
            }
            else {
                out.put(w0, w1);
                track_vp = false;
                if (proj_narrowed) {
                    out.put(proj_w0, proj_w1);
                    proj_narrowed = false;
                }
            }
            continue;
        }
        if (op == G_MTX && (((w0 & 0xFF) ^ G_MTX_PUSH) & G_MTX_PROJECTION)) {
            if (((w0 & 0xFF) ^ G_MTX_PUSH) & G_MTX_LOAD) {
                proj_w0 = w0;
                proj_w1 = w1;
                if (track_vp) {
                    narrowed_projection();
                    continue;
                }
            }
            else {
                proj_w0 = 0;
            }
            proj_narrowed = false;
            out.put(w0, w1);
            continue;
        }
        if (op == G_TEXRECT || op == G_FILLRECT) {
            const int ulx = int((w1 >> 12) & 0xFFF) / 4, uly = int(w1 & 0xFFF) / 4;
            const int lrx_q = int((w0 >> 12) & 0xFFF), lrx = lrx_q / 4;
            Align want;
            if (lrx <= track.x + 1) {
                // The panel.
                want.lorigin = want.rorigin = G_EX_ORIGIN_LEFT;
                want.ulx = want.lrx = -shift * 4;
            }
            else if (ulx == track.x && (op == G_FILLRECT ? (uly == track.y && lrx >= track_right - 1)
                                                         : (uly < track.y + 12 || uly >= track.y + track.h - 24))) {
                // The track view's fill, or a bar's first piece: out to the window's right edge.
                want.lorigin = G_EX_ORIGIN_LEFT;
                want.rorigin = G_EX_ORIGIN_RIGHT;
                want.ulx = (track_left - track.x) * 4;
                want.lrx = -lrx_q;
            }
            if (want != align) {
                gEXSetRectAlign(c, want.lorigin, want.rorigin, want.ulx, 0, want.lrx, 0);
                out.put(c, 2);
                align = want;
            }
        }
        out.put(w0, w1);
    }

    gEXSetRectAlign(c, G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    out.put(c, 2);
    gEXPopScissor(c);
    out.put(c, 1);
    if (last_scissor != nullptr) {
        out.put(last_scissor[0], last_scissor[1]);
    }
    out.put((uint32_t(G_DL) << 24) | (G_DL_NOPUSH << 16), end & 0x1FFFFFFF);
    if (!out.ok) {
        return;
    }
    // The game's first command becomes the branch to the buffer.
    wr_w(start, (uint32_t(G_DL) << 24) | (G_DL_NOPUSH << 16));
    wr_w(start + 4, buffer & 0x1FFFFFFF);
}

} // namespace

// At edit_draw's entry, with pgdl ($a2).
extern "C" void eb_edit_begin(uint8_t* rdram, uint32_t pgdl) {
    g_rdram = rdram;
    g_start = rd_w(pgdl);
    g_open = true;
}

// Before it returns, with gdl where the game goes on (*pgdl, $fp there).
extern "C" void eb_edit_end(uint8_t* rdram, uint32_t gdl) {
    if (!g_open) {
        return;
    }
    g_open = false;
    g_rdram = rdram;
    rewrite(g_start, gdl);
}
