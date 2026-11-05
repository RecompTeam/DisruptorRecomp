#include "game/input/states_row.h"

#include <iostream>

namespace {

namespace row = disruptor::states_row;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr uint32_t kCross = 0x40u;

void test_places() {
    expect(row::pause_line_y(0u) == 100 && row::kPauseY == 113 && row::pause_line_y(1u) == 126 && row::pause_line_y(3u) == 152,
           "RESUME, the row, ABORT, SOUND and MUSIC 13 apart from 100");
    expect(row::pause_line_y(3u) + 11 <= 166 && row::pause_line_y(0u) > 89, "the five fit the pause's box under PAUSED");
}

void test_steps() {
    row::Step step = row::pause(row::kResume, false, row::kDown, false);
    expect(step.on && step.pressed == 0u && step.cursor == row::kResume, "Down from RESUME comes onto the row");
    step = row::pause(row::kAbort, false, row::kUp, false);
    expect(step.on && step.cursor == row::kResume && step.pressed == 0u, "Up from ABORT comes back onto it");
    step = row::pause(row::kResume, true, row::kDown, false);
    expect(!step.on && step.pressed == row::kDown, "Down from the row lets the game go on to ABORT");
    step = row::pause(row::kResume, true, row::kUp, false);
    expect(!step.on && step.pressed == 0u, "Up from the row is RESUME");
    step = row::pause(row::kResume, false, row::kUp, false);
    expect(!step.on && step.pressed == row::kUp, "Up from RESUME goes round to MUSIC, as the game does");
    step = row::pause(row::kResume, true, row::kLeft | row::kRight, false);
    expect(step.on && step.pressed == 0u, "Left and Right do nothing on the row");
    step = row::pause(row::kResume, true, kCross, false);
    expect(step.open && step.pressed == 0u, "a choice on the row opens the screen and RESUME never sees it");
    step = row::pause(row::kResume, true, row::kStart, false);
    expect(!step.open && step.pressed == row::kStart, "Start keeps resuming");
    step = row::pause(row::kResume, true, kCross | row::kStart, false);
    expect(step.open && step.pressed == 0u, "a choice that opens the screen takes Start with it");
    step = row::pause(row::kResume, true, kCross, true);
    expect(!step.on && !step.open && step.pressed == kCross, "while ABORT asks there is no row");
    step = row::pause(row::kResume, true, row::kUp | row::kDown | row::kRight, false);
    expect(step.on && step.pressed == 0u, "Up with Down on the row steps nowhere");
    step = row::pause(row::kResume, false, row::kDown | kCross, false);
    expect(step.on && step.open && step.pressed == 0u, "a choice with the step onto the row is the row's, not RESUME's");
    step = row::pause(2u, false, row::kDown, false);
    expect(!step.on && step.pressed == row::kDown, "Down elsewhere is the game's");
}

}  // namespace

int main() {
    test_places();
    test_steps();
    if (g_failures != 0) return 1;
    std::cout << "disruptor states row: PASS\n";
    return 0;
}
