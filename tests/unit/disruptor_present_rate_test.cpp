#include "../../src/game/frame/present_rate.h"
#include "imgui.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* The host and the menu toolkit as the control sees them. */
struct Host {
    int enabled = 1;
    int target = 60;
    int blend = 2;
    int accepts = 1;
    int sets = 0;
    int set_enabled = -1;
    int set_target = -1;
    int set_blend = -1;
    double display_hz = 143.86;
};
struct Slider {
    std::string label;
    bool moved = false;
    int moved_to = 0;
    int shown = -1;
    int lowest = -1;
    int highest = -1;
    int disabled = 0;
    int disabled_when_drawn = -1;
    std::string text;
};
struct Switch {
    std::string label;
    bool clicked = false;
    bool shown = false;
    int disabled_when_drawn = -1;
};

/* The launcher's override decides which sentence the control shows, so every case sets it itself. */
void launcher_override(const char *value) {
#ifdef _WIN32
    _putenv_s("PSX_FRAME_INTERPOLATION_FPS", value);
#else
    if (value[0]) setenv("PSX_FRAME_INTERPOLATION_FPS", value, 1); else unsetenv("PSX_FRAME_INTERPOLATION_FPS");
#endif
}
Host g_host;
Slider g_slider;
Switch g_switch;

}  // namespace

void psx_host_video_get_interpolation(int *enabled, int *target_fps, int *blend) {
    if (enabled) *enabled = g_host.enabled;
    if (target_fps) *target_fps = g_host.target;
    if (blend) *blend = g_host.blend;
}

int psx_host_video_set_interpolation(int enabled, int target_fps, int blend) {
    ++g_host.sets;
    g_host.set_enabled = enabled;
    g_host.set_target = target_fps;
    g_host.set_blend = blend;
    return g_host.accepts;
}

extern "C" void gl_renderer_interpolation_diag(int *, int *, int *, double *host_hz, double *, std::uint64_t *) {
    if (host_hz) *host_hz = g_host.display_hz;
}

std::string g_hint;

namespace ImGui {
void BeginDisabled() { ++g_slider.disabled; }
bool BeginItemTooltip() { return true; }
bool Checkbox(const char *label, bool *value) {
    g_switch.label = label;
    g_switch.shown = *value;
    g_switch.disabled_when_drawn = g_slider.disabled;
    const bool taken = g_switch.clicked && g_slider.disabled == 0;
    if (taken) *value = !*value;
    return taken;
}
void EndDisabled() { --g_slider.disabled; }
void EndTooltip() {}
float GetFontSize() { return 13.0f; }
void PopTextWrapPos() {}
void PushTextWrapPos(float) {}
void SetNextItemWidth(float) {}
bool SliderInt(const char *label, int *value, int lowest, int highest, const char *, int) {
    g_slider.label = label;
    g_slider.shown = *value;
    g_slider.disabled_when_drawn = g_slider.disabled;
    g_slider.lowest = lowest;
    g_slider.highest = highest;
    if (g_slider.moved && g_slider.disabled == 0) *value = g_slider.moved_to;
    return g_slider.moved && g_slider.disabled == 0;
}
void TextDisabled(const char *text, ...) { g_slider.text = text; }
void TextUnformatted(const char *text, const char *end) { g_hint.assign(text, end ? end : text + std::strlen(text)); }
void PopStyleColor(int) {}
void PushStyleColor(int, const ImVec4 &) {}
void SameLine(float, float) {}
}  // namespace ImGui

#include "../../src/game/frame/present_rate.cpp"

namespace {

void test_the_rate_reaches_the_host() {
    g_host = Host{};
    expect(disruptor_present_rate_apply(45) == 1 && g_host.sets == 1 && g_host.set_enabled == 1 &&
               g_host.set_target == 45 && g_host.set_blend == 2,
           "a rate goes to the host as it was asked, with the blend mode the host had");
    g_host = Host{};
    g_host.accepts = 0;
    expect(disruptor_present_rate_apply(45) == 0 && g_host.sets == 1, "a rate the host refuses is reported as refused");
    g_host = Host{};
    g_host.enabled = 0;
    expect(disruptor_present_rate_apply(45) == 0 && g_host.sets == 0, "in-between frames that are off are not switched on");
}

void test_the_control_hands_over_what_the_user_chose() {
    int accepted = -7;
    launcher_override("");
    g_host = Host{};
    g_slider = Slider{};
    expect(disruptor_present_rate_control("Rate", "Rate hint", &accepted) == 0 && accepted == -7 && g_host.sets == 0,
           "a control nobody touched changes nothing");
    expect(g_slider.label == "Rate" && g_hint == "Rate hint", "the control is drawn under the label and with the hint it was given");
    expect(g_slider.shown == 60 && g_slider.lowest == 30 && g_slider.highest == 320 && g_slider.disabled == 0 &&
               g_slider.disabled_when_drawn == 0,
           "it shows the host's target between 30 and 320 on a display slower than that");
    expect(g_slider.text.find("or 320 when that rate is lower, not known or outside the host's range") != std::string::npos &&
               g_slider.text.find("launcher") == std::string::npos,
           "and says where its top comes from");
    launcher_override("90");
    (void)disruptor_present_rate_control("Rate", "Rate hint", &accepted);
    expect(g_slider.text.find("PSX_FRAME_INTERPOLATION_FPS of the launcher replaces it") != std::string::npos &&
               g_slider.text.find("not known") == std::string::npos,
           "with the launcher's override set it says the override wins at the next start");
    for (const char *refused : {"29", "1001", "-5"}) {
        launcher_override(refused);
        (void)disruptor_present_rate_control("Rate", "Rate hint", &accepted);
        expect(g_slider.text.find("launcher") == std::string::npos,
               "a launcher rate the start refuses is not announced as replacing the saved one");
    }
    launcher_override("");
    g_host.display_hz = 1000.6;
    (void)disruptor_present_rate_control("Rate", "Rate hint", &accepted);
    expect(g_slider.highest == 320 && g_slider.text.find("outside the host's range") != std::string::npos,
           "a refresh rate past the host's range gives 320, as the sentence says");
    g_host.display_hz = 359.6;
    (void)disruptor_present_rate_control("Rate", "Rate hint", &accepted);
    expect(g_slider.highest == 360, "a display faster than 320 gives its own rounded rate");
    g_host.display_hz = 143.86;

    g_slider = Slider{};
    g_slider.moved = true;
    g_slider.moved_to = 30;
    expect(disruptor_present_rate_control("Rate", "Rate hint", &accepted) == 1 && accepted == 30 && g_host.set_target == 30 && g_host.set_blend == 2,
           "a moved control sets the chosen rate and reports it");
    g_host = Host{};
    g_host.accepts = 0;
    accepted = -7;
    expect(disruptor_present_rate_control("Rate", "Rate hint", &accepted) == 0 && accepted == -7 && g_host.sets == 1,
           "a rate the host refuses is not reported as chosen");
    g_host = Host{};
    expect(disruptor_present_rate_control("Rate", "Rate hint", nullptr) == 1 && g_host.set_target == 30, "the caller may ask for no report");

    g_host = Host{};
    g_host.display_hz = 59.94;
    g_host.target = 0;
    g_slider = Slider{};
    (void)disruptor_present_rate_control("Rate", "Rate hint", &accepted);
    expect(g_slider.highest == 320 && g_slider.shown == 320, "on a 60 Hz display the top is 320 and a target of 0 is shown as the top");

    g_host = Host{};
    g_host.enabled = 0;
    g_slider = Slider{};
    g_slider.moved = true;
    g_slider.moved_to = 45;
    accepted = -7;
    expect(disruptor_present_rate_control("Rate", "Rate hint", &accepted) == 0 && accepted == -7 && g_host.sets == 0 && g_slider.disabled == 0 &&
               g_slider.disabled_when_drawn == 1,
           "with in-between frames off the control is drawn disabled and sets nothing");
    expect(g_slider.text.find("In-between frames are off") != std::string::npos, "and says why");
}

void test_the_switch_turns_the_frames_on_and_off() {
    g_host = Host{};
    g_host.enabled = 0;
    expect(disruptor_present_rate_enable(1) == 1 && g_host.sets == 1 && g_host.set_enabled == 1 &&
               g_host.set_target == 60 && g_host.set_blend == 2,
           "switching on keeps the rate and the blend mode the host had");
    g_host = Host{};
    expect(disruptor_present_rate_enable(0) == 1 && g_host.set_enabled == 0 && g_host.set_target == 60 &&
               g_host.set_blend == 2,
           "switching off does too");
    g_host = Host{};
    g_host.accepts = 0;
    expect(disruptor_present_rate_enable(1) == 0 && g_host.sets == 1, "a switch the host refuses is reported as refused");

    int state = -7;
    g_host = Host{};
    g_host.enabled = 0;
    g_slider = Slider{};
    g_switch = Switch{};
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 1, &state) == 0 && g_switch.label == "Switch" &&
               g_hint == "Switch hint",
           "the switch is drawn under the label and with the hint it was given");
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 1, &state) == 0 && state == -7 && g_host.sets == 0 && !g_switch.shown &&
               g_switch.disabled_when_drawn == 0,
           "a switch nobody touched changes nothing and shows the host's state");
    g_switch.clicked = true;
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 1, &state) == 1 && state == 1 && g_host.set_enabled == 1,
           "a click on a switch that is off turns the frames on and reports it");
    g_host = Host{};
    state = -7;
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 1, &state) == 1 && state == 0 && g_host.set_enabled == 0 && g_switch.shown,
           "a click on a switch that is on turns them off and reports it");
    g_host = Host{};
    g_host.enabled = 0;
    g_host.accepts = 0;
    state = -7;
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 1, &state) == 0 && state == -7 && g_host.sets == 1,
           "a switch the host refuses is not reported as changed");
    g_host = Host{};
    g_host.enabled = 0;
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 1, nullptr) == 1 && g_host.set_enabled == 1, "the caller may ask for no report");

    g_host = Host{};
    g_host.enabled = 0;
    g_slider = Slider{};
    state = -7;
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 0, &state) == 0 && state == -7 && g_host.sets == 0 && g_slider.disabled == 0 &&
               g_switch.disabled_when_drawn == 1,
           "without exact geometry the switch is drawn disabled and sets nothing");
    expect(g_slider.text.find("Enable exact geometry") != std::string::npos, "and says what it waits for");
    g_host = Host{};
    g_slider = Slider{};
    state = -7;
    expect(disruptor_present_rate_switch("Switch", "Switch hint", 0, &state) == 1 && state == 0 && g_host.set_enabled == 0 &&
               g_switch.disabled_when_drawn == 0 && g_slider.text.empty(),
           "frames that are on can be switched off without exact geometry too");
}

}  // namespace

int main() {
    test_the_rate_reaches_the_host();
    test_the_control_hands_over_what_the_user_chose();
    test_the_switch_turns_the_frames_on_and_off();
    expect(disruptor_present_rate_top(360.0) == 360 && disruptor_present_rate_top(479.86) == 480 &&
               disruptor_present_rate_top(320.6) == 321,
           "the control reaches the display's refresh rate, rounded");
    expect(disruptor_present_rate_top(59.94) == 320 && disruptor_present_rate_top(270.0) == 320 &&
               disruptor_present_rate_top(320.0) == 320 && disruptor_present_rate_top(320.4) == 320,
           "and at least 320 on a slower display");
    expect(disruptor_present_rate_top(0.0) == 320 && disruptor_present_rate_top(-1.0) == 320 &&
               disruptor_present_rate_top(29.9) == 320 && disruptor_present_rate_top(1000.6) == 320 &&
               disruptor_present_rate_top(1000.0) == 1000,
           "a refresh rate that is unknown or out of the host's range gives the least top");

    expect(disruptor_present_rate_shown(0, 270) == 270 && disruptor_present_rate_shown(-1, 144) == 144,
           "a target that follows the display is shown as the display's rate");
    expect(disruptor_present_rate_shown(135, 270) == 135 && disruptor_present_rate_shown(30, 120) == 30 &&
               disruptor_present_rate_shown(120, 120) == 120,
           "a target inside the range is shown as it is");
    expect(disruptor_present_rate_shown(29, 120) == 30 && disruptor_present_rate_shown(1, 120) == 30,
           "a target under the range is shown at its lower end");
    expect(disruptor_present_rate_shown(271, 270) == 270 && disruptor_present_rate_shown(144, 120) == 120,
           "a target over the display's rate is shown at the top");
    expect(disruptor_present_rate_accepted(0) && disruptor_present_rate_accepted(30) &&
               disruptor_present_rate_accepted(1000) && !disruptor_present_rate_accepted(-1) &&
               !disruptor_present_rate_accepted(29) && !disruptor_present_rate_accepted(1001),
           "the start takes 0 or a rate from 30 to 1000");
    expect(kDisruptorPresentRateLowest == 30 && kDisruptorPresentRateLeastTop == 320,
           "the range starts at the retail 30 and reaches at least 320");

    if (g_failures) return 1;
    std::cout << "Disruptor present rate tests passed\n";
    return 0;
}
