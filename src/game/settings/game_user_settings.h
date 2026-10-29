#ifndef DISRUPTOR_GAME_USER_SETTINGS_H
#define DISRUPTOR_GAME_USER_SETTINGS_H

#include <string>
#include <vector>

namespace PSXRecompV4 {

/// The game's fields of the [disruptor] section of settings.toml, which the framework's UserSettings derives from.
struct GameUserSettings {
    bool has_mouse_aim = false; bool mouse_aim = false;
    bool has_modern_controls = false; bool modern_controls = false;
    bool has_selection_wheels = false; bool selection_wheels = false;
    bool has_number_keys = false; bool number_keys = false;
    bool has_psionic_keys_use = false; bool psionic_keys_use = false;
    // Two keys a weapon and a psionic power as written, pick_keys.h reads them. Empty without the file's lists.
    bool has_pick_keys = false;
    std::vector<std::string> weapon_keys, weapon_keys_second, psionic_keys, psionic_keys_second;
    // Every keybinds.ini field's second key as written, second_keys.h reads it. Without it the scheme's own.
    bool has_key_second = false; std::vector<std::string> key_second;
    bool has_horizontal_sensitivity = false; double horizontal_sensitivity = 0.1;
    bool has_invert_horizontal = false; bool invert_horizontal = false;
    bool has_vertical_look = false; bool vertical_look = false;
    bool has_vertical_sensitivity = false; double vertical_sensitivity = 0.08;
    bool has_invert_vertical = false; bool invert_vertical = false;
    bool has_separate_vertical_speed = false; bool separate_vertical_speed = false;
    bool has_stick_layout = false; int stick_layout = 1;     // an index into kDisruptorStickLayouts
    bool has_look_speed = false; int look_speed = 100;       // percent of the D-pad's turn at a full push
    bool has_look_deadzone = false; int look_deadzone = 20;  // percent of the stick's length
    bool has_invert_look = false; bool invert_look = false;
    bool has_high_precision_camera = false; bool high_precision_camera = false;
    bool has_frame_unlock = false; bool frame_unlock = false;
    bool has_skip_intro = false; bool skip_intro = false;
    bool has_fast_disc_reads = false; bool fast_disc_reads = false;
    bool has_mute_in_background = false; bool mute_in_background = false;
    bool has_achievements = false; bool achievements = true;  // RetroAchievements with the launcher's account
    bool has_achievements_encore = false; bool achievements_encore = false;
    bool has_achievements_unofficial = false; bool achievements_unofficial = true;
    bool has_achievements_popups = false; bool achievements_popups = true;
    bool has_achievements_popup_time = false; int achievements_popup_time = 3;  // an index into kDisruptorPopupTimes
    bool has_achievements_corner = false; int achievements_corner = 0;  // an index into kDisruptorPopupCorners
    bool has_achievements_progress = false; bool achievements_progress = false;
    bool has_achievements_sound = false; bool achievements_sound = true;
    bool has_bass_cut = false; int bass_cut = 0;  // an index into kDisruptorBassCuts
    bool has_language_disc = false; std::string language_disc;  // image of another region's disc, "" for none
    bool has_language_discs = false; std::vector<std::string> language_discs;  // the ones named before, for a launcher to offer again
    bool has_hud_scale = false; int hud_scale = 100;
    bool has_ambient_occlusion = false; bool ambient_occlusion = false;
    bool has_occlusion_reach = false; int occlusion_reach = 160;       // camera depth units, 50..160
    bool has_occlusion_strength = false; int occlusion_strength = 100; // percent, 0..100
    bool has_smooth_fade = false; bool smooth_fade = false;
    bool has_dynamic_resolution = false; bool dynamic_resolution = false;
    bool has_render_thread = false; bool render_thread = false;
    bool has_resolution = false; std::string resolution;  // "1920x1080", "" the desktop's
    bool has_multisampling = false; int multisampling = 0;  // samples a pixel, one of kDisruptorSampleCounts
    bool has_sample_shading = false; bool sample_shading = false;
    bool has_downscale_averaging = false; bool downscale_averaging = false;
    bool has_edge_smoothing = false; bool edge_smoothing = false;
    bool has_gap_filling = false; bool gap_filling = true;
    bool has_depth_sorted_world = false; bool depth_sorted_world = false;
    bool has_facing_sprites = false; bool facing_sprites = false;
    bool has_sprite_depth_read = false; bool sprite_depth_read = true;  // no menu entry: a way out on a driver that misreads
    bool has_movie_filter = false; int movie_filter = 1;  // an index into kDisruptorMovieFilters
    bool has_detailed_logging = false; bool detailed_logging = false;
    bool has_draw_distance = false; int draw_distance = 100;  // percent of the game's own, one of kDisruptorDrawDistances
    bool has_fog_shape = false; int fog_shape = 0;  // an index into kDisruptorFogShapes
    // The Experimental tab's lighting, amounts in percent. The sun is the second mission's, the one open level with one.
    bool has_probe_lighting = false; bool probe_lighting = false;
    bool has_probe_strength = false; int probe_strength = 100;
    bool has_probe_bounce = false; int probe_bounce = 0;
    bool has_probe_glow = false; int probe_glow = 200;
    bool has_dynamic_shadows = false; bool dynamic_shadows = false;
    bool has_shadow_strength = false; int shadow_strength = 100;
    bool has_shadow_underneath = false; int shadow_underneath = 100;
    bool has_shot_lights = false; bool shot_lights = false;
    bool has_shot_light_strength = false; int shot_light_strength = 150;
    bool has_sun_lighting = false; bool sun_lighting = false;
    bool has_sun_direction = false; int sun_direction = 35;   // degrees round from +z
    bool has_sun_height = false; int sun_height = 50;         // degrees up, 1..89
    bool has_sun_strength = false; int sun_strength = 110;
    bool has_sun_shade = false; int sun_shade = 50;
    // An index into kDisruptorDiagnosticViews. A session's own: at start only a capsule's replay takes it from the file.
    bool has_diagnostic_view = false; int diagnostic_view = 0;
};
inline constexpr const char* kDisruptorDiagnosticViews[] = {"off", "shading", "depth", "normals", "fade", "mesh", "uv"};
inline constexpr int kDisruptorDiagnosticViewCount = 7;
inline constexpr int kDisruptorSampleCounts[] = {0, 2, 4, 8};
inline constexpr int kDisruptorDrawDistances[] = {100, 125, 150, 200};
inline constexpr const char* kDisruptorFogShapes[] = {"even", "soft", "late"};
inline constexpr int kDisruptorFogShapeCount = 3;
inline constexpr const char* kDisruptorBassCuts[] = {"original", "reduced", "tv_speaker"};
inline constexpr int kDisruptorBassCutCount = 3;
inline constexpr int kDisruptorPopupTimes[] = {3, 5, 8, 12, 15, 30};  // seconds
inline constexpr int kDisruptorPopupTimeCount = 6;
inline constexpr const char* kDisruptorPopupCorners[] = {"top_right", "top_left", "bottom_right", "bottom_left"};
inline constexpr int kDisruptorPopupCornerCount = 4;
inline constexpr const char* kDisruptorMovieFilters[] = {"original", "smooth"};
inline constexpr int kDisruptorMovieFilterCount = 2;
inline constexpr const char* kDisruptorStickLayouts[] = {"classic", "dual_analog", "southpaw"};
inline constexpr int kDisruptorStickLayoutCount = 3;
inline constexpr int kDisruptorLookSpeedLowest = 25, kDisruptorLookSpeedHighest = 300;
inline constexpr int kDisruptorLookDeadzoneHighest = 50;

}  // namespace PSXRecompV4

#endif
