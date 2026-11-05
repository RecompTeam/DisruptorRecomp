#pragma once
#include "psx_sdl.h"

enum { PSX_KB_L1 = 8, PSX_KB_R1 = 9, PSX_KB_L2 = 10, PSX_KB_R2 = 11 };
extern "C" SDL_Scancode psx_keybinds_get_button(int player, int button);
extern "C" SDL_Scancode psx_keybinds_get_button_alt(int player, int button);
