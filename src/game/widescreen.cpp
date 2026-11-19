// The race's views out to the window's edges (RT64's Aspect Ratio Expand), and to the screen's top and bottom in every
// window. The game draws one player's view in a box inside its 512x240 screen, a border of 20 pixels at the left and
// the right and 16 at the top and the bottom (sLayout1P, 20, 16, 472x208), which a TV's edge hid; the high-resolution
// mode's box is shorter still (sLayout1PHiRes, 20, 42, 472x160). RT64 widens a 3D view to the window only when it spans
// the whole width the frame's scissors do, and the race's map and its clears are drawn under the whole screen's, so
// with the border the view stayed in the 4:3 frame with black at the window's sides, and above and below it in every
// window, 4:3 too.
//
// So each view fills the screen, as its layout's part of it: one player's 512x240 in both modes, two players' one
// above the other (sLayout2PHorizontal, 400x104 at x 56) 512x120 each, side by side (sLayout2PVertical, 228x192)
// 256x240 each, and three or four players' quarters (224x96) 256x120 each. The layouts are written over the game's data
// once the image is unpacked, before the game reads them (render_update_screen_layout copies the mode's into
// gViewLayouts as a race starts, render_apply_split_layout the split's into sViewLayoutsMulti, and
// render_update_viewports makes gViewRects, the views' rectangles in pixels, and their viewports from them). Each
// view's projection is widened and heightened by as much (eb_view_projection, us.toml's hook in render_draw_race,
// before guPerspective): its field of view's tangent by the height's ratio, its aspect by the width's over it, so what
// the console showed stays where it was, the same size, and the track is drawn where the border was. One player's view
// and two players' one above the other span the screen's width, so RT64 widens them to the window; the others are
// narrower than the frame and stay 4:3 in the middle of the window, which the 4:3 picture fills from top to bottom.
//
// The HUD stays where the game puts it: the race's 3D (from render_draw_race's start to after its riders, where its
// HUD, the track's intro and the pause menu begin) reads the full views, and everything after it, and the menus drawn
// over a race, read the game's boxes (eb_view_rects writes one or the other over gViewLayouts, sViewLayoutsMulti and
// gViewRects; render_update_viewports, hooked at its start too, makes the full ones). The game's box for one player
// and two one above the other is its width already widened to the screen's, as src/game/hud.cpp places their HUD. The
// sky's backdrop is drawn down to sSkyClipHeight, which sky_scan_zbuffer sets after each frame from the depth buffer
// and the views' rectangles, on the scheduler's thread, where the game's boxes may be in place: it is set to each
// view's full height as the 3D starts, the backdrop drawn behind the whole view as on the stadium's tracks.
//
// And the race culls its world to that view: the stadium's sections and objects to within half as far across as ahead
// of the camera (arn_cull_models; half again as far with two screens one above the other), an outdoor track's sections
// to those with a corner within sCullSlope (0x68/256, 0x9C/256 with two screens) across of its depth
// (tube_cull_sections), neither above or below. Both are widened by eb_cull_scale (us.toml's hooks): by the views'
// widths' ratio, times the window's aspect ratio over 4:3 with the Graphics tab's Aspect Ratio Expand for the views
// RT64 widens to the window. At 32:9 the stands, barriers and track past the 4:3 frame's sides came and went with the
// camera.

#include <cmath>
#include <cstdint>

#include "recomp.h"
#include "ultramodern/config.hpp"
#include "recompui/recompui.h"

#include "eb.h"

namespace {
    constexpr gpr layout_1p_hi_res = (gpr)(int32_t)0x800C2DB4;  // sLayout1PHiRes: x, y, width, height
    constexpr gpr layout_1p = (gpr)(int32_t)0x800C2DC4;         // sLayout1P
    constexpr gpr layout_2p_vertical = (gpr)(int32_t)0x800C2D34; // sLayout2PVertical: its two views, 0x10 each
    constexpr gpr layout_2p_horizontal = (gpr)(int32_t)0x800C2D74; // sLayout2PHorizontal
    constexpr gpr view_layouts = (gpr)(int32_t)0x800C2DD4;      // gViewLayouts[4], one screen's, as unpacked
    constexpr gpr view_layouts_multi = (gpr)(int32_t)0x800C2E14; // sViewLayoutsMulti[3][4]: two, three, four screens
    constexpr gpr view_rects = (gpr)(int32_t)0x800E2038;        // gViewRects[4][4]: x, y, width, height in pixels
    constexpr gpr sky_clip_height = (gpr)(int32_t)0x800E9880;   // sSkyClipHeight[4]
    constexpr gpr num_views = (gpr)(int32_t)0x800C2ED4;         // gNumViews
    constexpr gpr cur_view = (gpr)(int32_t)0x800C2ED8;          // gCurView
    constexpr gpr inset_view_active = (gpr)(int32_t)0x800C2CC4; // gInsetViewActive
    constexpr gpr split_vertical = (gpr)(int32_t)0x800C2CC8;    // gSplitVertical
    constexpr gpr num_screens = (gpr)(int32_t)0x800E21B8;       // gNumScreens
    constexpr gpr vi_mode = (gpr)(int32_t)0x800CD5E8;           // gViMode: 2 in the high-resolution mode
    constexpr gpr screen_width = (gpr)(int32_t)0x800F1D8C;      // gScreenWidth
    constexpr gpr screen_height = (gpr)(int32_t)0x800F1D90;     // gScreenHeight

    struct Rect {
        int32_t x, y, w, h;
        bool operator==(const Rect&) const = default;
    };

    // Each view of each layout, in the screen's 512x240: the game's own, the box the HUD and the menus read (the
    // game's, widened to the screen's width where src/game/hud.cpp places the HUD for that), and the full one.
    struct View {
        Rect game, box, full;
    };
    constexpr View one_player { { 20, 16, 472, 208 }, { 0, 16, 512, 208 }, { 0, 0, 512, 240 } };
    constexpr View one_player_hi_res { { 20, 42, 472, 160 }, { 0, 42, 512, 160 }, { 0, 0, 512, 240 } };
    constexpr View views[] = {
        one_player,
        one_player_hi_res,
        { { 56, 16, 400, 104 }, { 0, 16, 512, 104 }, { 0, 0, 512, 120 } },     // two, one above the other
        { { 56, 120, 400, 104 }, { 0, 120, 512, 104 }, { 0, 120, 512, 120 } },
        { { 28, 24, 228, 192 }, { 28, 24, 228, 192 }, { 0, 0, 256, 240 } },     // two side by side
        { { 256, 24, 228, 192 }, { 256, 24, 228, 192 }, { 256, 0, 256, 240 } },
        { { 32, 24, 224, 96 }, { 32, 24, 224, 96 }, { 0, 0, 256, 120 } },       // three or four, quarters
        { { 256, 24, 224, 96 }, { 256, 24, 224, 96 }, { 256, 0, 256, 120 } },
        { { 32, 120, 224, 96 }, { 32, 120, 224, 96 }, { 0, 120, 256, 120 } },
        { { 256, 120, 224, 96 }, { 256, 120, 224, 96 }, { 256, 120, 256, 120 } },
    };

    Rect read(uint8_t* rdram, gpr at) {
        return { MEM_W(0, at), MEM_W(4, at), MEM_W(8, at), MEM_W(12, at) };
    }

    void write(uint8_t* rdram, gpr at, const Rect& r) {
        MEM_W(0, at) = r.x;
        MEM_W(4, at) = r.y;
        MEM_W(8, at) = r.w;
        MEM_W(12, at) = r.h;
    }

    // A layout's rectangle in pixels, as render_update_viewports makes it.
    Rect pixels(uint8_t* rdram, const Rect& r) {
        const int32_t sw = MEM_W(0, screen_width), sh = MEM_W(0, screen_height);
        return { r.x * sw / 512, r.y * sh / 240, r.w * sw / 512, r.h * sh / 240 };
    }

    bool hi_res(uint8_t* rdram) {
        return MEM_W(0, vi_mode) == 2;
    }

    // The view whose full rectangle (in pixels, or in the layout's units) is r: one player's in the screen's mode.
    const View* full_view(uint8_t* rdram, const Rect& r, bool in_pixels) {
        for (const View& v : views) {
            if ((in_pixels ? pixels(rdram, v.full) : v.full) == r) {
                return (v.full == one_player.full) && hi_res(rdram) ? &one_player_hi_res : &v;
            }
        }
        return nullptr;
    }

    const View* box_view(uint8_t* rdram, const Rect& r, bool in_pixels) {
        for (const View& v : views) {
            if ((in_pixels ? pixels(rdram, v.box) : v.box) == r) {
                return &v;
            }
        }
        return nullptr;
    }

    // One rectangle from the full view to its box, or back; one that is neither (the inset view, a layout's unused
    // views) is left.
    void swap(uint8_t* rdram, gpr at, bool full, bool in_pixels) {
        const Rect r = read(rdram, at);
        if (full) {
            if (const View* v = box_view(rdram, r, in_pixels)) {
                write(rdram, at, in_pixels ? pixels(rdram, v->full) : v->full);
            }
        }
        else if (const View* v = full_view(rdram, r, in_pixels)) {
            write(rdram, at, in_pixels ? pixels(rdram, v->box) : v->box);
        }
    }

    // The view `view` of those drawn, by its rectangle; none for the inset view.
    const View* view_drawn(uint8_t* rdram, int32_t view) {
        const int32_t n = MEM_W(0, num_views);
        if (n < 1 || n > 4 || view < 0 || view >= 4) {
            return nullptr;
        }
        return full_view(rdram, read(rdram, view_rects + (n - 1) * 64 + view * 16), true);
    }
}

void eb::widen_views(uint8_t* rdram) {
    write(rdram, layout_1p, one_player.full);
    write(rdram, layout_1p_hi_res, one_player_hi_res.full);
    write(rdram, view_layouts, one_player.full);
    write(rdram, layout_2p_horizontal, views[2].full);
    write(rdram, layout_2p_horizontal + 0x10, views[3].full);
    write(rdram, layout_2p_vertical, views[4].full);
    write(rdram, layout_2p_vertical + 0x10, views[5].full);
    for (int32_t screens = 3; screens <= 4; screens++) {
        for (int32_t view = 0; view < 4; view++) {
            write(rdram, view_layouts_multi + (screens - 2) * 0x40 + view * 0x10, views[6 + view].full);
        }
    }
}

// The full views (full) or the game's boxes in place, in the layouts and the views' rectangles: us.toml's hooks at
// render_draw_race's start and render_update_viewports' (full), and after render_draw_race's riders (boxes).
extern "C" void eb_view_rects(uint8_t* rdram, int32_t full) {
    for (int32_t view = 0; view < 4; view++) {
        swap(rdram, view_layouts + view * 0x10, full != 0, false);
    }
    for (int32_t i = 0; i < 12; i++) {
        swap(rdram, view_layouts_multi + i * 0x10, full != 0, false);
    }
    for (int32_t i = 0; i < 16; i++) {
        swap(rdram, view_rects + i * 0x10, full != 0, true);
    }
    if (full != 0) {
        const int32_t n = MEM_W(0, num_views);
        for (int32_t view = 0; (n >= 1) && (n <= 4) && (view < n); view++) {
            MEM_W(view * 4, sky_clip_height) = MEM_W(12, view_rects + (n - 1) * 64 + view * 16);
        }
    }
}

// Whether the race's view `view` is one widened to the screen's width, which RT64 widens to the window: one player's,
// the main view beside the inset one, and two players' one above the other (src/game/sky.cpp).
bool eb::view_widened(uint8_t* rdram, int32_t view) {
    const int32_t views = MEM_W(0, num_views);
    if (views == 1) {
        return true;
    }
    if (views != 2) {
        return false;
    }
    return MEM_W(0, inset_view_active) != 0 ? view == 0 : MEM_W(0, split_vertical) == 0;
}

// The projection of the view being drawn (us.toml's hook in render_draw_race, before guPerspective: the field of view
// in degrees in $a2, the aspect in $a3), widened and heightened with its view: the field of view's tangent times how
// much taller the view is than the game's, the aspect times how much wider over that. The inset view is the game's.
extern "C" void eb_view_projection(uint8_t* rdram, recomp_context* ctx) {
    const View* v = view_drawn(rdram, MEM_W(0, cur_view));
    if (!v) {
        return;
    }
    const float wider = float(v->full.w) / float(v->game.w), taller = float(v->full.h) / float(v->game.h);
    union { uint32_t u; float f; } fovy, aspect;
    fovy.u = (uint32_t)ctx->r6;
    aspect.u = (uint32_t)ctx->r7;
    constexpr float degrees = 3.14159265358979f / 180.0f;
    fovy.f = 2.0f * std::atan(std::tan(fovy.f * 0.5f * degrees) * taller) / degrees;
    aspect.f *= wider / taller;
    ctx->r6 = (int32_t)fovy.u;
    ctx->r7 = (int32_t)aspect.u;
}

// How much wider than the game's own the race's culling is (above): the views' widths' ratio to the game's (512/472
// with one screen, 512/400 with two one above the other, 256/228 side by side, 256/224 in quarters), times how much
// wider than 4:3 the window is with Expand for the views RT64 widens to it. From the screens' count, not the views'
// rectangles, which the game's boxes may hold where it culls.
extern "C" float eb_cull_scale(uint8_t* rdram) {
    switch (MEM_W(0, num_screens)) {
        case 1:
            return 512.0f / 472.0f * eb::window_wide();
        case 2:
            return MEM_W(0, split_vertical) != 0 ? 256.0f / 228.0f : 512.0f / 400.0f * eb::window_wide();
        case 3:
        case 4:
            return 256.0f / 224.0f;
        default:
            return 1.0f;
    }
}

float eb::window_wide() {
    if (ultramodern::renderer::get_graphics_config().ar_option != ultramodern::renderer::AspectRatio::Expand) {
        return 1.0f;
    }
    int width = 0;
    int height = 0;
    recompui::get_window_size(width, height);
    if ((width <= 0) || (height <= 0)) {
        return 1.0f;
    }
    const float wide = (float(width) / float(height)) / (4.0f / 3.0f);
    return wide > 1.0f ? wide : 1.0f;
}
