#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_experimental_tab() {
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

}

}  // namespace disruptor::menu
