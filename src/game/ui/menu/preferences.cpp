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

void follow_scheme(bool modern, bool save) {
    disruptor::settings::KeyLayout current;
    for (int index = 0; index < PSX_KB_COUNT; ++index) {
        const SDL_Scancode key = psx_keybinds_get_button(1, index);
        const char *name = key == SDL_SCANCODE_UNKNOWN ? "" : SDL_GetScancodeName(key);
        current[static_cast<size_t>(index)] = name && *name ? name : "None";
    }
    const disruptor::settings::KeyLayout next = disruptor::settings::layout_for_scheme(current, modern);
    if (next == current) return;
    for (int index = 0; index < PSX_KB_COUNT; ++index) {
        const std::string &name = next[static_cast<size_t>(index)];
        psx_keybinds_set_button(1, index, name == "None" ? SDL_SCANCODE_UNKNOWN : SDL_GetScancodeFromName(name.c_str()));
    }
    if (save) psx_keybinds_save();
}

}  // namespace

void set_modern_controls_live(bool enabled) {
    follow_scheme(enabled, true);
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

// A speed written once writes down the vertical choice the game runs with, as the launcher does.
void keep_vertical_choice() {
    const Option *separate = find_option("disruptor.separate_vertical_speed");
    if (g_preferences.changed.count(separate) == 0 && !env_override_present("PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y"))
        mark("disruptor.separate_vertical_speed", disruptor_mouse_vertical_follows_horizontal() == 0);
}

void mark_sensitivity(double value) {
    keep_vertical_choice();
    g_preferences.pending.has_horizontal_sensitivity = true;
    g_preferences.pending.horizontal_sensitivity = value;
    g_preferences.changed.insert(
        find_option("disruptor.horizontal_sensitivity"));
}

void mark_invert_x(bool value) { mark("disruptor.invert_horizontal", value); }

void mark_vertical_look(bool value) { mark("disruptor.vertical_look", value); }

void mark_vertical_sensitivity(double value) {
    keep_vertical_choice();
    g_preferences.pending.has_vertical_sensitivity = true;
    g_preferences.pending.vertical_sensitivity = value;
    g_preferences.changed.insert(
        find_option("disruptor.vertical_sensitivity"));
}

void mark_invert_y(bool value) { mark("disruptor.invert_vertical", value); }
void mark_separate_vertical(bool value) { mark("disruptor.separate_vertical_speed", value); }
void set_deadzone_live(int percent) {
    mark("controller.deadzone", percent);
    psx_input_set_deadzone(g_preferences.pending.deadzone);
}

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

void mark_mute_in_background(bool value) { mark("disruptor.mute_in_background", value); }

void mark_achievements(bool value) { mark("disruptor.achievements", value); }

void mark_bass_cut(int cut) { mark("disruptor.bass_cut", cut); }

void mark_skip_intro(bool value) { mark("disruptor.skip_intro", value); }

void mark_language_disc(const std::string &path) {
    g_language_disc = path;
    g_preferences.pending.has_language_disc = true;
    g_preferences.pending.language_disc = path;
    g_preferences.language_disc_changed = true;
}

void mark_hud_scale(int value) { mark("disruptor.hud_scale", value); }

void mark_frame_unlock(bool value) { mark("disruptor.frame_unlock", value); }

void mark_occlusion(bool value) { mark("disruptor.ambient_occlusion", value); }

void mark_occlusion_shape(int reach, int strength) {
    mark("disruptor.occlusion_reach", reach);
    mark("disruptor.occlusion_strength", strength);
}

void mark_smooth_fade(bool value) { mark("disruptor.smooth_fade", value); }

void mark_antialiasing(int level) { mark("disruptor.multisampling", level); }

void mark_draw_distance(int preset) { mark("disruptor.draw_distance", preset); }

void mark_fog_shape(int shape) { mark("disruptor.fog_shape", shape); }



void mark_detailed_logging(bool value) { mark("disruptor.detailed_logging", value); }

void mark_gap_filling(bool value) { mark("disruptor.gap_filling", value); }

void mark_world_depth(bool value) { mark("disruptor.depth_sorted_world", value); }

// What the renderer took, so a value it clamped or a variable set is saved as seen.
void mark_lighting() {
    const auto percent = [](float amount) { return static_cast<int>(std::lround(amount * 100.0f)); };
    mark("disruptor.probe_lighting", gl_renderer_ddgi() != 0);
    mark("disruptor.probe_strength", percent(gl_renderer_ddgi_strength()));
    mark("disruptor.probe_bounce", percent(gl_renderer_ddgi_bounce()));
    mark("disruptor.probe_glow", percent(gl_renderer_ddgi_glow()));
    mark("disruptor.dynamic_shadows", gl_renderer_ddgi_shadows() != 0);
    mark("disruptor.shadow_strength", percent(gl_renderer_ddgi_shadow_strength()));
    mark("disruptor.shadow_underneath", percent(gl_renderer_ddgi_contact()));
    mark("disruptor.shot_lights", gl_renderer_ddgi_lamps() != 0);
    mark("disruptor.shot_light_strength", percent(gl_renderer_ddgi_lamp_strength()));
    mark("disruptor.sun_lighting", gl_renderer_ddgi_sun() != 0);
}

void mark_sun(const int amounts[4]) {
    mark("disruptor.sun_direction", amounts[0]);
    mark("disruptor.sun_height", amounts[1]);
    mark("disruptor.sun_strength", amounts[2]);
    mark("disruptor.sun_shade", amounts[3]);
}

namespace {

// A release does not offer the lighting prototype or the diagnostic views: a developer build's saved setting leaves them off.
#if DISRUPTOR_DEV_TOOLS
constexpr bool kDeveloperBuild = true;
#else
constexpr bool kDeveloperBuild = false;
#endif

void apply_lighting(const PSXRecompV4::UserSettings &settings) {
    const auto value = [&](const char *key) { return kDeveloperBuild ? stored(settings, *find_option(key)) : 0; };
    gl_renderer_set_ddgi_environment(kDeveloperBuild);
    gl_renderer_set_ddgi_strength(value("disruptor.probe_strength") / 100.0f);
    gl_renderer_set_ddgi_bounce(value("disruptor.probe_bounce") / 100.0f);
    gl_renderer_set_ddgi_glow(value("disruptor.probe_glow") / 100.0f);
    gl_renderer_set_ddgi_shadow_strength(value("disruptor.shadow_strength") / 100.0f);
    gl_renderer_set_ddgi_contact(value("disruptor.shadow_underneath") / 100.0f);
    gl_renderer_set_ddgi_lamp_strength(value("disruptor.shot_light_strength") / 100.0f);
    gl_renderer_set_ddgi(value("disruptor.probe_lighting"));
    gl_renderer_set_ddgi_shadows(value("disruptor.dynamic_shadows"));
    gl_renderer_set_ddgi_lamps(value("disruptor.shot_lights"));
    gl_renderer_set_ddgi_sun(value("disruptor.sun_lighting"));
}

void apply_sticks(const PSXRecompV4::UserSettings &settings) {
    const auto value = [&](const char *key) { return stored(settings, *find_option(key)); };
    disruptor::sticks::tune({static_cast<disruptor::sticks::Layout>(value("disruptor.stick_layout")), value("disruptor.look_speed"),
                             value("disruptor.look_deadzone"), value("disruptor.invert_look") != 0});
}

// Only the second mission has a sun.
void apply_sun(const PSXRecompV4::UserSettings &settings) {
    const auto value = [&](const char *key) { return stored(settings, *find_option(key)); };
    const float sun[4] = {static_cast<float>(value("disruptor.sun_direction")), static_cast<float>(value("disruptor.sun_height")),
                          value("disruptor.sun_strength") / 100.0f, value("disruptor.sun_shade") / 100.0f};
    gl_renderer_ddgi_sun_set_mission(2, sun);
}

}  // namespace

// Views 1 to 4 are layers of the occlusion pass, 5 is the mesh drawn over in-between frames.
void apply_diagnostic_view(int view) {
    gl_renderer_set_ssao_view(view >= 1 && view <= 4 ? view : 0);
    gl_renderer_set_mesh_view(view == 5 ? 1 : 0);
    gl_renderer_set_uv_view(view == 6 ? 1 : 0);
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
        g_preferences.status = "Runtime settings path is unavailable";
        return false;
    }

    PSXRecompV4::UserSettings settings =
        PSXRecompV4::load_user_settings(g_preferences.path);
    if (settings.parse_error) {
        g_preferences.save_failed = true;
        g_preferences.status =
            "settings.toml is invalid; it was left untouched";
        return false;
    }
    merge_dirty_preferences(settings);
    if (!PSXRecompV4::save_user_settings(g_preferences.path, settings)) {
        g_preferences.save_failed = true;
        g_preferences.status =
            "Could not save settings; the previous file is intact";
        return false;
    }

    for (const Option *option : g_preferences.changed)
        disruptor_diagnostics_note("setting %s = %d", option->key, option->get(g_preferences.pending));
    g_preferences.clean();
    g_preferences.save_failed = false;
    g_preferences.status = "Settings saved";
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
    disruptor_selection_wheels_set_enabled(settings.has_selection_wheels && settings.selection_wheels);
    disruptor_number_keys_set_enabled(settings.has_number_keys && settings.number_keys);
    disruptor_psionic_keys_use_set_enabled(stored(settings, *find_option("disruptor.psionic_keys_use")) != 0);
    disruptor_smooth_movies_set_enabled(stored(settings, *find_option("disruptor.movie_filter")) != 0);
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
    disruptor_mouse_set_vertical_follows_horizontal(
        stored(settings, *find_option("disruptor.separate_vertical_speed")) == 0 &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y"));
    if (settings.has_invert_vertical &&
        !env_override_present("PSX_DISRUPTOR_MOUSE_INVERT_Y"))
        disruptor_mouse_set_invert_vertical(
            settings.invert_vertical ? 1 : 0);

    /* A scheme set by the environment alone takes its layout for this run and leaves keybinds.ini as it is. */
    follow_scheme(disruptor_modern_controls_enabled() != 0, !env_override_present("PSX_DISRUPTOR_MODERN_CONTROLS"));

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
    if (settings.has_mute_in_background)
        (void)psx_host_audio_set_mute_in_background(settings.mute_in_background ? 1 : 0);
    disruptor_bass_cut_set(stored(settings, *find_option("disruptor.bass_cut")));
    if (settings.has_hud_scale) gpu_ws_set_hud_scale(settings.hud_scale);
    if (!env_override_present("PSX_DISRUPTOR_FRAME_UNLOCK"))
        disruptor_frame_rate_set_unlocked(stored(settings, *find_option("disruptor.frame_unlock")));
    cdrom_set_fast_data_reads(stored(settings, *find_option("disruptor.fast_disc_reads")));
    if (settings.has_skip_intro &&
        !env_override_present("PSX_DISRUPTOR_SKIP_INTRO"))
        disruptor_intro_skip_set_enabled(settings.skip_intro ? 1 : 0);
    if (settings.has_language_disc) g_language_disc = settings.language_disc;
    if (!env_override_present("PSX_GL_SSAO")) {
        if (settings.has_ambient_occlusion) gl_renderer_set_ssao(settings.ambient_occlusion ? 1 : 0);
        /* A layer is a session's own. Only a capsule's replay starts in the one its recording was made in. */
        apply_diagnostic_view(kDeveloperBuild && settings.has_diagnostic_view &&
                                      env_override_present("PSX_DISRUPTOR_CAPSULE_REPLAY")
                                  ? settings.diagnostic_view : 0);
    }
    if (settings.has_smooth_fade) gl_renderer_set_smooth_fade(settings.smooth_fade ? 1 : 0);
    // A file may name one half of the list: the other is off, as the list starts.
    gl_renderer_set_multisampling(settings.multisampling);
    gl_renderer_set_sample_shading(settings.sample_shading ? 1 : 0);
    (void)disruptor_far_rendering_set_preset(stored(settings, *find_option("disruptor.draw_distance")));
    disruptor_far_rendering_force_from_environment();
    disruptor_far_rendering_set_fog_exponent(stored(settings, *find_option("disruptor.fog_shape")) + 1);
    // Downscale averaging and edge smoothing are withdrawn for now: off whatever an older file says.
    gl_renderer_set_downscale_averaging(0);
    gl_renderer_set_edge_smoothing(0);
    gl_renderer_set_ssao_shape(
        !env_override_present("PSX_GL_SSAO_RADIUS")
            ? static_cast<float>(settings.occlusion_reach) : -1.0f,
        !env_override_present("PSX_GL_SSAO_STRENGTH")
            ? settings.occlusion_strength / 100.0f : -1.0f);
    if (settings.has_gap_filling && !env_override_present("PSX_GL_CRACK_FILL") && !env_override_present("PSX_GL_GROWN_UNDER"))
        gl_renderer_set_gap_fill(settings.gap_filling ? 1 : 0);
    if (settings.has_depth_sorted_world && !env_override_present("PSX_GL_WORLD_DEPTH"))
        gl_renderer_set_world_depth(settings.depth_sorted_world ? 1 : 0);
    apply_sticks(settings);
    // The renderer reads the PSX_DDGI variables at its first update, after this: they still win.
    apply_sun(settings);
    apply_lighting(settings);
    // The session's start switched it off after the mouse module read its variable, so the variable is applied here too.
    if (const char *forced = std::getenv("PSX_DISRUPTOR_HIGH_PRECISION_CAMERA"))
        apply_smooth_turn(forced[0] != '\0' && forced[0] != '0');
    else
        apply_smooth_turn(stored(settings, *find_option("disruptor.high_precision_camera")) != 0);
}

void apply_pending_preferences() {
    const auto &pending = g_preferences.pending;
    if (changed("disruptor.mouse_aim"))
        disruptor_mouse_aim_set_enabled(pending.mouse_aim ? 1 : 0);
    if (changed("disruptor.modern_controls"))
        set_modern_controls_live(pending.modern_controls);
    if (changed("disruptor.selection_wheels"))
        disruptor_selection_wheels_set_enabled(pending.selection_wheels);
    if (changed("disruptor.number_keys"))
        disruptor_number_keys_set_enabled(pending.number_keys);
    if (changed("disruptor.psionic_keys_use"))
        disruptor_psionic_keys_use_set_enabled(pending.psionic_keys_use);
    if (changed("disruptor.movie_filter"))
        disruptor_smooth_movies_set_enabled(stored(pending, *find_option("disruptor.movie_filter")) != 0);
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
    if (changed("disruptor.separate_vertical_speed"))
        disruptor_mouse_set_vertical_follows_horizontal(
            pending.separate_vertical_speed ? 0 : 1);
    if (changed("controller.deadzone")) psx_input_set_deadzone(pending.deadzone);
    disruptor::sticks::Tuning sticks = disruptor::sticks::tuning();
    if (changed("disruptor.stick_layout")) sticks.layout = static_cast<disruptor::sticks::Layout>(pending.stick_layout);
    if (changed("disruptor.look_speed")) sticks.look_speed = pending.look_speed;
    if (changed("disruptor.look_deadzone")) sticks.look_deadzone = pending.look_deadzone;
    if (changed("disruptor.invert_look")) sticks.invert_look = pending.invert_look;
    disruptor::sticks::tune(sticks);
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
    if (changed("disruptor.mute_in_background"))
        (void)psx_host_audio_set_mute_in_background(pending.mute_in_background ? 1 : 0);
    if (changed("disruptor.achievements"))
        disruptor_achievements_configure(pending.achievements ? 1 : 0);
    if (changed("disruptor.achievements_encore") || changed("disruptor.achievements_unofficial") ||
        changed("disruptor.achievements_popups") || changed("disruptor.achievements_popup_time") ||
        changed("disruptor.achievements_corner") || changed("disruptor.achievements_progress") ||
        changed("disruptor.achievements_sound"))
        disruptor::achievements::configure(pending);
    if (changed("disruptor.bass_cut"))
        disruptor_bass_cut_set(stored(pending, *find_option("disruptor.bass_cut")));
    if (changed("disruptor.hud_scale"))
        gpu_ws_set_hud_scale(pending.hud_scale);
    if (changed("disruptor.frame_unlock"))
        disruptor_frame_rate_set_unlocked(pending.frame_unlock ? 1 : 0);
    if (changed("disruptor.skip_intro"))
        disruptor_intro_skip_set_enabled(pending.skip_intro ? 1 : 0);
    if (changed("disruptor.fast_disc_reads"))
        cdrom_set_fast_data_reads(stored(pending, *find_option("disruptor.fast_disc_reads")));
    if (changed("disruptor.ambient_occlusion"))
        gl_renderer_set_ssao(pending.ambient_occlusion ? 1 : 0);
    if (changed("disruptor.occlusion_reach") ||
        changed("disruptor.occlusion_strength"))
        gl_renderer_set_ssao_shape(static_cast<float>(pending.occlusion_reach), pending.occlusion_strength / 100.0f);
    if (changed("disruptor.smooth_fade"))
        gl_renderer_set_smooth_fade(pending.smooth_fade ? 1 : 0);
    if (changed("disruptor.multisampling")) {
        gl_renderer_set_multisampling(pending.multisampling);
        gl_renderer_set_sample_shading(pending.sample_shading ? 1 : 0);
    }
    if (changed("disruptor.draw_distance"))
        (void)disruptor_far_rendering_set_preset(stored(pending, *find_option("disruptor.draw_distance")));
    if (changed("disruptor.fog_shape"))
        disruptor_far_rendering_set_fog_exponent(stored(pending, *find_option("disruptor.fog_shape")) + 1);
    if (changed("disruptor.detailed_logging"))
        disruptor_diagnostics_set_detailed(pending.detailed_logging ? 1 : 0);
    if (changed("disruptor.gap_filling"))
        gl_renderer_set_gap_fill(pending.gap_filling ? 1 : 0);
    if (changed("disruptor.depth_sorted_world"))
        gl_renderer_set_world_depth(pending.depth_sorted_world ? 1 : 0);
    if (changed("disruptor.probe_lighting") || changed("disruptor.probe_strength") || changed("disruptor.probe_bounce") ||
        changed("disruptor.probe_glow") || changed("disruptor.dynamic_shadows") || changed("disruptor.shadow_strength") ||
        changed("disruptor.shadow_underneath") || changed("disruptor.shot_lights") ||
        changed("disruptor.shot_light_strength") || changed("disruptor.sun_lighting"))
        apply_lighting(pending);
    if (changed("disruptor.sun_direction") || changed("disruptor.sun_height") || changed("disruptor.sun_strength") ||
        changed("disruptor.sun_shade"))
        apply_sun(pending);
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
        g_preferences.status = "Runtime settings path is unavailable";
        return;
    }
    const std::string_view text(path);
    g_preferences.path = fs::path(std::u8string(text.begin(), text.end()));
    const PSXRecompV4::UserSettings settings =
        PSXRecompV4::load_user_settings(g_preferences.path);
    if (settings.parse_error) {
        g_preferences.loaded = false;
        g_preferences.status =
            "settings.toml is invalid; saved menu values were not applied";
        std::printf("disruptor: settings at start: the settings file is invalid and was not applied\n");
        return;
    }
    std::printf("disruptor: settings at start: %s\n", disruptor::settings::describe(settings).c_str());
    apply_saved_preferences(disruptor::settings::with_defaults(settings));
    apply_pending_preferences();
    /* A capsule recorded later copies the file: a layer left there by the last session must not go into it. */
    if (settings.has_diagnostic_view && settings.diagnostic_view != 0 && !env_override_present("PSX_DISRUPTOR_CAPSULE_REPLAY")) {
        mark_diagnostic_view(0);
        (void)flush_preferences();
    }
    g_preferences.loaded = true;
    if (!g_preferences.save_failed)
        g_preferences.status = "Settings loaded";
}

}  // namespace disruptor::menu
