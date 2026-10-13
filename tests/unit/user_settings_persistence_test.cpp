#include "config_loader.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;
using PSXRecompV4::UserSettings;

static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

static bool has_temp_sibling(const fs::path& directory) {
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".tmp.") != std::string::npos)
            return true;
    }
    return false;
}

int main() {
    const auto stamp = std::chrono::steady_clock::now()
                           .time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
        ("disruptor-user-settings-test-" + std::to_string(stamp));
    fs::create_directories(root);

    try {
        const fs::path path = root / fs::path(u8"settings-unicode-ü.toml");
        UserSettings missing = PSXRecompV4::load_user_settings(path);
        require(!missing.parse_error && !missing.has_mouse_aim &&
                    !missing.has_vsync && !missing.has_master_volume &&
                    !missing.has_audio_muted,
                "missing settings must fall through without overrides");

        UserSettings written;
        written.has_fullscreen = true;
        written.fullscreen = 2;
        written.has_vsync = true;
        written.vsync = -1;
        written.has_supersampling = true;
        written.supersampling = 3;
        written.has_frame_interpolation_fps = true;
        written.frame_interpolation_fps = 144;
        written.has_frame_interpolation_blend = true;
        written.frame_interpolation_blend = 2;
        written.has_aspect_ratio = true;
        written.aspect_num = 32;
        written.aspect_den = 9;
        written.has_adaptive_view = true;
        written.adaptive_view = false;
        written.has_master_volume = true;
        written.master_volume = 37;
        written.has_audio_muted = true;
        written.audio_muted = true;
        written.has_mouse_aim = true;
        written.mouse_aim = true;
        written.has_modern_controls = true;
        written.modern_controls = true;
        written.has_horizontal_sensitivity = true;
        written.horizontal_sensitivity = 0.125;
        written.has_invert_horizontal = true;
        written.invert_horizontal = true;
        written.has_vertical_look = true;
        written.vertical_look = true;
        written.has_vertical_sensitivity = true;
        written.vertical_sensitivity = 0.2;
        written.has_invert_vertical = true;
        written.invert_vertical = true;
        written.has_high_precision_camera = true;
        written.high_precision_camera = true;
        written.has_geometry_correction = true;
        written.geometry_correction = true;
        written.has_perspective_textures = true;
        written.perspective_textures = true;
        written.has_frame_unlock = true;
        written.frame_unlock = true;
        written.has_skip_intro = true;
        written.skip_intro = true;
        written.has_language_disc = true;
        written.language_disc = "C:\\Games \"old\"\\Disruptor (France).cue";
        written.has_language_discs = true;
        written.language_discs = {"C:\\Games \"old\"\\Disruptor (France).cue",
                                  "D:\\\xD0\x94\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB8\\Disruptor (Germany).cue",
                                  "a line\nbreak, a tab\t and \x7F from a hand edit"};
        written.has_hud_scale = true;
        written.hud_scale = 70;
        written.has_improved_shadows = true;
        written.improved_shadows = true;
        written.has_ambient_occlusion = true;
        written.ambient_occlusion = true;
        written.has_occlusion_reach = true;
        written.occlusion_reach = 240;
        written.has_occlusion_strength = true;
        written.occlusion_strength = 130;
        written.has_smooth_fade = true;
        written.smooth_fade = true;
        written.has_multisampling = true;
        written.multisampling = 8;
        written.has_sample_shading = true;
        written.sample_shading = true;
        written.has_downscale_averaging = true;
        written.downscale_averaging = true;
        written.has_edge_smoothing = true;
        written.edge_smoothing = true;
        written.has_gap_filling = true;
        written.gap_filling = false;
        written.has_depth_sorted_world = true;
        written.depth_sorted_world = true;
        written.has_diagnostic_view = true;
        written.diagnostic_view = 3;
        written.has_language = true;
        written.language = "en";

        require(PSXRecompV4::save_user_settings(path, written),
                "initial atomic save failed");
        require(fs::is_regular_file(path), "settings file was not published");
        require(!has_temp_sibling(root), "successful save leaked a temp file");

        UserSettings loaded = PSXRecompV4::load_user_settings(path);
        require(!loaded.parse_error, "saved settings failed to parse");
        require(loaded.has_fullscreen && loaded.fullscreen == 2,
                "fullscreen mode did not round-trip");
        require(loaded.has_vsync && loaded.vsync == -1,
                "vsync did not round-trip");
        require(loaded.has_supersampling && loaded.supersampling == 3,
                "internal resolution scale did not round-trip");
        require(loaded.has_frame_interpolation_fps &&
                    loaded.frame_interpolation_fps == 144 &&
                    loaded.has_frame_interpolation_blend &&
                    loaded.frame_interpolation_blend == 2,
                "interpolation preferences did not round-trip");
        require(loaded.has_aspect_ratio && loaded.aspect_num == 32 &&
                    loaded.aspect_den == 9 && loaded.has_adaptive_view &&
                    !loaded.adaptive_view,
                "fixed display aspect did not round-trip");
        require(loaded.has_master_volume && loaded.master_volume == 37 &&
                    loaded.has_audio_muted && loaded.audio_muted,
                "host audio controls did not round-trip");
        require(loaded.has_mouse_aim && loaded.mouse_aim &&
                    loaded.has_modern_controls && loaded.modern_controls &&
                    loaded.has_horizontal_sensitivity &&
                    loaded.horizontal_sensitivity == 0.125 &&
                    loaded.has_invert_horizontal && loaded.invert_horizontal &&
                    loaded.has_vertical_look && loaded.vertical_look &&
                    loaded.has_vertical_sensitivity &&
                    loaded.vertical_sensitivity == 0.2 &&
                    loaded.has_invert_vertical && loaded.invert_vertical &&
                    loaded.has_high_precision_camera &&
                    loaded.high_precision_camera &&
                    loaded.has_geometry_correction &&
                    loaded.geometry_correction &&
                    loaded.has_perspective_textures &&
                    loaded.perspective_textures &&
                    loaded.has_frame_unlock && loaded.frame_unlock &&
                    loaded.has_skip_intro && loaded.skip_intro &&
                    loaded.has_language_disc && loaded.language_disc == "C:\\Games \"old\"\\Disruptor (France).cue" &&
                    loaded.has_language_discs && loaded.language_discs == written.language_discs &&
                    loaded.has_hud_scale && loaded.hud_scale == 70 &&
                    loaded.has_improved_shadows && loaded.improved_shadows &&
                    loaded.has_ambient_occlusion && loaded.ambient_occlusion &&
                    loaded.has_occlusion_reach && loaded.occlusion_reach == 240 &&
                    loaded.has_occlusion_strength && loaded.occlusion_strength == 130 &&
                    loaded.has_smooth_fade && loaded.smooth_fade &&
                    loaded.has_multisampling && loaded.multisampling == 8 &&
                    loaded.has_sample_shading && loaded.sample_shading &&
                    loaded.has_downscale_averaging && loaded.downscale_averaging &&
                    loaded.has_edge_smoothing && loaded.edge_smoothing &&
                    loaded.has_gap_filling && !loaded.gap_filling &&
                    loaded.has_depth_sorted_world && loaded.depth_sorted_world &&
                    loaded.has_diagnostic_view && loaded.diagnostic_view == 3,
                "Disruptor preferences did not round-trip");
        require(loaded.has_language && loaded.language == "en",
                "merge-save dropped an unrelated known setting");

        loaded.mouse_aim = false;
        require(PSXRecompV4::save_user_settings(path, loaded),
                "atomic replacement of an existing file failed");
        UserSettings replaced = PSXRecompV4::load_user_settings(path);
        require(replaced.has_mouse_aim && !replaced.mouse_aim &&
                    replaced.has_language && replaced.has_aspect_ratio &&
                    replaced.aspect_num == 32 && replaced.aspect_den == 9 &&
                    replaced.has_supersampling &&
                    replaced.supersampling == 3 &&
                    replaced.has_fullscreen && replaced.fullscreen == 2 &&
                    replaced.has_adaptive_view && !replaced.adaptive_view &&
                    replaced.has_master_volume &&
                    replaced.master_volume == 37 &&
                    replaced.has_audio_muted && replaced.audio_muted,
                "replacement did not preserve the merged settings");
        require(!has_temp_sibling(root), "replacement leaked a temp file");

#ifdef _WIN32
        // Simulate a scanner/reader that temporarily prevents atomic replacement.
        HANDLE reader = ::CreateFileW(path.c_str(), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(reader != INVALID_HANDLE_VALUE, "could not hold settings open");
        std::thread release_reader([reader] {
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
            ::CloseHandle(reader);
        });
        replaced.mouse_aim = true;
        const bool recovered = PSXRecompV4::save_user_settings(path, replaced);
        release_reader.join();
        require(recovered, "save did not recover after a temporary replacement lock");
        require(PSXRecompV4::load_user_settings(path).mouse_aim,
                "recovered save did not publish the new settings");
        require(!has_temp_sibling(root), "recovered save leaked a temp file");

        // A lock that outlasts the retry budget must report failure and retain data.
        reader = ::CreateFileW(path.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require(reader != INVALID_HANDLE_VALUE, "could not hold settings open again");
        replaced.mouse_aim = false;
        const bool blocked_save = PSXRecompV4::save_user_settings(path, replaced);
        ::CloseHandle(reader);
        require(!blocked_save, "save unexpectedly replaced a persistently locked file");
        require(PSXRecompV4::load_user_settings(path).mouse_aim,
                "failed save damaged the previous settings");
        require(!has_temp_sibling(root), "locked save leaked a temp file");
#endif

        const fs::path scale_path = root / "internal-scale.toml";
        for (int scale = 1; scale <= 8; ++scale) {
            UserSettings preference;
            preference.has_supersampling = true;
            preference.supersampling = scale;
            require(PSXRecompV4::save_user_settings(scale_path, preference),
                    "internal scale save failed");
            const UserSettings roundtrip =
                PSXRecompV4::load_user_settings(scale_path);
            require(roundtrip.has_supersampling &&
                        roundtrip.supersampling == scale,
                    "valid 1x-8x internal scale did not round-trip");
        }
        for (int invalid_scale : {0, 9}) {
            {
                std::ofstream out(scale_path, std::ios::trunc);
                out << "[video]\nsupersampling = " << invalid_scale << "\n";
            }
            const UserSettings rejected =
                PSXRecompV4::load_user_settings(scale_path);
            require(!rejected.parse_error && !rejected.has_supersampling,
                    "out-of-range internal scale was accepted on load");
        }

        replaced.has_aspect_ratio = true;
        replaced.aspect_num = 32;
        replaced.aspect_den = 9;
        replaced.has_adaptive_view = true;
        replaced.adaptive_view = true;
        require(PSXRecompV4::save_user_settings(path, replaced),
                "adaptive display aspect save failed");
        UserSettings adaptive = PSXRecompV4::load_user_settings(path);
        require(adaptive.has_aspect_ratio && adaptive.aspect_num == 32 &&
                    adaptive.aspect_den == 9 && adaptive.has_adaptive_view &&
                    adaptive.adaptive_view,
                "adaptive display aspect did not round-trip");

        const fs::path interpolation = root / "interpolation.toml";
        for (const int mode : {0, 1, 2}) {
            UserSettings preference;
            preference.has_frame_interpolation_blend = true;
            preference.frame_interpolation_blend = mode;
            require(PSXRecompV4::save_user_settings(interpolation, preference),
                    "interpolation mode save failed");
            const UserSettings roundtrip =
                PSXRecompV4::load_user_settings(interpolation);
            require(roundtrip.has_frame_interpolation_blend &&
                        roundtrip.frame_interpolation_blend == mode,
                    "valid interpolation mode did not round-trip");
        }
        for (const int mode : {-1, 3, 99}) {
            {
                std::ofstream out(interpolation, std::ios::trunc);
                out << "[video]\nframe_interpolation_blend = " << mode << "\n";
            }
            const UserSettings rejected =
                PSXRecompV4::load_user_settings(interpolation);
            require(!rejected.parse_error &&
                        !rejected.has_frame_interpolation_blend,
                    "invalid interpolation mode was accepted on load");
            UserSettings preference;
            preference.has_frame_interpolation_blend = true;
            preference.frame_interpolation_blend = mode;
            require(PSXRecompV4::save_user_settings(interpolation, preference),
                    "invalid interpolation mode prevented saving other settings");
            require(!PSXRecompV4::load_user_settings(interpolation)
                         .has_frame_interpolation_blend,
                    "invalid interpolation mode was saved");
        }

        for (const int rate : {0, 30, 45, 120, 1000}) {
            UserSettings preference;
            preference.has_frame_interpolation_fps = true;
            preference.frame_interpolation_fps = rate;
            require(PSXRecompV4::save_user_settings(interpolation, preference),
                    "in-between frame rate save failed");
            const UserSettings roundtrip = PSXRecompV4::load_user_settings(interpolation);
            require(roundtrip.has_frame_interpolation_fps && roundtrip.frame_interpolation_fps == rate,
                    "an in-between frame rate from 30 FPS, or 0 for the display, did not round-trip");
        }
        for (const int rate : {-1, 1, 29, 1001}) {
            {
                std::ofstream out(interpolation, std::ios::trunc);
                out << "[video]\nframe_interpolation_fps = " << rate << "\n";
            }
            const UserSettings rejected = PSXRecompV4::load_user_settings(interpolation);
            require(!rejected.parse_error && !rejected.has_frame_interpolation_fps,
                    "an in-between frame rate outside 30 to 1000 FPS was accepted on load");
        }

        const fs::path invalid = root / "invalid.toml";
        {
            std::ofstream out(invalid, std::ios::trunc);
            out << "[disruptor\nmouse_aim = true\n";
        }
        require(PSXRecompV4::load_user_settings(invalid).parse_error,
                "whole-file TOML corruption was not surfaced");

        const fs::path partial = root / "partial.toml";
        {
            std::ofstream out(partial, std::ios::trunc);
            out << "[disruptor]\n"
                   "mouse_aim = false\n"
                   "horizontal_sensitivity = 99.0\n"
                   "vertical_look = true\n"
                   "vertical_sensitivity = -1.0\n"
                   "invert_vertical = true\n"
                   "geometry_correction = false\n"
                   "perspective_textures = true\n"
                   "[audio]\n"
                   "master_volume = 101\n"
                   "muted = \"yes\"\n";
        }
        UserSettings tolerant = PSXRecompV4::load_user_settings(partial);
        require(!tolerant.parse_error && tolerant.has_mouse_aim &&
                    !tolerant.mouse_aim &&
                    !tolerant.has_horizontal_sensitivity &&
                    tolerant.has_vertical_look && tolerant.vertical_look &&
                    !tolerant.has_vertical_sensitivity &&
                    tolerant.has_invert_vertical && tolerant.invert_vertical &&
                    tolerant.has_geometry_correction &&
                    !tolerant.geometry_correction &&
                    tolerant.has_perspective_textures &&
                    tolerant.perspective_textures &&
                    !tolerant.has_master_volume &&
                    !tolerant.has_audio_muted,
                "field validation must be tolerant and dependency-neutral");

        const fs::path blocked = root / "blocked.toml";
        fs::create_directory(blocked);
        {
            std::ofstream marker(blocked / "keep.txt", std::ios::trunc);
            marker << "original";
        }
        require(!PSXRecompV4::save_user_settings(blocked, written),
                "publication over a directory unexpectedly succeeded");
        require(fs::is_directory(blocked) &&
                    fs::is_regular_file(blocked / "keep.txt"),
                "failed publication damaged the original target");
        require(!has_temp_sibling(root), "failed publication leaked a temp file");

        fs::remove_all(root);
        std::cout << "user settings persistence: PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "user settings persistence: FAIL: " << e.what() << "\n";
        std::error_code ignored;
        fs::remove_all(root, ignored);
        return 1;
    }
}
