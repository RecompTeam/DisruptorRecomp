/*
 * The level's sky panorama as func_8003AF68 lays it out (Disruptor, SLUS-00224): a table of cells, each a block of an 8-bit
 * texture page 64 texels wide and 60 or 80 rows tall, 20 columns round the 1280 texels of a full turn and up to 4 rows. The
 * renderer draws the sky from it pixel by pixel, see gr_set_sky_cells.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace disruptor::sky_panorama {

inline constexpr uint32_t kDescriptorPointer = 0x80071704u; /* $gp + 0x5B8 */
inline constexpr int32_t kMaxRows = 4;
inline constexpr int32_t kMaxColumns = 24;
inline constexpr int32_t kCellWide = 64;
inline constexpr int32_t kLeastTall = 16, kMostTall = 128;  /* a cell's rows: 60 most levels, 80 the last mission's */
inline constexpr int32_t kPeriod = 1280;  /* the game wraps its scroll at this, five texels a heading step */
inline constexpr int32_t kFocal = 160;    /* the GTE's H: the world's projection */

struct Cells {
    int32_t rows = 0, columns = 0, tall = 0;
    std::array<int32_t, 2 * kMaxRows * kMaxColumns> xy{};  /* each cell's VRAM x in half-words and y, row by row */
    bool irregular = false;  /* blocks shared without repeating the table: a cell is known by its column, not its block */
    bool operator==(const Cells &) const = default;
};

inline bool in_ram(uint32_t address) { return address >= 0x80000000u && address <= 0x80200000u - 0x800u; }

enum class Sharing { kNone, kRepeating, kIrregular };

/* How cells share VRAM blocks. Repeating: in one row, the table repeating by their distance, so either one has the same next
 * columns. Irregular: otherwise, cells that overlap or a block twice in a column included: the renderer knows such a cell by
 * the column a tile tells and the frame row it stands at. */
inline Sharing sharing(const Cells &cells) {
    const int32_t count = cells.rows * cells.columns;
    const auto at = [&](int32_t row, int32_t column) {
        const auto index = static_cast<size_t>(2 * (row * cells.columns + (column % cells.columns + cells.columns) % cells.columns));
        return std::array<int32_t, 2>{cells.xy[index], cells.xy[index + 1]};
    };
    Sharing found = Sharing::kNone;
    for (int32_t first = 0; first < count; ++first) {
        for (int32_t second = first + 1; second < count; ++second) {
            const auto one = at(first / cells.columns, first % cells.columns);
            const auto other = at(second / cells.columns, second % cells.columns);
            if (one != other) {
                const bool across = one[0] < other[0] + kCellWide / 2 && other[0] < one[0] + kCellWide / 2;
                if (across && one[1] < other[1] + cells.tall && other[1] < one[1] + cells.tall) return Sharing::kIrregular;
                continue;
            }
            if (first % cells.columns == second % cells.columns) return Sharing::kIrregular;
            bool repeats = first / cells.columns == second / cells.columns;
            const int32_t step = second % cells.columns - first % cells.columns;
            for (int32_t row = 0; row < cells.rows && repeats; ++row)
                for (int32_t column = 0; column < cells.columns && repeats; ++column)
                    repeats = at(row, column) == at(row, column + step);
            found = repeats ? std::max(found, Sharing::kRepeating) : Sharing::kIrregular;
        }
    }
    return found;
}

/* The table at `descriptor`, or no rows when it is not the grid of whole cells the renderer draws from. */
template <typename Word>
Cells read(Word word, uint32_t descriptor) {
    const Cells none;
    if (!in_ram(descriptor)) return none;
    const auto rows = static_cast<int32_t>(word(descriptor)), columns = static_cast<int32_t>(word(descriptor + 4u));
    if (rows < 1 || rows > kMaxRows || columns < 1 || columns > kMaxColumns || columns * kCellWide != kPeriod) return none;
    const auto tall = static_cast<int32_t>(word(descriptor + 12u));
    if (tall < kLeastTall || tall > kMostTall) return none;
    for (int32_t row = 0; row <= rows; ++row)
        if (static_cast<int32_t>(word(descriptor + 8u + 4u * static_cast<uint32_t>(row))) != row * tall) return none;
    for (int32_t column = 0; column <= columns; ++column)
        if (static_cast<int32_t>(word(descriptor + 32u + 4u * static_cast<uint32_t>(column))) != column * kCellWide) return none;
    Cells cells;
    for (int32_t row = 0; row < rows; ++row) {
        for (int32_t column = 0; column < columns; ++column) {
            const uint32_t at = 96u * static_cast<uint32_t>(row) + 4u * static_cast<uint32_t>(column);
            const auto x = static_cast<int32_t>(word(descriptor + 128u + at)), y = static_cast<int32_t>(word(descriptor + 704u + at));
            if (x < 0 || x > 1024 - kCellWide / 2 || y < 0 || y > 512 - tall) return none;
            cells.xy[static_cast<size_t>(2 * (row * columns + column))] = x;
            cells.xy[static_cast<size_t>(2 * (row * columns + column) + 1)] = y;
        }
    }
    cells.rows = rows;
    cells.columns = columns;
    cells.tall = tall;
    cells.irregular = sharing(cells) == Sharing::kIrregular;
    return cells;
}

/* The panorama's texel indices, row by row, 64 a cell across and its rows down, read from VRAM through the table, and their hash:
 * what names a panorama, its extension's file included. */
struct Picture {
    int32_t width = 0, height = 0;
    std::vector<uint8_t> indices;
    uint64_t hash = 0;
};

inline uint64_t indices_hash(const std::vector<uint8_t> &indices) {
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t index : indices) hash = (hash ^ index) * 1099511628211ull;
    return hash;
}

inline Picture picture(const Cells &cells, const uint16_t *vram) {
    Picture taken;
    taken.width = cells.columns * kCellWide;
    taken.height = cells.rows * cells.tall;
    taken.indices.assign(static_cast<size_t>(taken.width) * static_cast<size_t>(taken.height), 0u);
    for (int32_t row = 0; row < cells.rows; ++row)
        for (int32_t column = 0; column < cells.columns; ++column) {
            const size_t cell = static_cast<size_t>(2 * (row * cells.columns + column));
            const auto x = static_cast<size_t>(cells.xy[cell]), y = static_cast<size_t>(cells.xy[cell + 1]);
            for (int32_t line = 0; line < cells.tall; ++line)
                for (int32_t texel = 0; texel < kCellWide; ++texel) {
                    const uint16_t pair = vram[(y + static_cast<size_t>(line)) * 1024u + x + static_cast<size_t>(texel / 2)];
                    taken.indices[static_cast<size_t>(row * cells.tall + line) * static_cast<size_t>(taken.width) +
                                  static_cast<size_t>(column * kCellWide + texel)] = static_cast<uint8_t>(pair >> (8 * (texel & 1)));
                }
        }
    taken.hash = indices_hash(taken.indices);
    return taken;
}

}  // namespace disruptor::sky_panorama
