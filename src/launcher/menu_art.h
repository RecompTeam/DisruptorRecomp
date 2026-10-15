#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace disruptor {
class DiscImage;
}

namespace disruptor::launcher {

// RGBA, eight bits a channel, red in the lowest byte.
struct Picture {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

// The twelve backdrops of the game's main menu, 320 by 240 with its logo, read from the US disc where the US disc
// keeps them: three WAD.IN pieces of four pictures and one palette each. Empty when the disc does not hold them there.
std::vector<Picture> menu_scenes(DiscImage& disc);
// The same from a raw image of 2352-byte sectors, the verified disc's data file.
std::vector<Picture> menu_scenes(const std::filesystem::path& data);

} // namespace disruptor::launcher
