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

#ifdef __cplusplus
}
#endif

#endif
