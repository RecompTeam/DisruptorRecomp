#pragma once

#include <array>
#include <cstdint>

/* A staircase's last riser (height 896 to 1024) ships one step back at 3904, so the landing's edge at 3648 is a hole. */
namespace disruptor::misplaced_riser {

inline constexpr uint32_t packed_vertex(uint32_t height, uint32_t along, uint32_t across) {
    return height / 16u | (along / 16u) << 10 | (across / 16u) << 20;
}

inline constexpr uint32_t kShippedAlong = 3904u;
inline constexpr uint32_t kLandingAlong = 3648u;
inline constexpr uint32_t kFootHeight = 1024u;
inline constexpr uint32_t kFirstFootVertex = 187u;  /* the riser's foot, used by its four faces alone */
inline constexpr std::array<uint32_t, 5> kFootAcross = {2048u, 1536u, 1024u, 512u, 0u};
inline constexpr uint32_t kLastPart = 0x80000000u;
inline constexpr uint32_t kMostParts = 512u;

/* A face as shipped and the landing edge's records its top corners move to. */
struct Face {
    uint32_t first, second, top_first, top_last;
};

inline constexpr std::array<Face, 4> kFaces = {{
    {0x3A5400D0u, 0x504EB200u, 251u, 252u},
    {0x3AD410D0u, 0x508EF200u, 252u, 253u},
    {0x3BD420D0u, 0x50CF3200u, 253u, 254u},
    {0x3CD430D0u, 0x510F7200u, 254u, 255u},
}};

inline uint32_t with_first_corner(uint32_t word, uint32_t record) { return (word & 0x003FFFFFu) | record << 22; }
inline uint32_t with_second_corner(uint32_t word, uint32_t record) { return (word & ~(1023u << 12)) | record << 12; }

/* Moves the riser to the landing's edge when `part` holds the staircase as shipped, and only then. */
template <typename Memory>
bool fixed_part(Memory &memory, uint32_t part) {
    const uint32_t size = memory.word(part) >> 8, offset = memory.word(part + 4u);
    constexpr uint32_t kFeetEnd = 8u + 4u * (kFirstFootVertex + kFootAcross.size());
    if (size < kFeetEnd || offset > size - kFeetEnd) return false;
    const uint32_t vertices = part + 8u + offset;
    for (uint32_t k = 0; k < kFootAcross.size(); k++)
        if (memory.word(vertices + 4u * (kFirstFootVertex + k)) != packed_vertex(kFootHeight, kShippedAlong, kFootAcross[k]))
            return false;
    std::array<uint32_t, kFaces.size()> found{};
    for (uint32_t face = 0; face < kFaces.size(); face++) {
        for (uint32_t at = part + 8u; at + 8u <= part + size && !found[face]; at += 4u)
            if (memory.word(at) == kFaces[face].first && memory.word(at + 4u) == kFaces[face].second) found[face] = at;
        if (!found[face]) return false;
    }
    for (uint32_t k = 0; k < kFootAcross.size(); k++)
        memory.write(vertices + 4u * (kFirstFootVertex + k), packed_vertex(kFootHeight, kLandingAlong, kFootAcross[k]));
    for (uint32_t face = 0; face < kFaces.size(); face++) {
        memory.write(found[face], with_first_corner(kFaces[face].first, kFaces[face].top_first));
        memory.write(found[face] + 4u, with_second_corner(kFaces[face].second, kFaces[face].top_last));
    }
    return true;
}

/* The parts of a room's mesh as the renderer walks them from its first: how many were fixed. */
template <typename Memory>
uint32_t fixed_parts(Memory &memory, uint32_t first) {
    uint32_t fixed = 0, part = first;
    for (uint32_t n = 0; n < kMostParts; n++) {
        const uint32_t header = memory.word(part);
        if (header & kLastPart || header >> 8 == 0u) break;
        fixed += fixed_part(memory, part) ? 1u : 0u;
        part += header >> 8;
    }
    return fixed;
}

}  // namespace disruptor::misplaced_riser
