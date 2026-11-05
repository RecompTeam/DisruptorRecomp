#pragma once

#include <cstdint>

/* SAVE STATES in the pause menu, a line the game does not have, under RESUME MISSION. While it is chosen the game's
 * cursor stays on RESUME, and the presses it would act on there are taken from the game. */
namespace disruptor::states_row {

/* The game's button bits, as the pause tick sees a press. */
inline constexpr uint32_t kUp = 0x1000u, kRight = 0x2000u, kDown = 0x4000u, kLeft = 0x8000u, kStart = 0x0800u;
inline constexpr uint32_t kChoices = 0x09F0u; /* what the pause takes as a choice: Start, Select and the face buttons */
inline constexpr uint32_t kResume = 0u, kAbort = 1u;

/* The pause's box holds four lines from y 106, 14 apart, and ends at 166. With the row the five stand 13 apart from
 * 100, under PAUSED: RESUME, the row, ABORT, SOUND, MUSIC, the last letters ending at 163. */
inline constexpr int32_t kPauseFirstY = 100, kPauseStep = 13;

/* The place of the game's line RESUME 0, ABORT 1, SOUND 2 or MUSIC 3 with the row second. */
constexpr int32_t pause_line_y(uint32_t line) { return kPauseFirstY + static_cast<int32_t>(line == 0u ? 0u : line + 1u) * kPauseStep; }
inline constexpr int32_t kPauseY = kPauseFirstY + kPauseStep;

struct Step {
    uint32_t cursor = 0;
    bool on = false;
    uint32_t pressed = 0;
    bool open = false;
};

/* Down from RESUME comes onto the row and Down from it goes on to ABORT, Up back. Start keeps resuming unless a choice
 * opens the screen with it, Up with Down steps nowhere, and while ABORT asks there is no row. */
constexpr Step pause(uint32_t cursor, bool on, uint32_t pressed, bool asking) {
    Step step{cursor, on && !asking && cursor == kResume, pressed, false};
    if (asking) return step;
    uint32_t up = pressed & kUp, down = pressed & kDown;
    if (up && down) {
        step.pressed &= ~(kUp | kDown);
        up = down = 0u;
    }
    if (step.on) {
        if (down) {
            step.on = false;
        } else if (up) {
            step.on = false;
            step.pressed &= ~kUp;
        }
    } else if (cursor == kResume && down) {
        step.on = true;
        step.pressed &= ~kDown;
    } else if (cursor == kAbort && up) {
        step.cursor = kResume;
        step.on = true;
        step.pressed &= ~kUp;
    }
    if (step.on) step.pressed &= ~(kLeft | kRight);
    if (step.on && (step.pressed & kChoices & ~kStart) != 0u) {
        step.open = true;
        step.pressed &= ~kChoices;
    }
    return step;
}

}  // namespace disruptor::states_row
