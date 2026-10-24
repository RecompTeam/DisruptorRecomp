#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

const char *cheat_result_message(int result, const char *success) {
    switch (result) {
        case DISRUPTOR_CHEAT_OK:
            return success;
        case DISRUPTOR_CHEAT_GAME_NOT_READY:
            return "Enter live gameplay before using this cheat";
        case DISRUPTOR_CHEAT_NETPLAY_BLOCKED:
            return "Cheats are disabled during netplay";
        case DISRUPTOR_CHEAT_ACHIEVEMENTS_BLOCKED:
            return "Cheats are off in a hardcore RetroAchievements session";
        case DISRUPTOR_CHEAT_UNVERIFIED_STATE:
            return "The game state did not match the verified USA executable; no changes were made";
        default:
            return "The cheat request was rejected";
    }
}

}  // namespace

void draw_cheats_tab() {
    const bool netplay = disruptor_cheats_netplay_blocked() != 0;
    const bool gameplay_ready = disruptor_cheats_gameplay_ready() != 0;

    ImGui::TextWrapped(
        "These are version-pinned gameplay cheats for testing and casual "
        "play\nThey are never saved as port settings and start off on every "
        "launch");
    if (netplay) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.25f, 1.0f),
                           "Cheats are unavailable while netplay is active");
    } else if (!gameplay_ready) {
        ImGui::TextDisabled("Enter live gameplay to enable cheats");
    }

    ImGui::SeparatorText("Player");
    bool god_mode = disruptor_cheats_god_mode_enabled() != 0;
    const bool disable_god_control = !god_mode && (netplay || !gameplay_ready);
    if (disable_god_control) ImGui::BeginDisabled();
    if (ImGui::Checkbox("God mode", &god_mode)) {
        const int result = disruptor_cheats_set_god_mode(god_mode ? 1 : 0);
        g_menu.cheat_status = cheat_result_message(
            result, god_mode ? "God mode enabled for this session"
                             : "God mode disabled");
    }
    if (disable_god_control) ImGui::EndDisabled();
    draw_status_badge("SESSION", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled(
        "Neutralises player damage at the game's central damage routine. "
        "A light hit cue remains, but saved health is not altered and the "
        "game is not marked as cheated");

    ImGui::SeparatorText("Inventory");
    if (netplay || !gameplay_ready) ImGui::BeginDisabled();
    if (ImGui::Button("Grant all weapons + psionics"))
        ImGui::OpenPopup("Confirm All Weapons");
    if (netplay || !gameplay_ready) ImGui::EndDisabled();
    ImGui::TextWrapped(
        "This reproduces the retail All Weapons cheat: all weapons and "
        "psionics are unlocked, ammunition and psionic energy are refilled, "
        "and the current game is marked as cheated");
    ImGui::TextColored(ImVec4(1.0f, 0.73f, 0.25f, 1.0f),
        "Important: the cheated marker affects the ending and is carried into "
        "memory-card/password saves");

    bool popup_open = true;
    if (ImGui::BeginPopupModal("Confirm All Weapons", &popup_open,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped(
            "Grant every weapon and psionic, refill their resources, and "
            "mark this game (including later saves) as cheated?");
        if (ImGui::Button("Grant and mark cheated")) {
            const int result = disruptor_cheats_grant_all_weapons();
            g_menu.cheat_status = cheat_result_message(
                result, "All weapons, psionics and resources granted\nGame marked as cheated");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    if (!g_menu.cheat_status.empty()) {
        ImGui::Separator();
        ImGui::TextWrapped("%s", g_menu.cheat_status.c_str());
    }
}

}  // namespace disruptor::menu
