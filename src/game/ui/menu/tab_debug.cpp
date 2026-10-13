#include "game/ui/menu/menu.h"

namespace disruptor::menu {
namespace {
constexpr const char *driver_words[] = {"off", "on", "from the next launch", "not offered by this driver"};
}  // namespace

void draw_debug_tab() {
    ImGui::TextDisabled("Tools for a report of a fault.");
    ImGui::Spacing();

    bool detailed = disruptor_diagnostics_detailed() != 0;
    if (option_toggle("disruptor.detailed_logging", &detailed)) {
        disruptor_diagnostics_set_detailed(detailed ? 1 : 0);
        mark_detailed_logging(detailed);
    }
    if (detailed) ImGui::TextDisabled("Driver messages: %s.", driver_words[disruptor_diagnostics_driver_state()]);

    ImGui::Separator();
    if (ImGui::Button("Record the screen") && !gl_renderer_recording_shown()) g_menu.record_shown = true;
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "For a report: two seconds of the pictures that reach the screen, with "
        "their times. It starts a second after this menu closes.");
    if (g_menu.record_shown) ImGui::TextDisabled("Armed. Close the menu and do what you want to show.");
    else if (!g_menu.shown_note.empty()) ImGui::TextDisabled("%s", g_menu.shown_note.c_str());

    ImGui::Separator();
    int view = gl_renderer_uv_view() ? 6 : gl_renderer_mesh_view() ? 5 : gl_renderer_ssao_view();
    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::Combo("Diagnostic view", &view, "Off\0Shading alone\0Depth\0Surface normals\0Fog fade\0Mesh\0Texture coordinates\0")) {
        apply_diagnostic_view(view);
        mark_diagnostic_view(view);
    }
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Shows a layer of the renderer in the picture's place, for reports. A "
        "replay capsule recorded with one on replays in it. Off again at the "
        "next start.");

    ImGui::Separator();
    bool one_layer = gl_renderer_one_layer() != 0;
    if (ImGui::Checkbox("One texture layer per wall", &one_layer)) gl_renderer_set_one_layer(one_layer ? 1 : 0);
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Near a wall the game draws finer pieces over the wall, half a texel "
        "off it. This leaves them out. Not saved.");
}

}  // namespace disruptor::menu
