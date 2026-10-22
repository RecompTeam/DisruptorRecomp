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
extern "C" void gpu_geometry_diagnostics_set_detail(int enabled);
extern "C" int gl_renderer_debug_messages_active(void);
extern "C" int gl_renderer_debug_message_take(char *text, int size, unsigned *id, int *severity, int *context, unsigned *lost);
extern "C" void gl_renderer_set_shown_count(int enabled);
extern "C" void gl_renderer_shown_count_take(unsigned *pictures, unsigned *between, double *longest_ms);
extern "C" int gpu_ws_displayed_flat(void);
extern "C" int gpu_ws_displayed_menu(void);
extern "C" int psx_audio_out_stats(double *fill_ms, double *target_ms, uint64_t *underruns, uint64_t *overflow_drops,
                                   double *correction, int *legacy, int *host_rate);
extern "C" int psx_audio_device_frames(void);
extern "C" double psx_audio_sdl_queued_ms(void);
extern "C" const char *psx_sdl_audio_driver(void);
extern "C" const char *psx_sdl_audio_device_name(void);

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
    uint64_t underruns = 0, drops = 0;
};

struct Sound {
    bool known = false;
    double queued_ms = 0.0, target_ms = 0.0, correction = 0.0;
    uint64_t underruns = 0, drops = 0;
    int rate = 0, legacy = 0;
};

Sound sound_now() {
    Sound sound;
    sound.known = psx_audio_out_stats(&sound.queued_ms, &sound.target_ms, &sound.underruns, &sound.drops, &sound.correction,
                                      &sound.legacy, &sound.rate) != 0;
    return sound;
}

SoundSetup sound_setup(const Sound& sound) {
    SoundSetup setup;
    setup.driver = psx_sdl_audio_driver();
    setup.device = psx_sdl_audio_device_name();
    setup.device_rate = sound.rate;
    setup.legacy = sound.legacy != 0;
    for (const char* name : {"SDL_AUDIO_DRIVER", "SDL_AUDIO_DEVICE_SAMPLE_FRAMES", "SDL_AUDIO_FREQUENCY", "PIPEWIRE_LATENCY", "PULSE_LATENCY_MSEC"})
        if (const char* value = std::getenv(name)) setup.environment.push_back(std::string(name) + "=" + value);
    return setup;
}

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
    gl_renderer_shown_count_take(&moment.shown, &moment.shown_between, &moment.shown_gap_ms);
    if (const Sound sound = sound_now(); sound.known) {
        moment.sound_queued_ms = sound.queued_ms;
        moment.sound_underruns = sound.underruns >= g_session.underruns ? sound.underruns - g_session.underruns : sound.underruns;
        g_session.underruns = sound.underruns;
        moment.sound_drops = sound.drops >= g_session.drops ? sound.drops - g_session.drops : sound.drops;
        g_session.drops = sound.drops;
        moment.sound_target_ms = sound.target_ms;
        moment.sound_sdl_ms = psx_audio_sdl_queued_ms();
        moment.sound_trim = sound.correction;
    }
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
    if (moment.shown_gap_ms >= 0.0) {
        std::snprintf(text, sizeof text, ", shown %u pictures, %u in between, longest gap %.1f ms", moment.shown, moment.shown_between,
                      moment.shown_gap_ms);
        line += text;
    }
    if (moment.minimized) line += ", minimized";
    if (moment.repeated) line += ", driver messages repeated " + std::to_string(moment.repeated);
    if (moment.lost) line += ", driver messages lost " + std::to_string(moment.lost);
    if (moment.sound_queued_ms >= 0.0) {
        std::snprintf(text, sizeof text, ", sound queued %.0f ms of %.0f, ", moment.sound_queued_ms, moment.sound_target_ms);
        line += text;
        if (moment.sound_sdl_ms >= 0.0) {
            std::snprintf(text, sizeof text, "SDL holds %.0f ms, ", moment.sound_sdl_ms);
            line += text;
        }
        std::snprintf(text, sizeof text, "rate trim %+.2f%%, %llu underruns, %llu overflow drops", moment.sound_trim * 100.0,
                      static_cast<unsigned long long>(moment.sound_underruns), static_cast<unsigned long long>(moment.sound_drops));
        line += text;
    }
    return line;
}

std::string sound_setup_line(const SoundSetup& setup) {
    std::string line = "sound: SDL driver " + (setup.driver.empty() ? std::string("unknown") : setup.driver) + ", device '" +
                       (setup.device.empty() ? std::string("unknown") : setup.device) + "'";
    if (setup.device_rate > 0 && setup.device_rate != 44100) line += ", the game's 44100 Hz resampled to " + std::to_string(setup.device_rate);
    if (setup.legacy) line += ", queued without the resampler";
    if (setup.environment.empty()) {
        line += ", none of SDL_AUDIO_DRIVER, SDL_AUDIO_DEVICE_SAMPLE_FRAMES, SDL_AUDIO_FREQUENCY, PIPEWIRE_LATENCY, PULSE_LATENCY_MSEC set";
    } else {
        line += ", set:";
        for (const auto& entry : setup.environment) line += " " + entry;
    }
    return line;
}

std::string movie_line(const MovieMoment& moment) {
    char text[320];
    const int length = std::snprintf(text, sizeof text,
                                     "movie %u frame %u: sound %.2f s into its data (pitch 0x%X), picture %.2f s at %d a second, "
                                     "sound minus picture %+.0f ms in the game",
                                     moment.movie, moment.frame, moment.sound_seconds, moment.pitch, moment.picture_seconds,
                                     moment.pictures_a_second, (moment.sound_seconds - moment.picture_seconds) * 1000.0);
    std::string line(text, static_cast<size_t>(std::max(length, 0)));
    if (moment.leaving_ms >= 0.0) {
        std::snprintf(text, sizeof text, ", sound leaves the game %.0f ms later", moment.leaving_ms);
        line += text;
    }
    return line;
}

std::string sound_line(int rate, int frames, double queue_ms) {
    char text[160];
    if (frames > 0 && rate > 0) {
        std::snprintf(text, sizeof text, "sound: device %d Hz with a buffer of %d frames (%.1f ms), queue held at %.0f ms", rate, frames,
                      1000.0 * frames / rate, queue_ms);
    } else {
        std::snprintf(text, sizeof text, "sound: device %d Hz, its buffer not told, queue held at %.0f ms", rate, queue_ms);
    }
    return text;
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
        if (const Sound sound = sound_now(); sound.known) {
            say(sound_line(sound.rate, psx_audio_device_frames(), sound.target_ms));
            say(sound_setup_line(sound_setup(sound)));
            session.underruns = sound.underruns;
            session.drops = sound.drops;
        }
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
    gpu_geometry_diagnostics_set_detail(g_session.detailed ? 1 : 0);
    gl_renderer_set_shown_count(g_session.detailed ? 1 : 0);
}

int disruptor_diagnostics_detailed(void) { return g_session.detailed ? 1 : 0; }

void disruptor_diagnostics_set_detailed(int enabled) {
    g_session.detailed = enabled != 0;
    gpu_geometry_diagnostics_set_detail(enabled != 0 ? 1 : 0);
    gl_renderer_set_shown_count(enabled != 0 ? 1 : 0);
}

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

double disruptor_diagnostics_sound_leaving_ms(void) {
    const disruptor::diagnostics::Sound sound = disruptor::diagnostics::sound_now();
    if (!sound.known) return -1.0;
    const double held = psx_audio_sdl_queued_ms();
    const int frames = psx_audio_device_frames();
    return sound.queued_ms + (held > 0.0 ? held : 0.0) + (frames > 0 && sound.rate > 0 ? 1000.0 * frames / sound.rate : 0.0);
}

}  // extern "C"
