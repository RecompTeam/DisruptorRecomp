#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_audio_tab() {
    ImGui::SeparatorText("Audio output");
    int volume = psx_host_audio_get_master_volume();
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("audio.master_volume", &volume) &&
        psx_host_audio_set_master_volume(volume)) {
        mark_master_volume(volume);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    int bass = disruptor_bass_cut();
    ImGui::SetNextItemWidth(260.0f);
    if (option_list("disruptor.bass_cut", &bass)) {
        disruptor_bass_cut_set(bass);
        mark_bass_cut(bass);
    }

    bool in_background = psx_host_audio_get_mute_in_background() != 0;
    if (option_toggle("disruptor.mute_in_background", &in_background) &&
        psx_host_audio_set_mute_in_background(in_background ? 1 : 0)) {
        mark_mute_in_background(in_background);
    }

    bool muted = psx_host_audio_get_muted() != 0;
    if (option_toggle("audio.muted", &muted) &&
        psx_host_audio_set_muted(muted ? 1 : 0)) {
        mark_audio_muted(muted);
    }
    ImGui::TextDisabled(
        "Host-side output only: game audio state and saves are unchanged\n"
        "Unmuting restores the selected volume");
}

}  // namespace disruptor::menu
