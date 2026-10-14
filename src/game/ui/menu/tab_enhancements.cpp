#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

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

    int hud_scale = gpu_ws_hud_scale();
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.hud_scale", &hud_scale)) {
        gpu_ws_set_hud_scale(hud_scale);
        mark_hud_scale(hud_scale);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
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

}  // namespace

void draw_enhancements_tab() {
    draw_aspect_controls();
    draw_internal_resolution_controls();

    bool geometry = gpu_geometry_correction_enabled() != 0;
    if (option_toggle("disruptor.geometry_correction", &geometry))
        apply_geometry_enabled(geometry);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool textures = gpu_texture_correction_enabled() != 0;
    const bool textures_apply = applies("disruptor.perspective_textures");
    if (!textures_apply) ImGui::BeginDisabled();
    if (option_toggle("disruptor.perspective_textures", &textures)) {
        gpu_texture_correction_set(textures ? 1 : 0);
        mark_textures(textures);
    }
    if (!textures_apply) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (!textures_apply)
        ImGui::TextDisabled("Enable exact geometry before perspective textures.");

    bool gaps = gl_renderer_gap_fill() != 0;
    const bool gaps_apply = applies("disruptor.gap_filling");
    if (!gaps_apply) ImGui::BeginDisabled();
    if (option_toggle("disruptor.gap_filling", &gaps)) {
        gl_renderer_set_gap_fill(gaps ? 1 : 0);
        mark_gap_filling(gaps);
    }
    if (!gaps_apply) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool by_depth = gl_renderer_world_depth() != 0;
    const bool depth_applies = applies("disruptor.depth_sorted_world");
    if (!depth_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.depth_sorted_world", &by_depth)) {
        gl_renderer_set_world_depth(by_depth ? 1 : 0);
        mark_world_depth(by_depth);
    }
    if (!depth_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool smooth_fade = gl_renderer_smooth_fade() != 0;
    const bool fade_applies = applies("disruptor.smooth_fade");
    if (!fade_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.smooth_fade", &smooth_fade)) {
        gl_renderer_set_smooth_fade(smooth_fade ? 1 : 0);
        mark_smooth_fade(smooth_fade);
    }
    if (!fade_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool occlusion = gl_renderer_ssao() != 0;
    const bool occlusion_applies = applies("disruptor.ambient_occlusion");
    if (!occlusion_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.ambient_occlusion", &occlusion)) {
        gl_renderer_set_ssao(occlusion ? 1 : 0);
        mark_occlusion(occlusion);
    }
    if (!occlusion_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    int reach = static_cast<int>(std::lround(gl_renderer_ssao_radius()));
    int strength = static_cast<int>(std::lround(gl_renderer_ssao_strength() * 100.0f));
    const bool shape_applies = applies("disruptor.occlusion_reach") &&
                               applies("disruptor.occlusion_strength");
    if (!shape_applies) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    const bool reach_moved = option_slider("disruptor.occlusion_reach", &reach);
    ImGui::SetNextItemWidth(260.0f);
    const bool strength_moved =
        option_slider("disruptor.occlusion_strength", &strength);
    if (!shape_applies) ImGui::EndDisabled();
    if (reach_moved || strength_moved) {
        gl_renderer_set_ssao_shape(static_cast<float>(reach), strength / 100.0f);
        mark_occlusion_shape(reach, strength);
    }

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
            find_option("video.frame_interpolation")->label,
            find_option("video.frame_interpolation")->hint,
            applies("video.frame_interpolation") ? 1 : 0,
            &present_enabled))
        mark_present_enabled(present_enabled != 0);
    int present_rate = 0;
    if (disruptor_present_rate_control(
            find_option("video.frame_interpolation_fps")->label,
            find_option("video.frame_interpolation_fps")->hint, &present_rate))
        mark_present_rate(present_rate);
#endif

    ImGui::SeparatorText("Startup");
    bool skip_intro = disruptor_intro_skip_enabled() != 0;
    if (option_toggle("disruptor.skip_intro", &skip_intro)) {
        disruptor_intro_skip_set_enabled(skip_intro ? 1 : 0);
        mark_skip_intro(skip_intro);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
}

}  // namespace disruptor::menu
