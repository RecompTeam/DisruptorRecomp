#pragma once

#include <cstdint>
#include <vector>

/* A save state's picture and where the game was when it was saved, without what reads them from the files. */
namespace disruptor::state_files {

inline constexpr uint8_t kRegular = 2u; /* the menu shows REGULAR for 2 and HARD for the rest */

struct Picture {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgb;

    bool empty() const { return width == 0u || height == 0u; }
};

/* A colour of the PlayStation's five bits widened to eight, white staying white. */
inline uint8_t widened(uint32_t five) { return static_cast<uint8_t>(five << 3 | five >> 2); }

/* Where the game was when the state was saved, as the screen kept beside it says. */
enum class Place : uint8_t { unknown = 0, play = 1, menus = 2 };

}  // namespace disruptor::state_files
