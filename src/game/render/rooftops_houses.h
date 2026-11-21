#pragma once

#include "game/render/quad_visibility.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace disruptor::rooftops_houses {

struct Course {
    uint16_t left, right, inset, height;
};

inline constexpr std::array<Course, 9> full_width{{
    {0, 1024, 0, 0}, {0, 1024, 0, 128}, {32, 992, 0, 256},
    {64, 960, 32, 384}, {96, 928, 64, 512}, {128, 896, 96, 640},
    {160, 864, 128, 768}, {192, 832, 160, 896}, {224, 800, 192, 976},
}};

inline constexpr std::array<Course, 9> narrow{{
    {0, 828, 0, 0}, {0, 828, 0, 102}, {26, 804, 0, 206},
    {52, 776, 26, 310}, {78, 752, 52, 414}, {102, 726, 78, 518},
    {130, 700, 104, 622}, {154, 674, 130, 724}, {182, 648, 156, 790},
}};

constexpr auto walls(const std::array<Course, 9> &profile, uint32_t front_plane, uint32_t rear) {
    constexpr std::size_t vertices = 4 * full_width.size();
    constexpr std::size_t faces = 2 * (full_width.size() - 1);
    std::array<uint32_t, 2 + vertices / 4 + vertices + 1 + 2 * faces + 2> mesh{};
    constexpr std::array<uint32_t, 8> tiles{240, 237, 234, 245, 244, 235, 232, 244};
    mesh[0] = (static_cast<uint32_t>(mesh.size() - 1) * 4u << 8) | 0xFFu;
    mesh[1] = static_cast<uint32_t>(vertices);
    const auto packed = [](uint32_t x, uint32_t z, uint32_t y) {
        return ((x / 2u) << 20) | ((z / 2u) << 10) | (y / 2u);
    };
    std::size_t cursor = 2;
    for (std::size_t row = 0; row < profile.size(); ++row) mesh[cursor++] = 0x51645164u;
    for (const Course &course : profile) {
        const uint32_t front = rear > front_plane ? front_plane + course.inset : front_plane - course.inset;
        mesh[cursor++] = packed(course.left, front, course.height);
        mesh[cursor++] = packed(course.left, rear, course.height);
        mesh[cursor++] = packed(course.right, front, course.height);
        mesh[cursor++] = packed(course.right, rear, course.height);
    }
    mesh[cursor++] = 0xFFFFFFFFu;
    for (uint32_t row = 0; row < tiles.size(); ++row) {
        for (uint32_t side = 0; side < 2u; ++side) {
            const uint32_t bottom = row * 4u + side * 2u;
            const uint32_t top = bottom + 4u;
            mesh[cursor++] = (top << 22) | ((top + 1u) << 12) | (tiles[row] << 2);
            mesh[cursor++] = ((bottom + 1u) << 22) | (bottom << 12) |
                quad_visibility::kBothTriangles | quad_visibility::kTwoSided;
        }
    }
    mesh[cursor++] = 0xFFFFFFFFu;
    mesh[cursor] = 0xFFFFFFFFu;
    return mesh;
}

constexpr auto distant_profile(uint32_t x, uint32_t y) {
    auto profile = full_width;
    for (Course &course : profile) {
        course.left = static_cast<uint16_t>(x + course.left * 3u / 4u);
        course.right = static_cast<uint16_t>(x + course.right * 3u / 4u);
        course.inset = static_cast<uint16_t>(course.inset * 3u / 4u);
        course.height = static_cast<uint16_t>(y + course.height * 3u / 4u);
    }
    return profile;
}

inline constexpr auto south = walls(full_width, 0, 256);
inline constexpr auto north = walls(full_width, 256, 0);
inline constexpr auto narrow_north = walls(narrow, 208, 0);
inline constexpr auto inner_north = walls(distant_profile(92, 172), 192, 0);
inline constexpr auto outer_south = walls(distant_profile(60, 0), 460, 652);
inline constexpr auto outer_north = walls(distant_profile(60, 0), 144, 0);

}
