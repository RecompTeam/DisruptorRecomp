#include "game/render/sky_panorama.h"

#include <cstdint>
#include <iostream>
#include <unordered_map>

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

void test_the_rooftop_table_is_read_cell_by_cell() {
    const Ram ram = rooftop();
    const Cells cells = read(ram, kAt);
    expect(cells.rows == 4 && cells.columns == 20, "four rows of twenty columns");
    expect(cells.xy[0] == 320 && cells.xy[1] == 360 && cells.xy[2] == 320 && cells.xy[3] == 420, "the first row's cells, x and y");
    expect(cells.xy[2 * (3 * 20 + 1)] == 576 && cells.xy[2 * (3 * 20 + 1) + 1] == 256, "the last row's second cell");
    expect(cells.xy[2 * 5] == cells.xy[0] && cells.xy[2 * 5 + 1] == cells.xy[1], "a column repeats five columns on");
    expect(read(ram, kAt) == cells, "a second read finds the same table");
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
    expect(read(ram, kAt).rows == 0, "a row that does not start on the 60-row grid");
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

void test_cells_that_share_vram_must_repeat_the_table() {
    Ram ram = rooftop();
    ram.words[kAt + 128u + 96u * 0u + 4u * 12u] = 320u;  /* column 10's block */
    ram.words[kAt + 704u + 96u * 0u + 4u * 12u] = 360u;
    expect(read(ram, kAt).rows == 0, "a block two columns share, with different columns after them, is refused");
    ram = rooftop();
    for (uint32_t column = 0; column < 20u; column += 5u) {
        ram.words[kAt + 128u + 96u * 1u + 4u * column] = 320u;  /* row 0's first block */
        ram.words[kAt + 704u + 96u * 1u + 4u * column] = 360u;
    }
    expect(read(ram, kAt).rows == 0, "a block two rows share is refused");
    ram = rooftop();
    for (uint32_t column = 0; column < 20u; column += 5u) ram.words[kAt + 128u + 96u * 2u + 4u * column] = 470u;  /* into (448, 360) */
    expect(read(ram, kAt).rows == 0, "cells that only overlap are refused");
    expect(read(rooftop(), kAt).rows == 4, "the rooftop's table, repeating every five columns, is taken");
}

}  // namespace

int main() {
    test_the_rooftop_table_is_read_cell_by_cell();
    test_a_table_the_sky_pass_cannot_draw_gives_none();
    test_cells_that_share_vram_must_repeat_the_table();
    if (g_failures) return 1;
    std::cout << "disruptor_sky_panorama: all tests passed\n";
    return 0;
}
