// The settings: RecompFrontend's General, Controls, Graphics, Sound and Mods tabs, with its default bindings. Its own
// Graphics options carry what a recomp needs: Resolution and MS Anti-Aliasing (high resolution), Framerate
// (interpolation to the display's rate) and Aspect Ratio and HUD Placement (widescreen). The General tab adds the
// game's own: Draw Distance (eb::draw_far, src/game/draw_distance.cpp). Its default bindings change in one way: the
// arrow keys steer and move through the menus too.

#include <cstdint>
#include <filesystem>
#include <variant>

#include "recompinput/recompinput.h"
#include "recompui/config.h"
#include "util/file.h"
#include "eb.h"

namespace {
    constexpr const char* draw_distance_id = "draw_distance";

    enum class DrawDistance : uint32_t {
        Original,
        Far,
    };
}

// The stock defaults put the C buttons on the arrow keys and the stick on WASD alone, and the game's menus read the stick
// (a few the D-pad too), so the arrows moved through none of them. Here the arrows are a second stick, steering and the
// menus like WASD, and the C buttons (the camera, C-down held for tricks) move to a T/F/G/H diamond. Only defaults: saved
// bindings still apply.
static void set_default_bindings() {
    using recompinput::GameInput;
    using recompinput::InputField;
    recompinput::set_default_mapping_for_keyboard(GameInput::X_AXIS_NEG,
        { InputField::keyboard(SDL_SCANCODE_A), InputField::keyboard(SDL_SCANCODE_LEFT) });
    recompinput::set_default_mapping_for_keyboard(GameInput::X_AXIS_POS,
        { InputField::keyboard(SDL_SCANCODE_D), InputField::keyboard(SDL_SCANCODE_RIGHT) });
    recompinput::set_default_mapping_for_keyboard(GameInput::Y_AXIS_POS,
        { InputField::keyboard(SDL_SCANCODE_W), InputField::keyboard(SDL_SCANCODE_UP) });
    recompinput::set_default_mapping_for_keyboard(GameInput::Y_AXIS_NEG,
        { InputField::keyboard(SDL_SCANCODE_S), InputField::keyboard(SDL_SCANCODE_DOWN) });
    recompinput::set_default_mapping_for_keyboard(GameInput::C_UP, { InputField::keyboard(SDL_SCANCODE_T) });
    recompinput::set_default_mapping_for_keyboard(GameInput::C_LEFT, { InputField::keyboard(SDL_SCANCODE_F) });
    recompinput::set_default_mapping_for_keyboard(GameInput::C_DOWN, { InputField::keyboard(SDL_SCANCODE_G) });
    recompinput::set_default_mapping_for_keyboard(GameInput::C_RIGHT, { InputField::keyboard(SDL_SCANCODE_H) });
}

bool eb::draw_far() {
    auto value = recompui::config::get_general_config().get_option_value(draw_distance_id);
    return std::get<uint32_t>(value) == static_cast<uint32_t>(DrawDistance::Far);
}

void eb::init_config() {
    set_default_bindings();
    std::filesystem::path app_dir = recompui::file::get_app_folder_path();
    if (!app_dir.empty()) {
        std::filesystem::create_directories(app_dir);
    }

    recompui::config::GeneralTabOptions general_options{};
    general_options.has_rumble_strength = true;
    general_options.has_gyro_sensitivity = true;
    general_options.has_mouse_sensitivity = true;
    recomp::config::Config& general = recompui::config::create_general_tab(general_options);
    general.add_enum_option(
        draw_distance_id,
        "Draw Distance",
        "How far across a stadium the track is drawn. <recomp-color primary>Far</recomp-color>: all of it in sight, lit "
        "as the near track is. <recomp-color primary>Original</recomp-color>: as a console draws it, the jumps and walls "
        "past a distance in the stadium's black fog, dark shapes against the stands, and the farthest left out.",
        {{DrawDistance::Original, "Original"}, {DrawDistance::Far, "Far"}},
        DrawDistance::Far
    );
    recompui::config::create_controls_tab();
    recompui::config::create_graphics_tab();
    recompui::config::create_sound_tab();
    recompui::config::create_mods_tab();
    recompui::config::finalize();
}
