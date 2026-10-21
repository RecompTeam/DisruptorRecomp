#pragma once

extern "C" {
// A key changed to a weapon or a psionic power the player has: the game's list shows for a second, as while its button is held.
void disruptor_weapon_list_show(void);
void disruptor_psionic_list_show(void);
void disruptor_weapon_list_state_loaded(void);
}
