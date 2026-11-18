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

#include <cstdint>

#include "recomp.h"

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
