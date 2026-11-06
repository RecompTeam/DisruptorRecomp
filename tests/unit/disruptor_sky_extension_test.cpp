#include "game/render/sky_extension.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <set>

namespace {

using namespace disruptor;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* Panoramas of a whole turn, a skyline from row 100. Streaks: rows of colour along the turn and a dark band at the top.
 * Clouds: blobs as wide as tall. Fire: a dark top over flames. */
struct Sky {
    sky_panorama::Picture picture;
    sky_extension::Palette palette{};
};

template <typename Value>
Sky panorama(Value value) {
    Sky sky;
    for (int index = 1; index < 256; ++index) {
        const int level = index / 8;
        sky.palette[static_cast<size_t>(index)] = static_cast<uint16_t>((level & 31) | ((level / 2) << 5) | ((31 - level / 2) << 10));
    }
    sky.picture.width = 1280;
    sky.picture.height = 240;
    sky.picture.indices.resize(1280u * 240u);
    for (int row = 0; row < 240; ++row)
        for (int column = 0; column < 1280; ++column)
            sky.picture.indices[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)] = static_cast<uint8_t>(
                std::clamp(static_cast<int>(row >= 100 ? 250.0 : value(row, column * 6.283185307179586 / 1280.0)), 1, 255));
    return sky;
}

Sky streaks() {
    return panorama([](int row, double turn) {
        return row < 6 ? 20.0 + row : 120.0 + 60.0 * std::sin(row * 0.7 + 3.0 * std::sin(turn * 5.0)) + 40.0 * std::sin(turn * 9.0 + row * 0.13);
    });
}

Sky clouds() {
    return panorama([](int row, double turn) {
        const double grain = static_cast<double>((row * 7919 + static_cast<int>(turn * 1000.0) * 104729) % 17) - 8.0;
        return 120.0 + 50.0 * std::sin(turn * 23.0 + 2.0 * std::sin(row * 0.21)) * std::sin(row * 0.19 + 1.3 * std::sin(turn * 17.0)) +
               30.0 * std::sin(turn * 41.0 + row * 0.2) + grain;
    });
}

Sky fire() {
    return panorama([](int row, double turn) {
        return row < 27 ? 20.0 + 10.0 * std::sin(turn * 7.0 + row * 0.3) : 200.0 + 20.0 * std::sin(turn * 5.0 + row * 0.4);
    });
}

int64_t row_apart(const sky_extension::Colours &colours, const uint8_t *one, const uint8_t *other, int width) {
    int64_t sum = 0;
    for (int column = 0; column < width; ++column) sum += colours.apart(one[column], other[column]);
    return sum;
}

std::set<uint8_t> indices_of(const Sky &sky, int first_row, int end_row) {
    std::set<uint8_t> found;
    for (int row = first_row; row < end_row; ++row)
        for (int column = 0; column < 1280; ++column) found.insert(sky.picture.indices[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)]);
    return found;
}

void test_streaks_grow_a_whole_turn_at_once_and_clouds_in_patches() {
    const Sky streaked = streaks(), clouded = clouds();
    expect(sky_extension::streaks(streaked.picture, sky_extension::Colours(streaked.palette), 6, 60), "rows of colour along the turn are streaks");
    expect(!sky_extension::streaks(clouded.picture, sky_extension::Colours(clouded.palette), 0, 60), "blobs as wide as tall are not");
}

void test_the_sky_grows_from_its_own_top_block() {
    for (const Sky &sky : {streaks(), clouds()}) {
        const auto made = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
        expect(made.width == 1280 && made.rows == sky_extension::kRows + made.band &&
                   made.indices.size() == 1280u * static_cast<size_t>(made.rows),
               "the extension is the panorama's width and the asked rows tall, over the band too");
        const std::set<uint8_t> top_block = indices_of(sky, made.band, sky_extension::kSourceRows);
        bool from_top = true;
        for (const uint8_t index : made.indices) from_top = from_top && top_block.contains(index);
        expect(from_top, "every index is one the panorama's top block holds below the band: none made, none from the skyline or the band");
    }
    expect(sky_extension::grow(streaks().picture, streaks().palette, sky_extension::kRows, 7u).band == 6,
           "the six dark rows the streaks end in are found as their top band");
    expect(sky_extension::grow(clouds().picture, clouds().palette, sky_extension::kRows, 7u).band == 0, "clouds to the top have no band");
}

void test_a_fire_sky_keeps_its_flames_below() {
    const Sky sky = fire();
    const auto made = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
    expect(made.band == 0, "a dark top's ripples are no band");
    const std::set<uint8_t> dark = indices_of(sky, 0, 27);
    bool kept = !made.indices.empty();
    for (const uint8_t index : made.indices) kept = kept && dark.contains(index);
    expect(kept, "the grown sky takes the dark top only: no flame is laid above it");
}

/* A plain sky with a little grain, its indices 96 to 103. */
Sky plain() {
    return panorama([](int row, double turn) { return 96.0 + static_cast<double>((row * 31 + static_cast<int>(turn * 2000.0) * 17) % 8); });
}

void test_a_sun_is_not_laid_again() {
    Sky sky = plain();
    for (int row = 0; row < 40; ++row)
        for (int column = 280; column < 320; ++column) {
            const double apart = std::hypot(row - 16.0, column - 300.0);
            if (apart < 10.0) sky.picture.indices[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)] = static_cast<uint8_t>(240 - 4 * static_cast<int>(apart));
        }
    const auto made = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
    int sun = 0;
    for (const uint8_t index : made.indices) sun += index >= 200 ? 1 : 0;
    expect(!made.indices.empty() && sun == 0, "a sun whole inside the panorama is never laid again above it");
}

void test_a_sky_lighter_and_darker_in_bands_grows_as_level_as_its_top() {
    /* Mission 13's kind: under a dark band, rows of grey 10 and a darker run of grey 9, each with a second colour as light. */
    Sky sky = panorama([](int row, double turn) {
        const int pair = static_cast<int>(turn * 640.0 / 6.283185307179586) % 2;
        return row < 4 ? 100.0 : row >= 16 && row < 32 ? 104.0 + pair : 102.0 + pair;
    });
    sky.palette[100] = static_cast<uint16_t>(8 | (8 << 5) | (8 << 10));
    sky.palette[102] = static_cast<uint16_t>(10 | (10 << 5) | (10 << 10));
    sky.palette[103] = static_cast<uint16_t>(11 | (10 << 5) | (9 << 10));
    sky.palette[104] = static_cast<uint16_t>(9 | (9 << 5) | (9 << 10));
    sky.palette[105] = static_cast<uint16_t>(10 | (9 << 5) | (8 << 10));
    const auto made = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
    const sky_extension::Colours colours(sky.palette);
    int64_t low = 1 << 30, high = 0;
    for (int row = 0; row < made.rows; ++row) {
        int64_t sum = 0;
        for (int column = 0; column < 1280; ++column) sum += sky_extension::lightness(colours, made.indices[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)]);
        low = std::min(low, sum / 1280);
        high = std::max(high, sum / 1280);
    }
    expect(made.band == 4 && made.to_row == 60, "the dark band is found and the darker run below is kept in the top block");
    expect(!made.indices.empty() && high - low <= 2, "every grown row is as light as the top: the darker run below makes no bands in it");
}

void test_the_top_block_ends_where_a_fire_sky_turns_to_flames() {
    const Sky sky = fire();
    const sky_extension::Colours colours(sky.palette);
    const int depth = sky_extension::top_depth(sky_extension::row_means(sky.picture, colours, sky_extension::kSourceRows), 0);
    expect(depth >= 24 && depth <= 27, "the top block is the dark rows above the flames");
}

void test_the_skyline_is_dark_rock_rising_from_the_block_bottom() {
    /* A dusk sky of dark purples: black peaks from the block's last row, a black cloud apart, a bright planet from below. */
    Sky sky = plain();
    for (int index = 96; index < 104; ++index)
        sky.palette[static_cast<size_t>(index)] = static_cast<uint16_t>((6 + index % 3) | ((2 + index % 2) << 5) | ((9 + index % 4) << 10));
    for (int index = 10; index < 16; ++index) sky.palette[static_cast<size_t>(index)] = static_cast<uint16_t>(1 | (1 << 5) | ((1 + index % 2) << 10));
    const auto set = [&](int row, int column, int index) { sky.picture.indices[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)] = static_cast<uint8_t>(index); };
    for (int row = 8; row < 60; ++row)
        for (int column = 0; column < 1280; ++column)
            if (std::abs(column - 300) * 3 < row - 8) set(row, column, 10 + column % 6);
    for (int row = 12; row < 16; ++row)
        for (int column = 700; column < 740; ++column) set(row, column, 12);
    for (int row = 30; row < 60; ++row)
        for (int column = 1000; column < 1100; ++column) set(row, column, 240);
    const std::vector<uint8_t> marked = sky_extension::skyline(sky.picture, sky_extension::Colours(sky.palette), 0, 60);
    const auto mark = [&](int row, int column) { return marked[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)] != 0; };
    expect(mark(59, 300) && mark(20, 300) && mark(9, 300), "a black peak from the block's last row is skyline to its tip");
    expect(!mark(13, 720), "a black cloud standing apart is not");
    expect(!mark(45, 1050), "nor a bright planet rising from below");
    expect(!mark(5, 300) && !mark(40, 500), "nor the sky");
}

void test_it_joins_the_panorama_and_wraps_round_the_turn() {
    for (const Sky &sky : {streaks(), clouds()}) {
        const auto made = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
        const sky_extension::Colours colours(sky.palette);
        const uint8_t *bottom = made.indices.data() + static_cast<size_t>(made.rows - 1) * 1280u;
        const int64_t join = row_apart(colours, bottom, sky.picture.indices.data() + 1280u * static_cast<size_t>(made.band), 1280);
        int64_t inside = 0;
        for (int row = made.band; row < made.band + 20; ++row)
            inside = std::max(inside, row_apart(colours, sky.picture.indices.data() + 1280u * static_cast<size_t>(row),
                                                sky.picture.indices.data() + 1280u * static_cast<size_t>(row + 1), 1280));
        expect(join <= inside * 2, "the extension's last row meets the first below the band as closely as neighbouring rows meet inside it");
        int64_t wrap = 0, across = 0;
        for (int row = 0; row < made.rows; ++row) {
            const uint8_t *line = made.indices.data() + static_cast<size_t>(row) * 1280u;
            wrap += colours.apart(line[1279], line[0]);
            for (int column = 0; column < 1279; ++column) across += colours.apart(line[column], line[column + 1]);
        }
        expect(wrap * 1279 <= across * 3, "the last column meets the first as neighbouring columns meet");
    }
}

void test_it_is_the_same_for_the_same_sky_and_does_not_repeat_itself() {
    for (const Sky &sky : {streaks(), clouds()}) {
        const auto once = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
        expect(once == sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u), "the same sky grows the same extension");
        int repeats = 0;
        for (int row = 0; row + sky_extension::kRise < once.rows; ++row)
            repeats += std::equal(once.indices.begin() + row * 1280, once.indices.begin() + (row + 1) * 1280,
                                  once.indices.begin() + (row + sky_extension::kRise) * 1280)
                           ? 1 : 0;
        expect(repeats == 0, "no row repeats a patch row higher, as a tiled sky would");
    }
    const Sky sky = clouds();
    expect(!(sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u) == sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 8u)),
           "another seed draws other patches among the fitting ones");
}

void test_it_is_quick_enough_for_a_worker() {
    for (const Sky &sky : {streaks(), clouds()}) {
        const auto started = std::chrono::steady_clock::now();
        (void)sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
        const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        std::cout << "grown in " << took << " ms\n";
        expect(took < 2000, "a sky grows in well under two seconds");
    }
}

void test_a_sky_with_no_band_grows_from_its_first_row() {
    Sky sky = streaks();
    for (int row = 0; row < 6; ++row)
        for (int column = 0; column < 1280; ++column)
            sky.picture.indices[static_cast<size_t>(row) * 1280u + static_cast<size_t>(column)] =
                sky.picture.indices[static_cast<size_t>(row + 12) * 1280u + static_cast<size_t>(column)];
    const auto made = sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u);
    expect(made.band == 0 && made.rows == sky_extension::kRows, "a sky whose top rows are like the rest has no band");
}

void test_a_revision_names_the_indices_and_the_palette() {
    Sky sky = streaks();
    sky.picture.hash = sky_panorama::indices_hash(sky.picture.indices);
    const uint64_t first = sky_extension::revision(sky.picture, sky.palette);
    expect(first == sky_extension::revision(sky.picture, sky.palette) && first != 0, "the same panorama has the same revision");
    Sky other = sky;
    other.picture.indices[5000] ^= 1u;
    other.picture.hash = sky_panorama::indices_hash(other.picture.indices);
    expect(sky_extension::revision(other.picture, other.palette) != first, "one index more makes another");
    other = sky;
    other.palette[200] ^= 1u;
    expect(sky_extension::revision(other.picture, other.palette) != first, "one colour of the palette too");
}

void test_a_picture_too_small_grows_nothing() {
    Sky sky = streaks();
    sky.picture.height = 30;
    expect(sky_extension::grow(sky.picture, sky.palette, sky_extension::kRows, 7u).rows == 0, "a panorama shorter than the top block grows nothing");
}

}  // namespace

int main() {
    test_streaks_grow_a_whole_turn_at_once_and_clouds_in_patches();
    test_the_sky_grows_from_its_own_top_block();
    test_a_fire_sky_keeps_its_flames_below();
    test_a_sun_is_not_laid_again();
    test_a_sky_lighter_and_darker_in_bands_grows_as_level_as_its_top();
    test_the_top_block_ends_where_a_fire_sky_turns_to_flames();
    test_the_skyline_is_dark_rock_rising_from_the_block_bottom();
    test_it_joins_the_panorama_and_wraps_round_the_turn();
    test_it_is_the_same_for_the_same_sky_and_does_not_repeat_itself();
    test_it_is_quick_enough_for_a_worker();
    test_a_sky_with_no_band_grows_from_its_first_row();
    test_a_revision_names_the_indices_and_the_palette();
    test_a_picture_too_small_grows_nothing();
    if (g_failures) return 1;
    std::cout << "disruptor_sky_extension: all tests passed\n";
    return 0;
}
