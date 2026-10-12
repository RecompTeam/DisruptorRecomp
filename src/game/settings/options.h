#pragma once

#include "config_loader.h"

#include <functional>
#include <span>

// The settings a player can change, said once for the launcher and for the in-game menu.
namespace disruptor::settings {

using Settings = PSXRecompV4::UserSettings;

enum class Kind { toggle, choice, slider };
enum class Unit { none, percent, frames, thousandths };

// A value is 0 or 1 for a toggle, an index into `choices` for a choice and a whole number of `unit` for a slider.
struct Option {
    const char* key; // "section.key" in settings.toml
    Kind kind;
    const char* label; // UTF-8, as is `hint` and every choice
    bool (*saved)(const Settings&);
    int (*get)(const Settings&);
    void (*set)(Settings&, int);
    int initial; // what a fresh installation runs with
    int lowest;
    int highest;
    std::span<const char* const> choices;
    std::span<const char* const> needs; // the toggles that must be on for this option to apply
    Unit unit;
    const char* hint;
};

std::span<const Option> options();
const Option* find_option(const char* key);
// Whether every toggle the option needs is on, as `on` tells, and every toggle those need in turn.
bool applies(const Option& option, const std::function<bool(const Option&)>& on);

} // namespace disruptor::settings
