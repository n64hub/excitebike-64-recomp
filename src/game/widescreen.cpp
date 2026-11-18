// The race's view out to the window's sides (RT64's Aspect Ratio Expand). The game draws one player's view in a box
// inside its 512x240 screen, a border of 20 pixels at the left and the right and 16 at the top and the bottom
// (sLayout1P, 20, 16, 472x208; sLayout1PHiRes the same box in the high-resolution mode's letterbox). RT64 widens a 3D
// view to the window only when it spans the whole width the frame's scissors do, and the race's map and its clears are
// drawn under the whole screen's, so with the border the view stayed in the 4:3 frame with black at the window's sides.
//
// So the view spans the screen's width: x 0 and 512 wide in both layouts, written over the game's data once the image
// is unpacked, before the game reads them (render_update_screen_layout copies the mode's layout into gViewLayouts as
// a race starts). Its projection is widened by as much (us.toml's hook in render_draw_race, before guPerspective), so
// the picture keeps its proportions and shows the track where the border was. At 4:3 that is the only change: the
// sides' 20 pixels show the track, the top and bottom borders stay.
//
// Two players' split one above the other (sLayout2PHorizontal, gSplitVertical 0) is two views of 400x104 at x 56 in the
// game's layout, so they span the screen's width too, their projections widened by 512/400, and RT64 widens them to the
// window as it does one player's. Split side by side (sLayout2PVertical, 228 wide each), with three or four players
// (quarters) and the inset view of one player's race, the views are narrower than the frame and stay as the game has
// them, 4:3 in the middle of the window.
//
// And the race culls its world to that view: the stadium's sections and objects to within half as far across as ahead
// of the camera (arn_cull_models; half again as far with two screens one above the other), an outdoor track's sections
// to those with a corner within sCullSlope (0x68/256, 0x9C/256 with two screens) across of its depth
// (tube_cull_sections). Both are widened by eb_cull_scale (us.toml's hooks): 512/472 for one player's view across the
// screen's whole width, 512/400 for two players' one above the other, times the window's aspect ratio over 4:3 with the
// Graphics tab's Aspect Ratio Expand, where RT64 widens the picture to the window. At 32:9 the stands, barriers and
// track past the 4:3 frame's sides came and went with the camera.

#include <cstdint>

#include "recomp.h"
#include "ultramodern/config.hpp"
#include "recompui/recompui.h"

#include "eb.h"

namespace {
    constexpr gpr layout_1p_hi_res = (gpr)(int32_t)0x800C2DB4;  // sLayout1PHiRes: x, y, width, height
    constexpr gpr layout_1p = (gpr)(int32_t)0x800C2DC4;         // sLayout1P
    constexpr gpr view_layouts = (gpr)(int32_t)0x800C2DD4;      // gViewLayouts[0], the layout in use, as unpacked
    constexpr gpr layout_2p_horizontal = (gpr)(int32_t)0x800C2D74; // sLayout2PHorizontal: its two views, 0x10 each
    constexpr gpr num_views = (gpr)(int32_t)0x800C2ED4;         // gNumViews
    constexpr gpr cur_view = (gpr)(int32_t)0x800C2ED8;          // gCurView
    constexpr gpr inset_view_active = (gpr)(int32_t)0x800C2CC4; // gInsetViewActive
    constexpr gpr split_vertical = (gpr)(int32_t)0x800C2CC8;    // gSplitVertical
    constexpr gpr num_screens = (gpr)(int32_t)0x800E21B8;       // gNumScreens
    constexpr float one_player = 512.0f / 472.0f;
    constexpr float two_players = 512.0f / 400.0f;

    // Two players' views, one above the other.
    bool split_horizontal(uint8_t* rdram) {
        return MEM_W(0, inset_view_active) == 0 && MEM_W(0, split_vertical) == 0;
    }

    void widen(uint8_t* rdram, gpr layout) {
        MEM_W(0, layout) = 0;           // x
        MEM_W(8, layout) = 512;         // width
    }
}

void eb::widen_views(uint8_t* rdram) {
    widen(rdram, layout_1p_hi_res);
    widen(rdram, layout_1p);
    widen(rdram, view_layouts);
    widen(rdram, layout_2p_horizontal);
    widen(rdram, layout_2p_horizontal + 0x10);
}

// How much wider than the game's own the projection of the view being drawn is (us.toml's hook in render_draw_race):
// one player's main view and two players' one above the other, as much as their views are wider; the others, the game's.
extern "C" float eb_view_scale(uint8_t* rdram) {
    const int32_t views = MEM_W(0, num_views);
    if (views == 1 || (views == 2 && MEM_W(0, inset_view_active) != 0 && MEM_W(0, cur_view) == 0)) {
        return one_player;
    }
    if (views == 2 && split_horizontal(rdram)) {
        return two_players;
    }
    return 1.0f;
}

// How much wider than the game's own the race's culling is (above): with Original or in a window no wider than 4:3,
// 512/472 with one screen, the view spanning the border's 40 pixels too, and 512/400 with two one above the other; with
// Expand that times how much wider than 4:3 the window is. The views the game keeps (side by side, three or four
// players) keep its culling.
extern "C" float eb_cull_scale(uint8_t* rdram) {
    const int32_t screens = MEM_W(0, num_screens);
    float scale;
    if (screens == 1) {
        scale = one_player;
    }
    else if (screens == 2 && split_horizontal(rdram)) {
        scale = two_players;
    }
    else {
        return 1.0f;
    }
    return scale * eb::window_wide();
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
