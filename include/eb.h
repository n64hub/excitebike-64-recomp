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

    // The game image, unpacked into RDRAM as the boot loader does (boot.cpp), before the game's entry runs.
    void unpack_game(uint8_t* rdram);

    // The race's views widened to the screen's width, over the unpacked image (widescreen.cpp).
    void widen_views(uint8_t* rdram);

    // How much wider than 4:3 the window is with the Graphics tab's Aspect Ratio Expand, where RT64 widens the picture
    // to it; 1 with Original or in a window no wider than 4:3 (widescreen.cpp).
    float window_wide();
}

#endif
