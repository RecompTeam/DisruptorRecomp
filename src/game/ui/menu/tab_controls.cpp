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

    bool number_keys = disruptor_number_keys_enabled() != 0;
    if (option_toggle("disruptor.number_keys", &number_keys)) {
        disruptor_number_keys_set_enabled(number_keys ? 1 : 0);
        mark("disruptor.number_keys", number_keys ? 1 : 0);
    }
    const bool picking = applies("disruptor.psionic_keys_use");
    if (!picking) ImGui::BeginDisabled();
    bool use = disruptor_psionic_keys_use_enabled() != 0;
    if (option_toggle("disruptor.psionic_keys_use", &use)) {
        disruptor_psionic_keys_use_set_enabled(use ? 1 : 0);
        mark("disruptor.psionic_keys_use", use ? 1 : 0);
    }
    if (!picking) ImGui::EndDisabled();

    bool wheels = disruptor_selection_wheels_enabled() != 0;
    if (option_toggle("disruptor.selection_wheels", &wheels)) {
        disruptor_selection_wheels_set_enabled(wheels ? 1 : 0);
        mark("disruptor.selection_wheels", wheels ? 1 : 0);
    }
    draw_status_badge("EXPERIMENTAL", ImVec4(1.0f, 0.73f, 0.25f, 1.0f));
    ImGui::TextDisabled("Change single keys in the launcher, on its Controls page");

    ImGui::SeparatorText("Mouse look");
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

    const bool turned = applies("disruptor.invert_horizontal") && applies("disruptor.horizontal_sensitivity");
    if (!turned) ImGui::BeginDisabled();
    bool invert_x = disruptor_mouse_invert_horizontal() != 0;
    if (option_toggle("disruptor.invert_horizontal", &invert_x)) {
        disruptor_mouse_set_invert_horizontal(invert_x ? 1 : 0);
        mark_invert_x(invert_x);
    }
    float sensitivity =
        static_cast<float>(disruptor_mouse_horizontal_sensitivity());
    if (option_slider("disruptor.horizontal_sensitivity", &sensitivity)) {
        const double applied =
            disruptor_mouse_set_horizontal_sensitivity(sensitivity);
        mark_sensitivity(applied);
    }
    if (!turned) ImGui::EndDisabled();
    ImGui::Unindent();

    const bool can_look = applies("disruptor.vertical_look");
    if (!can_look) ImGui::BeginDisabled();
    bool vertical_look = disruptor_mouse_vertical_look_enabled() != 0;
    if (option_toggle("disruptor.vertical_look", &vertical_look)) {
        disruptor_mouse_set_vertical_look_enabled(vertical_look ? 1 : 0);
        mark_vertical_look(vertical_look);
    }
    if (!can_look) ImGui::EndDisabled();

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

    const bool aiming = applies("disruptor.auto_aim");
    if (!aiming) ImGui::BeginDisabled();
    bool auto_aim = disruptor_mouse_auto_aim() != 0;
    if (option_toggle("disruptor.auto_aim", &auto_aim)) {
        disruptor_mouse_set_auto_aim(auto_aim ? 1 : 0);
        mark("disruptor.auto_aim", auto_aim);
    }
    if (!aiming) ImGui::EndDisabled();

    const bool dotted = applies("disruptor.crosshair");
    if (!dotted) ImGui::BeginDisabled();
    bool crosshair = disruptor_mouse_crosshair() != 0;
    if (option_toggle("disruptor.crosshair", &crosshair)) {
        disruptor_mouse_set_crosshair(crosshair ? 1 : 0);
        mark("disruptor.crosshair", crosshair);
    }
    if (!dotted) ImGui::EndDisabled();

    const bool styled = applies("disruptor.crosshair_shape") && applies("disruptor.crosshair_size") &&
                        applies("disruptor.crosshair_colour") && applies("disruptor.crosshair_opacity");
    if (!styled) ImGui::BeginDisabled();
    ImGui::Indent();
    int shape = crosshair_shape(), size = crosshair_size(), colour = crosshair_colour(), opacity = crosshair_opacity();
    ImGui::SetNextItemWidth(220.0f);
    const bool shaped = option_list("disruptor.crosshair_shape", &shape);
    if (shaped) mark("disruptor.crosshair_shape", shape);
    ImGui::SetNextItemWidth(260.0f);
    const bool sized = option_slider("disruptor.crosshair_size", &size);
    if (sized) mark("disruptor.crosshair_size", size);
    ImGui::SetNextItemWidth(220.0f);
    const bool coloured = option_list("disruptor.crosshair_colour", &colour);
    if (coloured) mark("disruptor.crosshair_colour", colour);
    ImGui::SetNextItemWidth(260.0f);
    const bool faded = option_slider("disruptor.crosshair_opacity", &opacity);
    if (faded) mark("disruptor.crosshair_opacity", opacity);
    if (shaped || sized || coloured || faded) set_crosshair_style(shape, size, colour, opacity);
    ImGui::Unindent();
    if (!styled) ImGui::EndDisabled();

    const bool tilted = looking && disruptor_mouse_vertical_pitch() != 0.0;
    if (!tilted) ImGui::BeginDisabled();
    if (ImGui::Button("Recenter vertical view"))
        disruptor_mouse_recenter_vertical();
    if (!tilted) ImGui::EndDisabled();
    ImGui::Unindent();

    ImGui::SeparatorText("Gamepad");
    if (const char* pad = psx_input_pad_name(0)) ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.45f, 1.0f), "Gamepad in use: %s", pad);
    else ImGui::TextDisabled("No gamepad in use");
    disruptor::sticks::Tuning sticks = disruptor::sticks::tuning();
    int layout = static_cast<int>(sticks.layout);
    ImGui::SetNextItemWidth(260.0f);
    if (option_list("disruptor.stick_layout", &layout)) {
        sticks.layout = static_cast<disruptor::sticks::Layout>(layout);
        disruptor::sticks::tune(sticks);
        mark("disruptor.stick_layout", layout);
    }
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    ImGui::Indent();
    const bool turning = applies("disruptor.look_speed") && applies("disruptor.look_deadzone");
    if (!turning) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.look_speed", &sticks.look_speed)) {
        disruptor::sticks::tune(sticks);
        mark("disruptor.look_speed", sticks.look_speed);
    }
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("disruptor.look_deadzone", &sticks.look_deadzone)) {
        disruptor::sticks::tune(sticks);
        mark("disruptor.look_deadzone", sticks.look_deadzone);
    }
    if (!turning) ImGui::EndDisabled();
    const bool tilting = applies("disruptor.invert_look");
    if (!tilting) ImGui::BeginDisabled();
    if (option_toggle("disruptor.invert_look", &sticks.invert_look)) {
        disruptor::sticks::tune(sticks);
        mark("disruptor.invert_look", sticks.invert_look ? 1 : 0);
    }
    if (!tilting) ImGui::EndDisabled();
    ImGui::Unindent();
    int deadzone = static_cast<int>(std::lround(psx_input_deadzone() * 100.0 / 32767.0));
    ImGui::SetNextItemWidth(260.0f);
    if (option_slider("controller.deadzone", &deadzone)) set_deadzone_live(deadzone);
    draw_status_badge("LIVE", ImVec4(0.35f, 0.90f, 0.45f, 1.0f));
    if (ImGui::TreeNode("Buttons")) {
#if defined(PSX_SDL3)
        const disruptor::settings::PadFamily family = disruptor::settings::family_shown(g_menu.pad_in_use);
#else
        const disruptor::settings::PadFamily family = disruptor::settings::PadFamily::xbox;
#endif
        if (ImGui::BeginTable("pad buttons", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
            for (const char* title : {disruptor::settings::family_name(family), "In the game"}) ImGui::TableSetupColumn(title);
            ImGui::TableHeadersRow();
            for (const disruptor::settings::StickUse& stick : disruptor::settings::stick_uses(layout)) {
                ImGui::TableNextRow();
                for (const char* text : {stick.where, stick.use}) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(text);
                }
            }
            for (const disruptor::settings::PadAction& action : disruptor::settings::pad_actions) {
                const char* source = psx_input_pad_source(action.field);
                const disruptor::settings::PadSource* known = source ? disruptor::settings::pad_source(source) : nullptr;
                const char* shown = source ? source : "-";
                ImGui::TableNextRow();
                for (const char* text : {known ? disruptor::settings::printed(*known, family) : shown, action.action}) {
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(text);
                }
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("The launcher's Controls page changes them");
        ImGui::TreePop();
    }
}

}  // namespace disruptor::menu
