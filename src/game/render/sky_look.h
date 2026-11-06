#pragma once

#include <array>
#include <cstdint>

/* A panorama's look (gpu_sky_look): the game lays 1280 texels a turn, a focal of 204 against the world's 160, so its sky slides
 * against the level while the view turns. A painted skyline shows that most. */
namespace disruptor::sky_look {

struct Look {
    int32_t size_per_mille = 1000;  /* of the game's lay, rows and columns about the horizon */
    bool smooth = false;            /* texels blended whatever the texture filter */
};

struct Pick {
    uint64_t picture;  /* sky_panorama::Picture::hash */
    Look look;
};

/* Mission 1's city: 191 texels a radian (Codex: the least slip over the middle third of 16:9), so 160/191 of its size, smoothed.
 * The owner chose it from four clips of one turn on 2026-11-07. */
inline constexpr std::array<Pick, 1> kPicks = {{
    {0xA6A30DE71D65100Dull, {838, true}},
}};

inline Look look(uint64_t picture) {
    for (const Pick &pick : kPicks)
        if (pick.picture == picture) return pick.look;
    return {};
}

inline constexpr int32_t kHorizonRow = 120;      /* the game lays panorama row r at frame row r, the horizon at 120 */
inline constexpr int32_t kLowestHorizonRow = 216;  /* a full look up: the frame's top this far above the horizon */

/* Rows to grow above the panorama's top so that a full look up finds grown sky at the frame's top, two to blend with. */
inline int32_t grown_rows(const Look &look, int32_t least) {
    const int64_t size = look.size_per_mille > 0 ? look.size_per_mille : 1000;
    const int64_t needed = (static_cast<int64_t>(kLowestHorizonRow) * 1000 + size - 1) / size - kHorizonRow + 2;
    return needed > least ? static_cast<int32_t>(needed) : least;
}

}  // namespace disruptor::sky_look
