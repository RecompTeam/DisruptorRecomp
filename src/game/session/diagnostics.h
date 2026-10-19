#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::diagnostics {

struct Stretch {
    uint32_t frames = 0;
    double waited = 0.0;   // seconds, summed
    double longest = 0.0;  // seconds, one frame
    void frame(double seconds);
};

struct Moment {
    uint32_t game_frames = 0, game_vblanks = 0, late = 0;  // the game's last window, as the frame rate module measures it
    const char* screen = "world";
    bool focused = true, minimized = false;
    uint32_t repeated = 0, lost = 0;  // driver messages said too often to print, and ones the queue had no room for
    double sound_queued_ms = -1.0;    // negative without a sound device
    uint64_t sound_underruns = 0;     // since the pulse before, or since the count restarted
    double sound_target_ms = 0.0, sound_sdl_ms = -1.0;  // the queue's aim, and what SDL holds after it (negative: not told)
    double sound_trim = 0.0;          // the resampler's rate correction, a share of one
    uint64_t sound_drops = 0;         // overflow drops since the pulse before
};

/// How the sound leaves: SDL's driver, the device, the rates, and which of the variables that steer SDL's sound are set.
struct SoundSetup {
    std::string driver, device;
    int device_rate = 0, frames = 0;
    bool legacy = false;
    std::vector<std::string> environment;  // NAME=value of each one set
};
std::string sound_setup_line(const SoundSetup& setup);

/// One look at a movie: the sound's place in its own data, the picture's, and how long sound takes to leave the game.
struct MovieMoment {
    uint32_t movie = 0, frame = 0;
    double sound_seconds = 0.0, picture_seconds = 0.0, leaving_ms = -1.0;
    int pictures_a_second = 0;
    uint16_t pitch = 0;
};
std::string movie_line(const MovieMoment& moment);

std::string pulse_line(const Stretch& stretch, const Moment& moment);

/// The sound's way out: the device's rate and buffer, `frames` 0 when the device does not tell it, and the queue held before it.
std::string sound_line(int rate, int frames, double queue_ms);

/// A line for each name that left `before` and each that came with `now`. A name stands as often as a device has it.
std::vector<std::string> device_changes(const std::vector<std::string>& before, const std::vector<std::string>& now);

/// Each kind of driver message is printed a few times, then counted.
class DriverMessages {
public:
    static constexpr uint32_t printed_of_a_kind = 3;
    /// The line to print, or empty for a kind said `printed_of_a_kind` times already.
    std::string line(uint32_t id, int severity, int context, std::string_view text);
    uint32_t take_repeated();

private:
    std::map<uint32_t, uint32_t> said_;
    uint32_t repeated_ = 0;
};

/// One step of the session's log at `seconds` since the process began. The game's frame hook calls it with the clock.
void frame_at(double seconds);

}  // namespace disruptor::diagnostics

extern "C" {
#endif

/* Driver messages: 0 not asked, 1 printed, 2 asked after the contexts were made, so from the next launch, 3 refused. */
enum { DISRUPTOR_DRIVER_OFF = 0, DISRUPTOR_DRIVER_ON = 1, DISRUPTOR_DRIVER_NEXT_LAUNCH = 2, DISRUPTOR_DRIVER_REFUSED = 3 };

/* At start, before the game's window: the saved setting, which PSX_DISRUPTOR_DETAILED_LOGGING replaces for a run. */
void disruptor_diagnostics_configure(int saved);
int disruptor_diagnostics_detailed(void);
void disruptor_diagnostics_set_detailed(int enabled);
int disruptor_diagnostics_driver_state(void);
/* A line of the detailed log with its time, printf style. Nothing while detailed logging is off. */
void disruptor_diagnostics_note(const char *format, ...);
/* How long a sample made now takes to leave for the device, in ms: the queue, SDL's own hold, the device's buffer. Negative: no sound. */
double disruptor_diagnostics_sound_leaving_ms(void);

#ifdef __cplusplus
}
#endif
