#include "game/render/archive_terminal.h"

#include "game/frame/stock_frames.h"
#include "game/input/vertical_camera.h"
#include "mod_plugins.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

extern "C" uint16_t sio_get_pad_buttons_slot(int slot);

namespace disruptor::archive_terminal {
namespace {

constexpr uint32_t kPlayerYaw = 0x80077624u;
constexpr uint16_t kUse = 1u << 12;
constexpr double kInteractionReach = 1000.0;
constexpr std::array<uint32_t, DISRUPTOR_ARCHIVE_FRAME_COUNT> kFrameEnds{8, 19, 46, 54, 88, 133, 151, 164};

std::optional<uint32_t> g_start;
bool g_use_down = false;
bool g_pressed = false;

bool aimed_at_screen(const int32_t (&eye)[3]) {
    const double across = static_cast<double>(DISRUPTOR_ARCHIVE_SCREEN_X) - eye[0];
    if (across <= 0.0 || across > kInteractionReach) return false;

    const double yaw = psx_mod_read_byte(kPlayerYaw) * (2.0 * std::numbers::pi / 256.0);
    const double forward_x = -std::sin(yaw);
    if (forward_x <= 0.0) return false;
    const double distance = across / forward_x;
    const double slope = (120.0 - disruptor_vertical_camera_projection_row16() / 65536.0) / 160.0;
    const double rise = distance * slope;
    if (std::hypot(distance, rise) > kInteractionReach) return false;

    const double height = eye[1] + rise;
    const double along = eye[2] + distance * std::cos(yaw);
    const double u = (static_cast<double>(DISRUPTOR_ARCHIVE_PANEL_LEFT_Z) - along) * 31.0 /
                     (DISRUPTOR_ARCHIVE_PANEL_LEFT_Z - DISRUPTOR_ARCHIVE_PANEL_MID_Z);
    const double v = (height - static_cast<double>(DISRUPTOR_ARCHIVE_SCREEN_TOP)) * 31.0 /
                     (DISRUPTOR_ARCHIVE_SCREEN_BOTTOM - DISRUPTOR_ARCHIVE_SCREEN_TOP);
    const double corner = (v < DISRUPTOR_ARCHIVE_GLASS_TOP + DISRUPTOR_ARCHIVE_GLASS_CORNER ||
                           v >= DISRUPTOR_ARCHIVE_GLASS_BOTTOM - DISRUPTOR_ARCHIVE_GLASS_CORNER)
                              ? DISRUPTOR_ARCHIVE_GLASS_CORNER : 0.0;
    return u >= DISRUPTOR_ARCHIVE_GLASS_LEFT + corner && u < DISRUPTOR_ARCHIVE_GLASS_RIGHT - corner &&
           v >= DISRUPTOR_ARCHIVE_GLASS_TOP && v < DISRUPTOR_ARCHIVE_GLASS_BOTTOM;
}

}

void after_input() {
    const bool use_down = (sio_get_pad_buttons_slot(0) & kUse) == 0u;
    if (!std::exchange(g_use_down, use_down) && use_down && disruptor_vertical_camera_input_allowed()) g_pressed = true;
}

void update(bool in_gauntlet, const int32_t (&eye)[3]) {
    const bool pressed = std::exchange(g_pressed, false);
    if (!in_gauntlet) {
        if (g_start) state_loaded();
        return;
    }

    if (!g_start && !pressed) return;

    const uint32_t tick = psx_mod_read_word(stock_frames::kTicks);
    if (g_start && tick - *g_start >= kFrameEnds.back()) {
        g_start.reset();
        gl_renderer_set_archive_terminal(0, 0, 0, 0);
    }
    if (!g_start && pressed && disruptor_vertical_camera_input_allowed() && aimed_at_screen(eye)) g_start = tick;
    if (!g_start) return;

    const auto next = std::upper_bound(kFrameEnds.begin(), kFrameEnds.end(), tick - *g_start);
    gl_renderer_set_archive_terminal(static_cast<int>(next - kFrameEnds.begin()) + 1, eye[0], eye[1], eye[2]);
}

void state_loaded() {
    g_use_down = (sio_get_pad_buttons_slot(0) & kUse) == 0u;
    g_pressed = false;
    if (!g_start) return;
    g_start.reset();
    gl_renderer_set_archive_terminal(0, 0, 0, 0);
}

}
