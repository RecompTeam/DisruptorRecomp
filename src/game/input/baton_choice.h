#pragma once

#include <cstdint>

extern "C" {
// The player put this weapon in hand: by a number key, on the wheel, or a capsule replays that choice.
void disruptor_weapon_chosen(uint8_t weapon);
void disruptor_weapon_choice_state_loaded(void);
bool disruptor_baton_held_by_choice(void);
}
