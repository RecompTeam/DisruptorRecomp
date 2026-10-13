#include "config_loader.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iomanip>
#include <limits>
#include <ostream>
#include <string>
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
        if (d.contains("improved_shadows")) try_get([&]{
            s.improved_shadows = toml::find<bool>(d, "improved_shadows");
            s.has_improved_shadows = true;
        });
        if (d.contains("ambient_occlusion")) try_get([&]{
            s.ambient_occlusion = toml::find<bool>(d, "ambient_occlusion");
            s.has_ambient_occlusion = true;
        });
        if (d.contains("occlusion_reach")) try_get([&]{
            const int reach = toml::find<int>(d, "occlusion_reach");
            if (reach >= 50 && reach <= 800) {
                s.occlusion_reach = reach;
                s.has_occlusion_reach = true;
            }
        });
        if (d.contains("occlusion_strength")) try_get([&]{
            const int percent = toml::find<int>(d, "occlusion_strength");
            if (percent >= 0 && percent <= 300) {
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
        if (d.contains("diagnostic_view")) try_get([&]{
            const std::string view = toml::find<std::string>(d, "diagnostic_view");
            for (int index = 0; index < kDisruptorDiagnosticViewCount; ++index) {
                if (view == kDisruptorDiagnosticViews[index]) {
                    s.diagnostic_view = index;
                    s.has_diagnostic_view = true;
                }
            }
        });
        if (d.contains("detailed_logging")) try_get([&]{
            s.detailed_logging = toml::find<bool>(d, "detailed_logging");
            s.has_detailed_logging = true;
        });
    }
}

void write_game_user_settings(std::ostream& f, const UserSettings& s) {
    // A TOML basic string: a quote, a backslash and a control character are escaped.
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

    if (s.has_mouse_aim || s.has_modern_controls ||
        s.has_horizontal_sensitivity || s.has_invert_horizontal ||
        s.has_vertical_look || s.has_vertical_sensitivity ||
        s.has_invert_vertical ||
        s.has_high_precision_camera || s.has_geometry_correction ||
        s.has_perspective_textures || s.has_frame_unlock || s.has_improved_shadows ||
        s.has_ambient_occlusion || s.has_occlusion_reach || s.has_occlusion_strength ||
        s.has_gap_filling || s.has_depth_sorted_world || s.has_diagnostic_view || s.has_detailed_logging ||
        s.has_language_discs ||
        s.has_language_disc || s.has_skip_intro ||
        s.has_hud_scale) {
        f << "\n[disruptor]\n";
        if (s.has_mouse_aim)
            f << "mouse_aim = " << (s.mouse_aim ? "true" : "false") << "\n";
        if (s.has_modern_controls)
            f << "modern_controls = " << (s.modern_controls ? "true" : "false") << "\n";
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
        if (s.has_improved_shadows)
            f << "improved_shadows = " << (s.improved_shadows ? "true" : "false") << "\n";
        if (s.has_ambient_occlusion)
            f << "ambient_occlusion = " << (s.ambient_occlusion ? "true" : "false") << "\n";
        if (s.has_occlusion_reach && s.occlusion_reach >= 50 && s.occlusion_reach <= 800)
            f << "occlusion_reach = " << s.occlusion_reach << "\n";
        if (s.has_occlusion_strength && s.occlusion_strength >= 0 && s.occlusion_strength <= 300)
            f << "occlusion_strength = " << s.occlusion_strength << "\n";
        if (s.has_gap_filling)
            f << "gap_filling = " << (s.gap_filling ? "true" : "false") << "\n";
        if (s.has_depth_sorted_world)
            f << "depth_sorted_world = " << (s.depth_sorted_world ? "true" : "false") << "\n";
        if (s.has_diagnostic_view && s.diagnostic_view >= 0 && s.diagnostic_view < kDisruptorDiagnosticViewCount)
            f << "diagnostic_view = " << quoted(kDisruptorDiagnosticViews[s.diagnostic_view]) << "\n";
        if (s.has_detailed_logging)
            f << "detailed_logging = " << (s.detailed_logging ? "true" : "false") << "\n";
    }
}

}  // namespace PSXRecompV4
