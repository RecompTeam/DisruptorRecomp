#pragma once

#include <cstddef>
#include <string_view>

namespace disruptor::settings {

// The length of a hint line's leading "Name:" that names a choice, as in "Modern: WASD" or "1.25x: further", or 0.
inline std::size_t choice_name(std::string_view line) {
    const std::size_t colon = line.find(": ");
    if (colon == std::string_view::npos || colon > 24) return 0;
    const auto digit = [&](std::size_t at) { return at < colon && line[at] >= '0' && line[at] <= '9'; };
    for (std::size_t at = 0; at < colon; ++at) {
        if (line[at] == ',') return 0;
        if (line[at] == '.' && !(at > 0 && digit(at - 1) && digit(at + 1))) return 0;
    }
    return colon + 1;
}

} // namespace disruptor::settings
