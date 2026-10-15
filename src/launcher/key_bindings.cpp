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
    {"]", "fast-forwards while held"},
    {"F1 to F11", "load a save state. With Shift they save one"},
    {"F12", "loads the newest replay capsule. Ctrl+F12 records one"},
    {"Alt+Enter", "switches between a window and fullscreen"},
};

std::span<const KeyAction> key_actions() { return actions; }

std::span<const FixedKey> fixed_keys() { return fixed; }

bool reserved_key(std::string_view name) {
    const std::string folded = settings::folded_key(name);
    if (folded == "escape" || folded == "`" || folded == "]") return true;
    for (int number = 1; number <= 12; ++number)
        if (folded == "f" + std::to_string(number)) return true;
    return false;
}

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

bool KeyBindings::save() const {
    auto lines = read_lines(file_);
    if (!lines) return false;
    std::vector<IniValue> values;
    for (std::size_t index = 0; index < settings::key_count; ++index) values.push_back({settings::key_fields[index], layout_[index]});
    put_section(*lines, "player1", values, 9, Headers::opened);
    return write_lines(file_, *lines);
}

} // namespace disruptor::launcher
