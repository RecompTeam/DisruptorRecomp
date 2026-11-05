#pragma once

#include <array>
#include <cctype>
#include <span>
#include <string_view>

namespace disruptor::settings {

// A source names a button by its place, as SDL does: a is the bottom face button, b the right one.
struct PadSource {
    const char* source;
    const char* xbox;
    const char* playstation;
    const char* nintendo;
    const char* gamecube;
};
inline constexpr std::array<PadSource, 12> pad_sources = {{
    {"a", "A", "Cross", "B", "A"},
    {"b", "B", "Circle", "A", "X"},
    {"x", "X", "Square", "Y", "B"},
    {"y", "Y", "Triangle", "X", "Y"},
    {"leftshoulder", "LB", "L1", "L", "ZL"},
    {"rightshoulder", "RB", "R1", "R", "Z"},
    {"lefttrigger", "LT", "L2", "ZL", "L"},
    {"righttrigger", "RT", "R2", "ZR", "R"},
    {"back", "View", "Create", "Minus", "Back"},
    {"start", "Menu", "Options", "Plus", "Start"},
    {"leftstick", "LS", "L3", "LS", "LS"},
    {"rightstick", "RS", "R3", "RS", "RS"},
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
enum class PadFamily { xbox, playstation3, playstation4, playstation5, nintendo, gamecube };

inline const char* family_name(PadFamily family) {
    if (family == PadFamily::xbox) return "Xbox";
    if (family == PadFamily::nintendo) return "Nintendo";
    if (family == PadFamily::gamecube) return "GameCube";
    return "PlayStation";
}

inline const char* printed(const PadSource& source, PadFamily family) {
    const std::string_view name = source.source;
    if (family == PadFamily::xbox) return source.xbox;
    if (family == PadFamily::nintendo) return source.nintendo;
    if (family == PadFamily::gamecube) return source.gamecube;
    if (family == PadFamily::playstation3 && name == "back") return "Select";
    if (family == PadFamily::playstation3 && name == "start") return "Start";
    if (family == PadFamily::playstation4 && name == "back") return "Share";
    return source.playstation;
}

struct StickUse {
    const char* where;
    const char* use;
};
// What the sticks and the D-pad do in play under a Stick layout, an index into its list.
inline std::span<const StickUse> stick_uses(int layout) {
    static constexpr StickUse classic[] = {{"Left stick or D-pad", "Move and turn"}};
    static constexpr StickUse dual_analog[] = {{"Left stick", "Move and strafe"}, {"Right stick", "Turn"}, {"D-pad", "Move and turn"}};
    static constexpr StickUse southpaw[] = {{"Right stick", "Move and strafe"}, {"Left stick", "Turn"}, {"D-pad", "Move and turn"}};
    if (layout == 1) return dual_analog;
    if (layout == 2) return southpaw;
    return classic;
}

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
