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
// sides' 20 pixels show the track, the top and bottom borders stay. Split screen's layouts and the inset view are left
// as the game has them.
//
// And the race culls its world to that view: the stadium's sections and objects to within half as far across as ahead
// of the camera (arn_cull_models), an outdoor track's sections to those with a corner within sCullSlope (0x68/256)
// across of its depth (tube_cull_sections). Both are widened by eb_cull_scale (us.toml's hooks): 512/472 for the view
// across the screen's whole width, times the window's aspect ratio over 4:3 with the Graphics tab's Aspect Ratio Expand,
// where RT64 widens the picture to the window. At 32:9 the stands, barriers and track past the 4:3 frame's sides came
// and went with the camera.

#include <cstdint>

#include "recomp.h"
#include "ultramodern/config.hpp"
#include "recompui/recompui.h"

#include "eb.h"

namespace {
    constexpr gpr layout_1p_hi_res = (gpr)(int32_t)0x800C2DB4;  // sLayout1PHiRes: x, y, width, height
    constexpr gpr layout_1p = (gpr)(int32_t)0x800C2DC4;         // sLayout1P
    constexpr gpr view_layouts = (gpr)(int32_t)0x800C2DD4;      // gViewLayouts[0], the layout in use, as unpacked

    void widen(uint8_t* rdram, gpr layout) {
        MEM_W(0, layout) = 0;           // x
        MEM_W(8, layout) = 512;         // width
    }
}

void eb::widen_views(uint8_t* rdram) {
    widen(rdram, layout_1p_hi_res);
    widen(rdram, layout_1p);
    widen(rdram, view_layouts);
}

// How much wider than the game's own the race's culling is (above): 512/472 with Original or in a window no wider than
// 4:3, the view spanning the border's 40 pixels too, and with Expand that times how much wider than 4:3 the window is.
extern "C" float eb_cull_scale(uint8_t* rdram) {
    float scale = 512.0f / 472.0f;
    if (ultramodern::renderer::get_graphics_config().ar_option == ultramodern::renderer::AspectRatio::Expand) {
        int width = 0;
        int height = 0;
        recompui::get_window_size(width, height);
        if ((width > 0) && (height > 0)) {
            const float wide = (float(width) / float(height)) / (4.0f / 3.0f);
            if (wide > 1.0f) {
                scale *= wide;
            }
        }
    }
    return scale;
}
