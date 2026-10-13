#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_controls_tab() {
    bool mouse_aim = disruptor_mouse_aim_enabled() != 0;
    if (option_toggle("disruptor.mouse_aim", &mouse_aim)) {
        disruptor_mouse_aim_set_enabled(mouse_aim ? 1 : 0);
        mark_mouse_aim(mouse_aim);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool modern = disruptor_modern_controls_enabled() != 0;
    if (option_toggle("disruptor.modern_controls", &modern)) {
        set_modern_controls_live(modern);
        mark_modern_controls(modern);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    if (modern)
        ImGui::TextWrapped("Select weapon: 1 baton, 2 pistol, 3-9 and 0 remaining weapons. "
                           "Number row or keypad; only weapons you own. Q still opens the weapon list.");

    float sensitivity =
        static_cast<float>(disruptor_mouse_horizontal_sensitivity());
    if (option_slider("disruptor.horizontal_sensitivity", &sensitivity)) {
        const double applied =
            disruptor_mouse_set_horizontal_sensitivity(sensitivity);
        mark_sensitivity(applied);
    }

    bool invert_x = disruptor_mouse_invert_horizontal() != 0;
    if (option_toggle("disruptor.invert_horizontal", &invert_x)) {
        disruptor_mouse_set_invert_horizontal(invert_x ? 1 : 0);
        mark_invert_x(invert_x);
    }

    const bool turn_applies = applies("disruptor.high_precision_camera");
    bool smooth = disruptor_high_precision_camera_enabled() != 0;
    if (!turn_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.high_precision_camera", &smooth)) {
        apply_smooth_turn(smooth);
        mark_smooth_turn(smooth);
    }
    if (!turn_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    ImGui::SeparatorText("Vertical look");
    bool vertical_look = disruptor_mouse_vertical_look_enabled() != 0;
    if (option_toggle("disruptor.vertical_look", &vertical_look)) {
        disruptor_mouse_set_vertical_look_enabled(vertical_look ? 1 : 0);
        mark_vertical_look(vertical_look);
    }
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));

    float vertical_sensitivity =
        static_cast<float>(disruptor_mouse_vertical_sensitivity());
    if (option_slider("disruptor.vertical_sensitivity", &vertical_sensitivity)) {
        const double applied =
            disruptor_mouse_set_vertical_sensitivity(vertical_sensitivity);
        mark_vertical_sensitivity(applied);
    }

    bool invert_y = disruptor_mouse_invert_vertical() != 0;
    if (option_toggle("disruptor.invert_vertical", &invert_y)) {
        disruptor_mouse_set_invert_vertical(invert_y ? 1 : 0);
        mark_invert_y(invert_y);
    }

    const double pitch_units = disruptor_mouse_vertical_pitch();
    if (pitch_units == 0.0) ImGui::BeginDisabled();
    if (ImGui::Button("Recenter vertical view"))
        disruptor_mouse_recenter_vertical();
    if (pitch_units == 0.0) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Text("Requested pitch: %+.2f deg", pitch_units * (360.0 / 256.0));
    ImGui::TextDisabled(
        "Range: +/-30.94 degrees. The original game has no pitch state.");
    ImGui::Separator();
    ImGui::TextWrapped(
        "The menu releases relative mouse capture while open and restores the "
        "previous state when closed. Gameplay input is neutralised while you "
        "interact with the menu.");
    ImGui::Text("Capture before opening: %s",
                g_menu.restore_mouse_capture ? "yes" : "no");
}

}  // namespace disruptor::menu
