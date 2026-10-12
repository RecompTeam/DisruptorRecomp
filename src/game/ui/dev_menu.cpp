/*
 * Host-side in-game settings menu for Disruptor.
 *
 * This overlay is deliberately outside the emulated PlayStation.  It draws
 * after the completed host presentation. Normal settings never write guest
 * VRAM/RAM; the Cheats tab calls only the narrow, version-pinned Disruptor
 * gameplay APIs whose save consequences are stated in the UI.
 */

#include "game/session/cheats.h"
#include "game/render/billboard_aspect.h"
#include "game/render/far_rendering.h"
#include "game/frame/frame_rate.h"
#include "game/session/capsule.h"
#include "game/session/intro_skip.h"
#include "game/input/mouse_aim.h"
#include "game/frame/present_rate.h"
#include "game/session/restart.h"
#include "game/settings/options.h"
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

namespace {

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

DevMenuState g_menu;

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

PreferenceState g_preferences;
std::string g_language_disc;

std::string utf8(const fs::path &path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

bool env_override_present(const char *name) {
    return name && std::getenv(name) != nullptr;
}

static constexpr std::array<SDL_Scancode, PSX_KB_COUNT> kModernKeybinds = {
    SDL_SCANCODE_W, SDL_SCANCODE_S,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_SPACE,
    SDL_SCANCODE_F, SDL_SCANCODE_E,
    SDL_SCANCODE_Q, SDL_SCANCODE_R,
    SDL_SCANCODE_A, SDL_SCANCODE_D,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_P, SDL_SCANCODE_TAB,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
};

static constexpr std::array<SDL_Scancode, PSX_KB_COUNT> kOriginalKeybinds = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_X, SDL_SCANCODE_S,
    SDL_SCANCODE_Z, SDL_SCANCODE_A,
    SDL_SCANCODE_Q, SDL_SCANCODE_W,
    SDL_SCANCODE_E, SDL_SCANCODE_R,
    SDL_SCANCODE_T, SDL_SCANCODE_Y,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_RSHIFT,
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
};

void capture_base_keybinds() {
    if (g_menu.base_keybinds_valid) return;
    bool is_packaged_modern_preset = true;
    for (int i = 0; i < PSX_KB_COUNT; ++i) {
        g_menu.base_keybinds[static_cast<size_t>(i)] =
            psx_keybinds_get_button(1, i);
        if (g_menu.base_keybinds[static_cast<size_t>(i)] !=
            kModernKeybinds[static_cast<size_t>(i)])
            is_packaged_modern_preset = false;
    }
    /* run.ps1/run.sh install the packaged modern map for an explicit launch
     * flag.  Treat that exact map as an overlay, not the restore baseline, so
     * turning Modern controls off live really returns to the packaged classic
     * mapping.  Any custom/non-exact map is preserved verbatim instead. */
    if (is_packaged_modern_preset)
        g_menu.base_keybinds = kOriginalKeybinds;
    g_menu.base_keybinds_valid = true;
}

void apply_modern_keybinds(bool enabled) {
    capture_base_keybinds();
    if (!enabled) {
        for (int i = 0; i < PSX_KB_COUNT; ++i)
            psx_keybinds_set_button(
                1, i, g_menu.base_keybinds[static_cast<size_t>(i)]);
        return;
    }

    for (int i = 0; i < PSX_KB_COUNT; ++i)
        psx_keybinds_set_button(
            1, i, kModernKeybinds[static_cast<size_t>(i)]);
}

void set_modern_controls_live(bool enabled) {
    apply_modern_keybinds(enabled);
    disruptor_modern_controls_set_enabled(enabled ? 1 : 0);
}

void mark(const char *key, int value) {
    const Option *option = find_option(key);
    option->set(g_preferences.pending, value);
    g_preferences.changed.insert(option);
}

bool changed(const char *key) {
    return g_preferences.changed.contains(find_option(key));
}

bool option_toggle(const char *key, bool *value) {
    return ImGui::Checkbox(find_option(key)->label, value);
}

bool option_slider(const char *key, int *value) {
    const Option &option = *find_option(key);
    const char *format = option.unit == Unit::percent ? "%d%%" : "%d";
    return ImGui::SliderInt(option.label, value, option.lowest,
                            option.highest, format,
                            ImGuiSliderFlags_AlwaysClamp);
}

bool option_slider(const char *key, float *value) {
    const Option &option = *find_option(key);
    return ImGui::SliderFloat(option.label, value, option.lowest / 1000.0f,
                              option.highest / 1000.0f, "%.3f",
                              ImGuiSliderFlags_Logarithmic);
}

bool option_list(const char *key, int *index, size_t skipped = 0) {
    const Option &option = *find_option(key);
    const auto shown = option.choices.subspan(skipped);
    return ImGui::Combo(option.label, index, shown.data(),
                        static_cast<int>(shown.size()));
}

void mark_mouse_aim(bool value) { mark("disruptor.mouse_aim", value); }

void mark_modern_controls(bool value) { mark("disruptor.modern_controls", value); }

void mark_sensitivity(double value) {
    g_preferences.pending.has_horizontal_sensitivity = true;
    g_preferences.pending.horizontal_sensitivity = value;
    g_preferences.changed.insert(
        find_option("disruptor.horizontal_sensitivity"));
}

void mark_invert_x(bool value) { mark("disruptor.invert_horizontal", value); }

void mark_vertical_look(bool value) { mark("disruptor.vertical_look", value); }

void mark_vertical_sensitivity(double value) {
    g_preferences.pending.has_vertical_sensitivity = true;
    g_preferences.pending.vertical_sensitivity = value;
    g_preferences.changed.insert(
        find_option("disruptor.vertical_sensitivity"));
}

void mark_invert_y(bool value) { mark("disruptor.invert_vertical", value); }

void mark_geometry(bool value) { mark("disruptor.geometry_correction", value); }

void mark_textures(bool value) { mark("disruptor.perspective_textures", value); }

void mark_vsync(int value) { mark("video.vsync", value + 1); }

void mark_aspect(int numerator, int denominator, bool adaptive) {
    g_preferences.pending.has_aspect_ratio = true;
    g_preferences.pending.aspect_num = numerator;
    g_preferences.pending.aspect_den = denominator;
    /* Keep the adaptive flag and its fixed 32:9 cap in the same atomic
     * settings update as the fallback/initial aspect. */
    g_preferences.pending.has_adaptive_view = true;
    g_preferences.pending.adaptive_view = adaptive;
    g_preferences.changed.insert(find_option("video.aspect_ratio"));
}

void mark_supersampling(int value) { mark("video.supersampling", value - 1); }

void mark_fullscreen(int value) { mark("video.fullscreen", value); }

void mark_master_volume(int value) { mark("audio.master_volume", value); }

void mark_audio_muted(bool value) { mark("audio.muted", value); }

void mark_skip_intro(bool value) { mark("disruptor.skip_intro", value); }

void mark_language_disc(const std::string &path) {
    g_language_disc = path;
    g_preferences.pending.has_language_disc = true;
    g_preferences.pending.language_disc = path;
    g_preferences.language_disc_changed = true;
}

void mark_hud_scale(int value) { mark("disruptor.hud_scale", value); }

void mark_frame_unlock(bool value) { mark("disruptor.frame_unlock", value); }

void mark_shadow_shape(bool improved) { mark("disruptor.improved_shadows", improved); }

void mark_occlusion(bool value) { mark("disruptor.ambient_occlusion", value); }

void mark_occlusion_shape(int reach, int strength) {
    mark("disruptor.occlusion_reach", reach);
    mark("disruptor.occlusion_strength", strength);
}

void mark_gap_filling(bool value) { mark("disruptor.gap_filling", value); }

void mark_world_depth(bool value) { mark("disruptor.depth_sorted_world", value); }

// Views 1 to 4 are layers of the occlusion pass, 5 is the mesh drawn over in-between frames.
void apply_diagnostic_view(int view) {
    gl_renderer_set_ssao_view(view >= 1 && view <= 4 ? view : 0);
    gl_renderer_set_mesh_view(view == 5 ? 1 : 0);
}

void mark_diagnostic_view(int value) {
    g_preferences.pending.has_diagnostic_view = true;
    g_preferences.pending.diagnostic_view = value;
    g_preferences.diagnostic_view_changed = true;
}

// With the vertical divided too a renderer turn of one step misses the game's next step in a median of 8604 pixels of 307200, without in 24274.
void apply_smooth_turn(bool enabled) {
    disruptor_high_precision_camera_set_enabled(enabled ? 1 : 0);
    if (!env_override_present("PSX_GEOMETRY_FULL_YAW")) gl_renderer_set_full_yaw(enabled ? 1 : 0);
}

void mark_smooth_turn(bool value) { mark("disruptor.high_precision_camera", value); }

void mark_present_rate(int value) { mark("video.frame_interpolation_fps", value); }

void mark_present_enabled(bool value) { mark("video.frame_interpolation", value); }

void merge_dirty_preferences(PSXRecompV4::UserSettings &settings) {
    const auto &pending = g_preferences.pending;
    for (const Option *option : g_preferences.changed)
        option->set(settings, option->get(pending));
    if (g_preferences.language_disc_changed) {
        settings.has_language_disc = true;
        settings.language_disc = pending.language_disc;
    }
    if (g_preferences.diagnostic_view_changed) {
        settings.has_diagnostic_view = true;
        settings.diagnostic_view = pending.diagnostic_view;
    }
}

bool flush_preferences() {
    if (!g_preferences.dirty()) return true;
    if (g_preferences.path.empty()) {
        g_preferences.save_failed = true;
        g_preferences.status = "Runtime settings path is unavailable.";
        return false;
    }

    PSXRecompV4::UserSettings settings =
        PSXRecompV4::load_user_settings(g_preferences.path);
    if (settings.parse_error) {
        g_preferences.save_failed = true;
        g_preferences.status =
            "settings.toml is invalid; it was left untouched.";
        return false;
    }
    merge_dirty_preferences(settings);
    if (!PSXRecompV4::save_user_settings(g_preferences.path, settings)) {
        g_preferences.save_failed = true;
        g_preferences.status =
            "Could not save settings; the previous file is intact.";
        return false;
    }

    g_preferences.clean();
    g_preferences.save_failed = false;
    g_preferences.status = "Settings saved.";
    std::fprintf(stdout, "disruptor: saved in-game settings to %s\n",
                 utf8(g_preferences.path).c_str());
    return true;
}

void apply_saved_preferences(const PSXRecompV4::UserSettings &settings) {
    /* Force the legacy INI + environment fallback to load before the saved
     * layer is applied.  Presence (including an explicit zero) makes an
     * environment value authoritative for this launch. */
    (void)disruptor_mouse_horizontal_sensitivity();
    (void)disruptor_mouse_invert_horizontal();

    if (settings.has_mouse_aim &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_AIM"))
        disruptor_mouse_aim_set_enabled(settings.mouse_aim ? 1 : 0);
    if (settings.has_modern_controls &&
        !env_override_present("PSX_DISRUPTOR_MODERN_CONTROLS"))
        set_modern_controls_live(settings.modern_controls);
    if (settings.has_horizontal_sensitivity &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_SENSITIVITY"))
        (void)disruptor_mouse_set_horizontal_sensitivity(
            settings.horizontal_sensitivity);
    if (settings.has_invert_horizontal &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_INVERT_X"))
        disruptor_mouse_set_invert_horizontal(
            settings.invert_horizontal ? 1 : 0);
    if (settings.has_vertical_look &&
        !env_override_present("PSX_DISRUPTOR_VERTICAL_LOOK"))
        disruptor_mouse_set_vertical_look_enabled(
            settings.vertical_look ? 1 : 0);
    if (settings.has_vertical_sensitivity &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y"))
        (void)disruptor_mouse_set_vertical_sensitivity(
            settings.vertical_sensitivity);
    if (settings.has_invert_vertical &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_INVERT_Y"))
        disruptor_mouse_set_invert_vertical(
            settings.invert_vertical ? 1 : 0);

    /* An inherited explicit environment override can enable Modern controls
     * without a run-script switch (and therefore without its preset copy).
     * Always reconcile the live mode with the in-memory keyboard overlay so
     * S/A do not retain conflicting classic face-button bindings. */
    apply_modern_keybinds(disruptor_modern_controls_enabled() != 0);

    /* main.cpp applies these fields before the environment layer. Repeat the
     * live application here so a host-UI soft session also gets the saved
     * state, while still respecting explicit per-launch overrides. */
    if (settings.has_geometry_correction &&
        !env_override_present("PSX_GEOMETRY_CORRECTION"))
        gpu_geometry_correction_set(settings.geometry_correction ? 1 : 0);
    if (settings.has_perspective_textures &&
        !env_override_present("PSX_TEXTURE_CORRECTION")) {
        gpu_texture_correction_set(
            settings.perspective_textures &&
                    gpu_geometry_correction_enabled()
                ? 1 : 0);
    }
    if (settings.has_supersampling)
        (void)psx_host_video_set_internal_scale(settings.supersampling);
    if (settings.has_fullscreen)
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(settings.fullscreen) == 0;
    if (settings.has_master_volume)
        (void)psx_host_audio_set_master_volume(settings.master_volume);
    if (settings.has_audio_muted)
        (void)psx_host_audio_set_muted(settings.audio_muted ? 1 : 0);
    if (settings.has_hud_scale) gpu_ws_set_hud_scale(settings.hud_scale);
    if (settings.has_frame_unlock &&
        !env_override_present("PSX_DISRUPTOR_FRAME_UNLOCK"))
        disruptor_frame_rate_set_unlocked(settings.frame_unlock ? 1 : 0);
    if (settings.has_skip_intro &&
        !env_override_present("PSX_DISRUPTOR_SKIP_INTRO"))
        disruptor_intro_skip_set_enabled(settings.skip_intro ? 1 : 0);
    if (settings.has_language_disc) g_language_disc = settings.language_disc;
    if (settings.has_improved_shadows && !env_override_present("PSX_DISRUPTOR_IMPROVED_SHADOWS"))
        gpu_set_shadow_shape(settings.improved_shadows ? 1 : 0);
    if (!env_override_present("PSX_GL_SSAO")) {
        if (settings.has_ambient_occlusion) gl_renderer_set_ssao(settings.ambient_occlusion ? 1 : 0);
        /* A layer is a session's own. Only a capsule's replay starts in the one its recording was made in. */
        apply_diagnostic_view(settings.has_diagnostic_view && env_override_present("PSX_DISRUPTOR_CAPSULE_REPLAY")
                                  ? settings.diagnostic_view : 0);
    }
    gl_renderer_set_ssao_shape(
        settings.has_occlusion_reach && !env_override_present("PSX_GL_SSAO_RADIUS")
            ? static_cast<float>(settings.occlusion_reach) : -1.0f,
        settings.has_occlusion_strength && !env_override_present("PSX_GL_SSAO_STRENGTH")
            ? settings.occlusion_strength / 100.0f : -1.0f);
    if (settings.has_gap_filling && !env_override_present("PSX_GL_CRACK_FILL") && !env_override_present("PSX_GL_GROWN_UNDER"))
        gl_renderer_set_gap_fill(settings.gap_filling ? 1 : 0);
    if (settings.has_depth_sorted_world && !env_override_present("PSX_GL_WORLD_DEPTH"))
        gl_renderer_set_world_depth(settings.depth_sorted_world ? 1 : 0);
    // The session's start switched it off after the mouse module read its variable, so the variable is applied here too.
    if (const char *forced = std::getenv("PSX_DISRUPTOR_HIGH_PRECISION_CAMERA"))
        apply_smooth_turn(forced[0] != '\0' && forced[0] != '0');
    else if (settings.has_high_precision_camera)
        apply_smooth_turn(settings.high_precision_camera);
}

void apply_pending_preferences() {
    const auto &pending = g_preferences.pending;
    if (changed("disruptor.mouse_aim"))
        disruptor_mouse_aim_set_enabled(pending.mouse_aim ? 1 : 0);
    if (changed("disruptor.modern_controls"))
        set_modern_controls_live(pending.modern_controls);
    if (changed("disruptor.horizontal_sensitivity"))
        (void)disruptor_mouse_set_horizontal_sensitivity(
            pending.horizontal_sensitivity);
    if (changed("disruptor.invert_horizontal"))
        disruptor_mouse_set_invert_horizontal(
            pending.invert_horizontal ? 1 : 0);
    if (changed("disruptor.vertical_look"))
        disruptor_mouse_set_vertical_look_enabled(
            pending.vertical_look ? 1 : 0);
    if (changed("disruptor.vertical_sensitivity"))
        (void)disruptor_mouse_set_vertical_sensitivity(
            pending.vertical_sensitivity);
    if (changed("disruptor.invert_vertical"))
        disruptor_mouse_set_invert_vertical(
            pending.invert_vertical ? 1 : 0);
    if (changed("disruptor.geometry_correction"))
        gpu_geometry_correction_set(pending.geometry_correction ? 1 : 0);
    if (changed("disruptor.perspective_textures"))
        gpu_texture_correction_set(
            pending.perspective_textures &&
                    gpu_geometry_correction_enabled()
                ? 1 : 0);
    if (changed("video.vsync"))
        (void)psx_host_video_set_vsync(pending.vsync);
    if (changed("video.aspect_ratio")) {
        if (pending.adaptive_view)
            (void)psx_host_video_set_adaptive_view(1);
        else
            (void)psx_host_video_set_display_aspect(
                pending.aspect_num, pending.aspect_den);
    }
    if (changed("video.supersampling"))
        (void)psx_host_video_set_internal_scale(pending.supersampling);
    if (changed("video.fullscreen"))
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(pending.fullscreen) == 0;
    if (changed("audio.master_volume"))
        (void)psx_host_audio_set_master_volume(pending.master_volume);
    if (changed("audio.muted"))
        (void)psx_host_audio_set_muted(pending.audio_muted ? 1 : 0);
    if (changed("disruptor.hud_scale"))
        gpu_ws_set_hud_scale(pending.hud_scale);
    if (changed("disruptor.frame_unlock"))
        disruptor_frame_rate_set_unlocked(pending.frame_unlock ? 1 : 0);
    if (changed("disruptor.skip_intro"))
        disruptor_intro_skip_set_enabled(pending.skip_intro ? 1 : 0);
    if (changed("disruptor.improved_shadows"))
        gpu_set_shadow_shape(pending.improved_shadows ? 1 : 0);
    if (changed("disruptor.ambient_occlusion"))
        gl_renderer_set_ssao(pending.ambient_occlusion ? 1 : 0);
    if (changed("disruptor.occlusion_reach") ||
        changed("disruptor.occlusion_strength"))
        gl_renderer_set_ssao_shape(static_cast<float>(pending.occlusion_reach), pending.occlusion_strength / 100.0f);
    if (changed("disruptor.gap_filling"))
        gl_renderer_set_gap_fill(pending.gap_filling ? 1 : 0);
    if (changed("disruptor.depth_sorted_world"))
        gl_renderer_set_world_depth(pending.depth_sorted_world ? 1 : 0);
    if (g_preferences.diagnostic_view_changed)
        apply_diagnostic_view(pending.diagnostic_view);
    if (changed("disruptor.high_precision_camera"))
        apply_smooth_turn(pending.high_precision_camera);
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    if (changed("video.frame_interpolation"))
        (void)disruptor_present_rate_enable(pending.frame_interpolation ? 1 : 0);
    if (changed("video.frame_interpolation_fps"))
        (void)disruptor_present_rate_apply(pending.frame_interpolation_fps);
#endif
}

void load_preferences_for_session() {
    const char *path = psx_host_user_settings_path();
    if (!path || !path[0]) {
        g_preferences.loaded = false;
        g_preferences.status = "Runtime settings path is unavailable.";
        return;
    }
    const std::string_view text(path);
    g_preferences.path = fs::path(std::u8string(text.begin(), text.end()));
    const PSXRecompV4::UserSettings settings =
        PSXRecompV4::load_user_settings(g_preferences.path);
    if (settings.parse_error) {
        g_preferences.loaded = false;
        g_preferences.status =
            "settings.toml is invalid; saved menu values were not applied.";
        return;
    }
    capture_base_keybinds();
    apply_saved_preferences(settings);
    apply_pending_preferences();
    /* A capsule recorded later copies the file: a layer left there by the last session must not go into it. */
    if (settings.has_diagnostic_view && settings.diagnostic_view != 0 && !env_override_present("PSX_DISRUPTOR_CAPSULE_REPLAY")) {
        mark_diagnostic_view(0);
        (void)flush_preferences();
    }
    g_preferences.loaded = true;
    if (!g_preferences.save_failed)
        g_preferences.status = "Settings loaded.";
}

bool imgui_sdl_init(SDL_Window *window) {
#if defined(PSX_SDL3)
    return ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext());
#else
    return ImGui_ImplSDL2_InitForOpenGL(window, SDL_GL_GetCurrentContext());
#endif
}

void imgui_sdl_shutdown() {
#if defined(PSX_SDL3)
    ImGui_ImplSDL3_Shutdown();
#else
    ImGui_ImplSDL2_Shutdown();
#endif
}

void imgui_sdl_new_frame() {
#if defined(PSX_SDL3)
    ImGui_ImplSDL3_NewFrame();
#else
    ImGui_ImplSDL2_NewFrame();
#endif
}

void imgui_sdl_process_event(const SDL_Event *event) {
#if defined(PSX_SDL3)
    (void)ImGui_ImplSDL3_ProcessEvent(event);
#else
    (void)ImGui_ImplSDL2_ProcessEvent(event);
#endif
}

void apply_disruptor_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 5.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding = 3.0f;

    const ImVec4 green(0.20f, 0.78f, 0.34f, 1.00f);
    const ImVec4 green_hover(0.28f, 0.90f, 0.43f, 1.00f);
    const ImVec4 green_active(0.14f, 0.62f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = green_hover;
    style.Colors[ImGuiCol_SliderGrab] = green;
    style.Colors[ImGuiCol_SliderGrabActive] = green_hover;
    style.Colors[ImGuiCol_Header] = ImVec4(0.10f, 0.38f, 0.18f, 1.00f);
    style.Colors[ImGuiCol_HeaderHovered] = green_active;
    style.Colors[ImGuiCol_HeaderActive] = green;
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.34f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = green_active;
    style.Colors[ImGuiCol_ButtonActive] = green;
}

bool initialize_imgui() {
    if (g_menu.imgui_ready) return true;
    if (!g_menu.window || g_menu.backend != PSX_HOST_UI_BACKEND_OPENGL)
        return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    /* Persist only the reviewed gameplay/presentation fields below.  ImGui's
     * ambient layout file is deliberately disabled: it is relative-path,
     * independently written, and outside our atomic settings contract. */
    io.IniFilename = nullptr;
    apply_disruptor_style();

    if (!imgui_sdl_init(g_menu.window)) {
        ImGui::DestroyContext();
        std::fprintf(stderr,
                     "disruptor: developer menu SDL backend initialization failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330 core")) {
        imgui_sdl_shutdown();
        ImGui::DestroyContext();
        std::fprintf(stderr,
                     "disruptor: developer menu OpenGL initialization failed\n");
        return false;
    }

    g_menu.imgui_ready = true;
    std::fprintf(stdout,
                 "disruptor: in-game settings menu ready (press ` to open)\n");
    return true;
}

// A second after the menu closes, two seconds of what is swapped to the screen go to shown/<unix time>/ beside settings.toml.
void start_shown_recording() {
    g_menu.record_shown = false;
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const fs::path folder = g_preferences.path.parent_path() / "shown" / std::to_string(now);
    std::error_code failed;
    fs::create_directories(folder, failed);
    if (failed) {
        g_menu.shown_note = "The folder for the pictures could not be made.";
        return;
    }
    g_menu.shown_note = gl_renderer_record_shown(utf8(folder).c_str(), 120, 240, 960) ? "Last recording: " + utf8(folder)
                                                                                      : "The recording could not start.";
}

void set_menu_open(bool open) {
    if (open == g_menu.open) return;
    if (open) {
        g_menu.restore_mouse_capture = disruptor_mouse_captured() != 0;
        if (g_menu.restore_mouse_capture)
            (void)disruptor_mouse_set_captured(0);
        g_menu.open = true;
    } else {
        g_menu.open = false;
        (void)flush_preferences();
        if (g_menu.record_shown) start_shown_recording();
        if (g_menu.restore_mouse_capture)
            (void)disruptor_mouse_set_captured(1);
        g_menu.restore_mouse_capture = false;
    }
}

bool scancode_event(const SDL_Event *event, SDL_Scancode scancode) {
    if (!event || event->type != SDL_KEYDOWN || event->key.repeat) return false;
#if defined(PSX_SDL3)
    return event->key.scancode == scancode;
#else
    return event->key.keysym.scancode == scancode;
#endif
}

void draw_status_badge(const char *label, const ImVec4 &colour) {
    ImGui::SameLine();
    ImGui::TextColored(colour, "[%s]", label);
}

void draw_controls_tab() {
    bool mouse_aim = disruptor_mouse_aim_enabled() != 0;
    if (option_toggle("disruptor.mouse_aim", &mouse_aim)) {
        disruptor_mouse_aim_set_enabled(mouse_aim ? 1 : 0);
        mark_mouse_aim(mouse_aim);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool modern = disruptor_modern_controls_enabled() != 0;
    if (option_toggle("disruptor.modern_controls", &modern)) {
        set_modern_controls_live(modern);
        mark_modern_controls(modern);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    float sensitivity =
        static_cast<float>(disruptor_mouse_horizontal_sensitivity());
    if (option_slider("disruptor.horizontal_sensitivity", &sensitivity)) {
        const double applied =
            disruptor_mouse_set_horizontal_sensitivity(sensitivity);
        mark_sensitivity(applied);
    }

    bool invert_x = disruptor_mouse_invert_horizontal() != 0;
    if (option_toggle("disruptor.invert_horizontal", &invert_x)) {
        disruptor_mouse_set_invert_horizontal(invert_x ? 1 : 0);
        mark_invert_x(invert_x);
    }

    const bool turn_applies = mouse_aim && gpu_geometry_correction_enabled() != 0;
    bool smooth = disruptor_high_precision_camera_enabled() != 0;
    if (!turn_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.high_precision_camera", &smooth)) {
        apply_smooth_turn(smooth);
        mark_smooth_turn(smooth);
    }
    if (!turn_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "The game turns in 256 steps of a circle. While the mouse is captured "
        "the picture is turned by the part of a step in between. Needs mouse aim "
        "and exact geometry.");

    ImGui::SeparatorText("Vertical look");
    bool vertical_look = disruptor_mouse_vertical_look_enabled() != 0;
    if (option_toggle("disruptor.vertical_look", &vertical_look)) {
        disruptor_mouse_set_vertical_look_enabled(vertical_look ? 1 : 0);
        mark_vertical_look(vertical_look);
    }
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));

    float vertical_sensitivity =
        static_cast<float>(disruptor_mouse_vertical_sensitivity());
    if (option_slider("disruptor.vertical_sensitivity", &vertical_sensitivity)) {
        const double applied =
            disruptor_mouse_set_vertical_sensitivity(vertical_sensitivity);
        mark_vertical_sensitivity(applied);
    }

    bool invert_y = disruptor_mouse_invert_vertical() != 0;
    if (option_toggle("disruptor.invert_vertical", &invert_y)) {
        disruptor_mouse_set_invert_vertical(invert_y ? 1 : 0);
        mark_invert_y(invert_y);
    }

    const double pitch_units = disruptor_mouse_vertical_pitch();
    if (pitch_units == 0.0) ImGui::BeginDisabled();
    if (ImGui::Button("Recenter vertical view"))
        disruptor_mouse_recenter_vertical();
    if (pitch_units == 0.0) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Requested pitch: %+.2f deg", pitch_units * (360.0 / 256.0));
    ImGui::TextDisabled(
        "Range: +/-30.94 degrees. The original game has no pitch state.");
    ImGui::Separator();
    ImGui::TextWrapped(
        "The menu releases relative mouse capture while open and restores the "
        "previous state when closed. Gameplay input is neutralised while you "
        "interact with the menu.");
    ImGui::Text("Capture before opening: %s",
                g_menu.restore_mouse_capture ? "yes" : "no");
}

void apply_geometry_enabled(bool enabled) {
    if (!enabled && gpu_texture_correction_enabled()) {
        gpu_texture_correction_set(0);
        mark_textures(false);
    }
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    if (!enabled) {
        (void)disruptor_present_rate_enable(0);
        mark_present_enabled(false);
    }
#endif
    gpu_geometry_correction_set(enabled ? 1 : 0);
    mark_geometry(enabled);
}

void draw_aspect_controls() {
    static constexpr int kAspectNumerators[] = {16, 21, 32, 32};
    static constexpr int kAspectDenominators[] = {9, 9, 9, 9};
    static constexpr int kFixedWideAspectCount = 3;
    static constexpr int kAdaptiveAspectIndex = 3;
    static constexpr int kAspectCount = 4;

    int numerator = 4;
    int denominator = 3;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    const bool adaptive = psx_host_video_get_adaptive_view() != 0;
    bool widescreen = adaptive || numerator * 3 != denominator * 4;
    if (adaptive) {
        g_menu.selected_wide_aspect = kAdaptiveAspectIndex;
    } else if (widescreen) {
        for (int index = 0; index < kFixedWideAspectCount; ++index) {
            if (numerator * kAspectDenominators[index] ==
                denominator * kAspectNumerators[index]) {
                g_menu.selected_wide_aspect = index;
                break;
            }
        }
    }
    g_menu.selected_wide_aspect = std::clamp(
        g_menu.selected_wide_aspect, 0, kAspectCount - 1);

    ImGui::SeparatorText("Display aspect");
    if (ImGui::Checkbox("Widescreen", &widescreen)) {
        if (widescreen &&
            g_menu.selected_wide_aspect == kAdaptiveAspectIndex) {
            g_menu.aspect_apply_failed =
                psx_host_video_set_adaptive_view(1) == 0;
            if (!g_menu.aspect_apply_failed)
                mark_aspect(32, 9, true);
        } else {
            const int requested_num = widescreen
                ? kAspectNumerators[g_menu.selected_wide_aspect] : 4;
            const int requested_den = widescreen
                ? kAspectDenominators[g_menu.selected_wide_aspect] : 3;
            g_menu.aspect_apply_failed =
                psx_host_video_set_display_aspect(
                    requested_num, requested_den) == 0;
            if (!g_menu.aspect_apply_failed)
                mark_aspect(requested_num, requested_den, false);
        }
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    int selected = g_menu.selected_wide_aspect;
    if (option_list("video.aspect_ratio", &selected, 1)) {
        g_menu.selected_wide_aspect = selected;
        if (widescreen) {
            if (selected == kAdaptiveAspectIndex) {
                g_menu.aspect_apply_failed =
                    psx_host_video_set_adaptive_view(1) == 0;
                if (!g_menu.aspect_apply_failed)
                    mark_aspect(32, 9, true);
            } else {
                const int requested_num = kAspectNumerators[selected];
                const int requested_den = kAspectDenominators[selected];
                g_menu.aspect_apply_failed =
                    psx_host_video_set_display_aspect(
                        requested_num, requested_den) == 0;
                if (!g_menu.aspect_apply_failed)
                    mark_aspect(requested_num, requested_den, false);
            }
        }
    }
    if (g_menu.aspect_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested display aspect.");
    }
    ImGui::TextDisabled(
        "4:3 disables widescreen. Fixed 16:9, 21:9 and 32:9 choices or "
        "live window matching (capped at 32:9) apply after the current "
        "frame and are saved.");

    if (gpu_sprite_placement_available()) {
        int shadow_shape = gpu_shadow_shape();
        ImGui::SetNextItemWidth(260.0f);
        if (option_list("disruptor.improved_shadows", &shadow_shape)) {
            gpu_set_shadow_shape(shadow_shape);
            mark_shadow_shape(shadow_shape != 0);
        }
        draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
        ImGui::TextDisabled(
            "Improved lays a shadow flat on the floor: as wide as the game makes it and as tall "
            "as the floor's perspective gives. Works with geometry correction on.");
    }

    int hud_scale = gpu_ws_hud_scale();
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.hud_scale", &hud_scale)) {
        gpu_ws_set_hud_scale(hud_scale);
        mark_hud_scale(hud_scale);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Widescreen only. Shrinks the health and ammunition displays toward "
        "their screen edges. The weapon keeps its size.");
}

void draw_internal_resolution_controls() {
    static constexpr int kScaleCount = 8;
    int selected = std::clamp(
        psx_host_video_get_internal_scale(), 1, kScaleCount) - 1;

    ImGui::SeparatorText("Rendering resolution");
    if (option_list("video.supersampling", &selected)) {
        const int requested = selected + 1;
        g_menu.internal_scale_apply_failed =
            psx_host_video_set_internal_scale(requested) == 0;
        if (!g_menu.internal_scale_apply_failed)
            mark_supersampling(requested);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (g_menu.internal_scale_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The renderer could not allocate that internal resolution.");
    }
    ImGui::TextDisabled(
        "Supersamples geometry and shading before presentation. GPU fill and "
        "memory cost grow roughly with the square of this value; changes "
        "apply immediately and are saved.");
}

void draw_enhancements_tab() {
    draw_aspect_controls();
    draw_internal_resolution_controls();

    bool geometry = gpu_geometry_correction_enabled() != 0;
    if (option_toggle("disruptor.geometry_correction", &geometry))
        apply_geometry_enabled(geometry);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool textures = gpu_texture_correction_enabled() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (option_toggle("disruptor.perspective_textures", &textures)) {
        gpu_texture_correction_set(textures ? 1 : 0);
        mark_textures(textures);
    }
    if (!geometry) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (!geometry)
        ImGui::TextDisabled("Enable exact geometry before perspective textures.");

    bool gaps = gl_renderer_gap_fill() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (option_toggle("disruptor.gap_filling", &gaps)) {
        gl_renderer_set_gap_fill(gaps ? 1 : 0);
        mark_gap_filling(gaps);
    }
    if (!geometry) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Closes hairline gaps between polygons and keeps the game's own "
        "patches off their neighbours.");

    bool by_depth = gl_renderer_world_depth() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (option_toggle("disruptor.depth_sorted_world", &by_depth)) {
        gl_renderer_set_world_depth(by_depth ? 1 : 0);
        mark_world_depth(by_depth);
    }
    if (!geometry) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Draws world polygons by their depth where the game's own order puts a "
        "far one over a near one.");

    ImGui::SeparatorText("Synchronisation");
    int vsync = psx_host_video_get_vsync();
    int vsync_index = std::clamp(vsync + 1, 0, 2);
    if (option_list("video.vsync", &vsync_index)) {
        const int requested = vsync_index - 1;
        if (psx_host_video_set_vsync(requested)) mark_vsync(requested);
    }

    bool unlocked = disruptor_frame_rate_unlocked() != 0;
    if (option_toggle("disruptor.frame_unlock", &unlocked)) {
        disruptor_frame_rate_set_unlocked(unlocked ? 1 : 0);
        mark_frame_unlock(unlocked);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    DisruptorFrameRateWindow window{};
    if (disruptor_frame_rate_last_window(&window) && window.vblanks != 0u) {
        ImGui::TextDisabled(
            "Game frames: %.1f per second. Frame work: %.2f VBlank average, "
            "%.2f peak, %u of %u frames over one VBlank.",
            59.94 * window.frames / window.vblanks,
            window.average_work_permille / 1000.0,
            window.peak_work_permille / 1000.0,
            window.late_frames, window.frames);
    }

#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    int present_enabled = 0;
    if (disruptor_present_rate_switch(
            find_option("video.frame_interpolation")->label, geometry ? 1 : 0,
            &present_enabled))
        mark_present_enabled(present_enabled != 0);
    int present_rate = 0;
    if (disruptor_present_rate_control(
            find_option("video.frame_interpolation_fps")->label, &present_rate))
        mark_present_rate(present_rate);
#endif

    ImGui::SeparatorText("Startup");
    bool skip_intro = disruptor_intro_skip_enabled() != 0;
    if (option_toggle("disruptor.skip_intro", &skip_intro)) {
        disruptor_intro_skip_set_enabled(skip_intro ? 1 : 0);
        mark_skip_intro(skip_intro);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled("Escape skips the logo or movie on screen.");
}

const char *cheat_result_message(int result, const char *success) {
    switch (result) {
        case DISRUPTOR_CHEAT_OK:
            return success;
        case DISRUPTOR_CHEAT_GAME_NOT_READY:
            return "Enter live gameplay before using this cheat.";
        case DISRUPTOR_CHEAT_NETPLAY_BLOCKED:
            return "Cheats are disabled during netplay.";
        case DISRUPTOR_CHEAT_UNVERIFIED_STATE:
            return "The game state did not match the verified USA executable; no changes were made.";
        default:
            return "The cheat request was rejected.";
    }
}

void draw_experimental_tab() {
    ImGui::TextDisabled(
        "Unfinished features of the OpenGL renderer. All of them need geometry "
        "correction on.");
    ImGui::Spacing();

    const bool geometry = gpu_geometry_correction_enabled() != 0;
    bool occlusion = gl_renderer_ssao() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (option_toggle("disruptor.ambient_occlusion", &occlusion)) {
        gl_renderer_set_ssao(occlusion ? 1 : 0);
        mark_occlusion(occlusion);
    }
    if (!geometry) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    int reach = static_cast<int>(std::lround(gl_renderer_ssao_radius()));
    int strength = static_cast<int>(std::lround(gl_renderer_ssao_strength() * 100.0f));
    if (!geometry) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    const bool reach_moved = option_slider("disruptor.occlusion_reach", &reach);
    ImGui::SetNextItemWidth(260.0f);
    const bool strength_moved =
        option_slider("disruptor.occlusion_strength", &strength);
    if (!geometry) ImGui::EndDisabled();
    if (reach_moved || strength_moved) {
        gl_renderer_set_ssao_shape(static_cast<float>(reach), strength / 100.0f);
        mark_occlusion_shape(reach, strength);
    }
    ImGui::TextDisabled("Shades corners and the floor under things.");
}

void draw_debug_tab() {
    ImGui::TextDisabled("Tools for a report of a fault in the picture.");
    ImGui::Spacing();

    if (ImGui::Button("Record the screen") && !gl_renderer_recording_shown()) g_menu.record_shown = true;
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "For a report: two seconds of the pictures that reach the screen, with "
        "their times. It starts a second after this menu closes.");
    if (g_menu.record_shown) ImGui::TextDisabled("Armed. Close the menu and do what you want to show.");
    else if (!g_menu.shown_note.empty()) ImGui::TextDisabled("%s", g_menu.shown_note.c_str());

    ImGui::Separator();
    int view = gl_renderer_mesh_view() ? 5 : gl_renderer_ssao_view();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Diagnostic view", &view, "Off\0Shading alone\0Depth\0Surface normals\0Fog fade\0Mesh\0")) {
        apply_diagnostic_view(view);
        mark_diagnostic_view(view);
    }
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Shows a layer of the renderer in the picture's place, for reports. A "
        "replay capsule recorded with one on replays in it. Off again at the "
        "next start.");
}

void draw_cheats_tab() {
    const bool netplay = disruptor_cheats_netplay_blocked() != 0;
    const bool gameplay_ready = disruptor_cheats_gameplay_ready() != 0;

    ImGui::TextWrapped(
        "These are version-pinned gameplay cheats for testing and casual "
        "play. They are never saved as port settings and start off on every "
        "launch.");
    if (netplay) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Cheats are unavailable while netplay is active.");
    } else if (!gameplay_ready) {
        ImGui::TextDisabled("Enter live gameplay to enable cheats.");
    }

    ImGui::SeparatorText("Player");
    bool god_mode = disruptor_cheats_god_mode_enabled() != 0;
    const bool disable_god_control = !god_mode && (netplay || !gameplay_ready);
    if (disable_god_control) ImGui::BeginDisabled();
    if (ImGui::Checkbox("God mode", &god_mode)) {
        const int result = disruptor_cheats_set_god_mode(god_mode ? 1 : 0);
        g_menu.cheat_status = cheat_result_message(
            result, god_mode ? "God mode enabled for this session."
                             : "God mode disabled.");
    }
    if (disable_god_control) ImGui::EndDisabled();
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Neutralises player damage at the game's central damage routine. "
        "A light hit cue remains, but saved health is not altered and the "
        "game is not marked as cheated.");

    ImGui::SeparatorText("Inventory");
    if (netplay || !gameplay_ready) ImGui::BeginDisabled();
    if (ImGui::Button("Grant all weapons + psionics"))
        ImGui::OpenPopup("Confirm All Weapons");
    if (netplay || !gameplay_ready) ImGui::EndDisabled();
    ImGui::TextWrapped(
        "This reproduces the retail All Weapons cheat: all weapons and "
        "psionics are unlocked, ammunition and psionic energy are refilled, "
        "and the current game is marked as cheated.");
    ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
        "Important: the cheated marker affects the ending and is carried into "
        "memory-card/password saves.");

    bool popup_open = true;
    if (ImGui::BeginPopupModal("Confirm All Weapons", &popup_open,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Grant every weapon and psionic, refill their resources, and "
            "mark this game (including later saves) as cheated?");
        if (ImGui::Button("Grant and mark cheated")) {
            const int result = disruptor_cheats_grant_all_weapons();
            g_menu.cheat_status = cheat_result_message(
                result, "All weapons, psionics and resources granted. Game marked as cheated.");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!g_menu.cheat_status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", g_menu.cheat_status.c_str());
    }
}

void draw_fullscreen_controls() {
    int mode = std::clamp(psx_host_video_get_fullscreen_mode(), 0, 2);

    ImGui::SeparatorText("Window presentation");
    if (option_list("video.fullscreen", &mode)) {
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(mode) == 0;
        if (!g_menu.fullscreen_apply_failed)
            mark_fullscreen(mode);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (g_menu.fullscreen_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested display mode.");
    }
    ImGui::TextDisabled(
        "Menu selections apply immediately and are saved. Alt+Enter remains "
        "a transient window/fullscreen toggle and does not overwrite the "
        "saved mode.");
}

void draw_audio_controls() {
    ImGui::SeparatorText("Audio output");
    int volume = psx_host_audio_get_master_volume();
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("audio.master_volume", &volume) &&
        psx_host_audio_set_master_volume(volume)) {
        mark_master_volume(volume);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool muted = psx_host_audio_get_muted() != 0;
    if (option_toggle("audio.muted", &muted) &&
        psx_host_audio_set_muted(muted ? 1 : 0)) {
        mark_audio_muted(muted);
    }
    ImGui::TextDisabled(
        "Host-side output only: game audio state and saves are unchanged. "
        "Unmuting restores the selected volume.");
}

void draw_system_tab() {
    draw_fullscreen_controls();
    draw_audio_controls();
    ImGui::TextWrapped(
        "Live menu choices are merged into the runtime's user-owned "
        "settings.toml. Writes use an atomic replacement, so a failed save "
        "leaves the previous file intact. Explicit launch flags still win "
        "for that run.");
    ImGui::SeparatorText("Settings persistence");
    ImGui::TextWrapped("Path: %s",
        g_preferences.path.empty()
            ? "unavailable" : utf8(g_preferences.path).c_str());
    if (g_preferences.save_failed)
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Not saved");
    else if (g_preferences.dirty())
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
                           "Unsaved changes");
    else
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "Saved");
    if (!g_preferences.status.empty())
        ImGui::TextWrapped("%s", g_preferences.status.c_str());
    if (g_preferences.dirty() && ImGui::Button("Save now"))
        (void)flush_preferences();
    ImGui::TextDisabled(
        "Current vertical pitch, menu layout, "
        "and mouse capture are not saved. Cheat controls start "
        "off each launch.");
    ImGui::SeparatorText("Restart required");
    ImGui::BulletText("Renderer backend");
    ImGui::BulletText("Audio backend and buffer configuration");
    ImGui::BulletText("BIOS, disc and memory-card wiring");
    ImGui::Separator();
    if (ImGui::Button("Close menu")) set_menu_open(false);
}

void draw_language_tab() {
    ImGui::TextWrapped("Disc of another region: %s", g_language_disc.empty() ? "none, the game is in English" : g_language_disc.c_str());
    if (ImGui::Button("Choose a disc image")) (void)psx_host_pick_disc_image_begin();
    ImGui::SameLine();
    if (ImGui::Button("English") && !g_language_disc.empty()) mark_language_disc(std::string());
    ImGui::SameLine();
    if (ImGui::Button("Restart now") && !psx_netplay_active() && flush_preferences() && disruptor_restart_arm()) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
    ImGui::TextDisabled("Movies, speech and words come from that disc at the next start. The French, German and Japanese discs are known.");
    ImGui::TextDisabled("The Japanese disc gives movies, speech and hints: its menus are in English on the disc itself.");
}

void draw_menu() {
    char picked[4096];
    if (psx_host_pick_disc_image_poll(picked, static_cast<int>(sizeof picked)) == 1) mark_language_disc(picked);
    bool keep_open = g_menu.open;
    ImGui::SetNextWindowSize(ImVec2(760.0f, 560.0f),
                             ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Disruptor Settings", &keep_open,
                     ImGuiWindowFlags_NoCollapse)) {
        ImGui::PushTextWrapPos(0.0f);  // a long note wraps at the window's edge, where it used to be cut off
        ImGui::TextDisabled("Press ` to toggle; Escape closes the menu.");
        ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
                           "The game is not paused while this menu is open.");

        if (ImGui::BeginTabBar("DisruptorDevTabs")) {
            if (ImGui::BeginTabItem("Controls")) {
                draw_controls_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Enhancements")) {
                draw_enhancements_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Experimental")) {
                draw_experimental_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Language")) {
                draw_language_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("System")) {
                draw_system_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Debug")) {
                draw_debug_tab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Cheats")) {
                draw_cheats_tab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::PopTextWrapPos();
    }
    ImGui::End();
    if (!keep_open && g_menu.open) set_menu_open(false);
}

void on_runtime_ready(void *, SDL_Window *window, int backend) {
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
    disruptor_mouse_recenter_vertical();
    disruptor_high_precision_camera_set_enabled(0);
    gte_ws_set_far_threshold(900);
    gte_ws_set_backdrop_repair_enabled(0);
    g_menu.window = window;
    g_menu.backend = backend;
    load_preferences_for_session();
    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();
}

void on_runtime_shutdown(void *) {
    disruptor_cheats_reset_session();
    disruptor_far_rendering_reset_session();
    disruptor_billboard_aspect_set_site_mask(
        disruptor_billboard_aspect_all_site_mask());
    disruptor_mouse_recenter_vertical();
    disruptor_high_precision_camera_set_enabled(0);
    gte_ws_set_far_threshold(900);
    gte_ws_set_backdrop_repair_enabled(0);
    (void)flush_preferences();
    if (disruptor_mouse_captured())
        (void)disruptor_mouse_set_captured(0);
    if (g_menu.base_keybinds_valid)
        apply_modern_keybinds(false);
    g_menu.open = false;
    g_menu.restore_mouse_capture = false;
    if (g_menu.imgui_ready) {
        ImGui_ImplOpenGL3_Shutdown();
        imgui_sdl_shutdown();
        ImGui::DestroyContext();
    }
    g_menu = DevMenuState{};
}

int on_sdl_event(void *, const SDL_Event *event) {
    const bool toggle = scancode_event(event, SDL_SCANCODE_GRAVE);
    const bool close =
        g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE);
    if (toggle || close) {
        set_menu_open(toggle ? !g_menu.open : false);
        return 1;
    }
    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE))
        disruptor_intro_skip_request();
    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_F12) && (SDL_GetModState() & KMOD_CTRL) != 0) {
        disruptor_capsule_toggle();
        return 1;
    }
    if (!g_menu.open || !g_menu.imgui_ready) return 0;
    imgui_sdl_process_event(event);
    return 1;
}

uint32_t current_flags(void *) {
    if (!g_menu.open || !g_menu.imgui_ready) return disruptor_capsule_notice() ? PSX_HOST_UI_VISIBLE : 0u;
    return PSX_HOST_UI_CAPTURE_KEYBOARD |
           PSX_HOST_UI_CAPTURE_MOUSE |
           PSX_HOST_UI_CAPTURE_GAMEPAD |
           PSX_HOST_UI_VISIBLE;
}

/* Drawn without the SDL backend's frame: that one reads and captures the mouse, which is the game's while the menu is closed. */
void render_notice(const char *word, int width, int height) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f));
    ImGui::Begin("##capsule", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::SetWindowFontScale(std::max(1.0f, static_cast<float>(height) / 360.0f));
    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.2f, 1.0f), "%s", word);
    ImGui::End();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void render_gl(void *, int width, int height) {
    if (const char *word = g_menu.open ? nullptr : disruptor_capsule_notice()) {
        if (initialize_imgui()) render_notice(word, width, height);
        return;
    }
    if (!g_menu.open || !initialize_imgui()) return;
    ImGui_ImplOpenGL3_NewFrame();
    imgui_sdl_new_frame();
    ImGui::NewFrame();
    draw_menu();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

struct MenuRegistration {
    MenuRegistration() {
        PsxHostUiHooks hooks{};
        hooks.abi_version = PSX_HOST_UI_ABI_VERSION;
        hooks.struct_size = static_cast<uint32_t>(sizeof(hooks));
        hooks.on_runtime_ready = on_runtime_ready;
        hooks.on_runtime_shutdown = on_runtime_shutdown;
        hooks.on_sdl_event = on_sdl_event;
        hooks.flags = current_flags;
        hooks.render_gl = render_gl;
        if (!psx_host_ui_register(&hooks)) {
            std::fprintf(stderr,
                         "disruptor: settings menu registration failed\n");
        }
    }
};

MenuRegistration g_registration;

}  // namespace
