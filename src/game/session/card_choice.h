#pragma once

#include <cstdint>
#include <string_view>

/* The game takes the left card at every start and the right one only when the player picks it under MEMORY CARD
 * OPTIONS, which the main menu no longer shows: a player whose saves are on the right card alone gets that card. A
 * left card a save state holds as chosen counts as the start's: it cannot be chosen any more. */
namespace disruptor::card_choice {

inline constexpr uint32_t kHandler = 0x8001C044u; /* the main menu, every frame it is shown */
inline constexpr uint32_t kSelected = 0x800715D8u; /* gp+1164: -1 none, 0 left, 1 right */
inline constexpr uint32_t kPresent = 0x8007141Cu;  /* gp+720: a word a slot, 1 while the game finds the card */
inline constexpr int kDirectoryFrames = 15;
inline constexpr int kFrameBytes = 128;
inline constexpr std::string_view kFile = "BASLUS-00224DISRPTOR"; /* the save path at 0x800100E8 after "bu%d0:" */

/* Whether a directory frame of a card opens the game's save file. */
constexpr bool opens_game_file(const uint8_t *frame) {
    constexpr int kName = 10;
    if (frame[0] != 0x51u || frame[kName + kFile.size()] != 0u) return false;
    for (size_t index = 0; index < kFile.size(); ++index)
        if (frame[kName + index] != static_cast<uint8_t>(kFile[index])) return false;
    return true;
}

constexpr int32_t chosen(int32_t selected, bool left_holds, bool right_holds, bool right_present) {
    return selected == 0 && !left_holds && right_holds && right_present ? 1 : selected;
}

}  // namespace disruptor::card_choice
