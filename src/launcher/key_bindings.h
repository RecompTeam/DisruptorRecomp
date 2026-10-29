#pragma once

#include "game/settings/key_layouts.h"
#include "game/settings/pick_keys.h"
#include "game/settings/second_keys.h"

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

// A field's first key (0), keybinds.ini's, or its second (1), settings.toml's.
struct KeySlot {
    std::string field;
    std::size_t which = 0;
};

// Player 1's keys: the first in keybinds.ini, whose other lines are written back as they were, the second handed over.
class KeyBindings {
public:
    explicit KeyBindings(std::filesystem::path file) : file_(std::move(file)) {}

    // A missing file reads as the modern layout, as does an unreadable one, which save() will not replace.
    void load();
    const KeyLayout& layout() const { return layout_; }
    std::string key(std::string_view field, std::size_t which = 0) const;

    // Puts `name` on `field`: the action that had it takes the old key and is returned, unshown fields drop it.
    std::optional<std::string> assign(std::string_view field, const std::string& name);
    // As above for either key. The place that had `name` takes the old one, none where a mouse button would go first.
    // A mouse button never goes first: nothing changes then.
    std::optional<KeySlot> assign(std::string_view field, std::size_t which, const std::string& name);
    void use(const KeyLayout& layout) { layout_ = layout; }
    // Every field's second key: a key, a mouse button or "None".
    const KeyLayout& second() const { return second_; }
    void use_second(const KeyLayout& second) { second_ = second; }
    // Leaves the field with no key, its stick twin too where it had the same first key.
    void clear(std::string_view field, std::size_t which = 0);
    // Takes the key off every field, first and second.
    void release(std::string_view name);
    // The place with the key: an action's first or second key, then a field the page does not show.
    std::optional<KeySlot> holder(std::string_view name) const;
    bool save() const;

private:
    std::filesystem::path file_;
    KeyLayout layout_ = settings::layout_of(settings::modern_keys);
    KeyLayout second_ = settings::default_second_keys(true);
};

// Take it: the field gives its key to a row of picks and takes the row's old key, none where an action has that one.
void give_to_pick(KeyBindings& keys, settings::PickKeys& picks, std::size_t row, std::size_t which, std::string_view field,
                  std::size_t column = 0);
// Take it: the row with `name` gives it to the field and takes the field's old key where a row may have it, else none.
void give_to_action(KeyBindings& keys, settings::PickKeys& picks, std::string_view field, const std::string& name,
                    std::size_t column = 0);

// A saved set of keys: the 24 fields, their second keys and every pick, in a file of its own beside keybinds.ini.
// A profile saved before second keys has none: loading it keeps the second keys in use.
struct KeyProfile {
    KeyLayout layout;
    settings::PickKeys picks;
    std::optional<KeyLayout> second;
};
bool save_profile(const std::filesystem::path& file, const KeyProfile& profile);
// A pick name `known` does not take keeps its default, as in settings.toml.
std::optional<KeyProfile> load_profile(const std::filesystem::path& file, const std::function<bool(std::string_view)>& known);

} // namespace disruptor::launcher
