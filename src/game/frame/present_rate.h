#ifndef DISRUPTOR_PRESENT_RATE_H
#define DISRUPTOR_PRESENT_RATE_H

/* The range of the in-between frame rate control: from the retail 30 pictures a second up to the
 * display's refresh rate. The top is 120 when that rate is lower, unknown or past the host's 1000. */
constexpr int kDisruptorPresentRateLowest = 30;
constexpr int kDisruptorPresentRateLeastTop = 120;
constexpr int kDisruptorPresentRateHighest = 1000;

constexpr int disruptor_present_rate_top(double display_hz) {
    const int display = display_hz >= kDisruptorPresentRateLowest && display_hz <= kDisruptorPresentRateHighest
        ? static_cast<int>(display_hz + 0.5) : 0;
    return display > kDisruptorPresentRateLeastTop ? display : kDisruptorPresentRateLeastTop;
}

/* A rate the host takes at start: 0 for the display's rate, or one inside the host's range. */
constexpr bool disruptor_present_rate_accepted(int frames_per_second) {
    return frames_per_second == 0 ||
        (frames_per_second >= kDisruptorPresentRateLowest && frames_per_second <= kDisruptorPresentRateHighest);
}

/* What the control shows for the host's target: 0 there means "the display's rate". */
constexpr int disruptor_present_rate_shown(int target_fps, int top) {
    if (target_fps <= 0 || target_fps > top) return top;
    return target_fps < kDisruptorPresentRateLowest ? kDisruptorPresentRateLowest : target_fps;
}

/* Switches in-between frames on or off at the rate and blend the host has. Returns 0 when the host refused. */
int disruptor_present_rate_enable(int enabled);
/* Draws the switch: frames that are off cannot be switched on while `available` is 0. Returns 1 and the state in `enabled` when the user changed it and the host took it. */
int disruptor_present_rate_switch(int available, int *enabled);
/* Sets the rate of in-between frames that are on. Returns 0 when they are off or the host refused. */
int disruptor_present_rate_apply(int frames_per_second);
/* Draws the control. Returns 1 and the rate in `accepted` when the user changed it and the host took it. */
int disruptor_present_rate_control(int *accepted);

#endif
