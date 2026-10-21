#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_system_tab() {
    ImGui::TextWrapped(
        "Live menu choices are merged into the runtime's user-owned "
        "settings.toml\nWrites use an atomic replacement, so a failed save "
        "leaves the previous file intact\nExplicit launch flags still win "
        "for that run");
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
        "and mouse capture are not saved\nCheat controls start "
        "off each launch");
    ImGui::SeparatorText("Restart required");
    ImGui::BulletText("Renderer backend");
    ImGui::BulletText("Audio backend and buffer configuration");
    ImGui::BulletText("BIOS, disc and memory-card wiring");
    ImGui::Separator();
    if (ImGui::Button("Close menu")) set_menu_open(false);
}

}  // namespace disruptor::menu
