#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "spu.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {
int g_detailed = 0;
std::vector<std::string> g_notes;
SpuVoiceState g_voice{};
}  // namespace

extern "C" int disruptor_diagnostics_detailed(void) { return g_detailed; }
extern "C" void disruptor_diagnostics_note(const char *format, ...) {
    char text[512];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    g_notes.emplace_back(text);
}
extern "C" double disruptor_diagnostics_sound_leaving_ms(void) { return 70.0; }
extern "C" void spu_get_voice_state(int voice, SpuVoiceState *out) { *out = voice == 22 ? g_voice : SpuVoiceState{}; }

namespace {
std::unordered_map<std::uint32_t, std::uint32_t> g_words;
PSXModFunctionEntryCallback g_hook = nullptr;
std::uint32_t g_hooked_at = 0;
int g_started = 1;
int g_netplay = 0;
int g_start = 3;
std::uint32_t g_asked_movie = 0xFFFFFFFFu;
}  // namespace

int psx_mod_game_started(void) { return g_started; }
int psx_netplay_active(void) { return g_netplay; }
uint32_t psx_mod_read_word(uint32_t address) {
    const auto found = g_words.find(address);
    return found == g_words.end() ? 0u : found->second;
}
int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    g_hooked_at = address;
    g_hook = callback;
    return 1;
}
extern "C" int disruptor_language_disc_sound_start(uint32_t movie) {
    g_asked_movie = movie;
    return g_start;
}

#include "../../src/game/language/language_movie_sound.cpp"

namespace disruptor::diagnostics {
std::string movie_line(const MovieMoment& moment) {
    char text[200];
    std::snprintf(text, sizeof text, "movie %u frame %u sound %.4f picture %.4f at %d pitch %X leaving %.0f", moment.movie,
                  moment.frame, moment.sound_seconds, moment.picture_seconds, moment.pictures_a_second, moment.pitch,
                  moment.leaving_ms);
    return text;
}
}  // namespace disruptor::diagnostics

namespace {

constexpr std::uint32_t kShown = 0x800485C4u, kElsewhere = 0x800484D8u;
constexpr std::uint32_t kFrameAt = 0x800715B8u, kMovieAt = 0x800716F4u, kVBlanksAt = 0x8005B920u;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* The $s5 the player is left with after a VSync with `wait` in $a0 that returns to `returns_to`. */
std::uint32_t after_vsync(std::uint32_t returns_to, std::uint32_t wait, std::uint32_t frame, std::uint32_t started) {
    CPUState cpu{};
    cpu.gpr[31] = returns_to;
    cpu.gpr[4] = wait;
    cpu.gpr[21] = started;
    g_words[kFrameAt] = frame;
    g_hook(&cpu, kVSync);
    return cpu.gpr[21];
}

/* The US loop over frames `first`..`last` from `started`: the frame numbers whose test keyed the sound on. */
std::uint32_t keyed_at(std::uint32_t first, std::uint32_t last, std::uint32_t started, int *keys) {
    std::uint32_t at = 0;
    *keys = 0;
    for (std::uint32_t frame = first; frame <= last; ++frame) {
        started = after_vsync(kShown, 0, frame, started);
        if ((started & 0xFFu) == 0u) {
            if (++*keys == 1) at = frame;
            started = 1u;
        }
    }
    return at;
}

bool noted(const std::string& start) {
    for (const auto& note : g_notes)
        if (note.rfind(start, 0) == 0) return true;
    return false;
}

/* A movie played in step: 25 frames a second at 60 VBlanks, voice 22 keyed on at frame 3 near the end of its ring. */
void play_movie(std::uint32_t first_vblank, std::uint32_t vblanks, std::uint32_t leap_at = 0u, std::uint32_t key_frame = 3u,
                std::uint32_t gap_at = 0u, double head_start = 0.0) {
    constexpr std::uint32_t kRingStart = 0x1010u, kRingEnd = 0x41010u;
    const double step = 0x7FA / 4096.0 * 735.0 * 16.0 / 28.0;
    double address = kRingEnd - 3000.0;
    g_words[kMovieAt] = 3u;
    g_voice = SpuVoiceState{};
    g_voice.pitch = 0x7FA;
    g_voice.start_lo = static_cast<std::uint16_t>(static_cast<std::uint32_t>(address) / 8u);
    for (std::uint32_t v = 0; v < vblanks; ++v) {
        g_words[kVBlanksAt] = first_vblank + v + (gap_at && v >= gap_at ? 40u : 0u);
        const std::uint32_t frame = 2u + v * 25u / 60u;
        if (frame >= key_frame) {
            if (g_voice.active) address += step;
            else address += head_start;
            if (v == leap_at) address += 50000.0;
            if (address >= kRingEnd) address -= kRingEnd - kRingStart;
            g_voice.active = 1;
            g_voice.cur_addr = static_cast<std::uint32_t>(address);
        }
        after_vsync(kElsewhere, 0xFFFFFFFFu, frame, 1);
    }
}

void test_movie_log() {
    g_notes.clear();
    g_detailed = 0;
    play_movie(5000u, 200u);
    expect(g_notes.empty(), "without detailed logging no movie is watched");

    g_detailed = 1;
    g_words[kFrameAt] = 400u;
    play_movie(6000u, 300u);
    expect(noted("movie 3 begins at frame 2, its sound waits for the frame the PAL player starts it on"),
           "a frame number come back small begins a movie, and the PAL start is named");
    expect(noted("movie 3 sound begins at frame 3, pitch 0x7FA, "), "the voice keyed on is noted with its frame and pitch");
    int lines = 0;
    double worst = 0.0;
    for (const auto& note : g_notes) {
        unsigned movie = 0, frame = 0, pitch = 0;
        double sound = 0.0, picture = 0.0, leaving = 0.0;
        int fps = 0;
        if (std::sscanf(note.c_str(), "movie %u frame %u sound %lf picture %lf at %d pitch %X leaving %lf", &movie, &frame, &sound,
                        &picture, &fps, &pitch, &leaving) == 7) {
            ++lines;
            worst = std::max(worst, std::abs(sound - picture));
            expect(fps == 25 && pitch == 0x7FAu && leaving == 70.0, "a movie line has the PAL rate, the pitch and how late sound leaves");
        }
    }
    expect(lines == 4, "a line a second while the sound plays");
    expect(worst < 0.05, "a movie in step reads as in step, across the ring's loop");
    const std::uint32_t last_frame = 2u + 299u * 25u / 60u;
    for (std::uint32_t v = 1; v <= 62u; ++v) {
        g_words[kVBlanksAt] = 6299u + v;
        after_vsync(kElsewhere, 0xFFFFFFFFu, last_frame, 1);
    }
    expect(noted("movie 3 ended at frame " + std::to_string(last_frame) + ", sound minus picture"), "a frame number still for a second ends it");

    g_notes.clear();
    play_movie(7000u, 200u, 100u);
    double leapt = 0.0;
    for (const auto& note : g_notes) {
        unsigned movie = 0, frame = 0, pitch = 0;
        double sound = 0.0, picture = 0.0, leaving = 0.0;
        int fps = 0;
        if (std::sscanf(note.c_str(), "movie %u frame %u sound %lf picture %lf at %d pitch %X leaving %lf", &movie, &frame, &sound, &picture,
                        &fps, &pitch, &leaving) == 7)
            leapt = std::max(leapt, std::abs(sound - picture));
    }
    expect(leapt < 0.05, "a leap of the decode address is counted as one VBlank's step, not whole");
    g_notes.clear();
    play_movie(8000u, 120u);
    play_movie(8120u, 60u);
    int begun = 0;
    for (const auto& note : g_notes) begun += note.rfind("movie 3 begins", 0) == 0;
    expect(begun == 2, "a movie that follows another at once begins again");

    g_notes.clear();
    play_movie(9000u, 200u, 0u, 4u);
    double late = 0.0;
    for (const auto& note : g_notes) {
        unsigned movie = 0, frame = 0, pitch = 0;
        double sound = 0.0, picture = 0.0, leaving = 0.0;
        int fps = 0;
        if (std::sscanf(note.c_str(), "movie %u frame %u sound %lf picture %lf at %d pitch %X leaving %lf", &movie, &frame, &sound, &picture,
                        &fps, &pitch, &leaving) == 7)
            late = sound - picture;
    }
    expect(late < -0.03 && late > -0.06, "a sound keyed on a frame after the PAL start reads as that much behind, not as in step");
    g_notes.clear();
    play_movie(10000u, 300u, 0u, 3u, 150u);
    g_words[kVBlanksAt] += 0u;
    int after_gap = 0;
    for (const auto& note : g_notes) after_gap += note.rfind("movie 3 frame", 0) == 0;
    expect(after_gap == 2, "a gap in the watch stops the lines, for loops of the voice may have gone uncounted");
    for (std::uint32_t v = 1; v <= 62u; ++v) {
        g_words[kVBlanksAt] = 10339u + v;
        after_vsync(kElsewhere, 0xFFFFFFFFu, 2u + 299u * 25u / 60u, 1);
    }
    expect(noted("movie 3 ended at frame " + std::to_string(2u + 299u * 25u / 60u) + ", watched from part way"),
           "and its end says the comparison is not known");

    g_notes.clear();
    play_movie(11000u, 200u, 0u, 3u, 0u, 1000.0);
    double ahead = 0.0;
    for (const auto& note : g_notes) {
        unsigned movie = 0, frame = 0, pitch = 0;
        double sound = 0.0, picture = 0.0, leaving = 0.0;
        int fps = 0;
        if (std::sscanf(note.c_str(), "movie %u frame %u sound %lf picture %lf at %d pitch %X leaving %lf", &movie, &frame, &sound, &picture,
                        &fps, &pitch, &leaving) == 7)
            ahead = sound - picture;
    }
    expect(noted("movie 3 sound begins at frame 3, pitch 0x7FA, 80 ms of it already played") && ahead > 0.07 && ahead < 0.09,
           "sound played before the watch saw the voice counts from the voice's start address");
    g_detailed = 0;
}

}  // namespace

int main() {
    test_movie_log();
    expect(g_hook != nullptr && g_hooked_at == 0x8004B3D4u, "the module hooks the entry of VSync");
    g_words[kMovieAt] = 7;
    int keys = 0;

    expect(keyed_at(2, 6, 0, &keys) == 3 && keys == 1, "a PAL movie's sound is keyed on once, on the stream's frame 3, the first shown being 2");
    expect(g_asked_movie == 7, "the start is the playing movie's");
    g_start = 0;
    expect(keyed_at(2, 6, 0, &keys) == 2 && keys == 1, "a disc without a start keeps the US key-on at the first frame shown");
    g_start = 2;
    expect(keyed_at(2, 6, 0, &keys) == 2 && keys == 1, "a start the first frame reaches is that frame");
    g_start = 5;
    expect(keyed_at(2, 6, 0, &keys) == 5 && keys == 1, "a later start waits for it");
    g_start = 3;

    expect(after_vsync(kShown, 0xFFFFFFFFu, 2, 0) == 0, "a VSync(-1) returning there is not the wait before the key-on");
    expect(after_vsync(kElsewhere, 0, 2, 0) == 0, "a VSync(0) returning elsewhere is left alone");
    expect(after_vsync(kShown, 0, 2, 1) == 1 && after_vsync(kShown, 0, 3, 1) == 1,
           "a sound already keyed on, as a state loaded after its start has it, is neither held nor keyed on again");
    expect(after_vsync(kShown, 0, 2, 0x101) == 0x101, "only the byte the player tests is looked at");

    const std::uint32_t held = after_vsync(kShown, 0, 2, 0);
    expect(held == 2u, "the hold is a value of $s5 the player never sets");
    expect(keyed_at(2, 6, held, &keys) == 3 && keys == 1, "a state saved while holding, loaded in another run, keys on once at the start");
    expect(keyed_at(4, 6, held, &keys) == 4 && keys == 1, "and one loaded past the start keys on at once");

    g_words[kVBlanksAt] = 1000u;
    expect(after_vsync(kShown, 0, 2, 0) == kHeld && disruptor_language_movie_sound_save_deferred() == 1,
           "a save waits while this run holds a movie's sound");
    g_words[kVBlanksAt] = 1025u;
    expect(after_vsync(kShown, 0, 2, kHeld) == kHeld, "a frame short of the start keeps the hold");
    g_words[kVBlanksAt] = 1055u;
    expect(after_vsync(kElsewhere, 0xFFFFFFFFu, 2, 0) == 0 && disruptor_language_movie_sound_save_deferred() == 1,
           "the VSyncs inside a movie frame keep the hold, counted from the last frame shown");
    expect(after_vsync(kShown, 0, 3, kHeld) == 0 && disruptor_language_movie_sound_save_deferred() == 0,
           "and it goes once the start lets the sound key on");
    g_words[kVBlanksAt] = 0xFFFFFFF0u;
    expect(after_vsync(kShown, 0, 2, 0) == kHeld && disruptor_language_movie_sound_save_deferred() == 1, "held again");
    g_words[kVBlanksAt] = 0xFFFFFFF0u + 30u;
    expect(after_vsync(kElsewhere, 0xFFFFFFFFu, 2, 0) == 0 && disruptor_language_movie_sound_save_deferred() == 1,
           "30 VBlanks keep it, across the count's wrap");
    g_words[kVBlanksAt] += 1u;
    expect(after_vsync(kElsewhere, 0xFFFFFFFFu, 2, 7) == 7 && disruptor_language_movie_sound_save_deferred() == 0,
           "a hold no movie frame has followed for 31 VBlanks is a player left: a save goes");
    expect(after_vsync(kShown, 0, 2, 0) == kHeld && disruptor_language_movie_sound_save_deferred() == 1, "held once more");
    disruptor_language_movie_sound_state_loaded();
    expect(disruptor_language_movie_sound_save_deferred() == 0, "a state loaded is not this run's hold");
    expect(after_vsync(kShown, 0, 2, kHeld) == kHeld && disruptor_language_movie_sound_save_deferred() == 1,
           "the hold it carries is this run's from its next movie frame");
    expect(after_vsync(kShown, 0, 3, kHeld) == 0, "let go at the start");

    for (int which = 0; which < 4; ++which) {
        g_netplay = which == 0;
        g_ls_mode = which == 1;
        g_ls_replay_active = which == 2;
        g_started = which == 3 ? 0 : 1;
        expect(after_vsync(kShown, 0, 2, 0) == 0, "netplay, lockstep, its replay and a game not started keep the US start");
        expect(after_vsync(kShown, 0, 2, held) == 0 && disruptor_language_movie_sound_save_deferred() == 0,
               "and a hold found there is let go so the US code keys on, and a save does not wait");
    }

    if (g_failures != 0) return 1;
    std::cout << "Disruptor movie sound start tests passed\n";
    return 0;
}
