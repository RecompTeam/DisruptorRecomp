/*
 * The menu controls for Disruptor's in-between frames: the switch and the rate.
 * Built only together with the frame interpolator.
 */

#include "game/frame/present_rate.h"

#include "game/ui/hint.h"

#include "host_ui.h"
#include "imgui.h"

#include <cstdint>
#include <cstdlib>

extern "C" void gl_renderer_interpolation_diag(int *enabled, int *suspended, int *history_frames,
                                               double *host_hz, double *target_hz, uint64_t *swaps);

int disruptor_present_rate_enable(int enabled) {
    int target = 0, blend = 0;
    psx_host_video_get_interpolation(nullptr, &target, &blend);
    return psx_host_video_set_interpolation(enabled ? 1 : 0, target, blend) != 0;
}

int disruptor_present_rate_switch(const char *label, const char *hint, int available, int *enabled) {
    int on = 0, changed = 0;
    psx_host_video_get_interpolation(&on, nullptr, nullptr);
    bool checked = on != 0;
    const bool locked = !available && !checked;
    if (locked) ImGui::BeginDisabled();
    const bool clicked = ImGui::Checkbox(label, &checked);
    disruptor::ui::hint(hint);
    if (clicked && disruptor_present_rate_enable(checked ? 1 : 0)) {
        if (enabled) *enabled = checked ? 1 : 0;
        changed = 1;
    }
    if (locked) ImGui::EndDisabled();
    if (locked) ImGui::TextDisabled("Enable exact geometry before in-between frames.");
    return changed;
}

int disruptor_present_rate_apply(int frames_per_second) {
    int enabled = 0, blend = 0;
    psx_host_video_get_interpolation(&enabled, nullptr, &blend);
    /* The rate never switches in-between frames on: that is the switch's business. */
    return enabled && psx_host_video_set_interpolation(1, frames_per_second, blend) != 0;
}

int disruptor_present_rate_control(const char *label, const char *hint, int *accepted) {
    int enabled = 0, target = 0, changed = 0;
    double display_hz = 0.0;
    psx_host_video_get_interpolation(&enabled, &target, nullptr);
    gl_renderer_interpolation_diag(nullptr, nullptr, nullptr, &display_hz, nullptr, nullptr);
    const int top = disruptor_present_rate_top(display_hz);
    int shown = disruptor_present_rate_shown(target, top);
    if (!enabled) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    const bool moved = ImGui::SliderInt(label, &shown, kDisruptorPresentRateLowest, top, "%d FPS",
                                        ImGuiSliderFlags_AlwaysClamp);
    disruptor::ui::hint(hint);
    if (moved && disruptor_present_rate_apply(shown)) {
        if (accepted) *accepted = shown;
        changed = 1;
    }
    if (!enabled) ImGui::EndDisabled();
    if (!enabled)
        ImGui::TextDisabled("In-between frames are off.");
    else if (const char *launcher = std::getenv("PSX_FRAME_INTERPOLATION_FPS");
             launcher && disruptor_present_rate_accepted(std::atoi(launcher)))
        ImGui::TextDisabled(
            "Pictures shown per second. Applied now and saved, but "
            "PSX_FRAME_INTERPOLATION_FPS of the launcher replaces it at the next start.");
    else
        ImGui::TextDisabled(
            "Pictures shown per second. The top is the display's refresh rate, rounded, or 320 when "
            "that rate is lower, not known or outside the host's range. Applied now and saved.");
    return changed;
}
