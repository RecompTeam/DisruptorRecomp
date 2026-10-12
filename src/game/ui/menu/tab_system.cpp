#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

void draw_fullscreen_controls() {
    int mode = std::clamp(psx_host_video_get_fullscreen_mode(), 0, 2);

    ImGui::SeparatorText("Window presentation");
    if (option_list("video.fullscreen", &mode)) {
        g_menu.fullscreen_apply_failed =
            psx_host_video_set_fullscreen_mode(mode) == 0;
        if (!g_menu.fullscreen_apply_failed)
            mark_fullscreen(mode);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (g_menu.fullscreen_apply_failed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
            "The runtime rejected the requested display mode.");
    }
    ImGui::TextDisabled(
        "Menu selections apply immediately and are saved. Alt+Enter remains "
        "a transient window/fullscreen toggle and does not overwrite the "
        "saved mode.");
}

void draw_audio_controls() {
    ImGui::SeparatorText("Audio output");
    int volume = psx_host_audio_get_master_volume();
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("audio.master_volume", &volume) &&
        psx_host_audio_set_master_volume(volume)) {
        mark_master_volume(volume);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool muted = psx_host_audio_get_muted() != 0;
    if (option_toggle("audio.muted", &muted) &&
        psx_host_audio_set_muted(muted ? 1 : 0)) {
        mark_audio_muted(muted);
    }
    ImGui::TextDisabled(
        "Host-side output only: game audio state and saves are unchanged. "
        "Unmuting restores the selected volume.");
}

}  // namespace

void draw_system_tab() {
    draw_fullscreen_controls();
    draw_audio_controls();
    ImGui::TextWrapped(
        "Live menu choices are merged into the runtime's user-owned "
        "settings.toml. Writes use an atomic replacement, so a failed save "
        "leaves the previous file intact. Explicit launch flags still win "
        "for that run.");
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
        "and mouse capture are not saved. Cheat controls start "
        "off each launch.");
    ImGui::SeparatorText("Restart required");
    ImGui::BulletText("Renderer backend");
    ImGui::BulletText("Audio backend and buffer configuration");
    ImGui::BulletText("BIOS, disc and memory-card wiring");
    ImGui::Separator();
    if (ImGui::Button("Close menu")) set_menu_open(false);
}

}  // namespace disruptor::menu
