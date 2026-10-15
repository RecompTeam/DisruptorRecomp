#pragma once

#include "game/settings/key_layouts.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace disruptor::launcher {

using settings::KeyLayout;

// A game action the Controls page lets the player put on a key, and its field in keybinds.ini.
struct KeyAction {
    const char* label;
    const char* field;
};
std::span<const KeyAction> key_actions();

// Keys the game keeps for itself: the menu, the mouse, save states, capsules and fast-forward.
bool reserved_key(std::string_view name);

// What the keys that cannot be rebound do, the same in both schemes.
struct FixedKey {
    const char* keys;
    const char* does;
};
std::span<const FixedKey> fixed_keys();

// Player 1's keys in keybinds.ini. The rest of the file, player 2 and comments, is written back as it was.
class KeyBindings {
public:
    explicit KeyBindings(std::filesystem::path file) : file_(std::move(file)) {}

    // A missing file reads as the modern layout, as does an unreadable one, which save() will not replace.
    void load();
    const KeyLayout& layout() const { return layout_; }
    std::string key(std::string_view field) const;

    // Puts `name` on `field`: the action that had it takes the old key and is returned, unshown fields drop it.
    std::optional<std::string> assign(std::string_view field, const std::string& name);
    void use(const KeyLayout& layout) { layout_ = layout; }
    bool save() const;

private:
    std::filesystem::path file_;
    KeyLayout layout_ = settings::layout_of(settings::modern_keys);
};

} // namespace disruptor::launcher
