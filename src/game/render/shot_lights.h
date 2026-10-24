#pragma once

#include <cstdint>

/* A shot's light comes from its picture, and so does its reach. The robot of mission 9 fires small bolts down wide
 * corridors: their light ends before the walls. A shot's record names its kind (0x8003C04C), the kind its description
 * (0x8003C060); the description's own bytes find a shot on any level, where the kind's number is the level's. */
namespace disruptor::shot_lights {

inline constexpr uint32_t kKindByte = 26u;
inline constexpr uint32_t kStageByte = 29u; /* 0 in flight, then the hit's frame (0x8003C554) */
inline constexpr uint32_t kKinds = 0x80076DE8u;
inline constexpr uint32_t kNoKind = 255u;
inline constexpr uint32_t kKeyFrom = 8u, kKeyBytes = 48u; /* past the description's pointer to its frames */
inline constexpr uint32_t kLevelFrom = 40u, kLevelBytes = 4u; /* the level's own: 43 43 43 on mission 9, 44 44 44 at level byte 4 */
inline constexpr uint32_t kRamStart = 0x80000000u, kRamEnd = 0x80200000u;

struct Light {
    uint32_t key;
    float flying;
    float hit;
};

inline constexpr Light kLights[] = {
    {0x746B8DC4u, 10.0f, 4.0f}, /* the robot's bolt: kind 4 on mission 9 (capsule 1792805320) and at level byte 4 (1792808933) */
};

inline bool in_ram(uint32_t address, uint32_t bytes) {
    return address >= kRamStart && address < kRamEnd && kRamEnd - address >= bytes;
}

template <class Memory>
uint32_t description_key(const Memory &memory, uint32_t description) {
    uint32_t key = 0x811C9DC5u;
    for (uint32_t at = kKeyFrom; at < kKeyFrom + kKeyBytes; ++at)
        if (at < kLevelFrom || at >= kLevelFrom + kLevelBytes) key = (key ^ (memory.byte(description + at) & 0xFFu)) * 0x01000193u;
    return key;
}

/* The gain for the light of the shot whose record is at `record`, 1 for what its picture gives. */
template <class Memory>
float gain(const Memory &memory, uint32_t record) {
    if (!in_ram(record, kStageByte + 1u)) return 1.0f;
    const uint32_t kind = memory.byte(record + kKindByte) & 0xFFu;
    if (kind == kNoKind) return 1.0f;
    const uint32_t description = memory.word(kKinds + 4u * kind);
    if (!in_ram(description, kKeyFrom + kKeyBytes)) return 1.0f;
    const uint32_t key = description_key(memory, description);
    for (const Light &light : kLights)
        if (light.key == key) return (memory.byte(record + kStageByte) & 0xFFu) == 0u ? light.flying : light.hit;
    return 1.0f;
}

} // namespace disruptor::shot_lights
