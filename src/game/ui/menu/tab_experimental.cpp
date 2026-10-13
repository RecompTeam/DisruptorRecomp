#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_experimental_tab() {
    ImGui::TextDisabled(
        "Unfinished features of the OpenGL renderer. All of them need geometry "
        "correction on.");
    ImGui::Spacing();

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
    ImGui::Separator();
    bool smooth_fade = gl_renderer_smooth_fade() != 0;
    const bool fade_applies = applies("disruptor.smooth_fade");
    if (!fade_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.smooth_fade", &smooth_fade)) {
        gl_renderer_set_smooth_fade(smooth_fade ? 1 : 0);
        mark_smooth_fade(smooth_fade);
    }
    if (!fade_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::Separator();
    int samples = 0;
    for (int level = 0; level < 5; ++level)
        if (PSXRecompV4::kDisruptorSampleCounts[level] == gl_renderer_multisampling()) samples = level;
    const bool sampling_applies = applies("disruptor.multisampling");
    if (!sampling_applies) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    if (option_list("disruptor.multisampling", &samples)) {
        gl_renderer_set_multisampling(PSXRecompV4::kDisruptorSampleCounts[samples]);
        mark_multisampling(samples);
    }
    if (!sampling_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    bool sample_shading = gl_renderer_sample_shading() != 0;
    const bool shading_applies = applies("disruptor.sample_shading");
    if (!shading_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.sample_shading", &sample_shading)) {
        gl_renderer_set_sample_shading(sample_shading ? 1 : 0);
        mark_sample_shading(sample_shading);
    }
    if (!shading_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    bool averaging = gl_renderer_downscale_averaging() != 0;
    if (option_toggle("disruptor.downscale_averaging", &averaging)) {
        gl_renderer_set_downscale_averaging(averaging ? 1 : 0);
        mark_downscale_averaging(averaging);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    bool edge_smoothing = gl_renderer_edge_smoothing() != 0;
    if (option_toggle("disruptor.edge_smoothing", &edge_smoothing)) {
        gl_renderer_set_edge_smoothing(edge_smoothing ? 1 : 0);
        mark_edge_smoothing(edge_smoothing);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
}

}  // namespace disruptor::menu
