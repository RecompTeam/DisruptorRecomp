#pragma once

#include "game/input/menu_pointer.h"
#include "game/input/mouse_aim.h"
#include "lockstep.h"
#include "psx_netplay.h"

#include <cstdlib>
#include <optional>

extern "C" void psx_host_video_get_display_aspect(int *numerator, int *denominator);
extern "C" int psx_host_ui_game_input_captured(void);

namespace disruptor::menu_pointing {

/* A replay plays what was recorded: the pointer of the run that replays it chooses nothing. */
inline bool replaying() {
    static const bool value = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr;
    return value;
}

inline bool allowed() { return !replaying() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0; }

/* The pointer in the front end's frame now, while the mouse is free over the window and no menu of the host's has it. */
inline std::optional<menu_pointer::Point> now() {
    double x = 0.0, y = 0.0;
    int width = 0, height = 0, numerator = 4, denominator = 3;
    if (!allowed() || psx_host_ui_game_input_captured() || !disruptor_mouse_menu_pointer(&x, &y, &width, &height)) return std::nullopt;
    psx_host_video_get_display_aspect(&numerator, &denominator);
    return menu_pointer::frame_point(x, y, width, height, numerator, denominator);
}

}  // namespace disruptor::menu_pointing
