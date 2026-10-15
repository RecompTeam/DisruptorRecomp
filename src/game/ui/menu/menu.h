/*
 * Host-side in-game settings menu for Disruptor.
 *
 * This overlay is deliberately outside the emulated PlayStation.  It draws
 * after the completed host presentation. Normal settings never write guest
 * VRAM/RAM; the Cheats tab calls only the narrow, version-pinned Disruptor
 * gameplay APIs whose save consequences are stated in the UI.
 */

#ifndef DISRUPTOR_UI_MENU_H
#define DISRUPTOR_UI_MENU_H

#include "game/session/cheats.h"
#include "game/render/billboard_aspect.h"
#include "game/render/far_rendering.h"
#include "game/frame/frame_rate.h"
#include "game/session/capsule.h"
#include "game/session/diagnostics.h"
#include "game/session/intro_skip.h"
#include "game/input/mouse_aim.h"
#include "game/frame/present_rate.h"
#include "game/session/restart.h"
#include "game/settings/key_layouts.h"
#include "game/settings/pad_layout.h"
#include "game/settings/options.h"
#include "game/ui/hint.h"
#include "config_loader.h"
#include "gpu.h"
#include "host_ui.h"
#include "psx_keybinds.h"
#include "psx_netplay.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#if defined(PSX_SDL3)
#include "imgui_impl_sdl3.h"
#else
#include "imgui_impl_sdl2.h"
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>

extern "C" void gte_ws_set_far_threshold(int threshold);
extern "C" void gte_ws_set_backdrop_repair_enabled(int enabled);
extern "C" void gl_renderer_set_world_depth(int enabled);
extern "C" int gl_renderer_world_depth(void);
extern "C" void gl_renderer_set_full_yaw(int enabled);
extern "C" void gl_renderer_set_mesh_view(int enabled);
extern "C" unsigned gl_renderer_picture(const unsigned char *rgb, int width, int height);
extern "C" void gl_renderer_picture_free(unsigned picture);
extern "C" void gl_renderer_set_smooth_fade(int enabled);
extern "C" int gl_renderer_smooth_fade(void);
extern "C" void gl_renderer_set_multisampling(int samples);
extern "C" int gl_renderer_multisampling(void);
extern "C" void gl_renderer_set_sample_shading(int enabled);
extern "C" int gl_renderer_sample_shading(void);
extern "C" int psx_input_deadzone(void);
extern "C" const char* psx_input_pad_name(int slot);
extern "C" const char* psx_input_pad_source(const char* field);
extern "C" void psx_input_set_deadzone(int raw);
extern "C" void gl_renderer_set_downscale_averaging(int enabled);
extern "C" int gl_renderer_downscale_averaging(void);
extern "C" void gl_renderer_set_edge_smoothing(int enabled);
extern "C" int gl_renderer_edge_smoothing(void);
extern "C" int gl_renderer_mesh_view(void);
extern "C" void gl_renderer_set_uv_view(int enabled);
extern "C" int gl_renderer_uv_view(void);
extern "C" int gl_renderer_record_shown(const char *folder, int skip, int count, int width);
extern "C" int gl_renderer_recording_shown(void);
extern "C" void gl_renderer_set_gap_fill(int enabled);
extern "C" int gl_renderer_gap_fill(void);
extern "C" void gl_renderer_set_ssao(int enabled);
extern "C" void gl_renderer_set_ssao_view(int view);
extern "C" int gl_renderer_ssao_view(void);
extern "C" int gl_renderer_ssao(void);
extern "C" void gl_renderer_set_ssao_shape(float radius, float strength);
extern "C" float gl_renderer_ssao_radius(void);
extern "C" float gl_renderer_ssao_strength(void);

namespace disruptor::menu {

struct DevMenuState {
    SDL_Window *window = nullptr;
    int backend = PSX_HOST_UI_BACKEND_NONE;
    bool imgui_ready = false;
    bool open = false;
    bool restore_mouse_capture = false;
    bool aspect_apply_failed = false;
    bool internal_scale_apply_failed = false;
    bool fullscreen_apply_failed = false;
    bool record_shown = false;  // armed in the menu, started when it closes
    std::string shown_note;
    std::vector<disruptor::capsule::Stored> capsules;  // read again once a second while the Debug tab shows them
    double capsules_read = -1.0;
    std::string capsule_asked, capsule_note;  // the one whose Delete was pressed once, and what came of the second press
    struct CapsuleThumb {
        uint32_t pictures = 0;  // how many the capsule had when its last was loaded, and that one's time
        int64_t revised = 0;
        unsigned picture = 0;
        float tall = 0.5625f;
    };
    std::map<std::string, CapsuleThumb> capsule_thumbs;  // by capsule: its last picture
    std::string capsule_film_of;         // the capsule under the mouse, as it was when its pictures were loaded
    uint32_t capsule_film_pictures = 0;
    int64_t capsule_film_revised = 0;
    std::vector<unsigned> capsule_film;  // its pictures in order, loaded a few a frame
    int selected_wide_aspect = 0;
    std::string cheat_status;
};

extern DevMenuState g_menu;

namespace fs = std::filesystem;
using disruptor::settings::find_option;
using disruptor::settings::Option;
using disruptor::settings::Unit;

struct PreferenceState {
    fs::path path;
    PSXRecompV4::UserSettings pending;
    std::set<const Option *> changed;
    bool language_disc_changed = false;
    bool diagnostic_view_changed = false;
    bool loaded = false;
    bool save_failed = false;
    std::string status;

    bool dirty() const {
        return !changed.empty() || language_disc_changed ||
               diagnostic_view_changed;
    }
    void clean() {
        changed.clear();
        language_disc_changed = diagnostic_view_changed = false;
    }
};

extern PreferenceState g_preferences;
extern std::string g_language_disc;

std::string utf8(const fs::path &path);
void set_modern_controls_live(bool enabled);
void mark(const char *key, int value);
void mark_mouse_aim(bool value);
void mark_modern_controls(bool value);
void mark_sensitivity(double value);
void mark_invert_x(bool value);
void mark_vertical_look(bool value);
void mark_vertical_sensitivity(double value);
void mark_separate_vertical(bool value);
void set_deadzone_live(int percent);
void mark_invert_y(bool value);
void mark_geometry(bool value);
void mark_textures(bool value);
void mark_vsync(int value);
void mark_aspect(int numerator, int denominator, bool adaptive);
void mark_supersampling(int value);
void mark_fullscreen(int value);
void mark_master_volume(int value);
void mark_audio_muted(bool value);
void mark_skip_intro(bool value);
void mark_language_disc(const std::string &path);
void mark_hud_scale(int value);
void mark_frame_unlock(bool value);
void mark_occlusion(bool value);
void mark_smooth_fade(bool value);
void mark_antialiasing(int level);
void mark_detailed_logging(bool value);
void mark_occlusion_shape(int reach, int strength);
void mark_gap_filling(bool value);
void mark_world_depth(bool value);
void apply_diagnostic_view(int view);
void mark_diagnostic_view(int value);
void apply_smooth_turn(bool enabled);
void mark_smooth_turn(bool value);
void mark_present_rate(int value);
void mark_present_enabled(bool value);
bool flush_preferences();
void load_preferences_for_session();

bool applies(const char *key);
bool option_toggle(const char *key, bool *value);
bool option_slider(const char *key, int *value);
bool option_slider(const char *key, float *value);
bool option_list(const char *key, int *index, size_t skipped = 0);
void draw_status_badge(const char *label, const ImVec4 &colour);

void draw_controls_tab();
void draw_display_tab();
void draw_enhancements_tab();
void draw_experimental_tab();
void draw_debug_tab();
void draw_cheats_tab();
void draw_system_tab();
void draw_language_tab();

void set_menu_open(bool open);

}  // namespace disruptor::menu

#endif
