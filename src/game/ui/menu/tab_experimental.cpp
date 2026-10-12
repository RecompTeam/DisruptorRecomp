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
}

}  // namespace disruptor::menu
