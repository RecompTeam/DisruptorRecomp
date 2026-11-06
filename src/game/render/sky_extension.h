/*
 * The sky above the panorama's top, grown on the player's machine from the level's own panorama: the vertical look shows
 * up to 96 rows more than the game drew. Image quilting: patches cut from the panorama's top block, laid row by row upward
 * round the whole turn, each picked among those whose overlap with the sky already laid matches best and joined to it
 * along the least visible cut. Indices are copied, never made, so every fog palette and the smooth fade read them as they
 * read the game's own.
 */
#pragma once

#include "game/render/sky_panorama.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <vector>

namespace disruptor::sky_extension {

inline constexpr int32_t kRows = 128;  /* 96 a full look up shows, the rest for filtering and a later wider view */
inline constexpr int32_t kPatchWidth = 128, kPatchHeight = 16, kOverlapX = 16, kOverlapY = 6;
inline constexpr int32_t kRise = kPatchHeight - kOverlapY;
inline constexpr int32_t kSourceRows = 60;  /* patches come from the top block: lower rows hold the skyline */
inline constexpr int32_t kStepX = 4, kStepY = 2;
inline constexpr int32_t kMaxBand = 12;
/* A sun's or a light's texel: its index under 1/400 of the top block's texels, and colours near it, within 48, under 1/50. */
inline constexpr int64_t kRareIndexShare = 400, kRareNearShare = 50, kNear = 48, kRareCost = 512;
inline constexpr int32_t kSkyNear = 12;
inline constexpr int64_t kSkylineCost = 4096;
inline constexpr int64_t kStreaks = 15;  /* percent: change along the rows under this share of the change down them makes streaks */
inline constexpr int32_t kLeastTall = 8;  /* a patch's rows when the sky's top block is shallow, see top_depth */

using Palette = std::array<uint16_t, 256>;

/* Rows top first. The panorama's top band (a few dark rows some skies end in, see top_band) is grown over too: the last row
 * lies against the panorama's row `band`, and the renderer gives the band over to it as the look rises. */
struct Extension {
    int32_t width = 0, rows = 0, band = 0;
    int32_t from_row = 0, to_row = 0;  /* the panorama rows the patches came from */
    bool streaks = false;              /* grown a whole turn at once, see streaks */
    std::vector<uint8_t> indices;
    bool operator==(const Extension &) const = default;
};

/* A panorama's revision: its indices, size and base palette. It names a grown sky and seeds its growth, in the game and in the
 * developer's tool alike, so the tool's picture is the game's. */
inline uint64_t revision(const sky_panorama::Picture &picture, const Palette &palette) {
    uint64_t hash = picture.hash ^ (static_cast<uint64_t>(picture.width) << 20) ^ static_cast<uint64_t>(picture.height);
    for (const uint16_t colour : palette) hash = (hash ^ colour) * 1099511628211ull;
    return hash == 0 ? 1 : hash;
}

struct Colours {
    std::array<std::array<int32_t, 3>, 256> rgb{};
    explicit Colours(const Palette &palette) {
        for (size_t index = 0; index < palette.size(); ++index)
            for (int channel = 0; channel < 3; ++channel) rgb[index][static_cast<size_t>(channel)] = (palette[index] >> (5 * channel)) & 31;
    }
    int32_t apart(uint8_t one, uint8_t other) const {
        int32_t sum = 0;
        for (size_t channel = 0; channel < 3; ++channel) {
            const int32_t step = rgb[one][channel] - rgb[other][channel];
            sum += step * step;
        }
        return sum;
    }
};

inline int32_t lightness(const Colours &colours, uint8_t index) {
    const auto &rgb = colours.rgb[index];
    return rgb[0] * 2 + rgb[1] * 4 + rgb[2];
}

inline std::vector<int32_t> row_means(const sky_panorama::Picture &picture, const Colours &colours, int32_t count) {
    std::vector<int32_t> means(static_cast<size_t>(count));
    for (int32_t row = 0; row < count; ++row) {
        int64_t sum = 0;
        for (int32_t column = 0; column < picture.width; ++column)
            sum += lightness(colours, picture.indices[static_cast<size_t>(row) * static_cast<size_t>(picture.width) + static_cast<size_t>(column)]);
        means[static_cast<size_t>(row)] = static_cast<int32_t>(sum / picture.width);
    }
    return means;
}

inline bool unlike(int32_t mean, int32_t reference) { return std::abs(mean - reference) > std::max(reference / 6, 8); }

/* The rows a sky ends in that step away from the rows below them, as a few dark ones some skies end in: each off the mean
 * of the eight rows below by more than a sixth of it, and by 8 at least, so that a dark sky's small ripples are no band. */
inline int32_t top_band(const std::vector<int32_t> &means) {
    for (int32_t band = kMaxBand; band > 0; --band) {
        int32_t below = 0;
        for (int32_t row = band; row < band + 8; ++row) below += means[static_cast<size_t>(row)];
        below /= 8;
        bool stepped = true;
        const bool darker = means[0] < below;
        for (int32_t row = 0; row < band && stepped; ++row)
            stepped = unlike(means[static_cast<size_t>(row)], below) && (means[static_cast<size_t>(row)] < below) == darker;
        if (stepped) return band;
    }
    return 0;
}

/* The rows below the band that look like the sky's top, the eight first rows' mean lightness: patches come from these,
 * so that a sky darker or lighter higher up is not laid upside down, as a fire sky's flames over its dark top. */
inline int32_t top_depth(const std::vector<int32_t> &means, int32_t band) {
    int32_t top = 0;
    for (int32_t row = band; row < band + 8; ++row) top += means[static_cast<size_t>(row)];
    top /= 8;
    int32_t depth = 0;
    while (band + depth < kSourceRows && !unlike(means[static_cast<size_t>(band + depth)], top)) ++depth;
    return std::min(std::max(depth, kLeastTall), kSourceRows - band);
}

/* The skyline inside the top block: dark texels, under 2/3 of the top four rows' lightness, of colours those rows hardly
 * hold, under 1/50 of them within 12, that reach the block's last row through each other, as mountains rising into the sky.
 * Clouds and suns stand apart, a planet is not dark. */
inline std::vector<uint8_t> skyline(const sky_panorama::Picture &picture, const Colours &colours, int32_t band, int32_t bottom) {
    const int32_t width = picture.width;
    const auto index_at = [&](int32_t row, int32_t column) {
        return picture.indices[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column)];
    };
    std::array<int64_t, 256> seen{};
    int64_t light = 0;
    for (int32_t row = band; row < band + 4; ++row)
        for (int32_t column = 0; column < width; ++column) {
            ++seen[index_at(row, column)];
            light += lightness(colours, index_at(row, column));
        }
    light /= 4 * static_cast<int64_t>(width);
    std::array<bool, 256> strange{};
    for (size_t index = 0; index < 256u; ++index) {
        int64_t near = 0;
        for (size_t other = 0; other < 256u; ++other)
            if (colours.apart(static_cast<uint8_t>(index), static_cast<uint8_t>(other)) <= kSkyNear) near += seen[other];
        strange[index] = near * kRareNearShare < 4 * static_cast<int64_t>(width) &&
                         lightness(colours, static_cast<uint8_t>(index)) * 3 < light * 2;
    }
    std::vector<uint8_t> marked(static_cast<size_t>(kSourceRows) * static_cast<size_t>(width), 0u);
    std::vector<std::array<int32_t, 2>> open;
    const auto reach = [&](int32_t row, int32_t column) {
        column = ((column % width) + width) % width;
        if (row < band || row >= bottom) return;
        uint8_t &mark = marked[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column)];
        if (mark || !strange[index_at(row, column)]) return;
        mark = 1;
        open.push_back({row, column});
    };
    for (int32_t column = 0; column < width; ++column) reach(bottom - 1, column);
    while (!open.empty()) {
        const auto [row, column] = open.back();
        open.pop_back();
        reach(row - 1, column);
        reach(row + 1, column);
        reach(row, column - 1);
        reach(row, column + 1);
    }
    return marked;
}

inline uint64_t next(uint64_t &state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

/* The least visible way through an overlap `across` wide and `along` long: for each step along it, the place across the
 * cut runs through. Neighbouring steps move by one place at most, and round a whole turn the last step meets the first. */
inline std::vector<int32_t> cut(const std::vector<int32_t> &error, int32_t along, int32_t across, bool round = false) {
    constexpr int64_t kFar = std::numeric_limits<int64_t>::max() / 4;
    std::vector<int64_t> cost(error.size());
    std::vector<int32_t> from(error.size(), 0), way(static_cast<size_t>(along), 0);
    int64_t best = kFar;
    for (int32_t start = round ? 0 : -1; start < (round ? across : 0); ++start) {
        for (int32_t place = 0; place < across; ++place)
            cost[static_cast<size_t>(place)] = start < 0 || place == start ? error[static_cast<size_t>(place)] : kFar;
        for (int32_t step = 1; step < along; ++step)
            for (int32_t place = 0; place < across; ++place) {
                int64_t low = kFar;
                for (int32_t near = std::max(0, place - 1); near <= std::min(across - 1, place + 1); ++near) {
                    const int64_t value = cost[static_cast<size_t>((step - 1) * across + near)];
                    if (value < low) low = value, from[static_cast<size_t>(step * across + place)] = near;
                }
                cost[static_cast<size_t>(step * across + place)] = std::min(kFar, low + error[static_cast<size_t>(step * across + place)]);
            }
        for (int32_t place = 0; place < across; ++place) {
            const int64_t total = cost[static_cast<size_t>((along - 1) * across + place)];
            if (total >= best || (start >= 0 && std::abs(place - start) > 1)) continue;
            best = total;
            int32_t at = place;
            for (int32_t step = along - 1; step >= 0; --step) {
                way[static_cast<size_t>(step)] = at;
                at = from[static_cast<size_t>(step * across + at)];
            }
        }
    }
    return way;
}

/* Each source row's texels moved by the step from its sky colour to the top rows', a row's sky colour its median channel by
 * channel, which sparse clouds or a sun leave alone, and through the colours the top block holds: a sky lighter or darker
 * further down grows as level as its top, so that stacking its rows makes no bands. */
inline std::vector<std::array<uint8_t, 256>> levelled(const sky_panorama::Picture &picture, const Colours &colours, int32_t band, int32_t bottom) {
    std::vector<std::array<uint8_t, 256>> tables(static_cast<size_t>(kSourceRows));
    std::vector<std::array<double, 3>> skies(static_cast<size_t>(kSourceRows), {0.0, 0.0, 0.0});
    std::array<bool, 256> held{};
    for (int32_t row = 0; row < kSourceRows; ++row) {
        for (size_t index = 0; index < 256u; ++index) tables[static_cast<size_t>(row)][index] = static_cast<uint8_t>(index);
        std::array<std::array<int32_t, 32>, 3> counts{};
        for (int32_t column = 0; column < picture.width; ++column) {
            const uint8_t index = picture.indices[static_cast<size_t>(row) * static_cast<size_t>(picture.width) + static_cast<size_t>(column)];
            if (row >= band && row < bottom) held[index] = true;
            for (size_t channel = 0; channel < 3u; ++channel) ++counts[channel][static_cast<size_t>(colours.rgb[index][channel])];
        }
        for (size_t channel = 0; channel < 3u; ++channel) {
            int32_t seen = 0, level = 0;
            while (level < 31 && (seen += counts[channel][static_cast<size_t>(level)]) * 2 < picture.width) ++level;
            skies[static_cast<size_t>(row)][channel] = level;
        }
    }
    std::array<double, 3> target{};
    for (int32_t row = band; row < band + 4; ++row)
        for (size_t channel = 0; channel < 3u; ++channel) target[channel] += skies[static_cast<size_t>(row)][channel] / 4.0;
    for (int32_t row = band; row < bottom; ++row)
        for (size_t index = 0; index < 256u; ++index) {
            if (!held[index]) continue;
            std::array<double, 3> want{};
            for (size_t channel = 0; channel < 3u; ++channel)
                want[channel] = colours.rgb[index][channel] + target[channel] - skies[static_cast<size_t>(row)][channel];
            const auto off = [&](size_t other) {
                double sum = 0.0;
                for (size_t channel = 0; channel < 3u; ++channel) sum += (colours.rgb[other][channel] - want[channel]) * (colours.rgb[other][channel] - want[channel]);
                return sum;
            };
            size_t best = index;
            double nearest = off(index);
            for (size_t other = 0; other < 256u; ++other)
                if (held[other] && off(other) < nearest) nearest = off(other), best = other;
            tables[static_cast<size_t>(row)][index] = static_cast<uint8_t>(best);
        }
    return tables;
}

/* A sky of streaks, much more alike along its rows than down them: it grows a whole turn at once, as rows shifted round,
 * since a patch's side would cut every streak it meets. */
inline bool streaks(const sky_panorama::Picture &picture, const Colours &colours, int32_t band, int32_t bottom) {
    int64_t along = 0, down = 0;
    for (int32_t row = band; row + 4 < bottom; ++row)
        for (int32_t column = 0; column + 4 < picture.width; column += 2) {
            const auto at = [&](int32_t y, int32_t x) {
                return picture.indices[static_cast<size_t>(y) * static_cast<size_t>(picture.width) + static_cast<size_t>(x)];
            };
            along += colours.apart(at(row, column), at(row, column + 4));
            down += colours.apart(at(row, column), at(row + 4, column));
        }
    return along * 100 < down * kStreaks;
}

inline Extension grow(const sky_panorama::Picture &picture, const Palette &palette, int32_t rows, uint64_t seed) {
    Extension made;
    const int32_t width = picture.width;
    if (width < 2 * kPatchWidth || picture.height < kSourceRows || rows <= 0) return made;
    const Colours colours(palette);
    std::vector<int32_t> apart(256u * 256u);
    for (size_t one = 0; one < 256u; ++one)
        for (size_t other = 0; other < 256u; ++other)
            apart[one * 256u + other] = colours.apart(static_cast<uint8_t>(one), static_cast<uint8_t>(other));
    const std::vector<int32_t> means = row_means(picture, colours, kSourceRows);
    const int32_t band = top_band(means);
    const int32_t bottom = band + top_depth(means, band);
    /* Patches as tall as the top block allows, four rows of choice at least: a shallow one is not left for the skyline. */
    const int32_t tall = std::clamp(bottom - band - 2 * kStepY, kLeastTall, kPatchHeight);
    const int32_t overlap = tall * kOverlapY / kPatchHeight, rise = tall - overlap;
    rows += band;
    /* The source rows a patch is cut from, twice round the turn so that one shifted a whole turn reads straight. */
    const int32_t span = 2 * width;
    const auto taken = [&](int32_t row, int32_t column) {
        return picture.indices[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column % width)];
    };
    const std::vector<std::array<uint8_t, 256>> level = levelled(picture, colours, band, bottom);
    std::vector<uint8_t> source(static_cast<size_t>(kSourceRows) * static_cast<size_t>(span));
    for (int32_t row = 0; row < kSourceRows; ++row)
        for (int32_t column = 0; column < span; ++column)
            source[static_cast<size_t>(row * span + column)] = level[static_cast<size_t>(row)][taken(row, column)];
    const auto from_source = [&](int32_t row, int32_t column) { return source[static_cast<size_t>(row * span + column)]; };
    /* Rare texels per patch, from a summed table over the source: a patch that would bring a sun again pays for it. */
    std::array<int64_t, 256> seen{};
    for (int32_t row = band; row < bottom; ++row)
        for (int32_t column = 0; column < width; ++column) ++seen[from_source(row, column)];
    const int64_t texels = static_cast<int64_t>(bottom - band) * width;
    std::array<bool, 256> rare{};
    for (size_t index = 0; index < 256u; ++index) {
        int64_t near = 0;
        for (size_t other = 0; other < 256u; ++other)
            if (apart[index * 256u + other] <= kNear) near += seen[other];
        rare[index] = seen[index] * kRareIndexShare < texels && near * kRareNearShare < texels;
    }
    std::vector<int32_t> rare_sum(static_cast<size_t>(kSourceRows + 1) * static_cast<size_t>(span + 1), 0);
    const auto sum_at = [&](int32_t row, int32_t column) -> int32_t & { return rare_sum[static_cast<size_t>(row * (span + 1) + column)]; };
    for (int32_t row = 0; row < kSourceRows; ++row)
        for (int32_t column = 0; column < span; ++column)
            sum_at(row + 1, column + 1) = sum_at(row, column + 1) + sum_at(row + 1, column) - sum_at(row, column) +
                                          (rare[from_source(row, column)] ? 1 : 0);
    const std::vector<uint8_t> marked = skyline(picture, colours, band, bottom);
    std::vector<int32_t> skyline_sum(static_cast<size_t>(kSourceRows + 1) * static_cast<size_t>(span + 1), 0);
    const auto skyline_at = [&](int32_t row, int32_t column) -> int32_t & { return skyline_sum[static_cast<size_t>(row * (span + 1) + column)]; };
    for (int32_t row = 0; row < kSourceRows; ++row)
        for (int32_t column = 0; column < span; ++column)
            skyline_at(row + 1, column + 1) = skyline_at(row, column + 1) + skyline_at(row + 1, column) - skyline_at(row, column) +
                                              marked[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(column % width)];
    const auto skyline_in = [&](int32_t y, int32_t x, int32_t wide) {
        return static_cast<int64_t>(skyline_at(y + tall, x + wide) - skyline_at(y, x + wide) - skyline_at(y + tall, x) + skyline_at(y, x));
    };
    const auto rare_in = [&](int32_t y, int32_t x, int32_t wide) {
        return static_cast<int64_t>(sum_at(y + tall, x + wide) - sum_at(y, x + wide) - sum_at(y + tall, x) + sum_at(y, x));
    };
    /* The canvas: whole patch rows, then the panorama's rows from the band down as the collar the first row joins. */
    const int32_t grown = (rows + rise - 1) / rise * rise;
    std::vector<uint8_t> canvas(static_cast<size_t>(grown + overlap) * static_cast<size_t>(width), 0u);
    const auto at = [&](int32_t row, int32_t column) -> uint8_t & {
        return canvas[static_cast<size_t>(row) * static_cast<size_t>(width) + static_cast<size_t>(((column % width) + width) % width)];
    };
    for (int32_t row = 0; row < overlap; ++row)
        for (int32_t column = 0; column < width; ++column) at(grown + row, column) = taken(band + row, column);
    const bool whole = streaks(picture, colours, band, bottom);
    const int32_t patches = whole ? 1 : (width + kPatchWidth - kOverlapX - 1) / (kPatchWidth - kOverlapX);
    std::vector<int32_t> starts(static_cast<size_t>(patches) + 1u);
    for (int32_t patch = 0; patch <= patches; ++patch)
        starts[static_cast<size_t>(patch)] = static_cast<int32_t>(static_cast<int64_t>(patch) * width / patches);
    uint64_t state = seed | 1u;
    std::vector<int64_t> costs;
    std::vector<std::array<int32_t, 2>> probes, candidates;
    constexpr std::array<int32_t, 2> kNone{0, -kSourceRows};
    std::vector<std::array<int32_t, 2>> below(static_cast<size_t>(patches), kNone);
    /* A patch may not cut the source the patch below, to its left or to its right was cut from, unless it is that patch's
     * own continuation: else a sun or a planet's cloud comes again beside itself. */
    const auto shares = [&](int32_t x, int32_t y, const std::array<int32_t, 2> &other, int32_t onward_x, int32_t onward_y) {
        if (other[1] < 0 || (x == ((onward_x % width) + width) % width && y == onward_y)) return false;
        const int32_t across = std::abs(x - other[0]) % width;
        return std::min(across, width - across) < kPatchWidth && std::abs(y - other[1]) < tall;
    };
    for (int32_t top = grown; top > 0; top -= rise) {
        const int32_t base = top - rise;
        const bool collar = top == grown;
        std::array<int32_t, 2> first = kNone, beside = kNone;
        for (int32_t patch = 0; patch < patches; ++patch) {
            const int32_t left = starts[static_cast<size_t>(patch)];
            const int32_t wide = starts[static_cast<size_t>(patch) + 1u] - left + (whole ? 0 : kOverlapX);
            const bool joins_left = patch > 0, joins_right = !whole && patch == patches - 1;
            probes.clear();
            for (int32_t row = rise; row < tall; ++row)
                for (int32_t column = 0; column < wide; column += 2) probes.push_back({row * span + column, at(base + row, left + column)});
            for (int32_t row = 0; row < rise; row += 2) {
                if (joins_left)
                    for (int32_t column = 0; column < kOverlapX; column += 2) probes.push_back({row * span + column, at(base + row, left + column)});
                if (joins_right)
                    for (int32_t column = wide - kOverlapX; column < wide; column += 2)
                        probes.push_back({row * span + column, at(base + row, left + column)});
            }
            /* The panorama's own row stays under the first patches, so the row meeting it counts twice: that join is seen. */
            if (collar)
                for (int32_t column = 0; column < wide; column += 2) probes.push_back({rise * span + column, at(base + rise, left + column)});
            /* Any patch within a twentieth of the best may be drawn, so that the sky does not repeat. */
            const int64_t slack = static_cast<int64_t>(probes.size()) / 4;
            const auto allowed_over = [&](int64_t low) { return low + std::max<int64_t>(low / 20, slack); };
            const auto &under = below[static_cast<size_t>(patch)];
            const auto refused = [&](int32_t x, int32_t y) {
                if (whole) {
                    const int32_t across = std::abs(x - under[0]) % width;
                    return std::min(across, width - across) < 8 && std::abs(y - under[1]) < 4;
                }
                return shares(x, y, under, under[0], under[1] - rise) ||
                       (joins_left && shares(x, y, beside, beside[0] + left - starts[static_cast<size_t>(patch) - 1u], beside[1])) ||
                       (joins_right && shares(x, y, first, first[0] - (width - left), first[1]));
            };
            constexpr int64_t kRefused = std::numeric_limits<int64_t>::max();
            candidates.clear();
            for (int32_t y = band; y + tall <= bottom; y += kStepY)
                for (int32_t x = 0; x < width; x += kStepX) candidates.push_back({x, y});
            /* The patches that go on exactly from the one below or beside, which the grid mostly misses. */
            const auto onward = [&](int32_t x, int32_t y) {
                if (y >= band && y + tall <= bottom) candidates.push_back({((x % width) + width) % width, y});
            };
            if (!whole && under[1] >= 0) onward(under[0], under[1] - rise);
            if (joins_left) onward(beside[0] + left - starts[static_cast<size_t>(patch) - 1u], beside[1]);
            std::vector<std::array<int32_t, 2>> fits;
            for (int32_t pass = 0; pass < 2 && fits.empty(); ++pass) {
                costs.clear();
                int64_t lowest = std::numeric_limits<int64_t>::max() / 2;
                for (const auto &[x, y] : candidates) {
                    if (pass == 0 && refused(x, y)) {
                        costs.push_back(kRefused);
                        continue;
                    }
                    const uint8_t *origin = source.data() + static_cast<size_t>(y * span + x);
                    const int64_t limit = allowed_over(lowest);
                    int64_t cost = whole ? 0 : rare_in(y, x, wide) * kRareCost + skyline_in(y, x, wide) * kSkylineCost;
                    for (size_t probe = 0; probe < probes.size() && cost <= limit; ++probe)
                        cost += apart[static_cast<size_t>(origin[probes[probe][0]]) * 256u + static_cast<size_t>(probes[probe][1])];
                    costs.push_back(cost);
                    lowest = std::min(lowest, cost);
                }
                const int64_t allowed = allowed_over(lowest);
                for (size_t index = 0; index < candidates.size(); ++index)
                    if (costs[index] <= allowed) fits.push_back(candidates[index]);
            }
            const auto pick = fits[static_cast<size_t>(next(state) % fits.size())];
            below[static_cast<size_t>(patch)] = pick;
            beside = pick;
            if (patch == 0) first = pick;
            const auto error = [&](int32_t row, int32_t column) {
                return apart[static_cast<size_t>(from_source(pick[1] + row, pick[0] + column)) * 256u + at(base + row, left + column)];
            };
            std::vector<int32_t> lower(static_cast<size_t>(wide), overlap);
            if (!collar) {
                std::vector<int32_t> surface(static_cast<size_t>(wide * overlap));
                for (int32_t column = 0; column < wide; ++column)
                    for (int32_t row = 0; row < overlap; ++row)
                        surface[static_cast<size_t>(column * overlap + row)] = error(rise + row, column);
                lower = cut(surface, wide, overlap, whole);
            } else {
                std::fill(lower.begin(), lower.end(), 0);
            }
            std::vector<int32_t> from_left(static_cast<size_t>(tall), 0), to_right(static_cast<size_t>(tall), kOverlapX - 1);
            if (joins_left || joins_right) {
                std::vector<int32_t> surface(static_cast<size_t>(tall * kOverlapX));
                if (joins_left) {
                    for (int32_t row = 0; row < tall; ++row)
                        for (int32_t column = 0; column < kOverlapX; ++column)
                            surface[static_cast<size_t>(row * kOverlapX + column)] = error(row, column);
                    from_left = cut(surface, tall, kOverlapX);
                }
                if (joins_right) {
                    for (int32_t row = 0; row < tall; ++row)
                        for (int32_t column = 0; column < kOverlapX; ++column)
                            surface[static_cast<size_t>(row * kOverlapX + column)] = error(row, wide - kOverlapX + column);
                    to_right = cut(surface, tall, kOverlapX);
                }
            }
            for (int32_t row = 0; row < tall; ++row)
                for (int32_t column = 0; column < wide; ++column) {
                    if (row >= rise && row - rise >= lower[static_cast<size_t>(column)]) continue;
                    if (column < kOverlapX && column < from_left[static_cast<size_t>(row)]) continue;
                    if (column >= wide - kOverlapX && column - (wide - kOverlapX) > to_right[static_cast<size_t>(row)]) continue;
                    at(base + row, left + column) = from_source(pick[1] + row, pick[0] + column);
                }
        }
    }
    made.width = width;
    made.rows = rows;
    made.band = band;
    made.from_row = band;
    made.to_row = bottom;
    made.streaks = whole;
    const auto first = canvas.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(grown - rows) * static_cast<size_t>(width));
    made.indices.assign(first, first + static_cast<std::ptrdiff_t>(static_cast<size_t>(rows) * static_cast<size_t>(width)));
    return made;
}

}  // namespace disruptor::sky_extension
