#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace disruptor::sticks {

enum class Layout { classic, dual_analog, southpaw }; // the order of the Stick layout list

struct Tuning {
    Layout layout = Layout::dual_analog;
    int look_speed = 100;   // percent of the D-pad's own turn at a full push
    int look_deadzone = 20; // percent of the stick's length
    bool invert_look = false;
    bool analog_movement = true;
    bool vibration = false;
};

struct Axes {
    int lx = 0, ly = 0, rx = 0, ry = 0; // SDL's raw axes, right and down positive
};

inline constexpr int kFullMovement = 32767;

struct Movement {
    int16_t forward = 0;
    int16_t strafe = 0;

    explicit operator bool() const { return forward != 0 || strafe != 0; }
};

struct Reading {
    uint16_t press = 0; // PlayStation pad bits to press
    double turn = 0.0;  // heading units this VBlank, positive to the left as the game counts
    double tilt = 0.0;  // pitch units this VBlank, positive up
    Movement movement;
};

inline constexpr double kTurnUnits = 2.0; // the D-pad's turn: 318 units in 160 VBlanks, measured
inline constexpr double kTiltUnits = 1.0;
inline constexpr uint16_t kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7;
inline constexpr uint16_t kStrafeLeft = 1u << 8, kStrafeRight = 1u << 9; // L2 and R2

// Each axis past `threshold` alone, as the runtime's own D-pad fold.
inline uint16_t presses(int x, int y, int threshold, uint16_t left, uint16_t right) {
    uint16_t press = 0;
    if (y < -threshold) press |= kUp;
    if (y > threshold) press |= kDown;
    if (x < -threshold) press |= left;
    if (x > threshold) press |= right;
    return press;
}

inline void stick_push(int x, int y, double zone, bool squared, double &out_x, double &out_y) {
    out_x = out_y = 0.0;
    const double along_x = std::clamp(x / 32767.0, -1.0, 1.0), along_y = std::clamp(y / 32767.0, -1.0, 1.0);
    const double length = std::hypot(along_x, along_y);
    if (zone >= 1.0 || length <= zone) return;
    const double push = std::min(1.0, (length - zone) / (1.0 - zone));
    const double curve = squared ? push : 1.0;
    out_x = along_x * push * curve / length;
    out_y = along_y * push * curve / length;
}

inline Movement move(int x, int y, int threshold) {
    double right = 0.0, down = 0.0;
    stick_push(x, y, std::clamp(threshold, 0, kFullMovement) / 32767.0, false, right, down);
    return {static_cast<int16_t>(std::lround(-down * kFullMovement)), static_cast<int16_t>(std::lround(right * kFullMovement))};
}

// Outside play the left stick is the D-pad, as the menus need. Without `tilting` the turn reads X alone, a push up turns not.
inline Reading read(const Tuning &tuning, const Axes &axes, int threshold, bool playing, bool tilting) {
    Reading reading;
    const bool classic = tuning.layout == Layout::classic;
    if (!playing || classic) {
        const bool analog = playing && tuning.analog_movement;
        reading.press = presses(axes.lx, analog ? 0 : axes.ly, threshold, kLeft, kRight);
        if (analog) reading.movement = move(0, axes.ly, threshold);
        return reading;
    }
    const bool southpaw = tuning.layout == Layout::southpaw;
    const int move_x = southpaw ? axes.rx : axes.lx, move_y = southpaw ? axes.ry : axes.ly;
    if (tuning.analog_movement) reading.movement = move(move_x, move_y, threshold);
    else reading.press = presses(move_x, move_y, threshold, kStrafeLeft, kStrafeRight);
    double x = 0.0, y = 0.0;
    stick_push(southpaw ? axes.lx : axes.rx, tilting ? (southpaw ? axes.ly : axes.ry) : 0,
               std::clamp(tuning.look_deadzone, 0, 95) / 100.0, true, x, y);
    const double speed = std::clamp(tuning.look_speed, 0, 1000) / 100.0;
    reading.turn = -x * kTurnUnits * speed;
    reading.tilt = (tuning.invert_look ? y : -y) * kTiltUnits * speed;
    return reading;
}

Tuning tuning();
void tune(const Tuning &tuning);
Movement movement();
void set_movement(Movement movement);

} // namespace disruptor::sticks
