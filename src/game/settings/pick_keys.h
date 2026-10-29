#pragma once

#include "game/settings/key_layouts.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::settings {

// Keys that put a weapon or a psionic power in hand: a row a weapon in the game's order, then a row a power.
inline constexpr std::size_t weapon_count = 10, psionic_count = 5, pick_count = weapon_count + psionic_count;
// As the game's lists name them. The baton has no name there, Blast shows as TeraBlast once the game raises it.
inline constexpr std::array<const char*, pick_count> pick_names = {
    "Baton", "18mm Semi", "18mm Auto", "Phase Rifle", "Phase Repeater", "Lockon Cannon", "AM Blaster", "AM Cyclone",
    "Zodiac", "Plasma Lance", "Shock", "Drain", "Heal", "Shield", "Blast"};
inline constexpr std::array<const char*, pick_count> first_pick_keys = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "None", "None", "None", "None", "None"};
inline constexpr std::array<const char*, pick_count> second_pick_keys = {
    "Keypad 1", "Keypad 2", "Keypad 3", "Keypad 4", "Keypad 5", "Keypad 6", "Keypad 7", "Keypad 8", "Keypad 9",
    "Keypad 0", "None", "None", "None", "None", "None"};

// Two keys a row, [row][0] its first and [row][1] its second, each a key name or "None".
using PickKeys = std::array<std::array<std::string, 2>, pick_count>;

inline PickKeys default_pick_keys() {
    PickKeys keys;
    for (std::size_t row = 0; row < pick_count; ++row) keys[row] = {first_pick_keys[row], second_pick_keys[row]};
    return keys;
}

inline bool psionic_row(std::size_t row) { return row >= weapon_count && row < pick_count; }

inline bool no_key(std::string_view name) { return folded_key(name) == "none"; }

// Shift, Ctrl, Alt and the system key never pick: the game's shortcuts and the system's need them.
inline bool modifier_key(std::string_view name) {
    const std::string folded = folded_key(name);
    for (const std::string_view side : {"left ", "right "})
        for (const std::string_view key : {"shift", "ctrl", "alt", "gui"})
            if (folded == std::string(side) + std::string(key)) return true;
    return false;
}

// A key no row may have.
inline bool refused_pick_key(std::string_view name) { return reserved_key(name) || modifier_key(name); }

struct PickSlot {
    std::size_t row;
    std::size_t which;
};

inline std::optional<PickSlot> pick_with(const PickKeys& keys, std::string_view name) {
    if (no_key(name)) return std::nullopt;
    const std::string folded = folded_key(name);
    for (std::size_t row = 0; row < pick_count; ++row)
        for (std::size_t which = 0; which < 2; ++which)
            if (folded_key(keys[row][which]) == folded) return PickSlot{row, which};
    return std::nullopt;
}

// The field of `layout` with the key: one the Controls page shows first, then any of the 24.
inline std::optional<std::size_t> field_with(const KeyLayout& layout, std::string_view name,
                                             const std::vector<std::size_t>& shown_first = {}) {
    if (no_key(name)) return std::nullopt;
    const std::string folded = folded_key(name);
    for (const std::size_t field : shown_first)
        if (field < key_count && folded_key(layout[field]) == folded) return field;
    for (std::size_t field = 0; field < key_count; ++field)
        if (folded_key(layout[field]) == folded) return field;
    return std::nullopt;
}

// A file's four lists, as settings.toml names them.
struct PickLists {
    const std::vector<std::string>& weapons;
    const std::vector<std::string>& weapons_second;
    const std::vector<std::string>& psionics;
    const std::vector<std::string>& psionics_second;
};

/* A missing entry or an unknown key name keeps its default, "None" is no key. A key no row may have, or one an earlier
 * entry has (every first key before the second ones), is no key, so every key picks one row at most. */
template <typename Known>
PickKeys pick_keys_from(const PickLists& lists, Known known) {
    PickKeys keys = default_pick_keys();
    const std::vector<std::string>* sources[2][2] = {{&lists.weapons, &lists.psionics},
                                                     {&lists.weapons_second, &lists.psionics_second}};
    for (std::size_t which = 0; which < 2; ++which)
        for (std::size_t row = 0; row < pick_count; ++row) {
            const std::vector<std::string>& list = *sources[which][psionic_row(row) ? 1 : 0];
            const std::size_t at = psionic_row(row) ? row - weapon_count : row;
            if (at >= list.size()) continue;
            if (no_key(list[at])) keys[row][which] = "None";
            else if (known(std::string_view(list[at]))) keys[row][which] = list[at];
        }
    std::vector<std::string> taken;
    for (std::size_t which = 0; which < 2; ++which)
        for (std::size_t row = 0; row < pick_count; ++row) {
            std::string& name = keys[row][which];
            if (no_key(name)) continue;
            const std::string folded = folded_key(name);
            bool earlier = false;
            for (const std::string& other : taken) earlier = earlier || other == folded;
            if (earlier || refused_pick_key(name)) name = "None";
            else taken.push_back(folded);
        }
    return keys;
}

// One list as settings.toml keeps it: `count` rows from `first`, their first or second keys.
inline std::vector<std::string> pick_key_list(const PickKeys& keys, std::size_t which, std::size_t first,
                                              std::size_t count) {
    std::vector<std::string> list;
    for (std::size_t row = first; row < first + count && row < pick_count; ++row) list.push_back(keys[row][which]);
    return list;
}

struct PickKeyChange {
    enum class Kind { assigned, cleared, unchanged, fixed, modifier, taken };
    Kind kind = Kind::unchanged;
    std::size_t field = 0;            // taken: the pad field with the key
    std::size_t column = 0;           // and whether its first or its second key
    std::optional<PickSlot> swapped;  // assigned: the row that had the key
    std::string given;                // what that row has now
};

/* The Controls page's edit of one key. Delete clears it. A fixed key, a modifier or a key any pad field of `pad` or of
 * `second` has is refused. A key another row has swaps, but a key an action has never moves: that row is left with none. */
inline PickKeyChange change_pick_key(PickKeys& keys, std::size_t row, std::size_t which, std::string_view name,
                                     const KeyLayout& pad, const std::vector<std::size_t>& shown_first = {},
                                     const KeyLayout& second = {}) {
    PickKeyChange change;
    if (row >= pick_count || which > 1) return change;
    if (folded_key(name) == "delete") {
        keys[row][which] = "None";
        change.kind = PickKeyChange::Kind::cleared;
        return change;
    }
    if (reserved_key(name)) change.kind = PickKeyChange::Kind::fixed;
    else if (modifier_key(name)) change.kind = PickKeyChange::Kind::modifier;
    if (change.kind != PickKeyChange::Kind::unchanged) return change;
    for (const std::size_t column : {0u, 1u})
        if (const auto field = field_with(column ? second : pad, name, shown_first)) {
            change.kind = PickKeyChange::Kind::taken;
            change.field = *field;
            change.column = column;
            return change;
        }
    const auto other = pick_with(keys, name);
    if (other && other->row == row && other->which == which) return change;
    std::string previous = keys[row][which];
    if (field_with(pad, previous) || field_with(second, previous)) previous = "None";
    if (other) {
        keys[other->row][other->which] = previous;
        change.swapped = other;
        change.given = previous;
    }
    keys[row][which] = std::string(name);
    change.kind = PickKeyChange::Kind::assigned;
    return change;
}

}  // namespace disruptor::settings
