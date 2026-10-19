/*
 * Starts a PAL movie's sound on the frame its own player starts it on.
 *
 * The US player keys a movie's sound on right after it shows the first frame (0x80048654, while $s5 is 0). The French
 * and German players wait until the stream's frame number reaches the movie's entry of a fifth movie table the US
 * executable lacks (their checks at 0x80048CB8 and 0x80048B2C): 3 for every movie of both discs, while the first frame
 * shown is number 2, so the US code would start their sound one frame, 40 ms, early. The player's VSync(0) of each frame
 * comes after the frame's fetch and before the key-on test: there $s5 is held at a value the game never gives it while
 * the frame number is short of the start, and set to 0 on the frame that reaches it. The hold lives in $s5 alone, so a
 * state loaded while held keeps it, and a save waits out a hold this run made, for a build without the module would
 * never let it go. Under netplay or lockstep the US code keeps its own start, and a hold found there is let go.
 */

#include "game/language/language.h"
#include "game/session/diagnostics.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "spu.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kVSync = 0x8004B3D4u;
constexpr uint32_t kFrameShown = 0x800485C4u;   /* after the VSync(0) between a frame's fetch and the key-on test */
constexpr uint32_t kFrameNumber = 0x800715B8u;  /* the stream's number of the frame fetched, written at 0x80048B14 */
constexpr uint32_t kMovie = 0x800716F4u;        /* the movie playing, written at 0x800147D8 */
constexpr int kStarted = 21;                    /* $s5: the player sets 0 when a movie begins and 1 at the key-on */
constexpr uint32_t kHeld = 2u;
constexpr int kWait = 4;
constexpr int kReturn = 31;
constexpr uint32_t kVBlanks = 0x8005B920u;  /* libetc's VBlank count, what VSync(-1) returns */
constexpr uint32_t kMostVBlanks = 30u;      /* a movie frame comes every 2 or 3: a hold this old means the player left */

constexpr int kMovieVoice = 22;           /* the player keys it on at 0x80048654: SpuSetKey(1, 0x400000) */
constexpr uint32_t kNoteVBlanks = 60u;     /* a line a second of the detailed log while a movie's sound plays */
constexpr uint32_t kStillVBlanks = 60u;    /* a stream frame number that stays this long means the movie ended */
constexpr double kBytesPerSample = 16.0 / 28.0;
constexpr double kSamplesPerVBlank = 44100.0 / 60.0;

bool g_hooked = false;
bool g_holding = false;
uint32_t g_held_at = 0u;

/* What the detailed log watches of a movie: where its sound has got to in its own data, against its frame number. */
struct MovieWatch {
    bool playing = false, sounding = false, joined_late = false;
    uint32_t movie = 0u, vblank = 0u, frame = 0u, frame_since = 0u;
    uint32_t keyed_frame = 0u, next_note = 0u, address = 0u;
    uint16_t pitch = 0u;
    double bytes = 0.0;
};

constexpr uint32_t kMostWatchGap = 30u;   /* VBlanks the watch may miss before what it counted is lost */
constexpr uint32_t kLateJoinFrames = 10u; /* a movie first seen this far past its start has no start to count from */

MovieWatch g_watch;

double bytes_a_second(uint16_t pitch) { return pitch / 4096.0 * 44100.0 * kBytesPerSample; }

disruptor::diagnostics::MovieMoment movie_moment(uint32_t frame) {
    disruptor::diagnostics::MovieMoment moment;
    moment.movie = g_watch.movie;
    moment.frame = frame;
    moment.pitch = g_watch.pitch;
    moment.pictures_a_second = disruptor_language_disc_sound_start(g_watch.movie) > 0 ? 25 : 30;
    moment.sound_seconds = g_watch.pitch ? g_watch.bytes / bytes_a_second(g_watch.pitch) : 0.0;
    moment.picture_seconds = static_cast<double>(frame - g_watch.keyed_frame) / moment.pictures_a_second;
    moment.leaving_ms = disruptor_diagnostics_sound_leaving_ms();
    return moment;
}

/* Once a VBlank: a frame number that comes back small begins a movie, one that stays still ends it. */
void watch_movie() {
    const uint32_t vblank = psx_mod_read_word(kVBlanks);
    if (vblank == g_watch.vblank) return;
    const uint32_t elapsed = vblank - g_watch.vblank;
    g_watch.vblank = vblank;
    const uint32_t frame = psx_mod_read_word(kFrameNumber);
    if (elapsed > kMostWatchGap && g_watch.playing) {
        /* The log was off or the game stood still: loops of the voice may have gone uncounted. */
        g_watch.joined_late = true;
        g_watch.sounding = false;
    }
    if (frame != g_watch.frame) {
        if (frame < g_watch.frame || (!g_watch.playing && frame > 0u)) {
            g_watch.playing = true;
            g_watch.sounding = false;
            g_watch.movie = psx_mod_read_word(kMovie);
            const int start = disruptor_language_disc_sound_start(g_watch.movie);
            g_watch.joined_late = frame > static_cast<uint32_t>(start > 0 ? start : 2) + kLateJoinFrames;
            disruptor_diagnostics_note("movie %u begins at frame %u, its sound %s", g_watch.movie, frame,
                                       start > 0 ? "waits for the frame the PAL player starts it on" : "starts with the US player's first frame");
        }
        g_watch.frame = frame;
        g_watch.frame_since = vblank;
    } else if (g_watch.playing && vblank - g_watch.frame_since > kStillVBlanks) {
        if (g_watch.joined_late) {
            disruptor_diagnostics_note("movie %u ended at frame %u, watched from part way, so no sound against picture", g_watch.movie,
                                       frame);
        } else if (g_watch.sounding) {
            const disruptor::diagnostics::MovieMoment moment = movie_moment(frame);
            disruptor_diagnostics_note("movie %u ended at frame %u, sound minus picture %+.0f ms in the game at the end", g_watch.movie,
                                       frame, (moment.sound_seconds - moment.picture_seconds) * 1000.0);
        } else {
            disruptor_diagnostics_note("movie %u ended at frame %u without its sound starting", g_watch.movie, frame);
        }
        g_watch.playing = g_watch.sounding = false;
        return;
    }
    if (!g_watch.playing) return;
    SpuVoiceState voice{};
    spu_get_voice_state(kMovieVoice, &voice);
    if (!g_watch.sounding) {
        if (!voice.active || g_watch.joined_late) return;
        /* Both clocks start where the player means them to: the PAL start frame, and the voice's own start address. */
        const int start = disruptor_language_disc_sound_start(g_watch.movie);
        g_watch.sounding = true;
        g_watch.keyed_frame = start > 0 ? static_cast<uint32_t>(start) : frame;
        g_watch.pitch = voice.pitch;
        g_watch.address = voice.cur_addr;
        const uint32_t origin = static_cast<uint32_t>(voice.start_lo) * 8u;
        g_watch.bytes = voice.cur_addr >= origin ? static_cast<double>(voice.cur_addr - origin) : 0.0;
        g_watch.next_note = vblank + kNoteVBlanks;
        disruptor_diagnostics_note("movie %u sound begins at frame %u, pitch 0x%X, %.0f ms of it already played", g_watch.movie, frame,
                                   voice.pitch, voice.pitch ? 1000.0 * g_watch.bytes / bytes_a_second(voice.pitch) : 0.0);
        return;
    }
    if (!voice.active) return;
    /* The voice loops over two halves its refills write: a step down, or past twice the pitch's own, is the loop. */
    const double expected = voice.pitch / 4096.0 * kSamplesPerVBlank * kBytesPerSample * elapsed;
    const double step = static_cast<double>(voice.cur_addr) - static_cast<double>(g_watch.address);
    g_watch.bytes += step >= 0.0 && step <= 2.0 * expected + 64.0 ? step : expected;
    g_watch.address = voice.cur_addr;
    if (vblank - g_watch.next_note < 0x80000000u) {
        g_watch.next_note = vblank + kNoteVBlanks;
        disruptor_diagnostics_note("%s", disruptor::diagnostics::movie_line(movie_moment(frame)).c_str());
    }
}

bool active() {
    return g_hooked && psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0;
}

void vsync_entry(CPUState *cpu, uint32_t) {
    if (!cpu) return;
    if (disruptor_diagnostics_detailed()) watch_movie();
    if (g_holding && psx_mod_read_word(kVBlanks) - g_held_at > kMostVBlanks) g_holding = false;
    if (cpu->gpr[kReturn] != kFrameShown || cpu->gpr[kWait] != 0u) return;
    uint32_t &started = cpu->gpr[kStarted];
    if (!active()) {
        if (started == kHeld) started = 0u;
    } else {
        const int64_t start = disruptor_language_disc_sound_start(psx_mod_read_word(kMovie));
        const int64_t frame = psx_mod_read_word(kFrameNumber);
        if (started == kHeld) {
            if (frame >= start) started = 0u;
        } else if ((started & 0xFFu) == 0u && frame < start) {
            started = kHeld;
        }
    }
    g_holding = started == kHeld;
    g_held_at = psx_mod_read_word(kVBlanks);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_language_movie_sound) {
    g_hooked = psx_mod_register_function_entry_plugin("disruptor.language.movie_sound", kVSync, vsync_entry) != 0;
    if (!g_hooked) std::fprintf(stderr, "disruptor: failed to register the hook for the movies' sound start\n");
}

}  // namespace

extern "C" int disruptor_language_movie_sound_save_deferred(void) { return g_holding ? 1 : 0; }

extern "C" void disruptor_language_movie_sound_state_loaded(void) { g_holding = false; }
