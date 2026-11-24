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
    constexpr std::uint32_t kLayer = PSX_HOST_UI_VISIBLE | PSX_HOST_UI_LAYER;
    expect(!menu_shown(0u) && !menu_shown(kLayer) && !menu_shown(kLayer | PSX_HOST_UI_APART),
           "the crosshair or a popup over the picture holds nothing");
    expect(menu_shown(PSX_HOST_UI_VISIBLE | PSX_HOST_UI_CAPTURE_KEYBOARD | PSX_HOST_UI_CAPTURE_MOUSE) &&
               menu_shown(PSX_HOST_UI_VISIBLE) && menu_shown(kLayer | PSX_HOST_UI_CAPTURE_GAMEPAD) &&
               menu_shown(kLayer | PSX_HOST_UI_SUSPEND_INTERPOLATION),
           "the open menu, the wheel, a browser taking input or a UI stopping the in-between frames holds");
    expect(!menu_shown(PSX_HOST_UI_CAPTURE_MOUSE), "input taken with nothing shown is no menu on screen");
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
        const DynresSample sample{period, wall, work, 0, 1};
        level = dynres_sample(&controller, now, &sample);
    }
    expect(level == 2, "a GPU over its frame at 3x settles at 2x, where the frame fits with room, not lower");
}

void incomplete_timing_skips_down_step_verification() {
    DynresController controller;
    DynresParams params;
    dynres_default_params(&params);
    constexpr double period = 0.03125;
    params.window_s = 4.0 * period;
    params.down_late = 1;
    params.target_load = 0.90;
    params.prior_scaled = 0.80;
    params.cooldown_down_s = 10.0;
    params.verify_fraction = 0.25;
    dynres_init(&controller, &params, 1, 3, 3);
    double now = 10.0;
    int level = 3;
    for (int index = 0; index < 4; ++index) {
        now += 1.5 * period;
        const DynresSample sample{period, 1.5 * period, index < 3 ? 1.6 * period : 0.0, 0, index < 3 ? 1 : -1};
        level = dynres_sample(&controller, now, &sample);
    }
    expect(level == 2 && !controller.post_active, "an incomplete window can ask for a lower resolution without scheduling verification");
    for (int index = 0; index < 8; ++index) {
        now += period;
        const DynresSample sample{period, period, 1.6 * period, 0, 1};
        level = dynres_sample(&controller, now, &sample);
    }
    expect(level == 2 && controller.undo_level == 0, "more complete timing does not undo a successful down step");
}

void valid_zero_work_counts_toward_coverage() {
    DynresController controller;
    DynresParams params;
    dynres_default_params(&params);
    constexpr double period = 0.03125;
    params.window_s = 4.0 * period;
    dynres_init(&controller, &params, 1, 3, 3);
    for (int index = 0; index < 4; ++index) {
        const DynresSample sample{period, period, index % 2 ? 0.0 : period, 0, 1};
        dynres_sample(&controller, 10.0 + index * period, &sample);
    }
    expect(controller.last_valid && controller.last_complete && controller.last_load == 0.5,
           "valid idle intervals count in the measured period");
}

void complete_idle_window_verifies_down_step() {
    DynresController controller;
    DynresParams params;
    dynres_default_params(&params);
    constexpr double period = 0.03125;
    params.window_s = 4.0 * period;
    params.down_late = 1;
    params.target_load = 0.90;
    params.prior_scaled = 0.80;
    dynres_init(&controller, &params, 1, 3, 3);
    double now = 10.0;
    for (int index = 0; index < 4; ++index) {
        now += 1.5 * period;
        const DynresSample sample{period, 1.5 * period, 1.6 * period, 0, 1};
        dynres_sample(&controller, now, &sample);
    }
    expect(controller.level == 2 && controller.post_active, "a fully measured down step is scheduled for verification");
    const double before = controller.f;
    for (int index = 0; index < 8; ++index) {
        now += period;
        const DynresSample sample{period, period, index < 4 ? 0.8 * period : 0.0, 0, 1};
        dynres_sample(&controller, now, &sample);
    }
    expect(controller.last_valid && !controller.post_active && controller.undo_level == 0 && controller.f > before,
           "a complete idle verification window is accepted and learned from");
}

void batched_gpu_results_cover_every_measured_frame() {
    DynresController controller;
    DynresParams params;
    dynres_default_params(&params);
    constexpr double period = 0.03125;
    params.window_s = 4.0 * period;
    constexpr int readings[][4] = {{0, 2, 1, 1}, {2, 1, 1, 0}};
    for (const auto &window : readings) {
        dynres_init(&controller, &params, 1, 3, 3);
        double now = 10.0;
        for (const int frames : window) {
            const DynresSample sample{period, period, frames * period * 0.75, 0, frames ? frames : -1};
            dynres_sample(&controller, now += period, &sample);
        }
        expect(controller.last_valid && !controller.last_complete && controller.last_load == 0.75,
               "batched GPU timings estimate load without certifying an aligned window");
    }
    params.up_after_s = params.window_s;
    dynres_init(&controller, &params, 1, 3, 2);
    double now = 10.0;
    for (const int frames : readings[0]) {
        const DynresSample sample{period, period, frames * period * 0.25, 0, frames ? frames : -1};
        dynres_sample(&controller, now += period, &sample);
    }
    const DynresSample quiet{period, period, period * 0.25, 0, 1};
    const bool ready = controller.up_ready != 0;
    const int level = dynres_sample(&controller, now + period, &quiet);
    expect(!ready && level == 2,
           "a quiet interval after an unpaired window cannot raise the resolution");
}

} // namespace

int main() {
    work_is_the_busy_total_between_readings();
    a_slow_frame_is_judged_and_a_pause_is_not();
    only_the_game_s_own_world_is_judged();
    the_controller_steps_down_under_load_and_never_below_the_floor();
    incomplete_timing_skips_down_step_verification();
    valid_zero_work_counts_toward_coverage();
    complete_idle_window_verifies_down_step();
    batched_gpu_results_cover_every_measured_frame();
    if (g_failures) return 1;
    std::cout << "adaptive resolution: PASS\n";
    return 0;
}
