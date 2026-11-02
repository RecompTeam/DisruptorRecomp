/*
 * The level's sky panorama as func_8003AF68 lays it out (Disruptor, SLUS-00224): a table of cells, each a 64x60 block of an
 * 8-bit texture page, 20 columns round the 1280 texels of a full turn and up to 4 rows. The renderer draws the sky from it
 * pixel by pixel, see gr_set_sky_cells.
 */
#pragma once

#include <array>
#include <cstdint>

namespace disruptor::sky_panorama {

inline constexpr uint32_t kDescriptorPointer = 0x80071704u; /* $gp + 0x5B8 */
inline constexpr int32_t kMaxRows = 4;
inline constexpr int32_t kMaxColumns = 24;
inline constexpr int32_t kCellWide = 64;
inline constexpr int32_t kCellTall = 60;
inline constexpr int32_t kPeriod = 1280;  /* the game wraps its scroll at this, five texels a heading step */
inline constexpr int32_t kFocal = 160;    /* the GTE's H: the world's projection */

struct Cells {
    int32_t rows = 0, columns = 0;
    std::array<int32_t, 2 * kMaxRows * kMaxColumns> xy{};  /* each cell's VRAM x in half-words and y, row by row */
    bool operator==(const Cells &) const = default;
};

inline bool in_ram(uint32_t address) { return address >= 0x80000000u && address <= 0x80200000u - 0x800u; }

/* Cells sharing a block lie in one row and the table repeats by their distance, so either one has the same next columns. */
inline bool shared_cells_repeat(const Cells &cells) {
    const int32_t count = cells.rows * cells.columns;
    const auto at = [&](int32_t row, int32_t column) {
        const auto index = static_cast<size_t>(2 * (row * cells.columns + (column % cells.columns + cells.columns) % cells.columns));
        return std::array<int32_t, 2>{cells.xy[index], cells.xy[index + 1]};
    };
    for (int32_t first = 0; first < count; ++first) {
        for (int32_t second = first + 1; second < count; ++second) {
            const auto one = at(first / cells.columns, first % cells.columns);
            const auto other = at(second / cells.columns, second % cells.columns);
            if (one != other) {
                const bool across = one[0] < other[0] + kCellWide / 2 && other[0] < one[0] + kCellWide / 2;
                if (across && one[1] < other[1] + kCellTall && other[1] < one[1] + kCellTall) return false;
                continue;
            }
            if (first / cells.columns != second / cells.columns) return false;
            const int32_t step = second % cells.columns - first % cells.columns;
            for (int32_t row = 0; row < cells.rows; ++row)
                for (int32_t column = 0; column < cells.columns; ++column)
                    if (at(row, column) != at(row, column + step)) return false;
        }
    }
    return true;
}

/* The table at `descriptor`, or no rows when it is not the regular grid of whole cells the renderer draws from. */
template <typename Word>
Cells read(Word word, uint32_t descriptor) {
    const Cells none;
    if (!in_ram(descriptor)) return none;
    const auto rows = static_cast<int32_t>(word(descriptor)), columns = static_cast<int32_t>(word(descriptor + 4u));
    if (rows < 1 || rows > kMaxRows || columns < 1 || columns > kMaxColumns || columns * kCellWide != kPeriod) return none;
    for (int32_t row = 0; row <= rows; ++row)
        if (static_cast<int32_t>(word(descriptor + 8u + 4u * static_cast<uint32_t>(row))) != row * kCellTall) return none;
    for (int32_t column = 0; column <= columns; ++column)
        if (static_cast<int32_t>(word(descriptor + 32u + 4u * static_cast<uint32_t>(column))) != column * kCellWide) return none;
    Cells cells;
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t column = 0; column < columns; ++column) {
            const uint32_t at = 96u * static_cast<uint32_t>(row) + 4u * static_cast<uint32_t>(column);
            const auto x = static_cast<int32_t>(word(descriptor + 128u + at)), y = static_cast<int32_t>(word(descriptor + 704u + at));
            if (x < 0 || x > 1024 - kCellWide / 2 || y < 0 || y > 512 - kCellTall) return none;
            cells.xy[static_cast<size_t>(2 * (row * columns + column))] = x;
            cells.xy[static_cast<size_t>(2 * (row * columns + column) + 1)] = y;
        }
    }
    cells.rows = rows;
    cells.columns = columns;
    return shared_cells_repeat(cells) ? cells : none;
}

}  // namespace disruptor::sky_panorama
