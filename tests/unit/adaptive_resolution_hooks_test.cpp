#include "game/render/adaptive_resolution.h"

#include <cstdarg>
#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void (*g_frame_hook)(void) = nullptr;
double g_now = 100.0, g_busy_ms = 0.0;
int g_scale = 3, g_sets = 0, g_refuse = 0, g_busy_wanted = 0;

constexpr double kPeriod = 1.0 / 59.94;

double clock_now() { return g_now; }

/* A scene whose GPU time grows with the pixels: 33 ms at 3x, 18 ms at 2x, 9 ms at 1x; a frame lasts at least the period. */
void frames(int count) {
    for (int i = 0; i < count; ++i) {
        const double busy = (6.0 + 3.0 * g_scale * g_scale) / 1000.0;
        g_now += busy > kPeriod ? busy : kPeriod;
        g_busy_ms += busy * 1000.0;
        g_frame_hook();
    }
}

} // namespace

extern "C" {
void mod_register_frame_hook(void (*hook)(void)) { g_frame_hook = hook; }
void gl_renderer_gpu_busy_enable(int enabled) { g_busy_wanted = enabled; }
double gl_renderer_gpu_busy_ms(void) { return g_busy_ms; }
int gpu_ws_displayed_flat(void) { return 0; }
int gpu_ws_displayed_menu(void) { return 0; }
int psx_host_ui_gl_visible(void) { return 0; }
int psx_netplay_active(void) { return 0; }
int psx_host_video_get_internal_scale(void) { return g_scale; }
int psx_host_video_set_internal_scale(int scale) {
    ++g_sets;
    if (g_refuse) return 0;
    g_scale = scale;
    return 1;
}
void disruptor_diagnostics_note(const char *, ...) {}
}

int main() {
    disruptor_adaptive_resolution_set_clock(clock_now);
    expect(g_frame_hook != nullptr, "the module puts its frame hook in");

    expect(disruptor_adaptive_resolution_set_scale(3) == 1 && g_scale == 3, "the player's scale is applied");
    disruptor_adaptive_resolution_set_enabled(1);
    expect(g_busy_wanted == 1, "turning it on asks the renderer for its GPU time");
    frames(900);
    expect(g_scale < 3, "a GPU over its frame for fifteen seconds lowers the scale");
    expect(disruptor_adaptive_resolution_scale() == 3, "the menu keeps showing and saving the player's scale");

    disruptor_adaptive_resolution_set_enabled(0);
    expect(g_scale == 3 && g_busy_wanted == 0, "turning it off draws at the player's scale again and stops the timing");

    disruptor_adaptive_resolution_set_enabled(1);
    frames(900);
    const int lowered = g_scale;
    g_refuse = 1;
    disruptor_adaptive_resolution_set_enabled(0);
    expect(g_scale == lowered && disruptor_adaptive_resolution_scale() == 3,
           "a refused way back leaves the drawn scale where it was and the player's scale as asked");
    g_refuse = 0;
    g_scale = 3;

    disruptor_adaptive_resolution_set_enabled(1);
    g_refuse = 1;
    const int before = g_sets;
    const double began = g_now;
    frames(900);
    expect(g_scale == 3 && g_sets - before <= static_cast<int>((g_now - began) / 10.0) + 1,
           "a refused step is asked again once a 10 s hold is over, not every frame");
    g_refuse = 0;

    if (g_failures) return 1;
    std::cout << "adaptive resolution hooks: PASS\n";
    return 0;
}