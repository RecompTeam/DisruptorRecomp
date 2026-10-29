#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

namespace disruptor::settings {

inline constexpr std::size_t key_count = 24;
// keybinds.ini fields in the runtime's order (PsxKeybindButton).
inline constexpr std::array<const char*, key_count> key_fields = {
    "up", "down", "left", "right", "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2",
    "l3", "r3", "start", "select", "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right"};
inline constexpr std::array<const char*, key_count> modern_keys = {
    "W", "S", "Left", "Right", "Return", "Space", "F", "E", "Q", "R", "A", "D",
    "None", "None", "P", "Tab", "None", "None", "None", "None", "None", "None", "None", "None"};
inline constexpr std::array<const char*, key_count> classic_keys = {
    "Up", "Down", "Left", "Right", "X", "S", "Z", "A", "Q", "W", "E", "R",
    "T", "Y", "Return", "Right Shift", "Up", "Down", "Left", "Right", "None", "None", "None", "None"};

using KeyLayout = std::array<std::string, key_count>;

inline KeyLayout layout_of(const std::array<const char*, key_count>& names) {
    KeyLayout layout;
    std::copy(names.begin(), names.end(), layout.begin());
    return layout;
}

inline std::string folded_key(std::string_view name) {
    std::string folded;
    for (const char letter : name) folded += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
    constexpr std::pair<std::string_view, std::string_view> aliases[] = {
        {"", "none"}, {"enter", "return"}, {"esc", "escape"}, {"lshift", "left shift"}, {"rshift", "right shift"},
        {"lctrl", "left ctrl"}, {"rctrl", "right ctrl"}, {"lalt", "left alt"}, {"ralt", "right alt"}};
    for (const auto& [shorter, full] : aliases)
        if (folded == shorter) return std::string(full);
    return folded;
}

// Keys the game keeps for itself: the menu, save states, capsules and fast-forward.
inline bool reserved_key(std::string_view name) {
    const std::string folded = folded_key(name);
    if (folded == "escape" || folded == "`" || folded == "b") return true;
    for (int number = 1; number <= 12; ++number)
        if (folded == "f" + std::to_string(number)) return true;
    return false;
}

inline bool same_layout(const KeyLayout& layout, const std::array<const char*, key_count>& names) {
    for (std::size_t index = 0; index < key_count; ++index)
        if (folded_key(layout[index]) != folded_key(names[index])) return false;
    return true;
}

// A layout the player made stays.
inline KeyLayout layout_for_scheme(const KeyLayout& current, bool modern) {
    if (modern && same_layout(current, classic_keys)) return layout_of(modern_keys);
    if (!modern && same_layout(current, modern_keys)) return layout_of(classic_keys);
    return current;
}

} // namespace disruptor::settings
