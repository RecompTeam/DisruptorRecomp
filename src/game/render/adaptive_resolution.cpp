#include "game/render/adaptive_resolution.h"

#include "game/session/diagnostics.h"

#include "mod_plugins.h"
#include "psx_netplay.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" void mod_register_frame_hook(void (*hook)(void));
extern "C" void gl_renderer_gpu_busy_enable(int enabled);
extern "C" double gl_renderer_gpu_busy_ms(void);
extern "C" int gpu_ws_displayed_flat(void);
extern "C" int gpu_ws_displayed_menu(void);
extern "C" int psx_host_ui_gl_visible(void);
extern "C" int psx_host_video_get_internal_scale(void);
extern "C" int psx_host_video_set_internal_scale(int scale);

namespace {

using namespace disruptor::adaptive_resolution;

constexpr double kPeriod = 1.0 / 59.94;
constexpr int kFloor = 1;
constexpr double kEntryHold = 5.0;   // a level's first seconds stream its sectors in
constexpr double kRefusedHold = 10.0;

struct State {
    bool enabled = false, world = false;
    int ceiling = 0, requested = 0;  // the controller's top, and the player's scale kept apart from what is drawn
    DynresController controller{};
    double last = 0.0;
    double busy_ms = 0.0;
};

State g;
const bool g_replaying = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr;
int forced() {  // a run's own choice over the saved one, read where the module's constructor may run before its globals
    static const int value = std::getenv("PSX_DYNRES") ? std::atoi(std::getenv("PSX_DYNRES")) : -1;
    return value;
}
const double g_work_scale = [] {
    const char* scale = std::getenv("PSX_DYNRES_WORK_SCALE");  // a weaker card simulated on a strong one
    return scale ? std::atof(scale) : 1.0;
}();

double steady_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

double (*g_clock)(void) = steady_seconds;

double seconds() { return g_clock(); }

void restart(int ceiling) {
    g.ceiling = g.requested = ceiling;
    DynresParams p;
    dynres_default_params(&p);
    dynres_init(&g.controller, &p, ceiling < kFloor ? ceiling : kFloor, ceiling, ceiling);
    g.last = 0.0;
}

void apply(int level, double now) {
    const int from = psx_host_video_get_internal_scale();
    if (level == from) return;
    const double began = seconds();
    if (psx_host_video_set_internal_scale(level)) {
        dynres_note_step_cost(&g.controller, seconds() - began);
        std::fprintf(stdout, "disruptor: dynamic resolution %dx to %dx, GPU load %.2f, %s, %.1f ms\n", from, level,
                     g.controller.last_load, g.controller.last_reason ? g.controller.last_reason : "", (seconds() - began) * 1000.0);
        return;
    }
    g.controller.level = from;
    dynres_hold(&g.controller, now, kRefusedHold);
    std::fprintf(stdout, "disruptor: dynamic resolution %dx refused, staying at %dx\n", level, from);
}

void frame() {
    if (!g.enabled) return;
    if (g.ceiling <= 0) restart(g.requested > 0 ? g.requested : psx_host_video_get_internal_scale());
    if (g.ceiling <= kFloor) return;
    const double now = seconds();
    const double busy_ms = gl_renderer_gpu_busy_ms();
    if (g.last == 0.0) {
        g.last = now;
        g.busy_ms = busy_ms;
        return;
    }
    const bool world = !gpu_ws_displayed_flat() && !gpu_ws_displayed_menu();
    if (world && !g.world) dynres_hold(&g.controller, now, kEntryHold);
    g.world = world;
    const DynresSample sample{kPeriod, now - g.last, work_seconds(g.busy_ms, busy_ms) * g_work_scale,
                              held(world, psx_host_ui_gl_visible() != 0, g_replaying, psx_netplay_active() != 0)};
    g.last = now;
    g.busy_ms = busy_ms;
    const unsigned long long windows = g.controller.windows + g.controller.held_windows;
    const int level = dynres_sample(&g.controller, now, &sample);
    if (g.controller.windows + g.controller.held_windows != windows)
        disruptor_diagnostics_note("dynamic resolution window: %dx, GPU load %.2f, %d late%s", g.controller.level,
                                   g.controller.last_load, g.controller.last_late, sample.held ? ", held" : "");
    apply(level, now);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_adaptive_resolution) {
    mod_register_frame_hook(frame);
    if (forced() >= 0) disruptor_adaptive_resolution_set_enabled(forced());
}

} // namespace

extern "C" void disruptor_adaptive_resolution_set_enabled(int enabled) {
    if (forced() >= 0) enabled = forced();
    if ((enabled != 0) == g.enabled) return;
    g.enabled = enabled != 0;
    gl_renderer_gpu_busy_enable(enabled);
    if (!g.enabled && g.requested > 0 && psx_host_video_get_internal_scale() != g.requested &&
        !psx_host_video_set_internal_scale(g.requested))
        std::fprintf(stdout, "disruptor: dynamic resolution off, the scale %dx was refused, %dx stays\n", g.requested,
                     psx_host_video_get_internal_scale());
    g.ceiling = 0;
}

extern "C" int disruptor_adaptive_resolution_enabled(void) { return g.enabled ? 1 : 0; }

extern "C" void disruptor_adaptive_resolution_set_clock(double (*clock)(void)) { g_clock = clock ? clock : steady_seconds; }

extern "C" int disruptor_adaptive_resolution_set_scale(int scale) {
    const int applied = psx_host_video_set_internal_scale(scale);
    if (!applied) return 0;
    g.requested = scale;
    if (g.enabled) restart(scale);
    return applied;
}

extern "C" int disruptor_adaptive_resolution_scale(void) {
    return g.requested > 0 ? g.requested : psx_host_video_get_internal_scale();
}
