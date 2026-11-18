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
}

#endif
