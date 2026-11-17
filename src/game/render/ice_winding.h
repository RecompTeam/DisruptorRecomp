#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace disruptor::ice_winding {

using Face = std::array<uint32_t, 2>;

struct VertexWord {
    uint32_t offset, value;
};

struct MeshRepair {
    uint32_t part_size, lights;
    std::span<const Face> faces;
    std::span<const VertexWord> vertices;
};

inline constexpr std::array<Face, 6> kFirstFaces = {{
    {0x314C634Cu, 0x31CC9200u}, {0x330C5318u, 0x328CE300u}, {0x330CF34Cu, 0x318C5300u},
    {0x340CD318u, 0x338D1300u}, {0x338CB34Cu, 0x320D1200u}, {0x318CF34Cu, 0x344C7300u},
}};
inline constexpr std::array<VertexWord, 8> kFirstVertices = {{
    {840u, 0x08332C31u}, {844u, 0x08C35433u}, {848u, 0x48335C20u}, {852u, 0x88331C26u},
    {856u, 0x49D32C30u}, {860u, 0x09D31425u}, {864u, 0x49F36432u}, {868u, 0x09D35420u},
}};
inline constexpr std::array<Face, 6> kSecondFaces = {{
    {0x348D334Cu, 0x350D5300u}, {0x358D734Cu, 0x360D9300u}, {0x35CD234Cu, 0x354D8300u},
    {0x34CD634Cu, 0x364D4300u}, {0x34CD234Cu, 0x35CD6200u}, {0x354D436Cu, 0x364D8200u},
}};
inline constexpr std::array<VertexWord, 8> kSecondVertices = {{
    {872u, 0x0AD36457u}, {876u, 0x0AD34C57u}, {880u, 0x0B433C4Fu}, {884u, 0x0B43744Fu},
    {888u, 0x0A734C55u}, {892u, 0x0A736455u}, {896u, 0x0A73744Au}, {900u, 0x0A733C4Au},
}};
inline constexpr std::array<Face, 4> kLeaningFaces = {{
    {0x5695C318u, 0x66960200u}, {0x5819934Cu, 0x66D61300u},
    {0x5655A360u, 0x5815E200u}, {0x57960360u, 0x58562300u},
}};
inline constexpr std::array<VertexWord, 9> kLeaningVertices = {{
    {1596u, 0x0AE26047u}, {1600u, 0x0B425C47u}, {1604u, 0x4A920438u},
    {1612u, 0x4B029433u}, {1616u, 0x0B629033u}, {1620u, 0x0B92C420u},
    {1624u, 0x4B22C820u}, {1784u, 0x4AB23824u}, {1788u, 0x0AD26C10u},
}};
inline constexpr std::array<MeshRepair, 3> kRepairs = {{
    {0x788u, 0xF8u, kFirstFaces, kFirstVertices},
    {0x788u, 0xF8u, kSecondFaces, kSecondVertices},
    {0xF68u, 0x1E4u, kLeaningFaces, kLeaningVertices},
}};

// Two ice blocks point inward, and four faces of a leaning crystal oppose their neighbours.
template <typename Read>
uint32_t culling_flags(uint32_t first, uint32_t second, uint32_t part, Read read) {
    for (const MeshRepair &repair : kRepairs) {
        bool matches = false;
        for (const Face &face : repair.faces) matches = matches || (first == face[0] && second == face[1]);
        if (!matches) continue;
        if ((part & 3u) || part < 0x80000000u || part > 0x80200000u - repair.part_size) return second;
        if (read(part) != (repair.part_size << 8 | 0xFFu) || read(part + 4u) != repair.lights) return second;
        for (const VertexWord &vertex : repair.vertices)
            if (read(part + vertex.offset) != vertex.value) return second;
        return second ^ 0x200u;
    }
    return second;
}

}
