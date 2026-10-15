#include "menu_art.h"

#include "raw_image.h"

#include <array>
#include <set>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr std::array<std::uint32_t, 3> pieces = {306, 307, 308};
constexpr int scene_width = 320, scene_height = 240, scenes_a_piece = 4;
constexpr std::uint32_t pictures_at = 0x11000;
constexpr std::uint32_t picture_bytes = scene_width * scene_height;
constexpr std::uint32_t palette_at = 0x621B0;
constexpr std::size_t fewest_colours = 64; // a real palette has 256 distinct words; zeros or a fill have one or two

std::uint32_t rgba(std::uint16_t word) {
    const auto eight = [](std::uint32_t five) { return (five << 3) | (five >> 2); };
    return eight(word & 31u) | (eight((word >> 5) & 31u) << 8) | (eight((word >> 10) & 31u) << 16) | 0xFF000000u;
}

} // namespace

std::vector<Picture> menu_scenes(DiscImage& disc) {
    std::vector<Picture> scenes;
    for (const std::uint32_t piece : pieces) {
        std::vector<std::uint8_t> pictures, words;
        if (!wad_piece_bytes(disc, piece, pictures_at, picture_bytes * scenes_a_piece, pictures) ||
            !wad_piece_bytes(disc, piece, palette_at, 512, words))
            return {};
        std::array<std::uint32_t, 256> palette{};
        std::set<std::uint16_t> distinct;
        for (std::size_t index = 0; index < palette.size(); ++index) {
            const auto word = static_cast<std::uint16_t>(words[2 * index] | (words[2 * index + 1] << 8));
            distinct.insert(word);
            palette[index] = rgba(word);
        }
        if (distinct.size() < fewest_colours) return {};
        for (int scene = 0; scene < scenes_a_piece; ++scene) {
            Picture picture{scene_width, scene_height, std::vector<std::uint32_t>(picture_bytes)};
            for (std::uint32_t pixel = 0; pixel < picture_bytes; ++pixel)
                picture.pixels[pixel] = palette[pictures[scene * picture_bytes + pixel]];
            scenes.push_back(std::move(picture));
        }
    }
    return scenes;
}

std::vector<Picture> menu_scenes(const fs::path& data) {
    RawImage image(data);
    return image.sectors() ? menu_scenes(image) : std::vector<Picture>{};
}

} // namespace disruptor::launcher
