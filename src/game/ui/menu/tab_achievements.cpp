#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

namespace achievements = disruptor::achievements;

enum class Shown { all, locked, unlocked };

Shown g_shown = Shown::all;

void reconfigure() { achievements::configure(g_preferences.pending); }

void account_rows() {
    const std::string player = achievements::player();
    if (player.empty()) {
        ImGui::TextDisabled("The account is chosen on the launcher's Achievements page");
        return;
    }
    ImGui::Text("Signed in as %s", player.c_str());
    ImGui::SameLine();
    const std::string profile = achievements::profile_address(player);
    ImGui::TextLinkOpenURL("Profile", profile.c_str());
    ImGui::SameLine();
    ImGui::TextLinkOpenURL("Disruptor's achievements", achievements::kGamePage);
    ImGui::SameLine();
    if (ImGui::SmallButton("Sign out")) ImGui::OpenPopup("Sign out of RetroAchievements?");
    if (ImGui::BeginPopupModal("Sign out of RetroAchievements?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The game forgets the account until you sign in again on the launcher");
        if (ImGui::Button("Sign out")) {
            disruptor_achievements_sign_out();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void entry_row(const achievements::Entry& entry) {
    const float side = 40.0f;
    const unsigned texture = achievement_picture(entry.unlocked ? entry.badge : entry.locked_badge);
    if (texture) ImGui::Image(static_cast<ImTextureID>(texture), ImVec2(side, side));
    else ImGui::Dummy(ImVec2(side, side));
    ImGui::SameLine();
    ImGui::BeginGroup();
    const ImVec4 title_colour = entry.unlocked ? ImVec4(0.95f, 0.78f, 0.30f, 1.0f) : ImVec4(0.85f, 0.88f, 0.85f, 1.0f);
    ImGui::TextColored(title_colour, "%s", entry.title.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%u %s%s", entry.points, entry.points == 1 ? "point" : "points",
                        entry.unofficial ? ", unofficial" : "");
    ImGui::TextWrapped("%s", entry.description.c_str());
    if (!entry.unlocked && !entry.measured.empty()) {
        ImGui::ProgressBar(std::clamp(entry.percent / 100.0f, 0.0f, 1.0f), ImVec2(220.0f, 0.0f),
                           entry.measured.c_str());
    }
    ImGui::EndGroup();
}

void list_rows() {
    const std::vector<achievements::Entry> list = achievements::achievement_list();
    if (list.empty()) {
        ImGui::TextDisabled("The list appears once the game is found");
        return;
    }
    std::uint32_t unlocked = 0, total = 0, points = 0, all_points = 0;
    for (const achievements::Entry& entry : list) {
        if (entry.unofficial) continue;
        ++total;
        all_points += entry.points;
        if (!entry.unlocked) continue;
        ++unlocked;
        points += entry.points;
    }
    ImGui::Text("%u of %u unlocked, %u of %u points", unlocked, total, points, all_points);
    int shown = static_cast<int>(g_shown);
    ImGui::RadioButton("All", &shown, static_cast<int>(Shown::all));
    ImGui::SameLine();
    ImGui::RadioButton("Locked", &shown, static_cast<int>(Shown::locked));
    ImGui::SameLine();
    ImGui::RadioButton("Unlocked", &shown, static_cast<int>(Shown::unlocked));
    g_shown = static_cast<Shown>(shown);
    ImGui::Separator();
    for (const achievements::Entry& entry : list) {
        if ((g_shown == Shown::locked && entry.unlocked) || (g_shown == Shown::unlocked && !entry.unlocked)) continue;
        ImGui::PushID(static_cast<int>(entry.id));
        entry_row(entry);
        ImGui::PopID();
    }
}

}  // namespace

void draw_achievements_tab() {
    const PSXRecompV4::UserSettings& settings = g_preferences.pending;
    ImGui::SeparatorText("This run");
    bool enabled = disruptor_achievements_enabled() != 0;
    if (option_toggle("disruptor.achievements", &enabled)) {
        disruptor_achievements_configure(enabled ? 1 : 0);
        mark_achievements(enabled);
    }
    std::array<char, 256> status{};
    disruptor_achievements_status(status.data(), static_cast<int>(status.size()));
    ImGui::TextWrapped("%s", status.data());
    if (disruptor_achievements_hardcore()) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "Hardcore");
    else ImGui::TextDisabled("Unlocks count as casual until RetroAchievements approves our client");
    account_rows();

    ImGui::SeparatorText("Notifications");
    bool popups = settings.achievements_popups;
    if (option_toggle("disruptor.achievements_popups", &popups)) {
        mark("disruptor.achievements_popups", popups);
        reconfigure();
    }
    int time = settings.achievements_popup_time;
    const bool popup_time_applies = applies("disruptor.achievements_popup_time");
    if (!popup_time_applies) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(220.0f);
    if (option_list("disruptor.achievements_popup_time", &time)) {
        mark("disruptor.achievements_popup_time", time);
        reconfigure();
    }
    if (!popup_time_applies) ImGui::EndDisabled();
    int corner = settings.achievements_corner;
    ImGui::SetNextItemWidth(220.0f);
    if (option_list("disruptor.achievements_corner", &corner)) {
        mark("disruptor.achievements_corner", corner);
        reconfigure();
    }
    bool progress = settings.achievements_progress;
    if (option_toggle("disruptor.achievements_progress", &progress)) {
        mark("disruptor.achievements_progress", progress);
        reconfigure();
    }
    bool sound = settings.achievements_sound;
    if (option_toggle("disruptor.achievements_sound", &sound)) {
        mark("disruptor.achievements_sound", sound);
        reconfigure();
    }

    ImGui::SeparatorText("When the game starts");
    bool encore = settings.achievements_encore;
    if (option_toggle("disruptor.achievements_encore", &encore)) {
        mark("disruptor.achievements_encore", encore);
        reconfigure();
    }
    bool unofficial = settings.achievements_unofficial;
    if (option_toggle("disruptor.achievements_unofficial", &unofficial)) {
        mark("disruptor.achievements_unofficial", unofficial);
        reconfigure();
    }

    ImGui::SeparatorText("Achievements");
    list_rows();
}

}  // namespace disruptor::menu
