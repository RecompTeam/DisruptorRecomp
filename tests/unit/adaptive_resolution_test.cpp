#include "game/render/adaptive_resolution.h"

#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

using namespace disruptor::adaptive_resolution;

void work_is_the_busy_total_between_readings() {
    expect(work_seconds(100.0, 112.5) == 0.0125, "12.5 ms of GPU time between readings is 0.0125 s of work");
    expect(work_seconds(100.0, 100.0) == 0.0, "no GPU time is no work");
    expect(work_seconds(100.0, 90.0) == 0.0, "a total that went back is no work, never a negative one");
}

void a_slow_frame_is_judged_and_a_pause_is_not() {
    const double period = 1.0 / 59.94;
    expect(gpu_bound(0.1, 0.09, period, 4.0), "100 ms with the GPU busy for 90 is past the gap and GPU-bound");
    expect(!gpu_bound(0.1, 0.02, period, 4.0), "100 ms with the GPU busy for 20 is a pause");
    expect(!gpu_bound(0.03, 0.03, period, 4.0), "a frame shorter than the gap is not past it");
    expect(gpu_bound(0.03, 0.03, period, 1.1), "30 ms with the GPU busy all of it is a late GPU-bound frame");
    expect(judged_wall(0.1, true, true, period, 4.0) == period * 4.0,
           "past the gap after a late GPU-bound frame is a card too slow: judged, at the edge of the gap");
    expect(judged_wall(0.1, true, false, period, 4.0) == 0.1, "a lone one stays a gap");
    expect(judged_wall(0.1, false, true, period, 4.0) == 0.1, "a pause after a late frame stays a gap");
    expect(judged_wall(0.03, false, false, period, 4.0) == 0.03, "a frame shorter than the gap is passed as it was");
}

void only_the_game_s_own_world_is_judged() {
    expect(!held(true, false, false, false), "the world shown with nothing else is judged");
    expect(held(false, false, false, false), "a menu, a movie or a load screen is held");
    expect(held(true, true, false, false), "the in-game menu is held");
    expect(held(true, false, true, false), "a capsule replay is held, its pictures stay at the recorded scale");
    expect(held(true, false, false, true), "netplay is held");
}

void the_controller_steps_down_under_load_and_never_below_the_floor() {
    DynresController controller;
    DynresParams params;
    dynres_default_params(&params);
    dynres_init(&controller, &params, 1, 3, 3);
    const double period = 1.0 / 59.94;
    double now = 10.0;
    int level = 3;
    for (int i = 0; i < 1200; ++i) {
        const double work = period * (0.3 + 0.1 * level * level);  // a frame's GPU time: 1.2 of the period at 3x, 0.7 at 2x
        const double wall = work > period ? work : period;
        now += wall;
        const DynresSample sample{period, wall, work, 0};
        level = dynres_sample(&controller, now, &sample);
    }
    expect(level == 2, "a GPU over its frame at 3x settles at 2x, where the frame fits with room, not lower");
}

} // namespace

int main() {
    work_is_the_busy_total_between_readings();
    a_slow_frame_is_judged_and_a_pause_is_not();
    only_the_game_s_own_world_is_judged();
    the_controller_steps_down_under_load_and_never_below_the_floor();
    if (g_failures) return 1;
    std::cout << "adaptive resolution: PASS\n";
    return 0;
}