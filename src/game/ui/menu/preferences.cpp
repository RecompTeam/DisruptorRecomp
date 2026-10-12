#include "game/ui/menu/menu.h"

namespace disruptor::menu {

PreferenceState g_preferences;

std::string g_language_disc;

std::string utf8(const fs::path &path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

namespace {

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

}  // namespace

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

namespace {

bool changed(const char *key) {
    return g_preferences.changed.contains(find_option(key));
}

}  // namespace

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

namespace {

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

}  // namespace

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

namespace {

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

}  // namespace

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

}  // namespace disruptor::menu
