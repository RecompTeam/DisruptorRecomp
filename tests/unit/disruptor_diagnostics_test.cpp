#include "game/session/diagnostics.h"

#include "game/frame/frame_rate.h"
#include "psx_sdl.h"

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace disruptor::diagnostics;

namespace {

struct Message {
    std::string text;
    unsigned id;
    int severity, context;
};

SDL_Window g_window;
void (*g_hook)(void) = nullptr;
SDL_WindowFlags g_flags = SDL_WINDOW_INPUT_FOCUS;
std::vector<std::string> g_devices;
std::deque<Message> g_messages;
unsigned g_lost = 0;
bool g_endless = false;
int g_takes = 0;
int g_asked = -1, g_active = 0, g_flat = 0, g_menu = 0, g_failures = 0;
std::filesystem::path g_output;
std::streamoff g_read = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

/* What the module printed since the last call, a line an entry. */
std::vector<std::string> printed() {
    std::fflush(stdout);
    std::ifstream file(g_output, std::ios::binary);
    file.seekg(g_read);
    std::vector<std::string> lines;
    for (std::string line; std::getline(file, line);) {
        g_read += static_cast<std::streamoff>(line.size()) + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

bool said(const std::vector<std::string> &lines, std::initializer_list<const char *> wanted) {
    if (lines.size() != wanted.size()) {
        for (const auto &line : lines) std::cerr << "  printed: " << line << "\n";
        return false;
    }
    size_t at = 0;
    for (const char *line : wanted) {
        if (lines[at++] == line) continue;
        std::cerr << "  printed: " << lines[at - 1] << "\n  wanted:  " << line << "\n";
        return false;
    }
    return true;
}

}  // namespace

extern "C" {

SDL_Window *sdl_window = &g_window;

void mod_register_frame_hook(void (*hook)(void)) { g_hook = hook; }
void gl_renderer_set_debug_messages(int enabled) { g_asked = enabled; }
int g_geometry_detail = -1;
void gpu_geometry_diagnostics_set_detail(int enabled) { g_geometry_detail = enabled; }
int gl_renderer_debug_messages_active(void) { return g_active; }
int gl_renderer_debug_message_take(char *text, int size, unsigned *id, int *severity, int *context, unsigned *lost) {
    *lost = g_lost;
    g_lost = 0;
    ++g_takes;
    if (g_endless) g_messages.push_back({"without end", 77, 1, 0});
    if (g_messages.empty()) return 0;
    std::snprintf(text, static_cast<size_t>(size), "%s", g_messages.front().text.c_str());
    *id = g_messages.front().id;
    *severity = g_messages.front().severity;
    *context = g_messages.front().context;
    g_messages.pop_front();
    return 1;
}
int g_shown_count = -1;
void gl_renderer_set_shown_count(int enabled) { g_shown_count = enabled; }
void gl_renderer_shown_count_take(unsigned *pictures, unsigned *between, double *longest_ms) {
    *pictures = 0;
    *between = 0;
    *longest_ms = -1.0;
}
int gpu_ws_displayed_flat(void) { return g_flat; }
int gpu_ws_displayed_menu(void) { return g_menu; }
int disruptor_frame_rate_last_window(DisruptorFrameRateWindow *out) {
    *out = DisruptorFrameRateWindow{30, 60, 2, 0, 0};
    return 1;
}
int g_sound = 0, g_sound_frames = 0;
double g_sound_queued = 0.0;
uint64_t g_sound_underruns = 0, g_sound_drops = 0;
int psx_audio_out_stats(double *fill_ms, double *target_ms, uint64_t *underruns, uint64_t *overflow_drops, double *correction, int *legacy,
                        int *host_rate) {
    *fill_ms = g_sound_queued;
    *target_ms = 45.0;
    *underruns = g_sound_underruns;
    *overflow_drops = g_sound_drops;
    *correction = 0.0;
    *legacy = 0;
    *host_rate = 48000;
    return g_sound;
}
int psx_audio_device_frames(void) { return g_sound_frames; }
double g_sdl_queued = -1.0;
double psx_audio_sdl_queued_ms(void) { return g_sdl_queued; }
const char *psx_sdl_audio_driver(void) { return "pipewire"; }
const char *psx_sdl_audio_device_name(void) { return "Built-in Audio"; }

}  // extern "C"

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *) { return g_flags; }
int SDL_NumJoysticks() { return static_cast<int>(g_devices.size()); }
SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int index) { return static_cast<SDL_JoystickID>(index + 1); }
const char *SDL_GetJoystickNameForID(SDL_JoystickID device) { return g_devices[device - 1].c_str(); }

namespace {

void test_the_lines() {
    Stretch stretch;
    stretch.frame(0.050);
    stretch.frame(0.016);
    expect(stretch.frames == 2 && stretch.waited > 0.0659 && stretch.waited < 0.0661 && stretch.longest == 0.050,
           "a stretch counts its frames, sums their waits and keeps the longest");

    expect(pulse_line(Stretch{600, 10.0, 0.0182}, Moment{30, 60, 0, "world", true, false, 0, 0}) ==
               "pulse: 600 frames in 10.0 s, 16.7 ms a frame, longest 18.2 ms, game 30 frames in 60 vblanks with 0 late, world, focused",
           "a pulse says the frames, their mean and longest wait, the game's own window and the screen");
    expect(pulse_line(Stretch{}, Moment{0, 0, 3, "menu", false, true, 4, 2}) ==
               "pulse: 0 frames in 0.0 s, 0.0 ms a frame, longest 0.0 ms, game 0 frames in 0 vblanks with 3 late, menu, not focused, "
               "minimized, driver messages repeated 4, driver messages lost 2",
           "and a window out of focus or minimized, and driver messages left out, a stretch of no frames dividing by nothing");

    Moment shown{30, 60, 0, "world", true, false, 0, 0};
    shown.shown = 1200;
    shown.shown_between = 1140;
    shown.shown_gap_ms = 9.84;
    expect(pulse_line(Stretch{600, 10.0, 0.0182}, shown) ==
               "pulse: 600 frames in 10.0 s, 16.7 ms a frame, longest 18.2 ms, game 30 frames in 60 vblanks with 0 late, world, focused, "
               "shown 1200 pictures, 1140 in between, longest gap 9.8 ms",
           "a pulse says what reached the screen when the renderer tells: pictures, in-between ones, the longest gap");

    Moment sounding{30, 60, 0, "world", true, false, 0, 0};
    sounding.sound_queued_ms = 44.6;
    sounding.sound_underruns = 2;
    sounding.sound_target_ms = 45.0;
    sounding.sound_trim = -0.0012;
    sounding.sound_drops = 3;
    expect(pulse_line(Stretch{600, 10.0, 0.0182}, sounding) ==
               "pulse: 600 frames in 10.0 s, 16.7 ms a frame, longest 18.2 ms, game 30 frames in 60 vblanks with 0 late, world, focused, "
               "sound queued 45 ms of 45, rate trim -0.12%, 2 underruns, 3 overflow drops",
           "with a sound device the pulse says how much sound waits against its aim, the rate trim, how often it ran dry and overflowed");
    sounding.sound_sdl_ms = 11.6;
    expect(pulse_line(Stretch{600, 10.0, 0.0182}, sounding).find("sound queued 45 ms of 45, SDL holds 12 ms, rate trim") != std::string::npos,
           "and what SDL holds after the queue when it tells");

    SoundSetup setup;
    setup.driver = "pipewire";
    setup.device = "Built-in Audio";
    setup.device_rate = 48000;
    expect(sound_setup_line(setup) ==
               "sound: SDL driver pipewire, device 'Built-in Audio', the game's 44100 Hz resampled to 48000, none of SDL_AUDIO_DRIVER, "
               "SDL_AUDIO_DEVICE_SAMPLE_FRAMES, SDL_AUDIO_FREQUENCY, PIPEWIRE_LATENCY, PULSE_LATENCY_MSEC set",
           "the setup line names the driver, the device and a resampling, and that none of SDL's sound variables is set");
    setup.device_rate = 44100;
    setup.legacy = true;
    setup.environment = {"SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256", "PIPEWIRE_LATENCY=256/48000"};
    setup.driver.clear();
    expect(sound_setup_line(setup) ==
               "sound: SDL driver unknown, device 'Built-in Audio', queued without the resampler, set: SDL_AUDIO_DEVICE_SAMPLE_FRAMES=256 "
               "PIPEWIRE_LATENCY=256/48000",
           "and the variables that are set, the queue without the resampler, a driver it was not told");

    MovieMoment movie;
    movie.movie = 3;
    movie.frame = 503;
    movie.sound_seconds = 20.017;
    movie.picture_seconds = 20.0;
    movie.pictures_a_second = 25;
    movie.pitch = 0x7FA;
    movie.leaving_ms = 69.6;
    expect(movie_line(movie) ==
               "movie 3 frame 503: sound 20.02 s into its data (pitch 0x7FA), picture 20.00 s at 25 a second, sound minus picture +17 ms "
               "in the game, sound leaves the game 70 ms later",
           "a movie line says where its sound and its picture are and how late sound leaves");
    movie.leaving_ms = -1.0;
    expect(movie_line(movie).find("in the game") + 11 == movie_line(movie).size(), "without a sound device the leaving is left out");
    expect(sound_line(48000, 480, 45.0) == "sound: device 48000 Hz with a buffer of 480 frames (10.0 ms), queue held at 45 ms",
           "the sound line names the device's rate, its buffer and the queue before it");
    expect(sound_line(44100, 0, 90.0) == "sound: device 44100 Hz, its buffer not told, queue held at 90 ms",
           "a device that does not tell its buffer is said so");

    const auto changes = device_changes({"A", "B", "B"}, {"B", "C"});
    expect(changes == std::vector<std::string>{"controller removed: A", "controller removed: B", "controller connected: C"},
           "a device that left and one that came are a line each, two of one name counted apart");
    expect(device_changes({"A", "B"}, {"A", "B"}).empty() && device_changes({}, {}).empty(), "no change is no line");

    DriverMessages driver;
    expect(driver.line(7, 2, 0, "shader failed\r\n ") == "driver (main context, high, id 7): shader failed",
           "a driver message names its context, its weight and its id, the line's end trimmed");
    expect(driver.line(7, 1, 1, "slow path") == "driver (presenter context, medium, id 7): slow path", "the presenter's context and a medium one");
    expect(!driver.line(7, 1, 0, "third").empty() && driver.line(7, 1, 0, "fourth").empty() && driver.line(7, 1, 0, "fifth").empty(),
           "a kind is printed three times and no more");
    expect(driver.take_repeated() == 2 && driver.take_repeated() == 0, "the ones left out are counted, and the count is handed over once");
    expect(!driver.line(8, 2, 0, "another kind").empty(), "another kind is printed still");
    for (uint32_t id = 100; id < 162; ++id) (void)driver.line(id, 1, 0, "kind");
    expect(driver.take_repeated() == 0 && driver.line(900, 2, 0, "one kind too many").empty() && driver.take_repeated() == 1 &&
               !driver.line(8, 2, 0, "a known kind").empty(),
           "past sixty-four kinds a new kind is only counted, a known one still printed");
}

void test_a_session() {
    expect(g_hook != nullptr, "the module registers a frame hook");
    expect(g_asked == -1 && disruptor_diagnostics_detailed() == 0 && disruptor_diagnostics_driver_state() == DISRUPTOR_DRIVER_OFF,
           "with nothing asked the detailed log is off and the driver is not asked for its messages");
    (void)printed();

    g_devices = {"Pad B", "Pad A"};
    g_messages.push_back({"ignored while off", 1, 2, 0});
    frame_at(2.5);
    expect(said(printed(), {"disruptor: first frame 2.5 s after the start"}), "every session says when its first frame came, and no more");
    frame_at(2.6);
    disruptor_diagnostics_note("setting %s = %d", "video.vsync", 0);
    expect(printed().empty() && g_messages.size() == 1, "with the detailed log off a frame and a note print nothing and take nothing");
    g_messages.clear();

    disruptor_diagnostics_configure(1);
    expect(g_asked == 1 && disruptor_diagnostics_detailed() == 1, "the saved setting switches it on and asks the driver for its messages");
    expect(g_shown_count == 1, "and the renderer counts what reaches the screen only then");
    expect(disruptor_diagnostics_driver_state() == DISRUPTOR_DRIVER_REFUSED, "asked at the start and not given, the driver refused");
    g_active = 1;
    expect(disruptor_diagnostics_driver_state() == DISRUPTOR_DRIVER_ON, "and given, they are on");

    frame_at(3.0);
    expect(said(printed(), {"disruptor: +3.0s detailed logging on, driver messages on", "disruptor: +3.0s controller connected: Pad A",
                            "disruptor: +3.0s controller connected: Pad B"}),
           "the detailed log opens with what it holds and the controllers present");

    g_messages.push_back({"shader failed\n", 5, 2, 0});
    g_lost = 2;
    frame_at(3.1);
    expect(said(printed(), {"disruptor: +3.1s driver (main context, high, id 5): shader failed"}), "a driver message is printed on the frame after it");

    g_devices = {"Pad B"};
    frame_at(3.5);
    expect(printed().empty(), "controllers are looked at once a second");
    frame_at(4.0);
    expect(said(printed(), {"disruptor: +4.0s controller removed: Pad A"}), "and a removed one is said then");

    disruptor_diagnostics_note("setting %s = %d", "video.vsync", 1);
    expect(said(printed(), {"disruptor: +4.0s setting video.vsync = 1"}), "a note carries the time of the last frame");

    for (int count = 0; count < 4; ++count) g_messages.push_back({"slow path", 9, 1, 1});
    frame_at(4.5);
    expect(said(printed(), {"disruptor: +4.5s driver (presenter context, medium, id 9): slow path",
                            "disruptor: +4.5s driver (presenter context, medium, id 9): slow path",
                            "disruptor: +4.5s driver (presenter context, medium, id 9): slow path"}),
           "a kind said four times is printed three times");

    frame_at(12.9);
    expect(printed().empty(), "no pulse before ten seconds are over");
    frame_at(13.0);
    expect(said(printed(), {"disruptor: +13.0s pulse: 6 frames in 10.0 s, 1666.7 ms a frame, longest 8400.0 ms, game 30 frames in 60 vblanks "
                            "with 2 late, world, focused, driver messages repeated 1, driver messages lost 2"}),
           "the pulse covers the frames since the log opened, with the messages left out and the ones the queue lost");

    g_flat = 1;
    g_flags = SDL_WINDOW_MINIMIZED;
    frame_at(23.0);
    expect(said(printed(), {"disruptor: +23.0s pulse: 1 frames in 10.0 s, 10000.0 ms a frame, longest 10000.0 ms, game 30 frames in 60 vblanks "
                            "with 2 late, flat screen, not focused, minimized"}),
           "the next pulse starts anew: a flat screen, a minimized window, nothing lost");
    g_menu = 1;
    g_flags = SDL_WINDOW_INPUT_FOCUS;
    frame_at(33.0);
    expect(said(printed(), {"disruptor: +33.0s pulse: 1 frames in 10.0 s, 10000.0 ms a frame, longest 10000.0 ms, game 30 frames in 60 vblanks "
                            "with 2 late, menu, focused"}),
           "the front end's menu is told from a flat screen");
    frame_at(40.0);
    expect(printed().empty(), "a frame between pulses prints nothing");

    disruptor_diagnostics_set_detailed(0);
    g_messages.push_back({"while off", 6, 2, 0});
    frame_at(50.0);
    disruptor_diagnostics_note("unseen");
    expect(printed().empty() && disruptor_diagnostics_driver_state() == DISRUPTOR_DRIVER_OFF && g_shown_count == 0,
           "switched off in the menu it stops at once, the count of shown pictures with it");
    g_messages.clear();

    disruptor_diagnostics_configure(0);
    expect(g_asked == 0, "a start with the setting off does not ask the driver");
    g_active = 0;
    g_devices.clear();
    disruptor_diagnostics_set_detailed(1);
    expect(disruptor_diagnostics_driver_state() == DISRUPTOR_DRIVER_NEXT_LAUNCH, "switched on in the menu, the driver's part waits for a launch");
    frame_at(51.0);
    expect(said(printed(), {"disruptor: +51.0s detailed logging on, driver messages from the next launch", "disruptor: +51.0s controllers: none"}),
           "and the log says so when it opens, and that no controller is there");
    frame_at(52.0);
    expect(printed().empty(), "that no controller is there is said once");
    g_devices = {""};
    frame_at(60.9);
    expect(said(printed(), {"disruptor: +60.9s controller connected: unnamed device"}),
           "a log opened again counts its ten seconds from there, and a device with no name is still said");
    frame_at(61.0);
    expect(said(printed(), {"disruptor: +61.0s pulse: 3 frames in 10.0 s, 3333.3 ms a frame, longest 8900.0 ms, game 30 frames in 60 vblanks "
                            "with 2 late, menu, focused"}),
           "and its pulse leaves out the frames before it");

    disruptor_diagnostics_set_detailed(0);
    frame_at(61.1);
    disruptor_diagnostics_set_detailed(1);
    frame_at(61.2);
    expect(said(printed(), {"disruptor: +61.2s detailed logging on, driver messages from the next launch",
                            "disruptor: +61.2s controller connected: unnamed device"}),
           "a log opened again within a second looks at the controllers at once");

    g_endless = true;
    g_takes = 0;
    frame_at(61.3);
    g_endless = false;
    g_messages.clear();
    expect(g_takes == 16 && printed().size() == 3, "a driver that never stops is asked sixteen times a frame and no more");

    g_sound = 1;
    g_sound_frames = 480;
    g_sound_underruns = 5;
    disruptor_diagnostics_set_detailed(0);
    frame_at(70.0);
    disruptor_diagnostics_set_detailed(1);
    frame_at(70.1);
    expect(said(printed(), {"disruptor: +70.1s detailed logging on, driver messages from the next launch",
                            "disruptor: +70.1s sound: device 48000 Hz with a buffer of 480 frames (10.0 ms), queue held at 45 ms",
                            "disruptor: +70.1s sound: SDL driver pipewire, device 'Built-in Audio', the game's 44100 Hz resampled to 48000, "
                            "none of SDL_AUDIO_DRIVER, SDL_AUDIO_DEVICE_SAMPLE_FRAMES, SDL_AUDIO_FREQUENCY, PIPEWIRE_LATENCY, PULSE_LATENCY_MSEC set",
                            "disruptor: +70.1s controller connected: unnamed device"}),
           "a log that opens with a sound device says its way out");
    g_sound_queued = 38.2;
    g_sound_underruns = 7;
    frame_at(80.1);
    const std::vector<std::string> sounded = printed();
    expect(sounded.size() == 1 && sounded[0].starts_with("disruptor: +80.1s pulse: 1 frames in 10.0 s") &&
               sounded[0].ends_with(", sound queued 38 ms of 45, rate trim +0.00%, 2 underruns, 0 overflow drops"),
           "and its pulse counts the underruns since the log opened");
    frame_at(90.1);
    const std::vector<std::string> quiet = printed();
    expect(quiet.size() == 1 && quiet[0].ends_with("sound queued 38 ms of 45, rate trim +0.00%, 0 underruns, 0 overflow drops"), "and the next pulse those since the one before");
    g_sound_underruns = 2;
    frame_at(100.1);
    const std::vector<std::string> restarted = printed();
    expect(restarted.size() == 1 && restarted[0].ends_with(", 2 underruns, 0 overflow drops"), "a count that went back restarted: its value is the underruns since");
    g_sound_underruns = 3;
    frame_at(110.1);
    const std::vector<std::string> after = printed();
    expect(after.size() == 1 && after[0].ends_with(", 1 underruns, 0 overflow drops"), "and counting goes on from there");
    g_sound_drops = 4;
    frame_at(120.1);
    const std::vector<std::string> dropped = printed();
    expect(dropped.size() == 1 && dropped[0].ends_with(", 0 underruns, 4 overflow drops"), "overflow drops are counted since the pulse before");
    frame_at(130.1);
    const std::vector<std::string> undropped = printed();
    expect(undropped.size() == 1 && undropped[0].ends_with(", 0 underruns, 0 overflow drops"), "and none new is none");
    g_sdl_queued = 11.6;
    expect(std::abs(disruptor_diagnostics_sound_leaving_ms() - (38.2 + 11.6 + 10.0)) < 1e-9,
           "sound leaves after the queue, what SDL holds and the device's buffer");
    g_sdl_queued = -1.0;
    expect(sound_line(48000, 65536, 45.0) == "sound: device 48000 Hz with a buffer of 65536 frames (1365.3 ms), queue held at 45 ms",
           "a buffer past sixteen bits is said whole");
}

}  // namespace

int main(int argc, char **argv) {
    const std::string mode = argc > 1 ? argv[1] : "";
    if (mode == "--forced") {
        expect(g_asked == 1 && disruptor_diagnostics_detailed() == 1, "the environment switches the detailed log on before any setting is read");
        disruptor_diagnostics_configure(0);
        expect(g_asked == 1 && disruptor_diagnostics_detailed() == 1, "and a saved setting that says off does not take it back");
    } else if (mode == "--refused") {
        expect(g_asked == 0 && disruptor_diagnostics_detailed() == 0, "the environment's zero leaves the detailed log off");
        disruptor_diagnostics_configure(1);
        expect(g_asked == 0 && disruptor_diagnostics_detailed() == 0, "against a saved setting that says on");
    } else {
        g_output = std::filesystem::temp_directory_path() / "disruptor-diagnostics-test.txt";
        if (!std::freopen(g_output.string().c_str(), "w", stdout)) {
            std::cerr << "FAIL: the test cannot take the standard output\n";
            return 1;
        }
        test_the_lines();
        test_a_session();
        std::fclose(stdout);
        std::error_code left;
        std::filesystem::remove(g_output, left);
    }
    if (g_failures) return 1;
    std::cerr << "disruptor diagnostics: PASS\n";
    return 0;
}
