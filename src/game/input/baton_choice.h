#pragma once

#include <cstdint>

extern "C" {
// The player put this weapon in hand: by a number key, on the wheel, or a capsule replays that choice.
void disruptor_weapon_chosen(uint8_t weapon);
void disruptor_weapon_choice_state_loaded(void);
bool disruptor_baton_held_by_choice(void);
}

namespace disruptor {

constexpr uint32_t kLevelAddress = 0x80071518u;  // gp+0x3CC, from 0x80056D24 by mission

// Levels 6 and 12 (missions 6, 11) start empty-handed: the original draws nothing from that, nor the baton by choice.
constexpr bool may_draw(uint8_t level, uint8_t in_hand, int weapon) {
    return (level != 6u && level != 12u) || (weapon != 0 && in_hand != 0xFFu);
}

}  // namespace disruptor
