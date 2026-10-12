#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

bool prerequisite_on(const Option &option) {
    const std::string_view key(option.key);
    if (key == "disruptor.geometry_correction")
        return gpu_geometry_correction_enabled() != 0;
    if (key == "disruptor.mouse_aim") return disruptor_mouse_aim_enabled() != 0;
    if (key == "disruptor.ambient_occlusion") return gl_renderer_ssao() != 0;
    return false;
}

}  // namespace

bool applies(const char *key) {
    return disruptor::settings::applies(*find_option(key), prerequisite_on);
}

bool option_toggle(const char *key, bool *value) {
    const Option &option = *find_option(key);
    const bool moved = ImGui::Checkbox(option.label, value);
    disruptor::ui::hint(option.hint);
    return moved;
}

bool option_slider(const char *key, int *value) {
    const Option &option = *find_option(key);
    const char *format = option.unit == Unit::percent ? "%d%%" : "%d";
    const bool moved =
        ImGui::SliderInt(option.label, value, option.lowest, option.highest,
                         format, ImGuiSliderFlags_AlwaysClamp);
    disruptor::ui::hint(option.hint);
    return moved;
}

bool option_slider(const char *key, float *value) {
    const Option &option = *find_option(key);
    const bool moved = ImGui::SliderFloat(
        option.label, value, option.lowest / 1000.0f,
        option.highest / 1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    disruptor::ui::hint(option.hint);
    return moved;
}

bool option_list(const char *key, int *index, size_t skipped) {
    const Option &option = *find_option(key);
    const auto shown = option.choices.subspan(skipped);
    const bool moved = ImGui::Combo(option.label, index, shown.data(),
                                    static_cast<int>(shown.size()));
    disruptor::ui::hint(option.hint);
    return moved;
}

void draw_status_badge(const char *label, const ImVec4 &colour) {
    ImGui::SameLine();
    ImGui::TextColored(colour, "[%s]", label);
}

}  // namespace disruptor::menu
