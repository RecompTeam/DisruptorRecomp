#pragma once

#include "game/settings/pick_keys.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace disruptor::settings {

// The mouse buttons a second key may be, with SDL's numbers. The middle button takes and lets go of the mouse.
inline constexpr std::array<std::pair<const char*, int>, 4> mouse_buttons = {
    {{"Left click", 1}, {"Right click", 3}, {"Mouse 4", 4}, {"Mouse 5", 5}}};

inline std::optional<int> mouse_button(std::string_view name) {
    const std::string folded = folded_key(name);
    for (const auto& [shown, number] : mouse_buttons)
        if (folded_key(shown) == folded) return number;
    return std::nullopt;
}

inline std::optional<std::size_t> key_field(std::string_view field) {
    for (std::size_t index = 0; index < key_count; ++index)
        if (field == key_fields[index]) return index;
    return std::nullopt;
}

// Each field's second key, settings.toml's key_second: a key, a mouse button or "None". The modern scheme has the mouse.
inline KeyLayout default_second_keys(bool modern) {
    KeyLayout second;
    second.fill("None");
    if (!modern) return second;
    for (const auto& [field, name] : {std::pair{"cross", "Left click"}, std::pair{"square", "Right click"},
                                      std::pair{"l1", "Mouse 4"}, std::pair{"r1", "Mouse 5"}})
        second[*key_field(field)] = name;
    return second;
}

// The PlayStation pad word's bit of each field, none for the sticks'.
inline constexpr std::array<std::uint16_t, key_count> pad_bits = {
    1u << 4,  1u << 6,  1u << 7,  1u << 5,  1u << 14, 1u << 13, 1u << 15, 1u << 12, 1u << 10, 1u << 11, 1u << 8, 1u << 9,
    1u << 1,  1u << 2,  1u << 3,  1u << 0,  0,        0,        0,        0,        0,        0,        0,       0};

/* Without a list the scheme's own. A list the player made is kept whatever the scheme: an entry missing from it is no
 * key, as is one neither a mouse button nor a key `known` takes, and one on a stick's field, which no button presses. */
template <typename Known>
KeyLayout second_keys_from(const std::vector<std::string>& list, bool written, bool modern, Known known) {
    if (!written) return default_second_keys(modern);
    KeyLayout second;
    second.fill("None");
    for (std::size_t index = 0; index < key_count && index < list.size(); ++index)
        if (pad_bits[index] != 0 && !no_key(list[index]) &&
            (mouse_button(list[index]) || known(std::string_view(list[index]))))
            second[index] = list[index];
    return second;
}

// Second keys as the game reads them: a scancode above 0, a mouse button's number below it, 0 for none.
using SecondCodes = std::array<int, key_count>;

template <typename Scancode>
SecondCodes second_codes(const KeyLayout& second, Scancode scancode) {
    SecondCodes codes{};
    for (std::size_t index = 0; index < key_count; ++index) {
        if (const auto button = mouse_button(second[index])) codes[index] = -*button;
        else if (!no_key(second[index])) codes[index] = scancode(std::string_view(second[index]));
    }
    return codes;
}

inline bool second_held(int code, const std::uint8_t* keys, std::size_t key_total, std::uint32_t mouse) {
    if (code > 0) return keys && static_cast<std::size_t>(code) < key_total && keys[code] != 0;
    return code < 0 && code >= -32 && (mouse & (1u << (-code - 1))) != 0;
}

// The pad bits the held second keys press. `mouse` is SDL's button mask, 0 while the game does not hold the mouse.
inline std::uint16_t second_press(const SecondCodes& codes, const std::uint8_t* keys, std::size_t key_total,
                                  std::uint32_t mouse) {
    std::uint16_t press = 0;
    for (std::size_t index = 0; index < key_count; ++index)
        if (second_held(codes[index], keys, key_total, mouse)) press |= pad_bits[index];
    return press;
}

inline bool any_mouse(const SecondCodes& codes) {
    for (const int code : codes)
        if (code < 0) return true;
    return false;
}

} // namespace disruptor::settings
