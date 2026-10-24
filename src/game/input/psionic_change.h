#pragma once

#include <cstdint>

/* A power's change reads its pictures from the disc in two reads while the load kind is 2 (the loader at 0x80019690):
 * from a pick to the power's use 21 to 23 frames at the stock 1x, 9 to 13 with the drive at 4x in that window (probe of
 * 2026-10-24). Movies and music are not read in it, and XA sound keeps its own pace at any speed. */
namespace disruptor::psionic_change {

inline constexpr uint32_t kCurrentPsionicAddress = 0x80077629u;
inline constexpr uint32_t kHeldPsionicAddress = 0x800715C0u; // gp + 0x474: the power a change has reached
inline constexpr uint32_t kLoadKindAddress = 0x80071324u;    // gp + 0x1D8: 2 while a power's pictures load
inline constexpr uint32_t kPicturesLoad = 2u;
inline constexpr uint8_t kBlast = 4u, kTeraBlast = 5u, kNoPsionic = 0xFFu; // the game holds Blast as 5 once it raises it
inline constexpr int kStockDivisor = 1;                                    // game.toml's disc_speed
inline constexpr int kQuickDivisor = 4;

inline uint8_t power_of(uint8_t id) { return id == kTeraBlast ? kBlast : id; }

/* The drive's speed this tick: 4x while a wanted quick change reads the power's pictures, else the stock 1x. */
template <class Memory>
int divisor(const Memory &memory, bool wanted) {
    return wanted && memory.byte(kLoadKindAddress) == kPicturesLoad ? kQuickDivisor : kStockDivisor;
}

} // namespace disruptor::psionic_change
