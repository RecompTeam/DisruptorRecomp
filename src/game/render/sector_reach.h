#pragma once

#include <array>
#include <cstdint>

/* The portal walk (0x8003A0F0) asks for the area of a sector it enters while that area is not in memory (0x8003A258).
 * A walk the longer draw distance stretches past the game's own far distance must not, or two areas load in turn. */
namespace disruptor::sector_reach {

inline constexpr uint32_t kSectors = 0x800776E4u; /* a word a sector, 0 while its area is not in memory */
inline constexpr uint32_t kCamera = 0x800775BCu;  /* x, then z */
inline constexpr uint32_t kScales = 0x80057E98u;  /* a halfword by |dz| + |dx| of a portal */
inline constexpr uint32_t kPortalOnStack = 188u;  /* the portal walked through, in the caller's frame */
inline constexpr uint32_t kPortalSector = 12u;
inline constexpr uint32_t kNoSector = 0xFFu;
inline constexpr uint32_t kDepthCap = 11u; /* the walk returns at once from this depth on (0x8003A130) */

struct Portal {
    int32_t first_x, first_z, second_x, second_z;
};

struct Offset {
    int32_t x, z;
};

constexpr int32_t low(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }
constexpr int32_t sum(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
constexpr int32_t difference(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}
constexpr int32_t magnitude(int32_t value) { return value < 0 ? -value : value; }
constexpr int32_t squared(int32_t x, int32_t z) { return sum(low(x, x), low(z, z)); }
constexpr bool between(int32_t value, int32_t one, int32_t other) {
    return (one < value && value < other) || (other < value && value < one);
}

constexpr uint32_t scale_index(const Portal &portal) {
    return static_cast<uint32_t>(magnitude(portal.second_z - portal.first_z) + magnitude(portal.first_x - portal.second_x));
}

/* The game's three distances of a portal, each squared as it leaves them in $v0: 0x8003A928, 0x8003A964, 0x8003AA74. */
constexpr int32_t first_distance(const Portal &portal, int32_t x, int32_t z) {
    return squared(difference(x, portal.first_x), difference(z, portal.first_z));
}
constexpr int32_t second_distance(const Portal &portal, int32_t x, int32_t z) {
    return squared(difference(x, portal.second_x), difference(z, portal.second_z));
}
constexpr Offset offset(const Portal &portal, int32_t x, int32_t z, uint32_t scale) {
    const int32_t along_z = low(portal.second_z - portal.first_z, static_cast<int32_t>(scale));
    const int32_t along_x = low(portal.first_x - portal.second_x, static_cast<int32_t>(scale));
    const int32_t across =
        sum(difference(low(along_z, x), sum(low(along_z, portal.first_x), low(along_x, portal.first_z))), low(along_x, z)) >> 8;
    return {low(across, along_z >> 8) >> 16, low(across, along_x >> 8) >> 16};
}
constexpr int32_t offset_distance(const Portal &portal, int32_t x, int32_t z, uint32_t scale) {
    const Offset away = offset(portal, x, z, scale);
    return squared(away.x, away.z);
}

/* The game's test of a portal against a far distance (0x8003A8E0 to 0x8003AAF4). */
constexpr bool within(const Portal &portal, int32_t x, int32_t z, uint32_t far, uint32_t scale) {
    const int32_t reach = low(static_cast<int32_t>(far), static_cast<int32_t>(far));
    if (reach >= first_distance(portal, x, z) || reach >= second_distance(portal, x, z)) return true;
    const Offset away = offset(portal, x, z, scale);
    if (reach < squared(away.x, away.z)) return false;
    if (portal.first_x != portal.second_x) return between(difference(x, away.x), portal.first_x, portal.second_x);
    return between(difference(z, away.z), portal.first_z, portal.second_z);
}

template <class Memory>
Portal portal_at(const Memory &memory, uint32_t gate) {
    return {static_cast<int32_t>(memory.half(gate)), static_cast<int32_t>(memory.half(gate + 2u)),
            static_cast<int32_t>(memory.half(gate + 4u)), static_cast<int32_t>(memory.half(gate + 6u))};
}

template <class Memory>
uint32_t scale_at(const Memory &memory, const Portal &portal) {
    return memory.half(kScales + 2u * scale_index(portal));
}

/* By depth, whether the walk is past the game's own far distance: entered through a portal it does not reach, or below. */
class Walk {
public:
    bool enter(uint32_t depth, bool reached) {
        if (depth >= kDepthCap) return false;
        past_[depth] = depth != 0u && (past_[depth - 1u] || !reached);
        return past_[depth];
    }

private:
    std::array<bool, kDepthCap> past_{};
};

struct Entry {
    bool hold = false;  /* turn the walk back at once: past the game's distance, into a sector out of memory */
    bool agrees = true; /* the copy of the test passes the portal at the distance walked with, as the game did */
};

/* The walk entering a sector at `depth`, `stack` its frame, the game's far distance and the one in its global. */
template <class Memory>
Entry enter(const Memory &memory, Walk &walk, uint32_t stack, uint32_t depth, uint32_t game_far, uint32_t walked_far) {
    if (depth == 0u || depth >= kDepthCap) {
        walk.enter(depth, true);
        return {};
    }
    const uint32_t gate = memory.word(stack + kPortalOnStack);
    const Portal portal = portal_at(memory, gate);
    const int32_t x = static_cast<int32_t>(memory.word(kCamera)), z = static_cast<int32_t>(memory.word(kCamera + 4u));
    const uint32_t scale = scale_at(memory, portal);
    Entry entry;
    entry.agrees = within(portal, x, z, walked_far, scale);
    if (!walk.enter(depth, within(portal, x, z, game_far, scale))) return entry;
    const uint32_t sector = memory.byte(gate + kPortalSector);
    entry.hold = sector != kNoSector && memory.word(kSectors + 4u * sector) == 0u;
    return entry;
}

} // namespace disruptor::sector_reach
