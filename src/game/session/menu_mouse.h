#ifndef DISRUPTOR_MENU_MOUSE_H
#define DISRUPTOR_MENU_MOUSE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A screen of the front end ran a moment ago and the pointer may choose on it: the mouse is let go and shown there. */
int disruptor_menu_pointing(void);
/* The mouse buttons down this frame while a menu points: the pad buttons to hold. A press over a row holds what that
 * row's draw under it gives, the main menu's items Cross, until the button goes up. */
uint16_t disruptor_menu_buttons(int left, int right);
/* Esc down this frame: the pad buttons to hold. Where it went down, while `allowed`, it chooses the screen's RETURN TO
 * MAIN MENU, the pause's RESUME MISSION or ABORT's NO, until it goes up or that place is left. */
uint16_t disruptor_menu_back(int allowed, int escape);
/* Esc went down for a menu of the host's, which it closed: it chooses nothing until it is up again. */
void disruptor_menu_back_taken(void);
/* The mouse moved or a key or button went down: the front end's demo waits again. */
void disruptor_menu_activity(void);
/* The strafe keys down: Left and Right to hold on the main menu and the front end's screens, nothing in play or the pause. */
uint16_t disruptor_menu_sides(int left, int right);
/* A screen of the front end or the main menu ran a moment ago, whoever points. */
int disruptor_front_end_shown(void);
/* SAVE STATES is no longer chosen on LOAD GAME or in the pause: a state loaded puts the game elsewhere. */
void disruptor_states_row_reset(void);

#ifdef __cplusplus
}
#endif

#endif
