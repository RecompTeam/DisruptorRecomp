#include "config_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <limits>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

// Defined as the framework's reader defines it: toml11's inline code must be one in a program.
#define TOML11_USE_UNRELEASED_TOML_FEATURES
#include "toml.hpp"

namespace PSXRecompV4 {

void read_game_user_settings(const toml::value& doc, UserSettings& s) {
    auto try_get = [](auto&& fn) { try { fn(); } catch (const std::exception&) {} };
    if (doc.contains("disruptor")) {
        const toml::value& d = toml::find(doc, "disruptor");
        if (d.contains("mouse_aim")) try_get([&]{
            s.mouse_aim = toml::find<bool>(d, "mouse_aim");
            s.has_mouse_aim = true;
        });
        if (d.contains("modern_controls")) try_get([&]{
            s.modern_controls = toml::find<bool>(d, "modern_controls");
            s.has_modern_controls = true;
        });
        if (d.contains("selection_wheels")) try_get([&]{
            s.selection_wheels = toml::find<bool>(d, "selection_wheels");
            s.has_selection_wheels = true;
        });
        if (d.contains("number_keys")) try_get([&]{
            s.number_keys = toml::find<bool>(d, "number_keys");
            s.has_number_keys = true;
        });
        if (d.contains("psionic_keys_use")) try_get([&]{
            s.psionic_keys_use = toml::find<bool>(d, "psionic_keys_use");
            s.has_psionic_keys_use = true;
        });
        for (const auto& entry : {std::pair{"weapon_keys", &s.weapon_keys}, std::pair{"weapon_keys_second", &s.weapon_keys_second},
                                  std::pair{"psionic_keys", &s.psionic_keys}, std::pair{"psionic_keys_second", &s.psionic_keys_second}})
            if (d.contains(entry.first)) try_get([&]{
                *entry.second = toml::find<std::vector<std::string>>(d, entry.first);
                s.has_pick_keys = true;
            });
        if (d.contains("key_second")) try_get([&]{
            s.key_second = toml::find<std::vector<std::string>>(d, "key_second");
            s.has_key_second = true;
        });
        if (d.contains("horizontal_sensitivity")) try_get([&]{
            double n = 0.0;
            try {
                n = toml::find<double>(d, "horizontal_sensitivity");
            } catch (const std::exception&) {
                n = static_cast<double>(
                    toml::find<int64_t>(d, "horizontal_sensitivity"));
            }
            if (std::isfinite(n) && n >= 0.005 && n <= 2.0) {
                s.horizontal_sensitivity = n;
                s.has_horizontal_sensitivity = true;
            }
        });
        if (d.contains("invert_horizontal")) try_get([&]{
            s.invert_horizontal = toml::find<bool>(d, "invert_horizontal");
            s.has_invert_horizontal = true;
        });
        if (d.contains("vertical_look")) try_get([&]{
            s.vertical_look = toml::find<bool>(d, "vertical_look");
            s.has_vertical_look = true;
        });
        if (d.contains("vertical_sensitivity")) try_get([&]{
            double n = 0.0;
            try {
                n = toml::find<double>(d, "vertical_sensitivity");
            } catch (const std::exception&) {
                n = static_cast<double>(
                    toml::find<int64_t>(d, "vertical_sensitivity"));
            }
            if (std::isfinite(n) && n >= 0.005 && n <= 2.0) {
                s.vertical_sensitivity = n;
                s.has_vertical_sensitivity = true;
            }
        });
        if (d.contains("invert_vertical")) try_get([&]{
            s.invert_vertical = toml::find<bool>(d, "invert_vertical");
            s.has_invert_vertical = true;
        });
        if (d.contains("separate_vertical_speed")) try_get([&]{
            s.separate_vertical_speed = toml::find<bool>(d, "separate_vertical_speed");
            s.has_separate_vertical_speed = true;
        });
        if (d.contains("high_precision_camera")) try_get([&]{
            s.high_precision_camera = toml::find<bool>(d, "high_precision_camera");
            s.has_high_precision_camera = true;
        });
        if (d.contains("geometry_correction")) try_get([&]{
            s.geometry_correction = toml::find<bool>(d, "geometry_correction");
            s.has_geometry_correction = true;
        });
        if (d.contains("perspective_textures")) try_get([&]{
            s.perspective_textures = toml::find<bool>(d, "perspective_textures");
            s.has_perspective_textures = true;
        });
        if (d.contains("frame_unlock")) try_get([&]{
            s.frame_unlock = toml::find<bool>(d, "frame_unlock");
            s.has_frame_unlock = true;
        });
        if (d.contains("skip_intro")) try_get([&]{
            s.skip_intro = toml::find<bool>(d, "skip_intro");
            s.has_skip_intro = true;
        });
        if (d.contains("mute_in_background")) try_get([&]{
            s.mute_in_background = toml::find<bool>(d, "mute_in_background");
            s.has_mute_in_background = true;
        });
        if (d.contains("achievements")) try_get([&]{
            s.achievements = toml::find<bool>(d, "achievements");
            s.has_achievements = true;
        });
        if (d.contains("achievements_encore")) try_get([&]{
            s.achievements_encore = toml::find<bool>(d, "achievements_encore");
            s.has_achievements_encore = true;
        });
        if (d.contains("achievements_unofficial")) try_get([&]{
            s.achievements_unofficial = toml::find<bool>(d, "achievements_unofficial");
            s.has_achievements_unofficial = true;
        });
        if (d.contains("achievements_popups")) try_get([&]{
            s.achievements_popups = toml::find<bool>(d, "achievements_popups");
            s.has_achievements_popups = true;
        });
        if (d.contains("achievements_progress")) try_get([&]{
            s.achievements_progress = toml::find<bool>(d, "achievements_progress");
            s.has_achievements_progress = true;
        });
        if (d.contains("achievements_sound")) try_get([&]{
            s.achievements_sound = toml::find<bool>(d, "achievements_sound");
            s.has_achievements_sound = true;
        });
        if (d.contains("achievements_popup_time")) try_get([&]{
            const int seconds = toml::find<int>(d, "achievements_popup_time");
            for (int index = 0; index < kDisruptorPopupTimeCount; ++index) {
                if (seconds == kDisruptorPopupTimes[index]) {
                    s.achievements_popup_time = index;
                    s.has_achievements_popup_time = true;
                }
            }
        });
        if (d.contains("achievements_corner")) try_get([&]{
            const std::string corner = toml::find<std::string>(d, "achievements_corner");
            for (int index = 0; index < kDisruptorPopupCornerCount; ++index) {
                if (corner == kDisruptorPopupCorners[index]) {
                    s.achievements_corner = index;
                    s.has_achievements_corner = true;
                }
            }
        });
        if (d.contains("stick_layout")) try_get([&]{
            const std::string layout = toml::find<std::string>(d, "stick_layout");
            for (int index = 0; index < kDisruptorStickLayoutCount; ++index) {
                if (layout == kDisruptorStickLayouts[index]) {
                    s.stick_layout = index;
                    s.has_stick_layout = true;
                }
            }
        });
        if (d.contains("look_speed")) try_get([&]{
            const int percent = toml::find<int>(d, "look_speed");
            if (percent >= kDisruptorLookSpeedLowest && percent <= kDisruptorLookSpeedHighest) {
                s.look_speed = percent;
                s.has_look_speed = true;
            }
        });
        if (d.contains("look_deadzone")) try_get([&]{
            const int percent = toml::find<int>(d, "look_deadzone");
            if (percent >= 0 && percent <= kDisruptorLookDeadzoneHighest) {
                s.look_deadzone = percent;
                s.has_look_deadzone = true;
            }
        });
        if (d.contains("invert_look")) try_get([&]{
            s.invert_look = toml::find<bool>(d, "invert_look");
            s.has_invert_look = true;
        });
        if (d.contains("bass_cut")) try_get([&]{
            const std::string cut = toml::find<std::string>(d, "bass_cut");
            for (int index = 0; index < kDisruptorBassCutCount; ++index) {
                if (cut == kDisruptorBassCuts[index]) {
                    s.bass_cut = index;
                    s.has_bass_cut = true;
                }
            }
        });
        if (d.contains("language_disc")) try_get([&]{
            s.language_disc = toml::find<std::string>(d, "language_disc");
            s.has_language_disc = true;
        });
        if (d.contains("language_discs")) try_get([&]{
            s.language_discs = toml::find<std::vector<std::string>>(d, "language_discs");
            s.has_language_discs = true;
        });
        if (d.contains("hud_scale")) try_get([&]{
            const int percent = toml::find<int>(d, "hud_scale");
            if (percent >= 50 && percent <= 100) {
                s.hud_scale = percent;
                s.has_hud_scale = true;
            }
        });
        if (d.contains("ambient_occlusion")) try_get([&]{
            s.ambient_occlusion = toml::find<bool>(d, "ambient_occlusion");
            s.has_ambient_occlusion = true;
        });
        if (d.contains("smooth_fade")) try_get([&]{
            s.smooth_fade = toml::find<bool>(d, "smooth_fade");
            s.has_smooth_fade = true;
        });
        if (d.contains("multisampling")) try_get([&]{
            const int samples = toml::find<int>(d, "multisampling");
            if (std::ranges::find(kDisruptorSampleCounts, samples) != std::end(kDisruptorSampleCounts)) {
                s.multisampling = samples;
                s.has_multisampling = true;
            }
        });
        if (d.contains("sample_shading")) try_get([&]{
            s.sample_shading = toml::find<bool>(d, "sample_shading");
            s.has_sample_shading = true;
        });
        if (d.contains("downscale_averaging")) try_get([&]{
            s.downscale_averaging = toml::find<bool>(d, "downscale_averaging");
            s.has_downscale_averaging = true;
        });
        if (d.contains("edge_smoothing")) try_get([&]{
            s.edge_smoothing = toml::find<bool>(d, "edge_smoothing");
            s.has_edge_smoothing = true;
        });
        if (d.contains("detailed_logging")) try_get([&]{
            s.detailed_logging = toml::find<bool>(d, "detailed_logging");
            s.has_detailed_logging = true;
        });
        if (d.contains("occlusion_reach")) try_get([&]{
            const int reach = toml::find<int>(d, "occlusion_reach");
            if (reach >= 50 && reach <= 160) {
                s.occlusion_reach = reach;
                s.has_occlusion_reach = true;
            }
        });
        if (d.contains("occlusion_strength")) try_get([&]{
            const int percent = toml::find<int>(d, "occlusion_strength");
            if (percent >= 0 && percent <= 100) {
                s.occlusion_strength = percent;
                s.has_occlusion_strength = true;
            }
        });
        if (d.contains("gap_filling")) try_get([&]{
            s.gap_filling = toml::find<bool>(d, "gap_filling");
            s.has_gap_filling = true;
        });
        if (d.contains("depth_sorted_world")) try_get([&]{
            s.depth_sorted_world = toml::find<bool>(d, "depth_sorted_world");
            s.has_depth_sorted_world = true;
        });
        if (d.contains("probe_lighting")) try_get([&]{
            s.probe_lighting = toml::find<bool>(d, "probe_lighting");
            s.has_probe_lighting = true;
        });
        if (d.contains("probe_strength")) try_get([&]{
            const int amount = toml::find<int>(d, "probe_strength");
            if (amount >= 0 && amount <= 200) {
                s.probe_strength = amount;
                s.has_probe_strength = true;
            }
        });
        if (d.contains("probe_bounce")) try_get([&]{
            const int amount = toml::find<int>(d, "probe_bounce");
            if (amount >= 0 && amount <= 100) {
                s.probe_bounce = amount;
                s.has_probe_bounce = true;
            }
        });
        if (d.contains("probe_glow")) try_get([&]{
            const int amount = toml::find<int>(d, "probe_glow");
            if (amount >= 0 && amount <= 400) {
                s.probe_glow = amount;
                s.has_probe_glow = true;
            }
        });
        if (d.contains("dynamic_shadows")) try_get([&]{
            s.dynamic_shadows = toml::find<bool>(d, "dynamic_shadows");
            s.has_dynamic_shadows = true;
        });
        if (d.contains("shadow_strength")) try_get([&]{
            const int amount = toml::find<int>(d, "shadow_strength");
            if (amount >= 0 && amount <= 200) {
                s.shadow_strength = amount;
                s.has_shadow_strength = true;
            }
        });
        if (d.contains("shadow_underneath")) try_get([&]{
            const int amount = toml::find<int>(d, "shadow_underneath");
            if (amount >= 0 && amount <= 200) {
                s.shadow_underneath = amount;
                s.has_shadow_underneath = true;
            }
        });
        if (d.contains("shot_lights")) try_get([&]{
            s.shot_lights = toml::find<bool>(d, "shot_lights");
            s.has_shot_lights = true;
        });
        if (d.contains("shot_light_strength")) try_get([&]{
            const int amount = toml::find<int>(d, "shot_light_strength");
            if (amount >= 0 && amount <= 400) {
                s.shot_light_strength = amount;
                s.has_shot_light_strength = true;
            }
        });
        if (d.contains("sun_lighting")) try_get([&]{
            s.sun_lighting = toml::find<bool>(d, "sun_lighting");
            s.has_sun_lighting = true;
        });
        if (d.contains("sun_direction")) try_get([&]{
            const int amount = toml::find<int>(d, "sun_direction");
            if (amount >= 0 && amount <= 360) {
                s.sun_direction = amount;
                s.has_sun_direction = true;
            }
        });
        if (d.contains("sun_height")) try_get([&]{
            const int amount = toml::find<int>(d, "sun_height");
            if (amount >= 1 && amount <= 89) {
                s.sun_height = amount;
                s.has_sun_height = true;
            }
        });
        if (d.contains("sun_strength")) try_get([&]{
            const int amount = toml::find<int>(d, "sun_strength");
            if (amount >= 1 && amount <= 400) {
                s.sun_strength = amount;
                s.has_sun_strength = true;
            }
        });
        if (d.contains("sun_shade")) try_get([&]{
            const int amount = toml::find<int>(d, "sun_shade");
            if (amount >= 0 && amount <= 90) {
                s.sun_shade = amount;
                s.has_sun_shade = true;
            }
        });
        /* The switch movie_filter replaced: off keeps the movies as they are, on takes the filter's default. */
        if (d.contains("smooth_movies") && !d.contains("movie_filter")) try_get([&]{
            if (!toml::find<bool>(d, "smooth_movies")) {
                s.movie_filter = 0;
                s.has_movie_filter = true;
            }
        });
        if (d.contains("movie_filter")) try_get([&]{
            const std::string filter = toml::find<std::string>(d, "movie_filter");
            for (int index = 0; index < kDisruptorMovieFilterCount; ++index) {
                if (filter == kDisruptorMovieFilters[index]) {
                    s.movie_filter = index;
                    s.has_movie_filter = true;
                }
            }
        });
        if (d.contains("draw_distance")) try_get([&]{
            int percent = toml::find<int>(d, "draw_distance");
            /* 250 and 300 were choices once: a file naming them draws at the longest kept. */
            if (percent == 250 || percent == 300) percent = kDisruptorDrawDistances[std::size(kDisruptorDrawDistances) - 1];
            if (std::ranges::find(kDisruptorDrawDistances, percent) != std::end(kDisruptorDrawDistances)) {
                s.draw_distance = percent;
                s.has_draw_distance = true;
            }
        });
        if (d.contains("fog_shape")) try_get([&]{
            const std::string shape = toml::find<std::string>(d, "fog_shape");
            for (int index = 0; index < kDisruptorFogShapeCount; ++index) {
                if (shape == kDisruptorFogShapes[index]) {
                    s.fog_shape = index;
                    s.has_fog_shape = true;
                }
            }
        });
        if (d.contains("diagnostic_view")) try_get([&]{
            const std::string view = toml::find<std::string>(d, "diagnostic_view");
            for (int index = 0; index < kDisruptorDiagnosticViewCount; ++index) {
                if (view == kDisruptorDiagnosticViews[index]) {
                    s.diagnostic_view = index;
                    s.has_diagnostic_view = true;
                }
            }
        });
    }
}

void write_game_user_settings(std::ostream& f, const UserSettings& s) {
    auto quoted = [](const std::string& text) {
        std::string out = "\"";
        for (const char letter : text) {
            const unsigned code = static_cast<unsigned char>(letter);
            char escape[8];
            if (letter == '\\' || letter == '"') std::snprintf(escape, sizeof(escape), "\\%c", letter);
            else if (code < 0x20 || code == 0x7F) std::snprintf(escape, sizeof(escape), "\\u%04X", code);
            else std::snprintf(escape, sizeof(escape), "%c", letter);
            out += escape;
        }
        return out + "\"";
    };

    if (s.has_mouse_aim || s.has_modern_controls || s.has_selection_wheels || s.has_number_keys || s.has_pick_keys ||
        s.has_key_second || s.has_psionic_keys_use ||
        s.has_horizontal_sensitivity || s.has_invert_horizontal ||
        s.has_vertical_look || s.has_vertical_sensitivity ||
        s.has_invert_vertical || s.has_separate_vertical_speed ||
        s.has_stick_layout || s.has_look_speed || s.has_look_deadzone || s.has_invert_look ||
        s.has_probe_lighting || s.has_probe_strength || s.has_probe_bounce || s.has_probe_glow ||
        s.has_dynamic_shadows || s.has_shadow_strength || s.has_shadow_underneath ||
        s.has_shot_lights || s.has_shot_light_strength || s.has_sun_lighting || s.has_sun_direction ||
        s.has_sun_height || s.has_sun_strength || s.has_sun_shade ||
        s.has_high_precision_camera || s.has_geometry_correction ||
        s.has_perspective_textures || s.has_frame_unlock ||
        s.has_ambient_occlusion || s.has_occlusion_reach || s.has_occlusion_strength || s.has_smooth_fade ||
        s.has_multisampling || s.has_sample_shading || s.has_downscale_averaging || s.has_edge_smoothing ||
        s.has_gap_filling || s.has_depth_sorted_world || s.has_movie_filter || s.has_draw_distance || s.has_fog_shape || s.has_diagnostic_view || s.has_detailed_logging ||
        s.has_language_discs || s.has_mute_in_background || s.has_achievements || s.has_bass_cut ||
        s.has_achievements_encore || s.has_achievements_unofficial || s.has_achievements_popups || s.has_achievements_progress || s.has_achievements_sound || s.has_achievements_popup_time || s.has_achievements_corner ||
        s.has_language_disc || s.has_skip_intro ||
        s.has_hud_scale) {
        f << "\n[disruptor]\n";
        if (s.has_mouse_aim)
            f << "mouse_aim = " << (s.mouse_aim ? "true" : "false") << "\n";
        if (s.has_modern_controls)
            f << "modern_controls = " << (s.modern_controls ? "true" : "false") << "\n";
        if (s.has_selection_wheels)
            f << "selection_wheels = " << (s.selection_wheels ? "true" : "false") << "\n";
        if (s.has_number_keys)
            f << "number_keys = " << (s.number_keys ? "true" : "false") << "\n";
        if (s.has_psionic_keys_use)
            f << "psionic_keys_use = " << (s.psionic_keys_use ? "true" : "false") << "\n";
        if (s.has_pick_keys) {
            for (const auto& [name, list] : {std::pair{"weapon_keys", &s.weapon_keys}, std::pair{"weapon_keys_second", &s.weapon_keys_second},
                                             std::pair{"psionic_keys", &s.psionic_keys}, std::pair{"psionic_keys_second", &s.psionic_keys_second}}) {
                f << name << " = [";
                for (size_t index = 0; index < list->size(); ++index) f << (index ? ", " : "") << quoted((*list)[index]);
                f << "]\n";
            }
        }
        if (s.has_key_second) {
            f << "key_second = [";
            for (size_t index = 0; index < s.key_second.size(); ++index) f << (index ? ", " : "") << quoted(s.key_second[index]);
            f << "]\n";
        }
        if (s.has_horizontal_sensitivity &&
            std::isfinite(s.horizontal_sensitivity) &&
            s.horizontal_sensitivity >= 0.005 &&
            s.horizontal_sensitivity <= 2.0) {
            f << "horizontal_sensitivity = "
              << std::setprecision(std::numeric_limits<double>::max_digits10)
              << s.horizontal_sensitivity << "\n";
        }
        if (s.has_invert_horizontal)
            f << "invert_horizontal = " << (s.invert_horizontal ? "true" : "false") << "\n";
        if (s.has_vertical_look)
            f << "vertical_look = " << (s.vertical_look ? "true" : "false") << "\n";
        if (s.has_vertical_sensitivity &&
            std::isfinite(s.vertical_sensitivity) &&
            s.vertical_sensitivity >= 0.005 &&
            s.vertical_sensitivity <= 2.0) {
            f << "vertical_sensitivity = "
              << std::setprecision(std::numeric_limits<double>::max_digits10)
              << s.vertical_sensitivity << "\n";
        }
        if (s.has_invert_vertical)
            f << "invert_vertical = " << (s.invert_vertical ? "true" : "false") << "\n";
        if (s.has_separate_vertical_speed)
            f << "separate_vertical_speed = " << (s.separate_vertical_speed ? "true" : "false") << "\n";
        if (s.has_stick_layout && s.stick_layout >= 0 && s.stick_layout < kDisruptorStickLayoutCount)
            f << "stick_layout = " << quoted(kDisruptorStickLayouts[s.stick_layout]) << "\n";
        if (s.has_look_speed && s.look_speed >= kDisruptorLookSpeedLowest && s.look_speed <= kDisruptorLookSpeedHighest)
            f << "look_speed = " << s.look_speed << "\n";
        if (s.has_look_deadzone && s.look_deadzone >= 0 && s.look_deadzone <= kDisruptorLookDeadzoneHighest)
            f << "look_deadzone = " << s.look_deadzone << "\n";
        if (s.has_invert_look)
            f << "invert_look = " << (s.invert_look ? "true" : "false") << "\n";
        if (s.has_high_precision_camera)
            f << "high_precision_camera = " << (s.high_precision_camera ? "true" : "false") << "\n";
        if (s.has_geometry_correction)
            f << "geometry_correction = " << (s.geometry_correction ? "true" : "false") << "\n";
        if (s.has_perspective_textures)
            f << "perspective_textures = " << (s.perspective_textures ? "true" : "false") << "\n";
        if (s.has_frame_unlock)
            f << "frame_unlock = " << (s.frame_unlock ? "true" : "false") << "\n";
        if (s.has_skip_intro)
            f << "skip_intro = " << (s.skip_intro ? "true" : "false") << "\n";
        if (s.has_mute_in_background)
            f << "mute_in_background = " << (s.mute_in_background ? "true" : "false") << "\n";
        if (s.has_achievements)
            f << "achievements = " << (s.achievements ? "true" : "false") << "\n";
        if (s.has_achievements_encore)
            f << "achievements_encore = " << (s.achievements_encore ? "true" : "false") << "\n";
        if (s.has_achievements_unofficial)
            f << "achievements_unofficial = " << (s.achievements_unofficial ? "true" : "false") << "\n";
        if (s.has_achievements_popups)
            f << "achievements_popups = " << (s.achievements_popups ? "true" : "false") << "\n";
        if (s.has_achievements_progress)
            f << "achievements_progress = " << (s.achievements_progress ? "true" : "false") << "\n";
        if (s.has_achievements_sound)
            f << "achievements_sound = " << (s.achievements_sound ? "true" : "false") << "\n";
        if (s.has_achievements_popup_time && s.achievements_popup_time >= 0 &&
            s.achievements_popup_time < kDisruptorPopupTimeCount)
            f << "achievements_popup_time = " << kDisruptorPopupTimes[s.achievements_popup_time] << "\n";
        if (s.has_achievements_corner && s.achievements_corner >= 0 &&
            s.achievements_corner < kDisruptorPopupCornerCount)
            f << "achievements_corner = " << quoted(kDisruptorPopupCorners[s.achievements_corner]) << "\n";
        if (s.has_bass_cut && s.bass_cut >= 0 && s.bass_cut < kDisruptorBassCutCount)
            f << "bass_cut = " << quoted(kDisruptorBassCuts[s.bass_cut]) << "\n";
        if (s.has_language_disc)
            f << "language_disc = " << quoted(s.language_disc) << "\n";
        if (s.has_language_discs) {
            f << "language_discs = [";
            for (size_t index = 0; index < s.language_discs.size(); ++index)
                f << (index ? ", " : "") << quoted(s.language_discs[index]);
            f << "]\n";
        }
        if (s.has_hud_scale && s.hud_scale >= 50 && s.hud_scale <= 100)
            f << "hud_scale = " << s.hud_scale << "\n";
        if (s.has_ambient_occlusion)
            f << "ambient_occlusion = " << (s.ambient_occlusion ? "true" : "false") << "\n";
        if (s.has_occlusion_reach && s.occlusion_reach >= 50 && s.occlusion_reach <= 160)
            f << "occlusion_reach = " << s.occlusion_reach << "\n";
        if (s.has_occlusion_strength && s.occlusion_strength >= 0 && s.occlusion_strength <= 100)
            f << "occlusion_strength = " << s.occlusion_strength << "\n";
        if (s.has_smooth_fade)
            f << "smooth_fade = " << (s.smooth_fade ? "true" : "false") << "\n";
        if (s.has_multisampling)
            f << "multisampling = " << s.multisampling << "\n";
        if (s.has_sample_shading)
            f << "sample_shading = " << (s.sample_shading ? "true" : "false") << "\n";
        if (s.has_downscale_averaging)
            f << "downscale_averaging = " << (s.downscale_averaging ? "true" : "false") << "\n";
        if (s.has_edge_smoothing)
            f << "edge_smoothing = " << (s.edge_smoothing ? "true" : "false") << "\n";
        if (s.has_detailed_logging)
            f << "detailed_logging = " << (s.detailed_logging ? "true" : "false") << "\n";
        if (s.has_gap_filling)
            f << "gap_filling = " << (s.gap_filling ? "true" : "false") << "\n";
        if (s.has_depth_sorted_world)
            f << "depth_sorted_world = " << (s.depth_sorted_world ? "true" : "false") << "\n";
        if (s.has_probe_lighting)
            f << "probe_lighting = " << (s.probe_lighting ? "true" : "false") << "\n";
        if (s.has_probe_strength && s.probe_strength >= 0 && s.probe_strength <= 200)
            f << "probe_strength = " << s.probe_strength << "\n";
        if (s.has_probe_bounce && s.probe_bounce >= 0 && s.probe_bounce <= 100)
            f << "probe_bounce = " << s.probe_bounce << "\n";
        if (s.has_probe_glow && s.probe_glow >= 0 && s.probe_glow <= 400)
            f << "probe_glow = " << s.probe_glow << "\n";
        if (s.has_dynamic_shadows)
            f << "dynamic_shadows = " << (s.dynamic_shadows ? "true" : "false") << "\n";
        if (s.has_shadow_strength && s.shadow_strength >= 0 && s.shadow_strength <= 200)
            f << "shadow_strength = " << s.shadow_strength << "\n";
        if (s.has_shadow_underneath && s.shadow_underneath >= 0 && s.shadow_underneath <= 200)
            f << "shadow_underneath = " << s.shadow_underneath << "\n";
        if (s.has_shot_lights)
            f << "shot_lights = " << (s.shot_lights ? "true" : "false") << "\n";
        if (s.has_shot_light_strength && s.shot_light_strength >= 0 && s.shot_light_strength <= 400)
            f << "shot_light_strength = " << s.shot_light_strength << "\n";
        if (s.has_sun_lighting)
            f << "sun_lighting = " << (s.sun_lighting ? "true" : "false") << "\n";
        if (s.has_sun_direction && s.sun_direction >= 0 && s.sun_direction <= 360)
            f << "sun_direction = " << s.sun_direction << "\n";
        if (s.has_sun_height && s.sun_height >= 1 && s.sun_height <= 89)
            f << "sun_height = " << s.sun_height << "\n";
        if (s.has_sun_strength && s.sun_strength >= 1 && s.sun_strength <= 400)
            f << "sun_strength = " << s.sun_strength << "\n";
        if (s.has_sun_shade && s.sun_shade >= 0 && s.sun_shade <= 90)
            f << "sun_shade = " << s.sun_shade << "\n";
        if (s.has_movie_filter && s.movie_filter >= 0 && s.movie_filter < kDisruptorMovieFilterCount)
            f << "movie_filter = " << quoted(kDisruptorMovieFilters[s.movie_filter]) << "\n";
        if (s.has_draw_distance &&
            std::ranges::find(kDisruptorDrawDistances, s.draw_distance) != std::end(kDisruptorDrawDistances))
            f << "draw_distance = " << s.draw_distance << "\n";
        if (s.has_fog_shape && s.fog_shape >= 0 && s.fog_shape < kDisruptorFogShapeCount)
            f << "fog_shape = " << quoted(kDisruptorFogShapes[s.fog_shape]) << "\n";
        if (s.has_diagnostic_view && s.diagnostic_view >= 0 && s.diagnostic_view < kDisruptorDiagnosticViewCount)
            f << "diagnostic_view = " << quoted(kDisruptorDiagnosticViews[s.diagnostic_view]) << "\n";
    }
}

}  // namespace PSXRecompV4
