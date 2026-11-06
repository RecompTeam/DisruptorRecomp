/* Developer tool: grows the sky above a dumped panorama (PSX_SKY_DUMP) with the game's own code and writes the extension
 * stacked on the panorama, through its base palette, as a PPM. disruptor-sky-grow <in.dump> <out.ppm> [rows]
 */
#include "game/render/sky_extension.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {

uint32_t word(const std::vector<uint8_t> &bytes, size_t at) {
    return static_cast<uint32_t>(bytes[at]) | static_cast<uint32_t>(bytes[at + 1]) << 8 | static_cast<uint32_t>(bytes[at + 2]) << 16 |
           static_cast<uint32_t>(bytes[at + 3]) << 24;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "disruptor-sky-grow <in.dump> <out.ppm> [rows]\n";
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 28 || std::memcmp(bytes.data(), "DSKYDMP1", 8) != 0) {
        std::cerr << "not a sky dump\n";
        return 2;
    }
    const uint32_t width = word(bytes, 8), height = word(bytes, 12), rows_of_cells = word(bytes, 16), columns = word(bytes, 20);
    if (width == 0 || width > 4096 || height == 0 || height > 512 || rows_of_cells > 4 || columns > 24) {
        std::cerr << "a sky dump of impossible size\n";
        return 2;
    }
    size_t at = 28 + 8 * static_cast<size_t>(rows_of_cells) * columns;
    const size_t count = static_cast<size_t>(width) * height;
    if (bytes.size() < at + 512 + count) {
        std::cerr << "a short sky dump\n";
        return 2;
    }
    disruptor::sky_panorama::Picture picture;
    picture.width = static_cast<int32_t>(width);
    picture.height = static_cast<int32_t>(height);
    disruptor::sky_extension::Palette palette{};
    for (auto &colour : palette) {
        colour = static_cast<uint16_t>(bytes[at] | bytes[at + 1] << 8);
        at += 2;
    }
    picture.indices.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + count));
    picture.hash = disruptor::sky_panorama::indices_hash(picture.indices);
    const int32_t rows = argc > 3 ? std::atoi(argv[3]) : disruptor::sky_extension::kRows;
    const auto started = std::chrono::steady_clock::now();
    const auto made = disruptor::sky_extension::grow(picture, palette, rows, disruptor::sky_extension::revision(picture, palette));
    const auto took = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::ofstream out(argv[2], std::ios::binary);
    out << "P6\n" << picture.width << ' ' << made.rows + picture.height << "\n255\n";
    const auto put = [&](uint8_t index) {
        for (const int shift : {0, 5, 10}) {
            const int five = (palette[index] >> shift) & 31;
            out.put(static_cast<char>((five << 3) | (five >> 2)));
        }
    };
    for (const uint8_t index : made.indices) put(index);
    for (size_t at = static_cast<size_t>(made.band) * static_cast<size_t>(picture.width); at < picture.indices.size(); ++at) put(picture.indices[at]);
    for (int32_t row = 0; row < made.band; ++row)
        for (int32_t column = 0; column < picture.width; ++column) put(0);
    std::cout << argv[1] << ": " << made.rows << " rows grown, " << made.band << " of them over the top band, from rows " << made.from_row
              << " to " << made.to_row << (made.streaks ? " as streaks" : " in patches") << ", in " << took << " ms\n";
    return 0;
}
