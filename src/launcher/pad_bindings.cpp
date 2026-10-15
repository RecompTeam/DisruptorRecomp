#include "pad_bindings.h"

#include "game/settings/pad_layout.h"
#include "ini_lines.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace disruptor::launcher {

namespace {

// The runtime's controller_map, in its order, with set_default_controller_mapping's sources.
constexpr std::array<const char*, PadBindings::field_count> fields = {
    "up", "down", "left", "right", "cross", "circle", "square", "triangle",
    "l1", "r1", "l2", "r2", "l3", "r3", "start", "select"};
constexpr std::array<const char*, PadBindings::field_count> defaults = {
    "dpup,lefty-", "dpdown,lefty+", "dpleft,leftx-", "dpright,leftx+", "a", "b", "x", "y",
    "leftshoulder", "rightshoulder", "lefttrigger", "righttrigger", "leftstick", "rightstick", "start", "back"};

std::size_t field_index(std::string_view field) {
    return static_cast<std::size_t>(std::find(fields.begin(), fields.end(), field) - fields.begin());
}

bool shown(std::size_t index) {
    return std::any_of(settings::pad_actions.begin(), settings::pad_actions.end(),
                       [index](const settings::PadAction& action) { return std::string_view(action.field) == fields[index]; });
}

// The other names parse_controller_source takes for a source.
constexpr std::pair<std::string_view, std::string_view> aliases[] = {
    {"view", "back"}, {"select", "back"}, {"menu", "start"}, {"lb", "leftshoulder"}, {"l1", "leftshoulder"},
    {"rb", "rightshoulder"}, {"r1", "rightshoulder"}, {"lt", "lefttrigger"}, {"l2", "lefttrigger"}, {"rt", "righttrigger"},
    {"r2", "righttrigger"}, {"dpadup", "dpup"}, {"dpaddown", "dpdown"}, {"dpadleft", "dpleft"}, {"dpadright", "dpright"},
    {"lsright", "leftx+"}, {"lsleft", "leftx-"}, {"lsdown", "lefty+"}, {"lsup", "lefty-"}, {"rsright", "rightx+"},
    {"rsleft", "rightx-"}, {"rsdown", "righty+"}, {"rsup", "righty-"}};

std::vector<std::string> split(const std::string& list) {
    std::vector<std::string> items;
    std::stringstream stream(list);
    for (std::string item; std::getline(stream, item, ',');) {
        item = lowered(trimmed(item));
        for (const auto& [alias, name] : aliases)
            if (item == alias) item = name;
        if (!item.empty() && item != "none" && item != "disabled") items.push_back(item);
    }
    return items;
}

std::string joined(const std::vector<std::string>& items) {
    std::string list;
    for (const std::string& item : items) list += (list.empty() ? "" : ",") + item;
    return list;
}

} // namespace

void PadBindings::use_defaults() { std::copy(defaults.begin(), defaults.end(), sources_.begin()); }

void PadBindings::load() {
    use_defaults();
    const auto lines = read_lines(file_);
    if (!lines) return;
    std::string section;
    for (const std::string& raw : *lines) {
        const std::string line = trimmed(raw.substr(0, raw.find_first_of(";#")));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lowered(trimmed(line.substr(1, line.size() - 2)));
            continue;
        }
        const auto equals = line.find('=');
        if (section != "mapping" || equals == std::string::npos) continue;
        const std::size_t index = field_index(lowered(trimmed(line.substr(0, equals))));
        if (index < field_count) sources_[index] = joined(split(line.substr(equals + 1)));
    }
}

std::string PadBindings::source(std::string_view field) const {
    const std::size_t index = field_index(field);
    if (index >= field_count) return {};
    const auto items = split(sources_[index]);
    return items.empty() ? std::string() : items.front();
}

std::optional<std::string> PadBindings::assign(std::string_view field, const std::string& source) {
    const std::size_t index = field_index(field);
    if (index >= field_count) return std::nullopt;
    const std::string wanted = lowered(trimmed(source));
    const std::vector<std::string> previous = split(sources_[index]);
    std::optional<std::string> swapped;
    for (std::size_t other = 0; other < field_count; ++other) {
        if (other == index) continue;
        std::vector<std::string> items = split(sources_[other]);
        const auto gone = std::remove(items.begin(), items.end(), wanted);
        if (gone == items.end()) continue;
        items.erase(gone, items.end());
        if (items.empty() && shown(other) && !previous.empty() && previous.front() != wanted && !swapped) {
            items.push_back(previous.front());
            swapped = fields[other];
        }
        sources_[other] = joined(items);
    }
    sources_[index] = wanted;
    return swapped;
}

bool PadBindings::save() const {
    auto lines = read_lines(file_);
    if (!lines) return false;
    std::vector<IniValue> values;
    for (std::size_t index = 0; index < field_count; ++index)
        values.push_back({fields[index], sources_[index].empty() ? std::string("none") : sources_[index]});
    put_section(*lines, "mapping", values, 0, Headers::closed);
    return write_lines(file_, *lines);
}

} // namespace disruptor::launcher
