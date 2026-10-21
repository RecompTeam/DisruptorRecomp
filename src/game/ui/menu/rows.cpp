#include "game/ui/menu/menu.h"

namespace disruptor::menu {

namespace {

bool prerequisite_on(const Option &option) {
    const std::string_view key(option.key);
    if (key == "disruptor.geometry_correction")
        return gpu_geometry_correction_enabled() != 0;
    if (key == "disruptor.mouse_aim") return disruptor_mouse_aim_enabled() != 0;
    if (key == "disruptor.ambient_occlusion") return gl_renderer_ssao() != 0;
    if (key == "disruptor.smooth_fade") return gl_renderer_smooth_fade() != 0;
    if (key == "disruptor.vertical_look") return disruptor_mouse_vertical_look_enabled() != 0;
    if (key == "disruptor.number_keys") return disruptor_number_keys_enabled() != 0;
    if (key == "disruptor.separate_vertical_speed")
        return disruptor_mouse_vertical_follows_horizontal() == 0;
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
    // Shown as a multiple of what a new installation has, as the launcher shows it.
    const float unit = static_cast<float>(option.initial) / 1000.0f;
    float times = *value / unit;
    const bool moved = ImGui::SliderFloat(
        option.label, &times, option.lowest / 1000.0f / unit,
        option.highest / 1000.0f / unit, times < 1.0f ? "%.2fx" : "%.1fx",
        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);
    if (moved) *value = times * unit;
    disruptor::ui::hint(option.hint);
    return moved;
}

bool option_list(const char *key, int *index, size_t skipped) {
    const Option &option = *find_option(key);
    const auto offered = option.choices.first(std::min(option.choices.size(), static_cast<size_t>(option.highest + 1)));
    const auto shown = offered.subspan(std::min(skipped, offered.size()));
    const bool moved = ImGui::Combo(option.label, index, shown.data(),
                                    static_cast<int>(shown.size()));
    disruptor::ui::hint(option.hint);
    if (option.notice) ImGui::TextColored(ImVec4(0.878f, 0.643f, 0.227f, 1.0f), "%s", option.notice);
    return moved;
}

void draw_status_badge(const char *label, const ImVec4 &colour) {
    ImGui::SameLine();
    ImGui::TextColored(colour, "[%s]", label);
}

}  // namespace disruptor::menu
