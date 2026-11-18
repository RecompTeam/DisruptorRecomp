#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace disruptor::launcher {

struct RegionDisc {
    std::string language;
    bool words = false; // menus and texts too, and not only movies, speech and hints
    std::filesystem::path image{};
};

// Which known disc of another region the image is, by its boot file alone.
std::optional<RegionDisc> region_disc(const std::filesystem::path& selected);

// Lays the image over the US disc as the game does. Throws with the game's reason when it does not fit.
RegionDisc check_region_disc(const std::filesystem::path& home_data, const std::filesystem::path& selected);

std::wstring describe(const RegionDisc& disc);

} // namespace disruptor::launcher
