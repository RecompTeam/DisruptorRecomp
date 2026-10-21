#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_language_tab() {
    ImGui::TextWrapped("Disc of another region: %s", g_language_disc.empty() ? "none, the game is in English" : g_language_disc.c_str());
    if (ImGui::Button("Choose a disc image")) (void)psx_host_pick_disc_image_begin();
    ImGui::SameLine();
    if (ImGui::Button("English") && !g_language_disc.empty()) mark_language_disc(std::string());
    ImGui::SameLine();
    if (ImGui::Button("Restart now") && !psx_netplay_active() && flush_preferences() && disruptor_restart_arm()) {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    }
    ImGui::TextDisabled("Movies, speech and words come from that disc at the next start\nThe French, German and Japanese discs are known");
    ImGui::TextDisabled("The Japanese disc gives movies, speech and hints: its menus are in English on the disc itself");
}

}  // namespace disruptor::menu
