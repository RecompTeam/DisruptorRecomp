#include "game/session/diagnostics.h"

#include "psx_sdl.h"
#include "game/frame/frame_rate.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <iterator>

extern "C" SDL_Window *sdl_window;
extern "C" void mod_register_frame_hook(void (*hook)(void));
extern "C" void gl_renderer_set_debug_messages(int enabled);
extern "C" int gl_renderer_debug_messages_active(void);
extern "C" int gl_renderer_debug_message_take(char *text, int size, unsigned *id, int *severity, int *context, unsigned *lost);
extern "C" int gpu_ws_displayed_flat(void);
extern "C" int gpu_ws_displayed_menu(void);

namespace disruptor::diagnostics {
namespace {

constexpr double pulse_seconds = 10.0;
constexpr double device_seconds = 1.0;
constexpr size_t driver_kinds = 64;
constexpr int driver_messages_a_frame = 16;  // the renderer's queue: a driver that never stops must not hold a frame

struct Session {
    bool detailed = false;
    bool asked_at_start = false;
    bool begun = false;
    bool announced = false;
    bool devices_known = false;
    double now = 0.0, last_frame = 0.0, next_pulse = 0.0, next_devices = 0.0;
    Stretch stretch;
    std::vector<std::string> devices;
    DriverMessages driver;
    uint32_t lost = 0;
};

Session g_session;
const std::chrono::steady_clock::time_point g_began = std::chrono::steady_clock::now();

void say(const std::string& text) { std::printf("disruptor: +%.1fs %s\n", g_session.now, text.c_str()); }

const char* device_name(int index) {
#if defined(PSX_SDL3)
    return SDL_GetJoystickNameForID(SDL_JoystickGetDeviceInstanceID(index));
#else
    return SDL_JoystickNameForIndex(index);
#endif
}

std::vector<std::string> devices_now() {
    std::vector<std::string> names;
    for (int index = 0, count = SDL_NumJoysticks(); index < count; ++index) {
        const char* name = device_name(index);
        names.emplace_back(name && name[0] ? name : "unnamed device");
    }
    std::sort(names.begin(), names.end());
    return names;
}

const char* driver_text(int state) {
    switch (state) {
        case DISRUPTOR_DRIVER_ON: return "on";
        case DISRUPTOR_DRIVER_NEXT_LAUNCH: return "from the next launch";
        case DISRUPTOR_DRIVER_REFUSED: return "not offered by this driver";
        default: return "off";
    }
}

Moment moment_now() {
    Moment moment;
    DisruptorFrameRateWindow window{};
    if (disruptor_frame_rate_last_window(&window)) {
        moment.game_frames = window.frames;
        moment.game_vblanks = window.vblanks;
        moment.late = window.late_frames;
    }
    moment.screen = gpu_ws_displayed_menu() ? "menu" : gpu_ws_displayed_flat() ? "flat screen" : "world";
    const SDL_WindowFlags flags = sdl_window ? SDL_GetWindowFlags(sdl_window) : 0;
    moment.focused = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    moment.minimized = (flags & SDL_WINDOW_MINIMIZED) != 0;
    return moment;
}

void take_driver_messages() {
    char text[200];
    for (int taken = 0; taken < driver_messages_a_frame; ++taken) {
        unsigned id = 0, lost = 0;
        int severity = 0, context = 0;
        const int given = gl_renderer_debug_message_take(text, sizeof text, &id, &severity, &context, &lost);
        g_session.lost += lost;
        if (!given) return;
        if (const std::string line = g_session.driver.line(id, severity, context, text); !line.empty()) say(line);
    }
}

void look_at_devices() {
    std::vector<std::string> names = devices_now();
    if (!g_session.devices_known && names.empty()) say("controllers: none");
    for (const auto& line : device_changes(g_session.devices, names)) say(line);
    g_session.devices = std::move(names);
    g_session.devices_known = true;
}

void frame_hook() { frame_at(std::chrono::duration<double>(std::chrono::steady_clock::now() - g_began).count()); }

struct Registration {
    Registration() {
        const char* forced = std::getenv("PSX_DISRUPTOR_DETAILED_LOGGING");
        if (forced && forced[0]) disruptor_diagnostics_configure(0);
        mod_register_frame_hook(frame_hook);
    }
};

Registration g_registration;

}  // namespace

void Stretch::frame(double seconds) {
    ++frames;
    waited += seconds;
    longest = std::max(longest, seconds);
}

std::string pulse_line(const Stretch& stretch, const Moment& moment) {
    char text[320];
    std::snprintf(text, sizeof text,
                  "pulse: %u frames in %.1f s, %.1f ms a frame, longest %.1f ms, game %u frames in %u vblanks with %u late, %s, %s",
                  stretch.frames, stretch.waited, stretch.frames ? stretch.waited / stretch.frames * 1000.0 : 0.0,
                  stretch.longest * 1000.0, moment.game_frames, moment.game_vblanks, moment.late, moment.screen,
                  moment.focused ? "focused" : "not focused");
    std::string line = text;
    if (moment.minimized) line += ", minimized";
    if (moment.repeated) line += ", driver messages repeated " + std::to_string(moment.repeated);
    if (moment.lost) line += ", driver messages lost " + std::to_string(moment.lost);
    return line;
}

std::vector<std::string> device_changes(const std::vector<std::string>& before, const std::vector<std::string>& now) {
    std::vector<std::string> gone, come, lines;
    std::set_difference(before.begin(), before.end(), now.begin(), now.end(), std::back_inserter(gone));
    std::set_difference(now.begin(), now.end(), before.begin(), before.end(), std::back_inserter(come));
    for (const auto& name : gone) lines.push_back("controller removed: " + name);
    for (const auto& name : come) lines.push_back("controller connected: " + name);
    return lines;
}

std::string DriverMessages::line(uint32_t id, int severity, int context, std::string_view text) {
    const bool known = said_.contains(id);
    if ((!known && said_.size() >= driver_kinds) || (known && said_[id] >= printed_of_a_kind)) {
        ++repeated_;
        return {};
    }
    ++said_[id];
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.remove_suffix(1);
    std::string line = context ? "driver (presenter context, " : "driver (main context, ";
    line += severity >= 2 ? "high, id " : "medium, id ";
    line += std::to_string(id) + "): ";
    line += text;
    return line;
}

uint32_t DriverMessages::take_repeated() {
    const uint32_t repeated = repeated_;
    repeated_ = 0;
    return repeated;
}

void frame_at(double seconds) {
    Session& session = g_session;
    const double waited = seconds - session.last_frame;
    session.now = seconds;
    session.last_frame = seconds;
    if (!session.begun) {
        session.begun = true;
        std::printf("disruptor: first frame %.1f s after the start\n", seconds);
    }
    if (!session.detailed) {
        session.announced = false;
        return;
    }
    if (!session.announced) {
        session.announced = true;
        session.devices_known = false;
        session.devices.clear();
        session.stretch = {};
        session.next_pulse = seconds + pulse_seconds;
        session.next_devices = seconds;
        say(std::string("detailed logging on, driver messages ") + driver_text(disruptor_diagnostics_driver_state()));
    } else {
        session.stretch.frame(waited);
    }
    take_driver_messages();
    if (seconds >= session.next_devices) {
        session.next_devices = seconds + device_seconds;
        look_at_devices();
    }
    if (seconds >= session.next_pulse) {
        Moment moment = moment_now();
        moment.repeated = session.driver.take_repeated();
        moment.lost = session.lost;
        say(pulse_line(session.stretch, moment));
        session.stretch = {};
        session.lost = 0;
        session.next_pulse = seconds + pulse_seconds;
    }
}

}  // namespace disruptor::diagnostics

using disruptor::diagnostics::g_session;

extern "C" {

void disruptor_diagnostics_configure(int saved) {
    const char* forced = std::getenv("PSX_DISRUPTOR_DETAILED_LOGGING");
    g_session.detailed = forced && forced[0] ? forced[0] != '0' : saved != 0;
    g_session.asked_at_start = g_session.detailed;
    gl_renderer_set_debug_messages(g_session.detailed ? 1 : 0);
}

int disruptor_diagnostics_detailed(void) { return g_session.detailed ? 1 : 0; }

void disruptor_diagnostics_set_detailed(int enabled) { g_session.detailed = enabled != 0; }

int disruptor_diagnostics_driver_state(void) {
    if (!g_session.detailed) return DISRUPTOR_DRIVER_OFF;
    if (gl_renderer_debug_messages_active()) return DISRUPTOR_DRIVER_ON;
    return g_session.asked_at_start ? DISRUPTOR_DRIVER_REFUSED : DISRUPTOR_DRIVER_NEXT_LAUNCH;
}

void disruptor_diagnostics_note(const char *format, ...) {
    if (!g_session.detailed) return;
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    disruptor::diagnostics::say(text);
}

}  // extern "C"
