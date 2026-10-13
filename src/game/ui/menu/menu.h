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
#include "game/input/selection_wheel.h"
#include "game/frame/present_rate.h"
#include "game/session/restart.h"
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
#include <filesystem>
#include <set>
#include <string>
#include <string_view>

extern "C" void gte_ws_set_far_threshold(int threshold);
extern "C" void gte_ws_set_backdrop_repair_enabled(int enabled);
extern "C" void gl_renderer_set_world_depth(int enabled);
extern "C" void gl_renderer_set_host_ui_suspended(int suspended);
extern "C" int gl_renderer_world_depth(void);
extern "C" void gl_renderer_set_full_yaw(int enabled);
extern "C" void gl_renderer_set_mesh_view(int enabled);
extern "C" int gl_renderer_mesh_view(void);
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
    bool base_keybinds_valid = false;
    bool record_shown = false;  // armed in the menu, started when it closes
    std::string shown_note;
    int selected_wide_aspect = 0;
    std::string cheat_status;
    std::array<SDL_Scancode, PSX_KB_COUNT> base_keybinds{};
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
void apply_modern_keybinds(bool enabled);
void set_modern_controls_live(bool enabled);
void mark(const char *key, int value);
void mark_mouse_aim(bool value);
void mark_modern_controls(bool value);
void mark_sensitivity(double value);
void mark_invert_x(bool value);
void mark_vertical_look(bool value);
void mark_vertical_sensitivity(double value);
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
void mark_shadow_shape(bool improved);
void mark_occlusion(bool value);
void mark_occlusion_shape(int reach, int strength);
void mark_gap_filling(bool value);
void mark_world_depth(bool value);
void mark_detailed_logging(bool value);
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
void draw_enhancements_tab();
void draw_experimental_tab();
void draw_debug_tab();
void draw_cheats_tab();
void draw_system_tab();
void draw_language_tab();

void set_menu_open(bool open);

}  // namespace disruptor::menu

#endif
