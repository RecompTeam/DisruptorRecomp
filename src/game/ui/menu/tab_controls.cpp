#include "game/ui/menu/menu.h"

namespace disruptor::menu {

void draw_controls_tab() {
    ImGui::SeparatorText("Scheme");
    int scheme = disruptor_modern_controls_enabled() != 0 ? 1 : 0;
    if (option_list("disruptor.modern_controls", &scheme)) {
        set_modern_controls_live(scheme == 1);
        mark_modern_controls(scheme == 1);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    bool wheels = disruptor_selection_wheels_enabled() != 0;
    if (option_toggle("disruptor.selection_wheels", &wheels)) {
        disruptor_selection_wheels_set_enabled(wheels ? 1 : 0);
        mark("disruptor.selection_wheels", wheels ? 1 : 0);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::TextDisabled("Change single keys in the launcher, on its Controls page.");

    ImGui::SeparatorText("Mouse");
    bool mouse_aim = disruptor_mouse_aim_enabled() != 0;
    if (option_toggle("disruptor.mouse_aim", &mouse_aim)) {
        disruptor_mouse_aim_set_enabled(mouse_aim ? 1 : 0);
        mark_mouse_aim(mouse_aim);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));

    ImGui::Indent();
    const bool turn_applies = applies("disruptor.high_precision_camera");
    bool smooth = disruptor_high_precision_camera_enabled() != 0;
    if (!turn_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.high_precision_camera", &smooth)) {
        apply_smooth_turn(smooth);
        mark_smooth_turn(smooth);
    }
    if (!turn_applies) ImGui::EndDisabled();
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::Unindent();

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

    ImGui::SeparatorText("Vertical look");
    bool vertical_look = disruptor_mouse_vertical_look_enabled() != 0;
    if (option_toggle("disruptor.vertical_look", &vertical_look)) {
        disruptor_mouse_set_vertical_look_enabled(vertical_look ? 1 : 0);
        mark_vertical_look(vertical_look);
    }
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));

    ImGui::Indent();
    const bool looking = applies("disruptor.invert_vertical");
    if (!looking) ImGui::BeginDisabled();
    bool invert_y = disruptor_mouse_invert_vertical() != 0;
    if (option_toggle("disruptor.invert_vertical", &invert_y)) {
        disruptor_mouse_set_invert_vertical(invert_y ? 1 : 0);
        mark_invert_y(invert_y);
    }
    if (!looking) ImGui::EndDisabled();

    const bool separable = applies("disruptor.separate_vertical_speed");
    if (!separable) ImGui::BeginDisabled();
    bool separate = disruptor_mouse_vertical_follows_horizontal() == 0;
    if (option_toggle("disruptor.separate_vertical_speed", &separate)) {
        disruptor_mouse_set_vertical_follows_horizontal(separate ? 0 : 1);
        mark_separate_vertical(separate);
    }
    if (!separable) ImGui::EndDisabled();

    const bool own_speed = applies("disruptor.vertical_sensitivity");
    if (!own_speed) ImGui::BeginDisabled();
    float vertical_sensitivity =
        static_cast<float>(disruptor_mouse_vertical_sensitivity());
    if (option_slider("disruptor.vertical_sensitivity", &vertical_sensitivity)) {
        const double applied =
            disruptor_mouse_set_vertical_sensitivity(vertical_sensitivity);
        mark_vertical_sensitivity(applied);
    }
    if (!own_speed) ImGui::EndDisabled();

    const bool tilted = vertical_look && disruptor_mouse_vertical_pitch() != 0.0;
    if (!tilted) ImGui::BeginDisabled();
    if (ImGui::Button("Recenter vertical view"))
        disruptor_mouse_recenter_vertical();
    if (!tilted) ImGui::EndDisabled();
    ImGui::Unindent();

    ImGui::SeparatorText("Gamepad");
    if (const char* pad = psx_input_pad_name(0)) ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "Gamepad in use: %s", pad);
    else ImGui::TextDisabled("No gamepad in use.");
    int deadzone = static_cast<int>(std::lround(psx_input_deadzone() * 100.0 / 32767.0));
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("controller.deadzone", &deadzone)) set_deadzone_live(deadzone);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (ImGui::TreeNode("Buttons")) {
        if (ImGui::BeginTable("pad buttons", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            for (const char* title : {"Xbox", "PlayStation", "In the game"}) ImGui::TableSetupColumn(title);
            ImGui::TableHeadersRow();
            ImGui::TableNextRow();
            for (const char* text : {"Left stick or D-pad", "Left stick or D-pad", "Move and turn"}) {
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(text);
            }
            for (const disruptor::settings::PadAction& action : disruptor::settings::pad_actions) {
                const char* source = psx_input_pad_source(action.field);
                const disruptor::settings::PadSource* known = source ? disruptor::settings::pad_source(source) : nullptr;
                const char* shown = source ? source : "-";
                ImGui::TableNextRow();
                for (const char* text : {known ? known->xbox : shown, known ? known->playstation : shown, action.action}) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(text);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("The launcher's Controls page changes them.");
        ImGui::TreePop();
    }
}

}  // namespace disruptor::menu
