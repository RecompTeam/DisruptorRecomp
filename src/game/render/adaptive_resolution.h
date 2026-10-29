#pragma once

/* Dynamic internal resolution: upstream's step controller fed with the GPU time of both contexts, see the .cpp. */

#ifdef __cplusplus
#include "dynamic_resolution.h"

namespace disruptor::adaptive_resolution {

/* The GPU time between two readings of the renderer's busy total, in seconds. */
inline double work_seconds(double before_ms, double after_ms) {
    return after_ms > before_ms ? (after_ms - before_ms) / 1000.0 : 0.0;
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
