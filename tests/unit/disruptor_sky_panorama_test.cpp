#include "game/render/sky_panorama.h"
#include "game/render/sky_look.h"

#include <cstdint>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace {

using namespace disruptor::sky_panorama;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr uint32_t kAt = 0x800AF8C8u;  /* where the rooftop level's table lay */

struct Ram {
    std::unordered_map<uint32_t, uint32_t> words;
    uint32_t operator()(uint32_t address) const { return words.contains(address) ? words.at(address) : 0u; }
};

/* The rooftop level's table as a capsule's RAM held it: 4 rows, 20 columns repeating every 5. */
Ram rooftop(uint32_t at = kAt) {
    Ram ram;
    ram.words[at] = 4u;
    ram.words[at + 4u] = 20u;
    for (uint32_t row = 0; row <= 4u; ++row) ram.words[at + 8u + 4u * row] = 60u * row;
    for (uint32_t column = 0; column <= 20u; ++column) ram.words[at + 32u + 4u * column] = 64u * column;
    const uint32_t xs[4][5] = {{320, 320, 352, 352, 384}, {384, 416, 416, 448, 448}, {480, 480, 512, 512, 544}, {544, 576, 576, 576, 576}};
    const uint32_t ys[4][5] = {{360, 420, 360, 420, 360}, {420, 360, 420, 360, 420}, {360, 420, 360, 420, 360}, {420, 256, 316, 376, 436}};
    for (uint32_t row = 0; row < 4u; ++row)
        for (uint32_t column = 0; column < 20u; ++column) {
            ram.words[at + 128u + 96u * row + 4u * column] = xs[row][column % 5u];
            ram.words[at + 704u + 96u * row + 4u * column] = ys[row][column % 5u];
        }
    return ram;
}

/* The last mission's table: 3 rows of 80, its first row's blocks each once, the rows below one block of plain sky. */
Ram last_mission() {
    Ram ram;
    ram.words[kAt] = 3u;
    ram.words[kAt + 4u] = 20u;
    for (uint32_t row = 0; row <= 3u; ++row) ram.words[kAt + 8u + 4u * row] = 80u * row;
    for (uint32_t column = 0; column <= 20u; ++column) ram.words[kAt + 32u + 4u * column] = 64u * column;
    for (uint32_t column = 0; column < 20u; ++column) {
        ram.words[kAt + 128u + 4u * column] = 320u + 32u * (column / 3u);
        ram.words[kAt + 704u + 4u * column] = 256u + 80u * (column % 3u);
        for (uint32_t row = 1; row < 3u; ++row) {
            ram.words[kAt + 128u + 96u * row + 4u * column] = 512u;
            ram.words[kAt + 704u + 96u * row + 4u * column] = 416u;
        }
    }
    return ram;
}

void test_the_rooftop_table_is_read_cell_by_cell() {
    const Ram ram = rooftop();
    const Cells cells = read(ram, kAt);
    expect(cells.rows == 4 && cells.columns == 20 && cells.tall == 60, "four rows of twenty columns, each cell 60 rows");
    expect(cells.xy[0] == 320 && cells.xy[1] == 360 && cells.xy[2] == 320 && cells.xy[3] == 420, "the first row's cells, x and y");
    expect(cells.xy[2 * (3 * 20 + 1)] == 576 && cells.xy[2 * (3 * 20 + 1) + 1] == 256, "the last row's second cell");
    expect(cells.xy[2 * 5] == cells.xy[0] && cells.xy[2 * 5 + 1] == cells.xy[1], "a column repeats five columns on");
    expect(read(ram, kAt) == cells, "a second read finds the same table");
}

void test_cells_of_80_rows_are_read_and_laid_out() {
    const Cells cells = read(last_mission(), kAt);
    expect(cells.rows == 3 && cells.tall == 80 && cells.irregular, "three rows of 80, the plain block in every column taken as irregular");
    if (cells.rows != 3 || cells.tall != 80) return;
    std::vector<uint16_t> vram(1024u * 512u, 0u);
    vram[(416u + 5u) * 1024u + 512u + 3u] = 0x2A17u;
    vram[(256u + 79u) * 1024u + 320u] = 0x0901u;
    const Picture taken = picture(cells, vram.data());
    expect(taken.width == 1280 && taken.height == 240, "a picture of a whole turn and three rows of 80");
    expect(taken.indices[static_cast<size_t>(80 + 5) * 1280u + 6u] == 0x17u && taken.indices[static_cast<size_t>(80 + 5) * 1280u + 7u] == 0x2Au,
           "a second row's texels from its block, two to a half-word, low byte first");
    expect(taken.indices[static_cast<size_t>(79) * 1280u] == 0x01u && taken.indices[static_cast<size_t>(79) * 1280u + 1u] == 0x09u,
           "a first row's last line is its block's 80th");
    Ram ram = last_mission();
    ram.words[kAt + 12u] = 8u;
    for (uint32_t row = 0; row <= 3u; ++row) ram.words[kAt + 8u + 4u * row] = 8u * row;
    expect(read(ram, kAt).rows == 0, "cells of too few rows to be a sky are none");
}

void test_a_table_the_sky_pass_cannot_draw_gives_none() {
    Ram ram = rooftop();
    expect(read(ram, 0u).rows == 0 && read(ram, 0x1F800000u).rows == 0, "a descriptor outside RAM is none");
    ram.words[kAt + 4u] = 16u;
    expect(read(ram, kAt).rows == 0, "columns that do not make the 1280 texels of a turn");
    ram = rooftop();
    ram.words[kAt] = 5u;
    expect(read(ram, kAt).rows == 0, "more rows than the frame has");
    ram = rooftop();
    ram.words[kAt + 8u + 4u * 2u] = 121u;
    expect(read(ram, kAt).rows == 0, "a row that does not start on its cells' grid");
    ram = rooftop();
    ram.words[kAt + 32u + 4u * 3u] = 190u;
    expect(read(ram, kAt).rows == 0, "a column that does not start on the 64-texel grid");
    ram = rooftop();
    ram.words[kAt + 128u + 96u * 1u + 4u * 7u] = 1000u;
    expect(read(ram, kAt).rows == 0, "a cell past VRAM's right edge");
    ram = rooftop();
    ram.words[kAt + 704u + 96u * 3u + 4u * 19u] = 460u;
    expect(read(ram, kAt).rows == 0, "or past its bottom");
    expect(read(rooftop(0xFFFFF800u), 0xFFFFF800u).rows == 0, "a whole table whose end wraps past the address space");
    expect(read(rooftop(0x801FFC00u), 0x801FFC00u).rows == 0, "or runs past RAM's end");
    ram = rooftop();
    ram.words[kAt + 128u + 96u * 2u + 4u * 6u] = 0x7FFFFFFFu;
    expect(read(ram, kAt).rows == 0, "a cell x that would overflow its own check");
    ram = rooftop();
    ram.words[kAt + 704u + 96u * 0u + 4u * 3u] = 0x7FFFFFFFu;
    expect(read(ram, kAt).rows == 0, "a cell y that would");
}

void test_cells_that_share_vram_without_repeating_are_irregular() {
    Ram ram = rooftop();
    ram.words[kAt + 128u + 96u * 0u + 4u * 12u] = 320u;  /* column 10's block */
    ram.words[kAt + 704u + 96u * 0u + 4u * 12u] = 360u;
    const Cells odd = read(ram, kAt);
    expect(odd.rows == 4 && odd.irregular, "a block two columns share, with different columns after them, is taken as irregular");
    ram = rooftop();
    for (uint32_t column = 1; column < 20u; column += 5u) {
        ram.words[kAt + 128u + 96u * 1u + 4u * column] = 320u;  /* row 0's first block, in the next column */
        ram.words[kAt + 704u + 96u * 1u + 4u * column] = 360u;
    }
    const Cells rows_share = read(ram, kAt);
    expect(rows_share.rows == 4 && rows_share.irregular, "and so is a block two rows share in different columns");
    ram = rooftop();
    for (uint32_t column = 0; column < 20u; column += 5u) {
        ram.words[kAt + 128u + 96u * 1u + 4u * column] = 320u;
        ram.words[kAt + 704u + 96u * 1u + 4u * column] = 360u;
    }
    const Cells twice = read(ram, kAt);
    expect(twice.rows == 4 && twice.irregular, "a block twice in one column is irregular: the frame row tells its row");
    ram = rooftop();
    for (uint32_t column = 0; column < 20u; column += 5u) ram.words[kAt + 128u + 96u * 2u + 4u * column] = 470u;  /* into (448, 360) */
    const Cells overlapping = read(ram, kAt);
    expect(overlapping.rows == 4 && overlapping.irregular, "and so are cells that only overlap");
    expect(read(rooftop(), kAt).rows == 4 && !read(rooftop(), kAt).irregular, "the rooftop's table, repeating every five columns, is taken");
}

void test_the_city_alone_is_drawn_smaller_and_smoothed() {
    namespace look = disruptor::sky_look;
    const look::Look city = look::look(0xA6A30DE71D65100Dull);
    expect(city.size_per_mille == 838 && city.smooth, "mission 1's city is drawn at 160/191 of its size, smoothed");
    const look::Look rooftop = look::look(0xD45C4422C609AD02ull);
    expect(rooftop.size_per_mille == 1000 && !rooftop.smooth && look::look(0u).size_per_mille == 1000, "every other sky as the game lays it");
    expect(look::grown_rows(city, 128) == 140, "a full look up at 0.838 reaches 258 rows above the horizon: 138 grown, two to blend");
    expect(look::grown_rows(rooftop, 128) == 128 && look::grown_rows(look::Look{0, false}, 128) == 128,
           "the game's size needs 96 and keeps the least, a size of 0 is the game's");
    for (size_t index = 0; index < look::kPicks.size(); ++index) {
        expect(look::kPicks[index].look.size_per_mille >= 250 && look::kPicks[index].look.size_per_mille <= 4000,
               "a pick is within what the renderer takes");
        for (size_t other = index + 1; other < look::kPicks.size(); ++other)
            expect(look::kPicks[index].picture != look::kPicks[other].picture, "a picture is picked once");
    }
}

}  // namespace

int main() {
    test_the_rooftop_table_is_read_cell_by_cell();
    test_cells_of_80_rows_are_read_and_laid_out();
    test_a_table_the_sky_pass_cannot_draw_gives_none();
    test_cells_that_share_vram_without_repeating_are_irregular();
    test_the_city_alone_is_drawn_smaller_and_smoothed();
    if (g_failures) return 1;
    std::cout << "disruptor_sky_panorama: all tests passed\n";
    return 0;
}
