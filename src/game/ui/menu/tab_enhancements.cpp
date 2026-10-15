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

}  // namespace

void draw_enhancements_tab() {
    ImGui::SeparatorText("Frame rate");
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
    ImGui::Indent();
    int present_rate = 0;
    if (disruptor_present_rate_control(
            find_option("video.frame_interpolation_fps")->label,
            find_option("video.frame_interpolation_fps")->hint, &present_rate))
        mark_present_rate(present_rate);
    ImGui::Unindent();
#endif

    ImGui::SeparatorText("Picture");
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

    bool smooth_fade = gl_renderer_smooth_fade() != 0;
    const bool fade_applies = applies("disruptor.smooth_fade");
    if (!fade_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.smooth_fade", &smooth_fade)) {
        gl_renderer_set_smooth_fade(smooth_fade ? 1 : 0);
        mark_smooth_fade(smooth_fade);
    }
    if (!fade_applies) ImGui::EndDisabled();
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

    ImGui::SeparatorText("Shading");
    bool occlusion = gl_renderer_ssao() != 0;
    const bool occlusion_applies = applies("disruptor.ambient_occlusion");
    if (!occlusion_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.ambient_occlusion", &occlusion)) {
        gl_renderer_set_ssao(occlusion ? 1 : 0);
        mark_occlusion(occlusion);
    }
    if (!occlusion_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::Indent();
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
    ImGui::Unindent();

    ImGui::SeparatorText("Startup");
    bool skip_intro = disruptor_intro_skip_enabled() != 0;
    if (option_toggle("disruptor.skip_intro", &skip_intro)) {
        disruptor_intro_skip_set_enabled(skip_intro ? 1 : 0);
        mark_skip_intro(skip_intro);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
}

}  // namespace disruptor::menu
