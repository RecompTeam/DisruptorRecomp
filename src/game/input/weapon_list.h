#pragma once

#include <cstdint>

extern "C" {
// A key changed to a weapon or a psionic power the player has: the game's list shows for a second, as while its button is held.
void disruptor_weapon_list_show(void);
void disruptor_psionic_list_show(void);
void disruptor_weapon_list_state_loaded(void);
// The reactor's countdown keeps the message line through the HUD builder: its mode read and its end.
void disruptor_list_message_builder_begins(void);
void disruptor_list_message_builder_ends(uint32_t forced);
void disruptor_list_message_state_loaded(void);
int disruptor_list_message_save_deferred(void);
}
