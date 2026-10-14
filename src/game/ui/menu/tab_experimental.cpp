#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_experimental_tab() {
    ImGui::TextDisabled(
        "Unfinished features of the OpenGL renderer. All of them need geometry "
        "correction on.");
    ImGui::Spacing();

    const Option& antialiasing = *find_option("disruptor.multisampling");
    settings::Settings running;
    running.multisampling = gl_renderer_multisampling();
    running.sample_shading = gl_renderer_sample_shading() != 0;
    int level = antialiasing.get(running);
    const bool sampling_applies = applies("disruptor.multisampling");
    if (!sampling_applies) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    if (option_list("disruptor.multisampling", &level)) {
        antialiasing.set(running, level);
        gl_renderer_set_multisampling(running.multisampling);
        gl_renderer_set_sample_shading(running.sample_shading ? 1 : 0);
        mark_antialiasing(level);
    }
    if (!sampling_applies) ImGui::EndDisabled();
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
