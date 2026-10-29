#pragma once

#include <array>
#include <cstdint>

/* The mesh renderer marks a corner it cannot light (|x - 160| reaches 384) or that lies past the far limit with every
 * side bit (0x800465BC), so a face whose other corners share one side is dropped (0x80046C1C) though it is on screen. */
namespace disruptor::marked_corners {

inline constexpr uint32_t kMarked = 0x80000000u;
inline constexpr uint32_t kNear = 1u;
inline constexpr uint32_t kLeft = 4u;
inline constexpr uint32_t kRight = 8u;
inline constexpr uint32_t kAbove = 16u;
inline constexpr uint32_t kBelow = 32u;
inline constexpr uint32_t kSides = 63u;
inline constexpr uint32_t kNearDepth = 16u;
inline constexpr int32_t kLightCentre = 160;
inline constexpr int32_t kLightReach = 384;
inline constexpr int32_t kBottom = 240;

/* A corner's sides as the vertex loop finds them (0x80046388..0x800463C4) from the depth and place a mark keeps. One
 * marked only for its depth keeps every side, the game's own far drop. One at the viewer may be projected to any
 * side, so it counts on none. */
inline uint32_t real_sides(uint32_t word, uint32_t screen, int32_t left, int32_t right) {
    if ((word & kMarked) == 0u) return word & kSides;
    const int32_t x = static_cast<int16_t>(screen & 0xFFFFu), y = static_cast<int16_t>(screen >> 16);
    if (x - kLightCentre < kLightReach && kLightCentre - x < kLightReach) return kSides;
    if (((word << 1) >> 17) <= kNearDepth) return kNear;
    return (x < left ? kLeft : 0u) | (x > right ? kRight : 0u) | (y < 0 ? kAbove : 0u) | (y > kBottom ? kBelow : 0u);
}

/* The sides all four corners share, the polygon loop's drop test with marked corners on their real sides. */
inline uint32_t shared_sides(const std::array<uint32_t, 4> &words, const std::array<uint32_t, 4> &screen, int32_t left,
                             int32_t right) {
    uint32_t shared = kSides;
    for (int corner = 0; corner < 4; ++corner) shared &= real_sides(words[corner], screen[corner], left, right);
    return shared;
}

}  // namespace disruptor::marked_corners
