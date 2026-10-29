#include "game/input/sticks.h"
#include "game/settings/pad_layout.h"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

using namespace disruptor::sticks;

int g_failures = 0;
constexpr int kFull = 32767, kThreshold = 12000;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool close(double actual, double expected) { return std::abs(actual - expected) < 0.000001; }

Tuning layout(Layout chosen) {
    Tuning tuning;
    tuning.layout = chosen;
    return tuning;
}

void test_classic_is_the_d_pad() {
    const Tuning classic = layout(Layout::classic);
    expect(read(classic, {-kFull, 0, 0, 0}, kThreshold, true, true).press == kLeft, "a push left turns left");
    expect(read(classic, {kFull, kFull, 0, 0}, kThreshold, true, true).press == (kRight | kDown), "each axis presses its own way");
    expect(read(classic, {kThreshold, -kThreshold, 0, 0}, kThreshold, true, true).press == 0, "the dead zone's edge presses nothing");
    const Reading right = read(classic, {0, 0, kFull, -kFull}, kThreshold, true, true);
    expect(right.press == 0 && right.turn == 0.0 && right.tilt == 0.0, "the right stick does nothing, as on the PlayStation");
}

void test_dual_analog() {
    const Tuning dual = layout(Layout::dual_analog);
    expect(read(dual, {-kFull, 0, 0, 0}, kThreshold, true, false).press == kStrafeLeft, "the left stick strafes left");
    expect(read(dual, {kFull, -kFull, 0, 0}, kThreshold, true, false).press == (kStrafeRight | kUp), "and right while walking on");
    expect(close(read(dual, {0, 0, kFull, 0}, kThreshold, true, false).turn, -kTurnUnits),
           "a full push right turns right as fast as the D-pad");
    expect(close(read(dual, {0, 0, -kFull, 0}, kThreshold, true, false).turn, kTurnUnits), "and left alike");
    const double rest = (16383 / 32767.0 - 0.2) / 0.8;
    expect(close(read(dual, {0, 0, 16383, 0}, kThreshold, true, false).turn, -kTurnUnits * rest * rest),
           "a half push past the dead zone turns by the square of the rest");
    expect(read(dual, {0, 0, static_cast<int>(kFull * 0.19), 0}, kThreshold, true, false).turn == 0.0,
           "a push inside the dead zone turns nothing");
}

void test_tilt() {
    Tuning dual = layout(Layout::dual_analog);
    expect(read(dual, {0, 0, 0, -kFull}, kThreshold, true, false).tilt == 0.0, "without Vertical look the stick does not tilt");
    expect(close(read(dual, {0, 0, 0, -kFull}, kThreshold, true, true).tilt, kTiltUnits), "pushed forward it looks up");
    dual.invert_look = true;
    expect(close(read(dual, {0, 0, 0, -kFull}, kThreshold, true, true).tilt, -kTiltUnits), "inverted it looks down");
    const Reading corner = read(layout(Layout::dual_analog), {0, 0, kFull, kFull}, kThreshold, true, true);
    expect(close(corner.turn, -kTurnUnits * std::sqrt(0.5)) && close(corner.tilt, -kTiltUnits * std::sqrt(0.5)),
           "a push to the corner is full along the stick, not along each axis");
    expect(close(read(layout(Layout::dual_analog), {0, 0, kFull, kFull}, kThreshold, true, false).turn, -kTurnUnits),
           "without the tilt the same push turns at full");
}

void test_speed_and_southpaw() {
    Tuning dual = layout(Layout::dual_analog);
    dual.look_speed = 200;
    expect(close(read(dual, {0, 0, kFull, 0}, kThreshold, true, false).turn, -2.0 * kTurnUnits), "the speed scales the turn");
    const Tuning southpaw = layout(Layout::southpaw);
    const Reading swapped = read(southpaw, {kFull, 0, -kFull, kFull}, kThreshold, true, false);
    expect(swapped.press == (kStrafeLeft | kDown) && close(swapped.turn, -kTurnUnits), "Southpaw swaps the sticks");
}

void test_menus_keep_the_d_pad() {
    for (const Layout chosen : {Layout::dual_analog, Layout::southpaw}) {
        const Reading menu = read(layout(chosen), {-kFull, kFull, kFull, 0}, kThreshold, false, true);
        expect(menu.press == (kLeft | kDown) && menu.turn == 0.0 && menu.tilt == 0.0,
               "out of play the left stick is the D-pad and nothing turns");
    }
}

void test_uses() {
    const auto classic = disruptor::settings::stick_uses(0), dual = disruptor::settings::stick_uses(1),
               southpaw = disruptor::settings::stick_uses(2);
    expect(classic.size() == 1 && std::string_view(classic[0].use) == "Move and turn", "Classic: one row for both");
    expect(dual.size() == 3 && std::string_view(dual[0].where) == "Left stick" && std::string_view(dual[1].where) == "Right stick" &&
               std::string_view(dual[1].use) == "Turn",
           "Dual analog: the left stick moves, the right turns");
    expect(southpaw.size() == 3 && std::string_view(southpaw[0].where) == "Right stick", "Southpaw: the right stick moves");
}

}  // namespace

int main() {
    test_classic_is_the_d_pad();
    test_dual_analog();
    test_tilt();
    test_speed_and_southpaw();
    test_menus_keep_the_d_pad();
    test_uses();
    if (g_failures != 0) return 1;
    std::cout << "disruptor sticks: PASS\n";
    return 0;
}
