#pragma once

#include <array>
#include <cstdint>

/* The mesh renderer splits a polygon nearer than 128 that crosses the screen's edge into four around a middle point,
 * which it puts at the four corners' average: on a trapezoid or a collapsed corner that lays a texture unlike the whole
 * polygon's two triangles do. */
namespace disruptor::near_split {

inline constexpr uint32_t kTileSpan = 31u;

struct Camera {
    uint32_t x, y, z;
};

/* A point as the renderer stores it (0x80047900..0x8004791C): x less the camera's, y and z taken from the camera's. */
inline std::array<uint32_t, 2> stored_point(uint32_t x, uint32_t y, uint32_t z, Camera camera) {
    return {x - camera.x, ((camera.y - y) & 0xFFFFu) + ((camera.z - z) << 16)};
}

/* The renderer's own split point: the sum of fields of packed vertices, shifted down by their count's log. */
inline std::array<uint32_t, 2> split_point(uint32_t sum_x, uint32_t sum_y, uint32_t sum_z, uint32_t shift,
                                          Camera camera) {
    return stored_point(sum_x >> shift, sum_y >> shift, sum_z >> shift, camera);
}

/* A packed vertex's fields in 16ths: x from bits 20..29, y from 10..19, z from 0..9. */
inline uint32_t field_x(uint32_t packed) { return (packed >> 16) & 16368u; }
inline uint32_t field_y(uint32_t packed) { return (packed >> 6) & 16368u; }
inline uint32_t field_z(uint32_t packed) { return (packed & 1023u) << 4; }

/* The split's middle point on the diagonal from corner 1 to corner 3 that the whole polygon's two triangles share, where
 * the tile's middle and the game's middle colour already lie, instead of the four corners' average. */
inline std::array<uint32_t, 2> centre_on_diagonal(uint32_t corner1, uint32_t corner3, Camera camera) {
    return split_point(field_x(corner1) + field_x(corner3), field_y(corner1) + field_y(corner3),
                       field_z(corner1) + field_z(corner3), 1u, camera);
}

/* The texture table entry of a face: 8 bytes per tile from the table at `table` (0x80046D14). */
inline uint32_t uv_entry(uint32_t table, uint32_t first_descriptor) { return table + ((first_descriptor & 4092u) << 1); }

/* Whether the face's tile is the 32 by 32 square the split's quarters assume, inside its texture page. */
inline bool square_tile(uint32_t first_uv_word, uint32_t second_uv_word) {
    const uint32_t first = first_uv_word & 0xFFFFu;
    return (second_uv_word & 0xFFFFu) == first + kTileSpan && (first & 0xFFu) <= 255u - kTileSpan &&
           (first >> 8) <= 255u - kTileSpan;
}

/* The quarters whose texture words the game ends at texel 15, by the corner they hold; the corner-2 one starts at 16. */
enum class Quarter { corner0, corner1, corner3 };

/* A quarter's four packet texture words meeting its neighbours at texel 16 across and down of the tile at `base`, where
 * the game ends one at 15 and starts the next at 16, dropping a texel column and row across the face's middle. Moving
 * the edge points onto texel lines instead would leave the edge line a neighbour face keeps and open a crack. */
inline std::array<uint32_t, 4> quarter_uvs(Quarter quarter, std::array<uint32_t, 4> words, uint32_t base) {
    const auto at = [base](std::array<uint32_t, 4> &packet, int slot, uint32_t across, uint32_t down) {
        packet[slot] = (packet[slot] & 0xFFFF0000u) | ((base & 0xFFFFu) + across + (down << 8));
    };
    switch (quarter) {
        case Quarter::corner0:
            at(words, 1, 16u, 0u);
            at(words, 2, 0u, 16u);
            at(words, 3, 16u, 16u);
            break;
        case Quarter::corner1:
            at(words, 2, 16u, 16u);
            at(words, 3, 31u, 16u);
            break;
        case Quarter::corner3:
            at(words, 1, 16u, 16u);
            at(words, 3, 16u, 31u);
            break;
    }
    return words;
}

}  // namespace disruptor::near_split
