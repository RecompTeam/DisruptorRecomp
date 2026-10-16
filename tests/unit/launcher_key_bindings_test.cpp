#include "key_bindings.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace disruptor::launcher;
using disruptor::settings::classic_keys;
using disruptor::settings::layout_for_scheme;
using disruptor::settings::layout_of;
using disruptor::settings::modern_keys;

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

void schemes() {
    const KeyLayout modern = layout_of(modern_keys), classic = layout_of(classic_keys);
    require(layout_for_scheme(classic, true) == modern && layout_for_scheme(modern, false) == classic,
            "a scheme's own layout gives way to the other scheme's when the scheme changes");
    require(layout_for_scheme(modern, true) == modern && layout_for_scheme(classic, false) == classic,
            "a scheme keeps its own layout");
    KeyLayout custom = modern;
    custom[0] = "Z";
    require(layout_for_scheme(custom, false) == custom && layout_for_scheme(custom, true) == custom,
            "a layout the player made stays whatever the scheme");
    KeyLayout spelled = modern;
    spelled[4] = "enter";
    spelled[15] = "tab";
    require(layout_for_scheme(spelled, false) == classic, "names compare as SDL reads them, case and short names aside");
    KeyLayout shifted = classic;
    shifted[15] = "RShift";
    require(layout_for_scheme(shifted, true) == modern, "the runtime's short names count as the full ones");
}

void reserved() {
    for (const char* name : {"Escape", "esc", "`", "B", "b", "F1", "f12"})
        require(reserved_key(name), "the game's own keys cannot be bound");
    for (const char* name : {"W", "F", "]", "F13", "Return", "Space", "Right Shift"})
        require(!reserved_key(name), "other keys can");
}

void stick_twins(const fs::path& folder) {
    const auto unique = [](const KeyBindings& keys, const std::string& name, const char* field) {
        for (const char* other : disruptor::settings::key_fields)
            if (std::string_view(other) != field && keys.key(other) == name) return false;
        return keys.key(field) == name;
    };
    KeyBindings keys(folder / "twins.ini");
    keys.use(layout_of(classic_keys));
    keys.assign("cross", "Up");
    require(unique(keys, "Up", "cross") && keys.key("up") == "X" && keys.key("ls_up") == "X",
            "an arrow taken from the classic layout leaves the stick too, which follows the arrow's new key");
    keys.use(layout_of(classic_keys));
    keys.assign("left", "X");
    require(keys.key("left") == "X" && keys.key("ls_left") == "X" && unique(keys, "Left", "cross"),
            "the stick follows a direction's new key, and the old one is only the action's that took it");
    keys.use(layout_of(classic_keys));
    require(!keys.assign("cross", "T") && unique(keys, "T", "cross") && keys.key("l3") == "None",
            "a field the page does not show gives its key up");
    keys.use(layout_of(modern_keys));
    keys.assign("cross", "W");
    require(keys.key("up") == "Return" && keys.key("ls_up") == "None", "an unbound stick stays unbound");
}

void failed_files(const fs::path& folder) {
    const fs::path blocked = folder / "blocked.ini";
    fs::create_directories(blocked / "inside");
    KeyBindings unreadable(blocked);
    unreadable.load();
    require(unreadable.layout() == layout_of(modern_keys) && !unreadable.save() && fs::is_directory(blocked / "inside") &&
                !fs::exists(fs::path(blocked).concat(".new")),
            "a keybinds.ini that cannot be read is not replaced");

    const fs::path file = folder / "kept.ini";
    write(file, "[player1]\nup = W\n");
    fs::create_directories(fs::path(file).concat(".new") / "inside");
    KeyBindings unwritable(file);
    unwritable.load();
    unwritable.assign("up", "I");
    require(!unwritable.save() && read(file) == "[player1]\nup = W\n", "a file that cannot be written leaves the old one");
    fs::remove_all(fs::path(file).concat(".new"));
#ifdef _WIN32
    {
        std::ifstream held(file, std::ios::binary);
        require(!unwritable.save() && read(file) == "[player1]\nup = W\n" && !fs::exists(fs::path(file).concat(".new")),
                "a file that cannot be replaced is reported, and the written copy goes");
    }
#endif
    require(unwritable.save() && read(file).find("up        = I\n") != std::string::npos, "once free it is written");
#ifdef _WIN32
    const HANDLE held = hold_unreadable(file);
    require(held != INVALID_HANDLE_VALUE, "the test holds the file");
    unwritable.assign("up", "K");
    const bool saved = unwritable.save();
    CloseHandle(held);
    require(!saved && read(file).find("up        = I\n") != std::string::npos,
            "a file another program holds is reported and left as it was");
#endif
}

void file_round_trip(const fs::path& folder) {
    const fs::path file = folder / "keybinds.ini";
    fs::remove(file);
    KeyBindings missing(file);
    missing.load();
    require(missing.layout() == layout_of(modern_keys), "a missing file reads as the modern layout");

    write(file, "# kept comment\n[player1]\nup        = W\ndown = S\nsquare = F\ntriangle = E\nUnknown = 1\n\n"
                "[player2]\nup        = I\ncross     = None\n");
    KeyBindings keys(file);
    keys.load();
    require(keys.key("up") == "W" && keys.key("square") == "F" && keys.key("cross") == "None" && keys.key("l1") == "None",
            "player 1's keys are read, a field the file does not name is unbound");
    const auto swapped = keys.assign("triangle", "F");
    require(swapped && *swapped == "square" && keys.key("triangle") == "F" && keys.key("square") == "E",
            "a key already on an action moves there, and that action takes the old key");
    require(!keys.assign("cross", "X") && keys.key("cross") == "X", "a free key is simply taken");
    require(keys.save(), "the file is written");
    const std::string text = read(file);
    require(text.find("# kept comment\n") == 0 && text.find("Unknown = 1\n") != std::string::npos &&
                text.find("[player2]\nup        = I\ncross     = None\n") != std::string::npos,
            "comments, unknown lines and player 2 are kept as they were");
    require(text.find("triangle  = F\n") != std::string::npos && text.find("square    = E\n") != std::string::npos &&
                text.find("cross     = X\n") != std::string::npos && text.find("cross     = X\n") < text.find("[player2]"),
            "player 1's lines are rewritten, and the fields it lacked are added to its own section");
    KeyBindings again(file);
    again.load();
    require(again.layout() == keys.layout(), "what was written reads back");

    write(file, "[player2]\nup = I\n");
    KeyBindings without(file);
    without.load();
    without.use(layout_of(classic_keys));
    require(without.save(), "a file without player 1 is written");
    KeyBindings added(file);
    added.load();
    require(added.layout() == layout_of(classic_keys) && read(file).find("[player2]\nup = I\n") == 0,
            "player 1's section is added after what was there");
    fs::remove(file);
}

} // namespace

int main() {
    const fs::path folder = fs::temp_directory_path() / "disruptor-key-bindings-test";
    try {
        fs::create_directories(folder);
        schemes();
        reserved();
        stick_twins(folder);
        failed_files(folder);
        file_round_trip(folder);
        fs::remove_all(folder);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        fs::remove_all(folder);
        return 1;
    }
    std::cout << "launcher key bindings: PASS\n";
    return 0;
}
