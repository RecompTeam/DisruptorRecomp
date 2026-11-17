#pragma once

#include <array>
#include <cstdint>

namespace disruptor::quad_visibility {

inline constexpr uint32_t kReverseWinding = 0x200u;
inline constexpr uint32_t kSecondTriangle = 0x100u;
inline constexpr uint32_t kBothTriangles = 0x400u;
inline constexpr uint32_t kTwoSided = 0x800u;

inline int64_t signed_area(const std::array<uint32_t, 4> &screen, int first, int second, int third) {
    const auto x = [&screen](int corner) { return static_cast<int16_t>(screen[corner]); };
    const auto y = [&screen](int corner) { return static_cast<int16_t>(screen[corner] >> 16); };
    return int64_t{x(first) - x(second)} * (y(third) - y(second)) -
           int64_t{x(third) - x(second)} * (y(first) - y(second));
}

// Guest culling uses diagonal 0,2, while the emitted triangles share diagonal 1,3.
inline uint32_t culling_flags(const std::array<uint32_t, 4> &screen, uint32_t flags) {
    if (flags & kTwoSided) return flags;
    const int direction = flags & kReverseWinding ? -1 : 1;
    const int64_t first = direction * signed_area(screen, 0, 1, 2);
    const int64_t second = direction * signed_area(screen, 0, 2, 3);
    const bool rejected = flags & kBothTriangles ? first < 0 && second < 0 : flags & kSecondTriangle ? second < 0 : first < 0;
    if (rejected &&
        (direction * signed_area(screen, 0, 1, 3) > 0 || direction * signed_area(screen, 3, 1, 2) > 0))
        return flags ^ kReverseWinding;
    return flags;
}

}
