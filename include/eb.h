#ifndef __EB_H__
#define __EB_H__

#include <cstdint>
#include <string_view>

namespace eb {
    inline constexpr std::u8string_view program_id = u8"Excitebike64Recompiled";
    inline constexpr std::string_view program_name = "Excitebike 64: Recompiled";

    // The game's code section (register_overlays.cpp) and the patches over it (register_patches.cpp).
    void register_overlays();
    void register_patches();

    // The settings (config.cpp): RecompFrontend's tabs.
    void init_config();
    // Whether the General tab's Draw Distance is Far: the stadium drawn to its far side, unfogged (draw_distance.cpp).
    bool draw_far();

    // The game image, unpacked into RDRAM as the boot loader does (boot.cpp), before the game's entry runs.
    void unpack_game(uint8_t* rdram);

    // The race's views widened to the screen's width, over the unpacked image (widescreen.cpp).
    void widen_views(uint8_t* rdram);

    // How much wider than 4:3 the window is with the Graphics tab's Aspect Ratio Expand, where RT64 widens the picture
    // to it; 1 with Original or in a window no wider than 4:3 (widescreen.cpp).
    float window_wide();

    // Whether the race's view `view` is one widened to the screen's width and by RT64 to the window (widescreen.cpp).
    bool view_widened(uint8_t* rdram, int32_t view);

    // The game's own view and the full one, in pixels, of the view whose box the HUD reads is `box` (x, y, width,
    // height in pixels); false for a rectangle that is no view's box (widescreen.cpp).
    bool view_of_box(uint8_t* rdram, const int32_t box[4], int32_t game[4], int32_t full[4]);

    // The finish's checkerboard over the view whose box is `box`, out to the view's edges and the window's sides, as
    // commands for `put`; false, writing nothing, where it can't be placed (checkerboard.cpp).
    bool draw_checkerboard(uint8_t* rdram, const int32_t box[4], void (*put)(void* ctx, uint32_t w0, uint32_t w1),
                           void* ctx);
}

#endif
