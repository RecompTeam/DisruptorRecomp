#ifndef DISRUPTOR_GAME_USER_SETTINGS_H
#define DISRUPTOR_GAME_USER_SETTINGS_H

#include <string>
#include <vector>

namespace PSXRecompV4 {

/// The game's fields of the [disruptor] section of settings.toml, which the framework's UserSettings derives from.
struct GameUserSettings {
    bool has_mouse_aim = false; bool mouse_aim = false;
    bool has_modern_controls = false; bool modern_controls = false;
    bool has_horizontal_sensitivity = false; double horizontal_sensitivity = 0.1;
    bool has_invert_horizontal = false; bool invert_horizontal = false;
    bool has_vertical_look = false; bool vertical_look = false;
    bool has_vertical_sensitivity = false; double vertical_sensitivity = 0.08;
    bool has_invert_vertical = false; bool invert_vertical = false;
    bool has_high_precision_camera = false; bool high_precision_camera = false;
    bool has_frame_unlock = false; bool frame_unlock = false;
    bool has_skip_intro = false; bool skip_intro = false;
    bool has_language_disc = false; std::string language_disc;  // image of another region's disc, "" for none
    bool has_language_discs = false; std::vector<std::string> language_discs;  // the ones named before, for a launcher to offer again
    bool has_hud_scale = false; int hud_scale = 100;
    bool has_improved_shadows = false; bool improved_shadows = false;
    bool has_ambient_occlusion = false; bool ambient_occlusion = false;
    bool has_occlusion_reach = false; int occlusion_reach = 160;       // camera depth units, 50..800
    bool has_occlusion_strength = false; int occlusion_strength = 80;  // percent, 0..300
    bool has_gap_filling = false; bool gap_filling = true;
    bool has_depth_sorted_world = false; bool depth_sorted_world = false;
    // An index into kDisruptorDiagnosticViews. A session's own: at start only a capsule's replay takes it from the file.
    bool has_diagnostic_view = false; int diagnostic_view = 0;
};
inline constexpr const char* kDisruptorDiagnosticViews[] = {"off", "shading", "depth", "normals", "fade", "mesh"};
inline constexpr int kDisruptorDiagnosticViewCount = 6;

}  // namespace PSXRecompV4

#endif
