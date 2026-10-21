#pragma once

#include "config_loader.h"

#include <functional>
#include <span>
#include <string>

namespace disruptor::settings {

using Settings = PSXRecompV4::UserSettings;

enum class Kind { toggle, choice, slider };
enum class Unit { none, percent, frames, thousandths };

// A value is 0 or 1 for a toggle, an index into `choices` for a choice and a whole number of `unit` for a slider. Text is UTF-8.
struct Option {
    const char* key;
    Kind kind;
    const char* label;
    bool (*saved)(const Settings&);
    int (*get)(const Settings&);
    void (*set)(Settings&, int);
    int initial;
    int lowest;
    int highest;
    std::span<const char* const> choices;
    std::span<const char* const> needs;
    Unit unit;
    const char* hint;
    const char* notice = nullptr;  // why choices past `highest` are not offered, shown apart from the hint
};

std::span<const Option> options();
const Option* find_option(const char* key);
bool applies(const Option& option, const std::function<bool(const Option&)>& on);
// The option's value in `settings`, or the one a fresh installation runs with where the file does not name it.
int stored(const Settings& settings, const Option& option);
// `settings` with each disruptor option the file does not name at its initial value. The runtime's own keys keep game.toml's.
Settings with_defaults(Settings settings);
// Every option as `key=value` in the table's order, for the game's log.
std::string describe(const Settings& settings);

} // namespace disruptor::settings
