#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One completed measurement window of gameplay frames (about one second). */
typedef struct DisruptorFrameRateWindow {
    uint32_t frames;
    uint32_t vblanks;
    uint32_t late_frames;
    uint32_t average_work_permille;
    uint32_t peak_work_permille;
} DisruptorFrameRateWindow;

/* Session-owned: the unlock is never written to settings.toml. */
int disruptor_frame_rate_unlocked(void);
void disruptor_frame_rate_set_unlocked(int enabled);

/* Guest CPU speed inside the unlocked frame loop: 1, 2, 4 or 8. */
int disruptor_frame_rate_cpu_multiplier(void);
void disruptor_frame_rate_set_cpu_multiplier(int multiplier);

int disruptor_frame_rate_last_window(DisruptorFrameRateWindow *out);

#ifdef __cplusplus
}
#endif
