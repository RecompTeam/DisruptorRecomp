#include "key_bindings.h"

#include "ini_lines.h"

#include <algorithm>
#include <iterator>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr KeyAction actions[] = {
    {"Move forward", "up"},          {"Move back", "down"},        {"Turn left", "left"},
    {"Turn right", "right"},         {"Strafe left", "l2"},        {"Strafe right", "r2"},
    {"Fire / confirm", "cross"},     {"Jump / cancel", "circle"},  {"Psionic attack", "square"},
    {"Use", "triangle"},             {"Choose weapon", "l1"},      {"Choose psionic", "r1"},
    {"Map", "select"},               {"Pause", "start"},
};

// The classic layout puts the left stick on the arrows too: a stick field with its direction's key follows that key.
constexpr std::pair<std::string_view, std::string_view> stick_twins[] = {
    {"up", "ls_up"}, {"down", "ls_down"}, {"left", "ls_left"}, {"right", "ls_right"}};

std::size_t field_index(std::string_view field) {
    const auto found = std::find(settings::key_fields.begin(), settings::key_fields.end(), field);
    return static_cast<std::size_t>(found - settings::key_fields.begin());
}

bool shown(std::size_t index) {
    return std::any_of(std::begin(actions), std::end(actions),
                       [index](const KeyAction& action) { return field_index(action.field) == index; });
}

bool same_key(std::string_view one, std::string_view other) {
    return settings::folded_key(one) == settings::folded_key(other);
}

} // namespace

constexpr FixedKey fixed[] = {
    {"Escape", "frees the mouse, closes the settings, skips logos and movies"},
    {"Backquote (`)", "opens the settings"},
    {"B", "fast-forwards while held"},
    {"F1 to F11", "load a save state\nWith Shift they save one"},
    {"F12", "loads the newest replay capsule\nCtrl+F12 records one"},
    {"Alt+Enter", "switches between a window and fullscreen"},
};

std::span<const KeyAction> key_actions() { return actions; }

std::span<const FixedKey> fixed_keys() { return fixed; }


void KeyBindings::load() {
    layout_ = settings::layout_of(settings::modern_keys);
    std::error_code missing;
    if (!fs::exists(file_, missing)) return;
    const auto lines = read_lines(file_);
    if (!lines) return;
    KeyLayout read;
    read.fill("None");
    std::string section;
    for (const std::string& raw : *lines) {
        const std::string line = trimmed(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            section = lowered(trimmed(line.substr(1, line.find(']') - 1)));
            continue;
        }
        const auto equals = line.find('=');
        if (section != "player1" || equals == std::string::npos) continue;
        const std::size_t index = field_index(lowered(trimmed(line.substr(0, equals))));
        if (index < settings::key_count) read[index] = trimmed(line.substr(equals + 1));
    }
    layout_ = read;
}

std::string KeyBindings::key(std::string_view field) const {
    const std::size_t index = field_index(field);
    return index < settings::key_count ? layout_[index] : std::string();
}

std::optional<std::string> KeyBindings::assign(std::string_view field, const std::string& name) {
    const std::size_t index = field_index(field);
    if (index >= settings::key_count) return std::nullopt;
    const KeyLayout before = layout_;
    std::optional<std::string> swapped;
    for (const KeyAction& action : actions) {
        const std::size_t other = field_index(action.field);
        if (other == index || !same_key(before[other], name)) continue;
        layout_[other] = before[index];
        swapped = action.field;
    }
    layout_[index] = name;
    for (const auto& [direction, stick] : stick_twins) {
        const std::size_t leader = field_index(direction), follower = field_index(stick);
        if (same_key(before[follower], before[leader]) && !same_key(before[leader], "None")) layout_[follower] = layout_[leader];
    }
    for (std::size_t other = 0; other < settings::key_count; ++other) {
        if (shown(other) || !same_key(layout_[other], name)) continue;
        const auto twin = std::find_if(std::begin(stick_twins), std::end(stick_twins),
                                       [other](const auto& pair) { return field_index(pair.second) == other; });
        if (twin != std::end(stick_twins) && same_key(layout_[field_index(twin->first)], name)) continue;
        layout_[other] = "None";
    }
    return swapped;
}

void KeyBindings::clear(std::string_view field) {
    const std::size_t index = field_index(field);
    if (index >= settings::key_count) return;
    const std::string old = layout_[index];
    layout_[index] = "None";
    for (const auto& [direction, stick] : stick_twins)
        if (field_index(direction) == index && same_key(layout_[field_index(stick)], old)) layout_[field_index(stick)] = "None";
}

void KeyBindings::release(std::string_view name) {
    if (same_key(name, "None")) return;
    for (std::string& key : layout_)
        if (same_key(key, name)) key = "None";
}

void give_to_pick(KeyBindings& keys, settings::PickKeys& picks, std::size_t row, std::size_t which, std::string_view field) {
    const std::string name = keys.key(field);
    if (row >= settings::pick_count || which > 1 || settings::no_key(name) || settings::refused_pick_key(name)) return;
    std::string previous = picks[row][which];
    if (settings::field_with(keys.layout(), previous)) previous = "None";
    if (settings::no_key(previous)) keys.clear(field);
    else keys.assign(field, previous);
    keys.release(name);
    while (const auto held = settings::pick_with(picks, name)) picks[held->row][held->which] = "None";
    picks[row][which] = name;
}

void give_to_action(KeyBindings& keys, settings::PickKeys& picks, std::string_view field, const std::string& name) {
    const auto slot = settings::pick_with(picks, name);
    const auto holder = settings::field_with(keys.layout(), name);
    if (!slot || field_index(field) >= settings::key_count || (holder && *holder != field_index(field))) return;
    const std::string previous = keys.key(field);
    keys.assign(field, name);
    const bool free = !settings::no_key(previous) && !settings::refused_pick_key(previous) &&
                      !settings::field_with(keys.layout(), previous) && !settings::pick_with(picks, previous);
    picks[slot->row][slot->which] = free ? previous : "None";
}

namespace {

std::string pick_entry(std::size_t row, std::size_t which) {
    return (which ? "second_" : "first_") + std::to_string(row);
}

} // namespace

bool save_profile(const fs::path& file, const KeyProfile& profile) {
    std::vector<std::string> lines{"# Keys the launcher's Controls page saved, and loads back"};
    std::vector<IniValue> fields, picks;
    for (std::size_t index = 0; index < settings::key_count; ++index) fields.push_back({settings::key_fields[index], profile.layout[index]});
    for (std::size_t which = 0; which < 2; ++which)
        for (std::size_t row = 0; row < settings::pick_count; ++row) picks.push_back({pick_entry(row, which), profile.picks[row][which]});
    put_section(lines, "player1", fields, 9, Headers::opened);
    put_section(lines, "picks", picks, 9, Headers::opened);
    return write_lines(file, lines);
}

std::optional<KeyProfile> load_profile(const fs::path& file, const std::function<bool(std::string_view)>& known) {
    std::error_code missing;
    if (!fs::exists(file, missing)) return std::nullopt;
    const auto lines = read_lines(file);
    if (!lines) return std::nullopt;
    KeyProfile profile;
    profile.layout.fill("None");
    const settings::PickKeys defaults = settings::default_pick_keys();
    std::vector<std::string> lists[2][2];  // [which][0 weapons, 1 psionics]
    for (std::size_t which = 0; which < 2; ++which)
        for (std::size_t row = 0; row < settings::pick_count; ++row) lists[which][settings::psionic_row(row) ? 1 : 0].push_back(defaults[row][which]);
    std::string section;
    for (const std::string& raw : *lines) {
        const std::string line = trimmed(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            section = lowered(trimmed(line.substr(1, line.find(']') - 1)));
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = lowered(trimmed(line.substr(0, equals))), value = trimmed(line.substr(equals + 1));
        if (section == "player1") {
            const std::size_t index = field_index(key);
            if (index < settings::key_count) profile.layout[index] = value;
            continue;
        }
        for (std::size_t which = 0; section == "picks" && which < 2; ++which)
            for (std::size_t row = 0; row < settings::pick_count; ++row)
                if (key == pick_entry(row, which))
                    lists[which][settings::psionic_row(row) ? 1 : 0][settings::psionic_row(row) ? row - settings::weapon_count : row] = value;
    }
    profile.picks = settings::pick_keys_from({lists[0][0], lists[1][0], lists[0][1], lists[1][1]}, known);
    return profile;
}

bool KeyBindings::save() const {
    auto lines = read_lines(file_);
    if (!lines) return false;
    std::vector<IniValue> values;
    for (std::size_t index = 0; index < settings::key_count; ++index) values.push_back({settings::key_fields[index], layout_[index]});
    put_section(*lines, "player1", values, 9, Headers::opened);
    return write_lines(file_, *lines);
}

} // namespace disruptor::launcher
