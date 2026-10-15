#pragma once

#include <cstddef>
#include <string_view>

namespace disruptor::settings {

// The length of a hint line's leading "Name:" that names a choice, as in "Modern: WASD", or 0.
inline std::size_t choice_name(std::string_view line) {
    const std::size_t colon = line.find(": ");
    if (colon == std::string_view::npos || colon > 24 || line.substr(0, colon).find_first_of(".,") != std::string_view::npos)
        return 0;
    return colon + 1;
}

} // namespace disruptor::settings
