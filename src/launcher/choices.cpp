#include "choices.h"

#include <fstream>
#include <sstream>
#include <string>

namespace disruptor::launcher {

namespace {

constexpr const char* check_key = "check_for_updates = ";
constexpr const char* window_key = "window = ";
constexpr int smallest_side = 200;
constexpr int largest_side = 16384;
constexpr int farthest = 1000000; // further than any desktop reaches, near enough that sums stay in an int

std::optional<WindowPlace> place_of(const std::string& text) {
    std::istringstream input(text);
    WindowPlace place;
    int maximized = 0;
    if (!(input >> place.x >> place.y >> place.width >> place.height >> maximized)) return std::nullopt;
    if (place.width < smallest_side || place.height < smallest_side || place.width > largest_side ||
        place.height > largest_side || place.x < -farthest || place.x > farthest || place.y < -farthest ||
        place.y > farthest || (maximized != 0 && maximized != 1))
        return std::nullopt;
    place.maximized = maximized == 1;
    return place;
}

} // namespace

LauncherChoices read_choices(const std::filesystem::path& file) {
    LauncherChoices choices;
    std::ifstream input(file, std::ios::binary);
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with(check_key)) choices.check_for_updates = line.substr(std::string(check_key).size()) != "false";
        if (line.starts_with(window_key)) choices.window = place_of(line.substr(std::string(window_key).size()));
    }
    return choices;
}

bool write_choices(const std::filesystem::path& file, const LauncherChoices& choices) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    output << check_key << (choices.check_for_updates ? "true" : "false") << "\n";
    if (const auto& place = choices.window)
        output << window_key << place->x << " " << place->y << " " << place->width << " " << place->height << " "
               << (place->maximized ? 1 : 0) << "\n";
    output.close();
    return !output.fail();
}

} // namespace disruptor::launcher
