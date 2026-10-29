#pragma once

#include <charconv>
#include <iterator>
#include <string_view>

namespace disruptor::resolution {

/* The display resolutions offered, the desktop's first: saved as written here, "" for the desktop. */
inline constexpr const char* kChoices[] = {"Desktop",   "1280x720",  "1280x800",  "1366x768",  "1600x900",
                                           "1680x1050", "1920x1080", "1920x1200", "2560x1080", "2560x1440",
                                           "2560x1600", "3440x1440", "3840x2160"};

struct Size {
    int width = 0, height = 0;
};

/* "1920x1080" as its two numbers; anything else, the empty desktop choice among them, is 0 by 0. */
inline Size parse(std::string_view text) {
    const size_t cross = text.find('x');
    if (cross == std::string_view::npos) return {};
    Size size;
    const auto [width_end, width_error] = std::from_chars(text.data(), text.data() + cross, size.width);
    const auto [height_end, height_error] = std::from_chars(text.data() + cross + 1, text.data() + text.size(), size.height);
    if (width_error != std::errc{} || height_error != std::errc{} || width_end != text.data() + cross ||
        height_end != text.data() + text.size() || size.width <= 0 || size.height <= 0)
        return {};
    return size;
}

/* The list's index of a saved resolution, the desktop's for one the list lacks. */
inline int index_of(std::string_view text) {
    for (int i = 1; i < static_cast<int>(std::size(kChoices)); ++i)
        if (text == kChoices[i]) return i;
    return 0;
}

inline std::string_view at(int index) {
    return index > 0 && index < static_cast<int>(std::size(kChoices)) ? kChoices[index] : std::string_view{};
}

} // namespace disruptor::resolution
