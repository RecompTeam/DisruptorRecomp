#ifndef PSX_MOVIE_FILTER_H
#define PSX_MOVIE_FILTER_H

/* A movie frame cleaned in place before it is shown larger: MDEC's block edges smoothed, flat edges three pixels deep,
 * the grain taken out of flat areas, the colour blurred a pixel. */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

namespace movie_filter {

inline constexpr int kAlpha = 64, kBeta = 18, kLimit = 36;
inline constexpr double kGrain = 14.0;  // the luma difference a neighbour is still averaged at, in steps of 255
inline constexpr long long kGridPercent = 110;  // a grid's mean step over the median phase's, below which no grid

inline int luma(uint32_t pixel) {
    return (77 * static_cast<int>((pixel >> 16) & 255u) + 150 * static_cast<int>((pixel >> 8) & 255u) +
            29 * static_cast<int>(pixel & 255u)) >> 8;
}

inline uint32_t &at(uint32_t *pixels, int width, bool columns, int along, int across) {
    return columns ? pixels[across * width + along] : pixels[along * width + across];
}

inline uint32_t at(const uint32_t *pixels, int width, bool columns, int along, int across) {
    return columns ? pixels[across * width + along] : pixels[along * width + across];
}

/* Block start phase along x or y, or -1: the weaker of its two edges in 16 counts (MOVIE3 has a 16-row pattern). */
inline int grid_phase(const uint32_t *pixels, int width, int height, bool columns) {
    long long sums[16] = {}, counts[16] = {};
    const int along = columns ? width : height, across = columns ? height : width;
    for (int a = 1; a < along; ++a)
        for (int b = 0; b < across; ++b) {
            const int here = luma(at(pixels, width, columns, a, b));
            sums[a % 16] += std::abs(here - luma(at(pixels, width, columns, a - 1, b)));
            ++counts[a % 16];
        }
    long long means[16], sorted[16], scores[8];
    for (int phase = 0; phase < 16; ++phase)
        sorted[phase] = means[phase] = counts[phase] ? sums[phase] * 1000 / counts[phase] : 0;
    for (int phase = 0; phase < 8; ++phase) scores[phase] = std::min(means[phase], means[phase + 8]);
    std::sort(sorted, sorted + 16);
    const long long median = (sorted[7] + sorted[8]) / 2;
    const int best = static_cast<int>(std::max_element(scores, scores + 8) - scores);
    return median > 0 && scores[best] * 100 >= median * kGridPercent ? best : -1;
}

inline int channel(uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 255u); }

inline uint32_t with_channel(uint32_t pixel, int shift, int value) {
    return (pixel & ~(255u << shift)) | (static_cast<uint32_t>(std::clamp(value, 0, 255)) << shift);
}

/* A flat edge is averaged three pixels deep on each side, any other small step moved towards its neighbour. */
inline void deblock_edges(uint32_t *pixels, int width, int height, bool columns, int phase) {
    const int along = columns ? width : height, across = columns ? height : width;
    for (int a = phase < 3 ? phase + 8 : phase; a + 2 < along; a += 8)
        for (int b = 0; b < across; ++b) {
            uint32_t &p2 = at(pixels, width, columns, a - 3, b), &p1 = at(pixels, width, columns, a - 2, b);
            uint32_t &p0 = at(pixels, width, columns, a - 1, b), &q0 = at(pixels, width, columns, a, b);
            uint32_t &q1 = at(pixels, width, columns, a + 1, b), &q2 = at(pixels, width, columns, a + 2, b);
            const int lp2 = luma(p2), lp1 = luma(p1), lp0 = luma(p0), lq0 = luma(q0), lq1 = luma(q1), lq2 = luma(q2);
            if (std::abs(lp0 - lq0) >= kAlpha || std::abs(lp1 - lp0) >= kBeta || std::abs(lq1 - lq0) >= kBeta) continue;
            const bool flat = std::abs(lp0 - lq0) < kAlpha / 4 + 2 && std::abs(lp2 - lp0) < kBeta && std::abs(lq2 - lq0) < kBeta;
            uint32_t n2 = p2, n1 = p1, n0 = p0, m0 = q0, m1 = q1, m2 = q2;
            for (const int shift : {16, 8, 0}) {
                const int P2 = channel(p2, shift), P1 = channel(p1, shift), P0 = channel(p0, shift);
                const int Q0 = channel(q0, shift), Q1 = channel(q1, shift), Q2 = channel(q2, shift);
                if (flat) {
                    n0 = with_channel(n0, shift, (P2 + 2 * P1 + 2 * P0 + 2 * Q0 + Q1 + 4) / 8);
                    n1 = with_channel(n1, shift, (P2 + P1 + P0 + Q0 + 2) / 4);
                    n2 = with_channel(n2, shift, (4 * P2 + 2 * P1 + P0 + Q0 + 4) / 8);
                    m0 = with_channel(m0, shift, (Q2 + 2 * Q1 + 2 * Q0 + 2 * P0 + P1 + 4) / 8);
                    m1 = with_channel(m1, shift, (Q2 + Q1 + Q0 + P0 + 2) / 4);
                    m2 = with_channel(m2, shift, (4 * Q2 + 2 * Q1 + Q0 + P0 + 4) / 8);
                } else {
                    const int delta = std::clamp(((Q0 - P0) * 4 + P1 - Q1) / 8, -kLimit, kLimit);
                    n0 = with_channel(n0, shift, P0 + delta);
                    m0 = with_channel(m0, shift, Q0 - delta);
                    n1 = with_channel(n1, shift, P1 + delta / 2);
                    m1 = with_channel(m1, shift, Q1 - delta / 2);
                }
            }
            p2 = n2; p1 = n1; p0 = n0; q0 = m0; q1 = m1; q2 = m2;
        }
}

/* A 5x5 bilateral pass: neighbours weighted by distance and by how near their luma is, so edges stay sharp. */
inline void remove_grain(uint32_t *pixels, int width, int height, std::vector<int> &scratch) {
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    scratch.resize(count * 2);
    int *lumas = scratch.data(), *source = lumas + count;
    for (size_t index = 0; index < count; ++index) {
        lumas[index] = luma(pixels[index]);
        source[index] = static_cast<int>(pixels[index]);
    }
    std::array<int, 25> spatial{};
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            spatial[(y + 2) * 5 + x + 2] = static_cast<int>(std::lround(256.0 * std::exp(-(x * x + y * y) / 2.42)));
    std::array<int, 256> near{};
    for (int step = 0; step < 256; ++step)
        near[step] = static_cast<int>(std::lround(256.0 * std::exp(-(step * step) / (2.0 * kGrain * kGrain))));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int centre = lumas[static_cast<size_t>(y) * width + x];
            int red = 0, green = 0, blue = 0, total = 0;
            for (int dy = -2; dy <= 2; ++dy) {
                const size_t row = static_cast<size_t>(std::clamp(y + dy, 0, height - 1)) * width;
                for (int dx = -2; dx <= 2; ++dx) {
                    const size_t from = row + std::clamp(x + dx, 0, width - 1);
                    const int weight = spatial[(dy + 2) * 5 + dx + 2] * near[std::abs(lumas[from] - centre)];
                    const uint32_t pixel = static_cast<uint32_t>(source[from]);
                    red += weight * channel(pixel, 16);
                    green += weight * channel(pixel, 8);
                    blue += weight * channel(pixel, 0);
                    total += weight;
                }
            }
            const size_t here = static_cast<size_t>(y) * width + x;
            pixels[here] = (pixels[here] & 0xFF000000u) | static_cast<uint32_t>((red + total / 2) / total) << 16 |
                           static_cast<uint32_t>((green + total / 2) / total) << 8 |
                           static_cast<uint32_t>((blue + total / 2) / total);
        }
}

/* Chroma through a 3x3 binomial blur with luma kept, each move shortened alike so that no channel clips. */
inline void smooth_chroma(uint32_t *pixels, int width, int height, std::vector<int> &scratch) {
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    scratch.resize(count * 4);
    int *blue = scratch.data(), *red = blue + count, *row_blue = red + count, *row_red = row_blue + count;
    for (size_t index = 0; index < count; ++index) {
        const int y = luma(pixels[index]);
        blue[index] = channel(pixels[index], 0) - y;
        red[index] = channel(pixels[index], 16) - y;
    }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t row = static_cast<size_t>(y) * width, here = row + x;
            const size_t left = row + std::max(x - 1, 0), right = row + std::min(x + 1, width - 1);
            row_blue[here] = blue[left] + 2 * blue[here] + blue[right];
            row_red[here] = red[left] + 2 * red[here] + red[right];
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t here = static_cast<size_t>(y) * width + x;
            const size_t up = static_cast<size_t>(std::max(y - 1, 0)) * width + x;
            const size_t down = static_cast<size_t>(std::min(y + 1, height - 1)) * width + x;
            const int new_blue = (row_blue[up] + 2 * row_blue[here] + row_blue[down] + 8) / 16;
            const int new_red = (row_red[up] + 2 * row_red[here] + row_red[down] + 8) / 16;
            const int moved_blue = new_blue - blue[here], moved_red = new_red - red[here];
            const int moved[3] = {moved_red, -(29 * moved_blue + 77 * moved_red) / 150, moved_blue};
            const uint32_t pixel = pixels[here];
            const int levels[3] = {channel(pixel, 16), channel(pixel, 8), channel(pixel, 0)};
            int share = 256;
            for (int index = 0; index < 3; ++index) {
                if (levels[index] + moved[index] > 255)
                    share = std::min(share, (255 - levels[index]) * 256 / moved[index]);
                if (levels[index] + moved[index] < 0) share = std::min(share, levels[index] * 256 / -moved[index]);
            }
            pixels[here] = (pixel & 0xFF000000u) | static_cast<uint32_t>(levels[0] + moved[0] * share / 256) << 16 |
                           static_cast<uint32_t>(levels[1] + moved[1] * share / 256) << 8 |
                           static_cast<uint32_t>(levels[2] + moved[2] * share / 256);
        }
}

/* Returns whether the frame had a block grid and was cleaned. */
inline bool clean(uint32_t *pixels, int width, int height, std::vector<int> &scratch) {
    if (width < 16 || height < 16) return false;
    const int columns = grid_phase(pixels, width, height, true);
    if (columns >= 0) deblock_edges(pixels, width, height, true, columns);
    const int rows = grid_phase(pixels, width, height, false);
    if (rows >= 0) deblock_edges(pixels, width, height, false, rows);
    if (columns < 0 && rows < 0) return false;
    remove_grain(pixels, width, height, scratch);
    smooth_chroma(pixels, width, height, scratch);
    return true;
}

}  // namespace movie_filter

#endif
