#pragma once

#include <stdint.h>

#ifdef __cplusplus
#include <map>
#include <string>
#include <string_view>
#include <vector>

// What the game's log holds for a bug report: a few lines of every session, and more while detailed logging is on.
namespace disruptor::diagnostics {

/// The frames of a stretch of a session, gathered a frame at a time.
struct Stretch {
    uint32_t frames = 0;
    double waited = 0.0;   // seconds, the sum over the frames
    double longest = 0.0;  // seconds, the longest wait for one frame
    void frame(double seconds);
};

/// What a pulse says beside its stretch.
struct Moment {
    uint32_t game_frames = 0, game_vblanks = 0, late = 0;  // the game's own last window, as the frame rate module measures it
    const char* screen = "world";
    bool focused = true, minimized = false;
    uint32_t repeated = 0, lost = 0;  // driver messages said too often to print, and ones the queue had no room for
};

/// The log's line for a stretch: frames the frame loop finished, their mean and longest wait, and the moment.
std::string pulse_line(const Stretch& stretch, const Moment& moment);

/// A line for each name that left `before` and each that came with `now`. A name stands as often as a device has it.
std::vector<std::string> device_changes(const std::vector<std::string>& before, const std::vector<std::string>& now);

/// The graphics driver's messages, each kind printed a few times and counted from then on.
class DriverMessages {
public:
    static constexpr uint32_t printed_of_a_kind = 3;
    /// The line to print, or empty for a kind said `printed_of_a_kind` times already.
    std::string line(uint32_t id, int severity, int context, std::string_view text);
    /// How many were left unprinted since the last call.
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

#ifdef __cplusplus
}
#endif
