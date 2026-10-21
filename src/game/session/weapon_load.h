#pragma once

#include "mod_plugins.h"

#include <cstdint>

namespace disruptor {

/* A weapon switch empties the player's hands (+0x7F) once the old weapon is down, then loads the new one's pictures
 * with the player pointer zero and load kind 1 at gp+0x1D8 (0x800180F0): 25 frames of a switch, measured. */
inline bool weapon_switching(bool hands_empty) {
    constexpr uint32_t kLoadKind = 0x80071324u, kWantedWeapon = 0x80077627u, kHeldWeapon = 0x80071484u;
    const uint8_t kind = psx_mod_read_byte(kLoadKind);
    return kind == 1u || (kind == 0u && hands_empty && psx_mod_read_byte(kWantedWeapon) != psx_mod_read_byte(kHeldWeapon));
}

}  // namespace disruptor
