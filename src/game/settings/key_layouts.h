#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>

// The two control schemes' keyboard layouts, by keybinds.ini's key names, for the launcher and the game.
namespace disruptor::settings {

inline constexpr std::size_t key_count = 24;
// keybinds.ini's fields of a player, in the runtime's order (PsxKeybindButton).
inline constexpr std::array<const char*, key_count> key_fields = {
    "up", "down", "left", "right", "cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2",
    "l3", "r3", "start", "select", "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right"};
inline constexpr std::array<const char*, key_count> modern_keys = {
    "W", "S", "Left", "Right", "Return", "Space", "F", "E", "Q", "R", "A", "D",
    "None", "None", "P", "Tab", "None", "None", "None", "None", "None", "None", "None", "None"};
inline constexpr std::array<const char*, key_count> classic_keys = {
    "Up", "Down", "Left", "Right", "X", "S", "Z", "A", "Q", "W", "E", "R",
    "T", "Y", "Return", "Right Shift", "Up", "Down", "Left", "Right", "None", "None", "None", "None"};

// The modern scheme's mouse buttons by the fields they press, as mouse_aim.cpp maps them.
struct MouseButton {
    const char* field;
    const char* name;
};
inline constexpr std::array<MouseButton, 4> modern_mouse = {{
    {"cross", "Left click"}, {"square", "Right click"}, {"l1", "Mouse 4"}, {"r1", "Mouse 5"}}};

using KeyLayout = std::array<std::string, key_count>;

inline KeyLayout layout_of(const std::array<const char*, key_count>& names) {
    KeyLayout layout;
    std::copy(names.begin(), names.end(), layout.begin());
    return layout;
}

// A key's name as SDL reads it: case aside, with the runtime's own short names, and nothing as "none".
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

inline bool same_layout(const KeyLayout& layout, const std::array<const char*, key_count>& names) {
    for (std::size_t index = 0; index < key_count; ++index)
        if (folded_key(layout[index]) != folded_key(names[index])) return false;
    return true;
}

// A scheme's keys: the other scheme's own layout gives way to this one's, a layout the player made stays.
inline KeyLayout layout_for_scheme(const KeyLayout& current, bool modern) {
    if (modern && same_layout(current, classic_keys)) return layout_of(modern_keys);
    if (!modern && same_layout(current, modern_keys)) return layout_of(classic_keys);
    return current;
}

} // namespace disruptor::settings
