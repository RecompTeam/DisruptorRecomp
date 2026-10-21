#pragma once

#include "game/settings/key_layouts.h"
#include "game/settings/pick_keys.h"

#include <cstddef>
#include <filesystem>
#include <functional>
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

using settings::reserved_key;

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
    // Leaves the field with no key, its stick twin too where it had the same.
    void clear(std::string_view field);
    // Takes the key off every field.
    void release(std::string_view name);
    bool save() const;

private:
    std::filesystem::path file_;
    KeyLayout layout_ = settings::layout_of(settings::modern_keys);
};

// Take it: the field gives its key to a row of picks and takes the row's old key, none where an action has that one.
void give_to_pick(KeyBindings& keys, settings::PickKeys& picks, std::size_t row, std::size_t which, std::string_view field);
// Take it: the row with `name` gives it to the field and takes the field's old key where a row may have it, else none.
void give_to_action(KeyBindings& keys, settings::PickKeys& picks, std::string_view field, const std::string& name);

// A saved set of keys: the 24 fields and every pick, in a file of its own beside keybinds.ini.
struct KeyProfile {
    KeyLayout layout;
    settings::PickKeys picks;
};
bool save_profile(const std::filesystem::path& file, const KeyProfile& profile);
// A pick name `known` does not take keeps its default, as in settings.toml.
std::optional<KeyProfile> load_profile(const std::filesystem::path& file, const std::function<bool(std::string_view)>& known);

} // namespace disruptor::launcher
