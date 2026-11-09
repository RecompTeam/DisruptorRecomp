#pragma once

/* Dynamic internal resolution: upstream's step controller fed with the game's own GPU time, see the .cpp. */

#ifdef __cplusplus
#include "dynamic_resolution.h"
#include "host_ui_flags.h"

#include <cstdint>

namespace disruptor::adaptive_resolution {

/* The GPU time between two readings of the renderer's busy total, in seconds. */
inline double work_seconds(double before_ms, double after_ms) {
    return after_ms > before_ms ? (after_ms - before_ms) / 1000.0 : 0.0;
}

/* An interval longer than `factor` periods that the GPU was busy for most of. */
inline bool gpu_bound(double wall, double work, double period, double factor) {
    return wall > period * factor && work >= 0.75 * wall;
}

/* A GPU-bound interval past the gap right after a late GPU-bound one is judged at the gap's edge: a card too slow, also
 * when its frames straddle the gap. A lone one, as a stall that drained queued frames, stays a pause. */
inline double judged_wall(double wall, bool slow, bool after_late, double period, double gap_factor) {
    return slow && after_late ? period * gap_factor : wall;
}

/* The host's UI as a menu: it takes input or stops the in-between frames. The crosshair and popups laid over the
 * picture do neither, and the game's GPU time under them is still the game's. */
inline bool menu_shown(std::uint32_t ui_flags) {
    constexpr std::uint32_t kInput = PSX_HOST_UI_CAPTURE_KEYBOARD | PSX_HOST_UI_CAPTURE_MOUSE | PSX_HOST_UI_CAPTURE_GAMEPAD;
    return (ui_flags & PSX_HOST_UI_VISIBLE) != 0 && (psx_host_ui_stops_interpolation(ui_flags) || (ui_flags & kInput) != 0);
}

/* Nothing is judged while the picture is not the world or the frame is not the game's own. */
inline bool held(bool world, bool menu_open, bool replaying, bool netplay) {
    return !world || menu_open || replaying || netplay;
}

} // namespace disruptor::adaptive_resolution

extern "C" {
#endif

void disruptor_adaptive_resolution_set_enabled(int enabled);
int disruptor_adaptive_resolution_enabled(void);
/* The player's internal scale, the ceiling while adapting. 0 when the renderer refused it. */
int disruptor_adaptive_resolution_set_scale(int scale);
int disruptor_adaptive_resolution_scale(void);
/* A test's own clock in seconds, NULL for the steady one. */
void disruptor_adaptive_resolution_set_clock(double (*clock)(void));

#ifdef __cplusplus
}
#endif
