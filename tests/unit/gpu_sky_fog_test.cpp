#include "gpu_sky_fog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

namespace {

constexpr std::uint16_t kClearRow = 300;
constexpr std::uint16_t kClutX = 0x14;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    if (g_failures <= 20) std::cerr << "FAIL: " << message << '\n';
}

std::uint16_t clut_of(std::int32_t step) { return static_cast<std::uint16_t>((kClearRow + step) << 6 | kClutX); }

std::array<std::uint32_t, PSX_SKY_FOG_WORDS> tile_words(std::int32_t x, std::int32_t y, std::int32_t w, std::int32_t h,
                                                        std::int32_t step) {
    return {0x64808080u, static_cast<std::uint32_t>(static_cast<std::uint16_t>(y)) << 16 | static_cast<std::uint16_t>(x),
            static_cast<std::uint32_t>(clut_of(step)) << 16 | 0x7424u,
            static_cast<std::uint32_t>(h) << 16 | static_cast<std::uint32_t>(w)};
}

struct Tile {
    std::uint32_t key;
    std::array<std::uint32_t, PSX_SKY_FOG_WORDS> words;
};

/* An opening the way the game fills it: tiles of up to 64 columns, rows of 60 lines from the top. */
std::vector<Tile> fill(PsxSkyFog &fog, std::uint32_t &key, std::int32_t left, std::int32_t right, std::int32_t bottom,
                       std::int32_t step, std::int32_t top = 0) {
    std::vector<Tile> tiles;
    psx_sky_fog_open(&fog, step);
    for (std::int32_t y = top; y < bottom; y += 60)
        for (std::int32_t x = left; x < right; x = (x / 64 + 1) * 64 + (x < 0 ? -64 : 0)) {
            const std::int32_t end = std::min(right, (x / 64 + 1) * 64 + (x < 0 ? -64 : 0));
            Tile tile{key += 0x18u, tile_words(x, y, end - x, std::min(60, bottom - y), step)};
            psx_sky_fog_tile(&fog, tile.key, tile.words.data());
            tiles.push_back(tile);
        }
    return tiles;
}

/* The palette the tile is drawn with, or 0 when it is drawn as the game made it. */
std::uint16_t take(PsxSkyFog &fog, const Tile &tile) {
    std::uint16_t clut = 0u;
    return psx_sky_fog_take(&fog, tile.key, tile.words.data(), &clut) ? clut : 0u;
}

void test_the_clearest_sky_runs_across_openings_that_overlap() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto doorway = fill(*fog, key, 105, 210, 240, 7);
    const auto left = fill(*fog, key, -54, 106, 240, 3);
    const auto wide = fill(*fog, key, 32, 290, 240, 0);
    expect(take(*fog, doorway[1]) == clut_of(0), "the doorway's fogged tile takes the wide opening's clear palette");
    expect(take(*fog, left[0]) == clut_of(0), "a side tile clear of the wide opening takes it too: its own opening meets it");
    expect(take(*fog, wide[2]) == 0u, "the clearest opening is drawn as the game made it");
}

void test_openings_that_share_an_edge_are_one_sky() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto far = fill(*fog, key, 133, 175, 240, 6);
    fill(*fog, key, 175, 374, 240, 0);
    expect(take(*fog, far[0]) == clut_of(0), "an opening beside a clearer one takes its palette");
    auto apart = std::make_unique<PsxSkyFog>();
    const auto alone = fill(*apart, key, 133, 174, 240, 6);
    fill(*apart, key, 175, 374, 240, 0);
    expect(take(*apart, alone[0]) == 0u, "a column of something else between them keeps each its own fog");
}

void test_the_clearest_sky_is_reached_through_other_openings() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto farthest = fill(*fog, key, 0, 40, 240, 8);
    const auto middle = fill(*fog, key, 40, 100, 240, 5);
    fill(*fog, key, 100, 320, 240, 0);
    const auto window = fill(*fog, key, 330, 374, 240, 4);
    expect(take(*fog, farthest[0]) == clut_of(0), "an opening reaches the clear one through the one between");
    expect(take(*fog, middle[0]) == clut_of(0), "and so does the one between");
    expect(take(*fog, window[0]) == 0u, "an opening that meets none keeps its fog");
}

void test_openings_meet_across_rows_too() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto far = fill(*fog, key, 64, 128, 240, 5);
    fill(*fog, key, 0, 320, 188, 1);
    expect(take(*fog, far.back()) == clut_of(1), "below a clearer opening's last row the tile takes its palette too");
    auto lower = std::make_unique<PsxSkyFog>();
    const auto high = fill(*lower, key, 64, 128, 60, 5);
    fill(*lower, key, 0, 320, 120, 1, 60);
    expect(take(*lower, high[0]) == clut_of(1), "an opening just above a clearer one meets it on the row they share");
    auto parted = std::make_unique<PsxSkyFog>();
    const auto top = fill(*parted, key, 64, 128, 60, 5);
    fill(*parted, key, 0, 320, 120, 1, 61);
    expect(take(*parted, top[0]) == 0u, "a row of something else between them keeps each its own fog");
}

void test_equal_or_foggier_openings_change_nothing() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto first = fill(*fog, key, 0, 128, 120, 4);
    fill(*fog, key, 0, 320, 240, 4);
    fill(*fog, key, 0, 320, 240, 9);
    expect(take(*fog, first[0]) == 0u, "an opening as foggy or foggier takes nothing from a tile");
}

void test_a_tile_is_drawn_once_from_its_own_packet() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto foggy = fill(*fog, key, 0, 64, 60, 6);
    fill(*fog, key, 0, 320, 240, 0);
    Tile changed = foggy[0];
    changed.words[2] ^= 1u;
    expect(take(*fog, changed) == 0u, "a packet whose words changed since it was built is drawn as it stands");
    expect(take(*fog, foggy[0]) == 0u, "and its record went with that draw");
    std::uint16_t clut = 0u;
    expect(!psx_sky_fog_take(fog.get(), 0x800D0000u, foggy[0].words.data(), &clut), "an unknown packet is drawn as it stands");
    const auto next = fill(*fog, key, 0, 64, 60, 6);
    expect(take(*fog, next[0]) == 0u, "a frame whose drawn tile had changed still ends, and its clear opening stays its own");
    expect(!psx_sky_fog_take(fog.get(), 0u, foggy[0].words.data(), &clut), "so is one without an address");
    expect(clut == 0u, "a tile drawn as it stands leaves the palette alone");
}

void test_a_packet_built_again_keeps_one_record() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    fill(*fog, key, 0, 320, 240, 0);
    psx_sky_fog_open(fog.get(), 6);
    const Tile first{0x800E0000u, tile_words(0, 0, 64, 60, 6)}, second{0x800E1000u, tile_words(64, 0, 64, 60, 6)};
    psx_sky_fog_tile(fog.get(), first.key, first.words.data());
    psx_sky_fog_tile(fog.get(), second.key, second.words.data());
    expect(take(*fog, first) == clut_of(0), "two packets 4 KB apart are both recorded");
    psx_sky_fog_open(fog.get(), 6);
    psx_sky_fog_tile(fog.get(), second.key, second.words.data());
    expect(take(*fog, second) == 0u, "the packet built again in a frame with no clear sky keeps its fog");
    expect(take(*fog, second) == 0u, "and its record from the frame before went with it");
}

void test_a_ninth_live_packet_in_one_run_of_slots_drops_the_first() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    fill(*fog, key, 0, 320, 240, 0);
    psx_sky_fog_open(fog.get(), 6);
    std::vector<Tile> tiles;
    for (std::uint32_t i = 0; i <= PSX_SKY_FOG_PROBES; i++) {
        tiles.push_back({0x800E0200u + i * 0x1000u, tile_words(0, 0, 8, 60, 6)});
        psx_sky_fog_tile(fog.get(), tiles.back().key, tiles.back().words.data());
    }
    expect(take(*fog, tiles[0]) == 0u, "the first of nine live packets in one run of slots keeps the game's fog");
    bool others = true;
    for (std::uint32_t i = 1; i <= PSX_SKY_FOG_PROBES; i++) others &= take(*fog, tiles[i]) == clut_of(0);
    expect(others, "and the other eight are taken");
}

void test_a_frame_ends_with_its_first_drawn_tile() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto doorway = fill(*fog, key, 105, 210, 120, 7);
    fill(*fog, key, 0, 320, 240, 0);
    expect(take(*fog, doorway[0]) == clut_of(0), "the first frame's tile is cleared");
    const auto next = fill(*fog, key, 105, 210, 120, 7);
    expect(take(*fog, doorway[1]) == clut_of(0), "a tile of the frame being drawn keeps its frame while the next is built");
    expect(take(*fog, next[0]) == 0u, "the next frame does not take the openings of the one before");
    const auto later = fill(*fog, key, 0, 320, 60, 2);
    fill(*fog, key, 0, 320, 60, 1);
    expect(take(*fog, doorway[2]) == 0u, "a record two frames old is drawn as it stands, whatever lies over it now");
    expect(take(*fog, later[0]) == clut_of(1), "a frame begun after a draw has its own openings");
}

void test_a_reset_forgets_every_record() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto foggy = fill(*fog, key, 0, 64, 60, 6);
    fill(*fog, key, 0, 320, 240, 0);
    psx_sky_fog_reset(fog.get());
    expect(take(*fog, foggy[0]) == 0u, "a packet built before a reset meets no record after it");
    key = 0x800C0000u;
    const auto again = fill(*fog, key, 0, 64, 60, 6);
    expect(again[0].key == foggy[0].key && take(*fog, again[0]) == 0u,
           "the same packet built again over no clearer opening is drawn as it stands");
}

void test_too_many_openings_leave_the_frame_as_the_game_made_it() {
    auto fog = std::make_unique<PsxSkyFog>();
    std::uint32_t key = 0x800C0000u;
    const auto foggy = fill(*fog, key, 0, 64, 60, 8);
    for (std::uint32_t i = 0; i < PSX_SKY_FOG_OPENINGS; i++) fill(*fog, key, 0, 64, 60, 0);
    expect(take(*fog, foggy[0]) == 0u, "past the table the tiles are drawn as the game made them");
    const auto after = fill(*fog, key, 0, 64, 60, 5);
    fill(*fog, key, 0, 64, 60, 0);
    expect(take(*fog, after[0]) == clut_of(0), "and the frame after the full one is cleared again");
    auto full = std::make_unique<PsxSkyFog>();
    const auto first = fill(*full, key, 0, 8, 60, 9);
    for (std::int32_t i = 1; i < static_cast<std::int32_t>(PSX_SKY_FOG_OPENINGS); i++)
        fill(*full, key, 8 * i, 8 * i + 8, 60, i == static_cast<std::int32_t>(PSX_SKY_FOG_OPENINGS) - 1 ? 0 : 9);
    expect(take(*full, first[0]) == clut_of(0), "a full table of openings is walked to its last one");
}

/* Random frames against openings settled pair by pair until nothing changes: each tile takes the least step it reaches. */
void test_each_tile_takes_the_least_step_it_reaches() {
    std::mt19937 random(20261029u);
    int tiles = 0, cleared = 0;
    for (int round = 0; round < 2000; round++) {
        auto fog = std::make_unique<PsxSkyFog>();
        std::uint32_t key = 0x800C0000u;
        struct Opening {
            std::int32_t left, right, top, bottom, step, least;
            std::vector<Tile> tiles;
        };
        std::vector<Opening> openings;
        const int count = 1 + static_cast<int>(random() % 6u);
        for (int i = 0; i < count; i++) {
            const std::int32_t left = static_cast<std::int32_t>(random() % 360u) - 54;
            const std::int32_t right = left + 1 + static_cast<std::int32_t>(random() % 120u);
            const std::int32_t top = static_cast<std::int32_t>(random() % 3u) * 60;
            const std::int32_t bottom = top + 1 + static_cast<std::int32_t>(random() % (240u - static_cast<std::uint32_t>(top)));
            const std::int32_t step = static_cast<std::int32_t>(random() % 12u);
            openings.push_back({left, right, top, bottom, step, step, fill(*fog, key, left, right, bottom, step, top)});
        }
        for (bool changed = true; changed;) {
            changed = false;
            for (Opening &one : openings)
                for (const Opening &other : openings)
                    if (one.left <= other.right && other.left <= one.right && one.top <= other.bottom &&
                        other.top <= one.bottom && other.least < one.least) {
                        one.least = other.least;
                        changed = true;
                    }
        }
        for (const Opening &opening : openings)
            for (const Tile &tile : opening.tiles) {
                const std::uint16_t clut = take(*fog, tile);
                ++tiles;
                cleared += clut != 0u;
                expect(clut == (opening.least < opening.step ? clut_of(opening.least) : 0u),
                       "a tile takes the palette of the least step its opening reaches, and only when it is clearer");
            }
    }
    expect(tiles > 5000 && cleared * 10 > tiles, "the random frames cleared enough tiles to tell");
}

}  // namespace

int main() {
    test_the_clearest_sky_runs_across_openings_that_overlap();
    test_openings_that_share_an_edge_are_one_sky();
    test_the_clearest_sky_is_reached_through_other_openings();
    test_openings_meet_across_rows_too();
    test_equal_or_foggier_openings_change_nothing();
    test_a_tile_is_drawn_once_from_its_own_packet();
    test_a_packet_built_again_keeps_one_record();
    test_a_ninth_live_packet_in_one_run_of_slots_drops_the_first();
    test_a_frame_ends_with_its_first_drawn_tile();
    test_a_reset_forgets_every_record();
    test_too_many_openings_leave_the_frame_as_the_game_made_it();
    test_each_tile_takes_the_least_step_it_reaches();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "gpu_sky_fog: PASS\n";
    return 0;
}
