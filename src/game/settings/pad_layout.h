#pragma once

#include <array>
#include <cctype>
#include <string_view>

namespace disruptor::settings {

// A gamepad button by input.ini's name for it and by what Xbox and PlayStation pads print on it.
struct PadSource {
    const char* source;
    const char* xbox;
    const char* playstation;
};
inline constexpr std::array<PadSource, 12> pad_sources = {{
    {"a", "A", "Cross"},
    {"b", "B", "Circle"},
    {"x", "X", "Square"},
    {"y", "Y", "Triangle"},
    {"leftshoulder", "LB", "L1"},
    {"rightshoulder", "RB", "R1"},
    {"lefttrigger", "LT", "L2"},
    {"righttrigger", "RT", "R2"},
    {"back", "View", "Create"},
    {"start", "Menu", "Options"},
    {"leftstick", "LS", "L3"},
    {"rightstick", "RS", "R3"},
}};

inline const PadSource* pad_source(std::string_view source) {
    for (const PadSource& known : pad_sources) {
        const std::string_view name = known.source;
        if (name.size() != source.size()) continue;
        bool same = true;
        for (std::size_t index = 0; index < name.size() && same; ++index)
            same = name[index] == std::tolower(static_cast<unsigned char>(source[index]));
        if (same) return &known;
    }
    return nullptr;
}

// Pads whose printed names differ: PlayStation 3 has Select and Start, 4 has Share, 5 has Create and Options.
enum class PadFamily { xbox, playstation3, playstation4, playstation5 };

inline const char* printed(const PadSource& source, PadFamily family) {
    const std::string_view name = source.source;
    if (family == PadFamily::xbox) return source.xbox;
    if (family == PadFamily::playstation3 && name == "back") return "Select";
    if (family == PadFamily::playstation3 && name == "start") return "Start";
    if (family == PadFamily::playstation4 && name == "back") return "Share";
    return source.playstation;
}

// A PlayStation button Disruptor acts on, by its input.ini field, and the source the runtime gives it by default.
struct PadAction {
    const char* field;
    const char* action;
    const char* source;
};
inline constexpr std::array<PadAction, 10> pad_actions = {{
    {"cross", "Fire, confirm", "a"},
    {"circle", "Jump, cancel", "b"},
    {"square", "Psionic attack", "x"},
    {"triangle", "Use", "y"},
    {"l1", "Choose weapon", "leftshoulder"},
    {"r1", "Choose psionic", "rightshoulder"},
    {"l2", "Strafe left", "lefttrigger"},
    {"r2", "Strafe right", "righttrigger"},
    {"select", "Map", "back"},
    {"start", "Pause", "start"},
}};

} // namespace disruptor::settings
