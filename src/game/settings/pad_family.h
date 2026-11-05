#pragma once

#include "game/settings/pad_layout.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdlib>

namespace disruptor::settings {

constexpr PadFamily family_of(SDL_GamepadType type) {
    switch (type) {
    case SDL_GAMEPAD_TYPE_PS3: return PadFamily::playstation3;
    case SDL_GAMEPAD_TYPE_PS4: return PadFamily::playstation4;
    case SDL_GAMEPAD_TYPE_PS5: return PadFamily::playstation5;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return PadFamily::nintendo;
    case SDL_GAMEPAD_TYPE_GAMECUBE: return PadFamily::gamecube;
    default: return PadFamily::xbox;
    }
}

// The pad a player pressed or pushed last: its buttons name the bindings. A stick or trigger counts past a third.
class PadInUse {
public:
    void note(const SDL_Event& event) {
        if (event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) pressed(event.gbutton.which);
        else if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION && std::abs(static_cast<int>(event.gaxis.value)) > kPushed)
            pressed(event.gaxis.which);
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && event.gdevice.which == id_) forget();
        else if (event.type == SDL_EVENT_KEY_DOWN) pad_last_ = false;
    }
    SDL_JoystickID id() const { return id_; }
    // Whether a pad was pressed after the last key.
    bool pad_last() const { return pad_last_; }

private:
    void pressed(SDL_JoystickID pad) {
        id_ = pad;
        pad_last_ = true;
    }
    void forget() {
        id_ = 0;
        pad_last_ = false;
    }

    static constexpr int kPushed = 32767 / 3;
    SDL_JoystickID id_ = 0;
    bool pad_last_ = false;
};

// The pad in use while it is connected, else the first one connected, 0 with none.
inline SDL_JoystickID pad_shown(const PadInUse& used) {
    int count = 0;
    SDL_JoystickID* pads = SDL_GetGamepads(&count);
    SDL_JoystickID chosen = 0;
    for (int index = 0; pads && index < count; ++index)
        if (pads[index] == used.id()) chosen = used.id();
    if (chosen == 0 && pads && count > 0) chosen = pads[0];
    SDL_free(pads);
    return chosen;
}

inline PadFamily family_shown(const PadInUse& used) {
    const SDL_JoystickID pad = pad_shown(used);
    return pad != 0 ? family_of(SDL_GetGamepadTypeForID(pad)) : PadFamily::xbox;
}

} // namespace disruptor::settings
