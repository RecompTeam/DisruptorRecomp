#include "pad_bindings.h"

#include "game/settings/pad_layout.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using disruptor::launcher::PadBindings;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write(const fs::path& path, const std::string& text) { std::ofstream(path, std::ios::binary) << text; }

#ifdef _WIN32
// Held by another program with delete sharing alone: it can be neither read nor replaced.
HANDLE hold_unreadable(const fs::path& file) {
    return CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}
#endif

void defaults(const fs::path& folder) {
    PadBindings pads(folder / "missing.ini");
    pads.load();
    for (const auto& action : disruptor::settings::pad_actions)
        require(pads.source(action.field) == action.source, "a missing file reads as the runtime's own mapping");
    require(pads.source("up") == "dpup" && pads.source("l3") == "leftstick", "movement and stick clicks too");
    for (const auto& action : disruptor::settings::pad_actions)
        require(disruptor::settings::pad_source(action.source) != nullptr, "every default source has its printed names");
    require(disruptor::settings::pad_source("LeftTrigger") && !disruptor::settings::pad_source("dpup"),
            "names are found whatever their case, and the D-pad is no button to rebind");
}

void reading(const fs::path& folder) {
    const fs::path file = folder / "input.ini";
    write(file, "; comment\n[controller]\nenabled = true\ndeadzone = 9000\n\n[mapping]\nCross = B ; swapped by hand\n"
                "circle = a,rightstick\nsquare = none\n# kept\n[later]\ntriangle = b\n");
    PadBindings pads(file);
    pads.load();
    require(pads.source("cross") == "b" && pads.source("circle") == "a" && pads.source("square").empty() &&
                pads.source("triangle") == "y",
            "the mapping is read with comments cut, case aside, none as nothing and unnamed fields at their defaults");
}

void assigning() {
    PadBindings pads("unused.ini");
    const auto swapped = pads.assign("cross", "b");
    require(swapped && *swapped == "circle" && pads.source("cross") == "b" && pads.source("circle") == "a",
            "a button already on an action moves there, and that action takes the old one");
    require(!pads.assign("triangle", "rightstick") && pads.source("triangle") == "rightstick" && pads.source("r3").empty(),
            "a button the page does not show is simply taken from it");
    pads.use_defaults();
    require(!pads.assign("cross", "a") && pads.source("cross") == "a" && pads.source("circle") == "b",
            "a button put back on its own action changes nothing else");
}

void hand_edits(const fs::path& folder) {
    const fs::path file = folder / "hand.ini";
    write(file, "[mapping]\ncircle = b\nsquare = B\nl1 = lb\nr1 = R1\n");
    PadBindings pads(file);
    pads.load();
    require(pads.source("l1") == "leftshoulder" && pads.source("r1") == "rightshoulder",
            "the runtime's other names for a button read as its own name");
    pads.assign("cross", "b");
    require(pads.source("cross") == "b" && (pads.source("circle") == "a") != (pads.source("square") == "a") &&
                (pads.source("circle").empty() || pads.source("square").empty()),
            "a button two actions had by hand moves over, and only one of them takes the old one");
    pads.assign("triangle", "leftshoulder");
    require(pads.source("l1") == "y", "a button named by another name moves over as well");

    write(file, "[mapping]\n[unfinished\ncross = a\n");
    PadBindings broken(file);
    broken.load();
    broken.assign("cross", "y");
    require(broken.save(), "the file is written");
    PadBindings again(file);
    again.load();
    require(again.source("cross") == "y", "a line the runtime takes for no section stays in the section before it");

    write(file, "[mapping]]\ncross = b\n");
    PadBindings doubled(file);
    doubled.load();
    require(doubled.source("cross") == "a", "a section the runtime names mapping] is not the mapping");
    doubled.assign("cross", "y");
    require(doubled.save() && read(file).find("[mapping]]\ncross = b\n") == 0, "and it is kept as it was");
    PadBindings reread(file);
    reread.load();
    require(reread.source("cross") == "y", "the binding goes to a mapping section of its own");
}

void printed_names() {
    using disruptor::settings::PadFamily;
    const auto& back = *disruptor::settings::pad_source("back");
    const auto& start = *disruptor::settings::pad_source("start");
    require(std::string(printed(back, PadFamily::xbox)) == "View" && std::string(printed(back, PadFamily::playstation3)) == "Select" &&
                std::string(printed(start, PadFamily::playstation3)) == "Start" &&
                std::string(printed(back, PadFamily::playstation4)) == "Share" &&
                std::string(printed(back, PadFamily::playstation5)) == "Create" &&
                std::string(printed(start, PadFamily::playstation4)) == "Options",
            "a button is named as the pad in hand prints it");
}

void writing(const fs::path& folder) {
    const fs::path file = folder / "kept.ini";
    write(file, "; PSXRecomp input mapping.\n[controller]\ndeadzone = 9000\n\n[mapping]\nup = dpup,lefty-\ncross = a\n\n[other]\nx = 1\n");
    PadBindings pads(file);
    pads.load();
    pads.assign("square", "rightshoulder");
    pads.assign("triangle", "rightstick");
    require(pads.save(), "the file is written");
    const std::string text = read(file);
    require(text.find("; PSXRecomp input mapping.\n[controller]\ndeadzone = 9000\n") == 0 &&
                text.find("[other]\nx = 1\n") != std::string::npos,
            "comments, the controller section and other sections are kept");
    require(text.find("square = rightshoulder\n") != std::string::npos && text.find("r1 = x\n") != std::string::npos &&
                text.find("triangle = rightstick\n") != std::string::npos && text.find("r3 = none\n") != std::string::npos &&
                text.find("select = back\n") < text.find("[other]") && text.find("up = dpup,lefty-\n") != std::string::npos,
            "the mapping is rewritten in its own section, a button with nothing is none, lists stay lists");
    PadBindings again(file);
    again.load();
    for (const char* field : {"up", "cross", "square", "r1", "r3", "triangle", "select"})
        require(again.source(field) == pads.source(field), "what was written reads back");

    const fs::path fresh = folder / "fresh.ini";
    PadBindings first(fresh);
    first.assign("start", "back");
    require(first.save() && read(fresh).find("[mapping]\n") == 0, "a missing file is written with its mapping");

    const fs::path blocked = folder / "blocked.ini";
    fs::create_directories(blocked / "inside");
    PadBindings unreadable(blocked);
    unreadable.load();
    require(unreadable.source("cross") == "a" && !unreadable.save() && fs::is_directory(blocked / "inside"),
            "a file that cannot be read reads as the defaults and is not replaced");
#ifdef _WIN32
    const HANDLE held = hold_unreadable(file);
    require(held != INVALID_HANDLE_VALUE, "the test holds the file");
    PadBindings locked(file);
    locked.assign("cross", "y");
    const bool saved = locked.save();
    CloseHandle(held);
    require(!saved && read(file).find("square = rightshoulder\n") != std::string::npos,
            "a file another program holds is reported and left as it was");
#endif
}

} // namespace

int main() {
    const fs::path folder = fs::temp_directory_path() / "disruptor-pad-bindings-test";
    try {
        fs::remove_all(folder);
        fs::create_directories(folder);
        defaults(folder);
        reading(folder);
        assigning();
        hand_edits(folder);
        printed_names();
        writing(folder);
        fs::remove_all(folder);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        fs::remove_all(folder);
        return 1;
    }
    std::cout << "launcher pad bindings: PASS\n";
    return 0;
}
