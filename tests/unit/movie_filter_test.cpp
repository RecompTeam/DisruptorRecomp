#include "movie_filter.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

uint32_t grey(int value) {
    const auto level = static_cast<uint32_t>(value);
    return 0xFF000000u | level << 16 | level << 8 | level;
}

/* 8x8 blocks whose levels differ by a few steps, with a level of noise as video has, the grid starting at (phase_x,
 * phase_y). */
std::vector<uint32_t> blocks(int width, int height, int phase_x, int phase_y) {
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int block = (x - phase_x + 64) / 8 + (y - phase_y + 64) / 8;
            pixels[static_cast<size_t>(y) * width + x] = grey(100 + (block % 2) * 6 + (x * 7 + y * 13) % 3 - 1);
        }
    return pixels;
}

int step(const std::vector<uint32_t> &pixels, int width, int x, int y) {
    return std::abs(movie_filter::luma(pixels[static_cast<size_t>(y) * width + x]) -
                    movie_filter::luma(pixels[static_cast<size_t>(y) * width + x - 1]));
}

void test_grid_found_at_its_phase() {
    const std::vector<uint32_t> pixels = blocks(64, 48, 3, 5);
    expect(movie_filter::grid_phase(pixels.data(), 64, 48, true) == 3, "the block edges along x are found at phase 3");
    expect(movie_filter::grid_phase(pixels.data(), 64, 48, false) == 5, "the block edges along y are found at phase 5");
}

void test_small_steps_smoothed() {
    std::vector<uint32_t> pixels = blocks(64, 48, 0, 0);
    std::vector<int> scratch;
    const int before = step(pixels, 64, 16, 20);
    expect(movie_filter::clean(pixels.data(), 64, 48, scratch), "a frame of blocks is cleaned");
    expect(step(pixels, 64, 16, 20) * 2 < before, "the step across a block edge is less than half of what it was");
}

void test_real_edge_kept() {
    std::vector<uint32_t> pixels = blocks(64, 48, 0, 0);
    for (int y = 0; y < 48; ++y)
        for (int x = 32; x < 64; ++x) pixels[static_cast<size_t>(y) * 64 + x] = grey(230);
    std::vector<int> scratch;
    movie_filter::clean(pixels.data(), 64, 48, scratch);
    expect(step(pixels, 64, 32, 10) > 100, "a step of more than the threshold at a block edge stays an edge");
}

void test_frame_without_grid_left() {
    std::vector<uint32_t> pixels(64 * 48);
    for (int y = 0; y < 48; ++y)
        for (int x = 0; x < 64; ++x) pixels[static_cast<size_t>(y) * 64 + x] = 0xFF000000u | uint32_t(x * 3) << 16 | uint32_t(y * 4);
    const std::vector<uint32_t> kept = pixels;
    std::vector<int> scratch;
    expect(!movie_filter::clean(pixels.data(), 64, 48, scratch) && pixels == kept, "a smooth frame without blocks is left");
    std::vector<uint32_t> black(64 * 48, 0xFF000000u);
    expect(!movie_filter::clean(black.data(), 64, 48, scratch), "a black frame is no movie frame");
}

void test_stripe_on_a_flat_picture_left() {
    std::vector<uint32_t> pixels(64 * 48, grey(100));
    for (int y = 0; y < 48; ++y)
        for (int x = 16; x < 24; ++x) pixels[static_cast<size_t>(y) * 64 + x] = grey(106);
    const std::vector<uint32_t> kept = pixels;
    std::vector<int> scratch;
    expect(!movie_filter::clean(pixels.data(), 64, 48, scratch) && pixels == kept,
           "one stripe eight pixels wide on a flat picture is no block grid");
}

void test_saturated_boundary_keeps_luma() {
    std::vector<uint32_t> pixels = blocks(64, 48, 0, 0);
    for (int y = 0; y < 48; ++y)
        for (int x = 40; x < 64; ++x) pixels[static_cast<size_t>(y) * 64 + x] = x < 52 ? 0xFFFFFFFFu : 0xFFFF0000u;
    std::vector<int> before;
    for (const uint32_t pixel : pixels) before.push_back(movie_filter::luma(pixel));
    std::vector<int> scratch;
    movie_filter::clean(pixels.data(), 64, 48, scratch);
    int worst = 0;
    for (size_t index = 0; index < pixels.size(); ++index)
        worst = std::max(worst, std::abs(movie_filter::luma(pixels[index]) - before[index]));
    expect(worst <= 4, "a white pixel beside saturated red keeps its brightness: a colour move that would clip is shortened");
}

void test_colour_blurred_luma_kept() {
    std::vector<uint32_t> pixels = blocks(64, 48, 0, 0);
    for (int y = 0; y < 48; ++y) pixels[static_cast<size_t>(y) * 64 + 40] = 0xFFC86464u;
    std::vector<int> before_luma;
    for (const uint32_t pixel : pixels) before_luma.push_back(movie_filter::luma(pixel));
    std::vector<int> scratch;
    movie_filter::clean(pixels.data(), 64, 48, scratch);
    const uint32_t beside = pixels[20 * 64 + 39];
    expect(((beside >> 16) & 255u) > (beside & 255u) + 4, "a red line's colour spreads a pixel beside it");
    expect(std::abs(movie_filter::luma(pixels[20 * 64 + 39]) - before_luma[20 * 64 + 39]) <= 2,
           "the pixel beside it keeps its brightness");
    expect((pixels[0] >> 24) == 255u, "alpha stays");
}

/* The mean luma step between neighbours inside the block at (8..15, 8..15), where only the grain varies. */
double grain(const std::vector<uint32_t> &pixels, int width) {
    int sum = 0, count = 0;
    for (int y = 9; y < 15; ++y)
        for (int x = 9; x < 15; ++x, ++count) sum += step(pixels, width, x, y);
    return static_cast<double>(sum) / count;
}

void test_grain_smoothed() {
    std::vector<uint32_t> pixels = blocks(64, 48, 0, 0);
    const double before = grain(pixels, 64);
    std::vector<int> scratch;
    movie_filter::clean(pixels.data(), 64, 48, scratch);
    expect(grain(pixels, 64) * 2 < before, "the grain inside a block is less than half of what it was");
}

}  // namespace

int main() {
    test_grain_smoothed();
    test_grid_found_at_its_phase();
    test_small_steps_smoothed();
    test_real_edge_kept();
    test_frame_without_grid_left();
    test_stripe_on_a_flat_picture_left();
    test_saturated_boundary_keeps_luma();
    test_colour_blurred_luma_kept();
    if (g_failures == 0) std::cout << "movie filter: all passed\n";
    return g_failures == 0 ? 0 : 1;
}
