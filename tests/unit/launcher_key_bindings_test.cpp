#include "key_bindings.h"

#include "game/settings/pick_keys.h"
#include "game/settings/second_keys.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

void pick_keys() {
    namespace keys = disruptor::settings;
    using Kind = keys::PickKeyChange::Kind;
    const auto known = [](std::string_view name) { return name != "Nonsense"; };
    const std::vector<std::string> none;
    const keys::PickKeys defaults = keys::pick_keys_from({none, none, none, none}, known);
    require(defaults == keys::default_pick_keys() && defaults[0][0] == "1" && defaults[9][0] == "0" &&
                defaults[0][1] == "Keypad 1" && defaults[9][1] == "Keypad 0" && defaults[10][0] == "None" &&
                defaults[14][1] == "None",
            "without lists each weapon has its number and the keypad's, the powers nothing");
    const std::vector<std::string> weapons{"Z", "None", "Nonsense", "Left Shift", "F5", "Z"};
    const std::vector<std::string> weapons_second{"none", "X", "Keypad 3", "B", "3"};
    const std::vector<std::string> psionics{"C", "X", "V"};
    const std::vector<std::string> psionics_second{"", "", "", "", "G"};
    const keys::PickKeys read = keys::pick_keys_from({weapons, weapons_second, psionics, psionics_second}, known);
    require(read[0][0] == "Z" && read[1][0] == "None" && read[2][0] == "3",
            "a name is kept, None is no key and an unknown name keeps the default");
    require(read[3][0] == "None" && read[4][0] == "None", "a modifier and a fixed key pick nothing");
    require(read[5][0] == "None", "a key an earlier weapon has is no key");
    require(read[0][1] == "None" && read[2][1] == "Keypad 3" && read[3][1] == "None" && read[4][1] == "None",
            "the second list is read alike, a first key wins over a second");
    require(read[1][1] == "None", "a power's first key wins over a weapon's second");
    require(read[6][0] == "7" && read[6][1] == "Keypad 7", "entries past a list's end keep their defaults");
    require(read[10][0] == "C" && read[11][0] == "X" && read[12][0] == "V" && read[14][1] == "G" && read[10][1] == "None",
            "the powers' lists fill their rows, an empty name is no key");
    require(keys::modifier_key("Right Alt") && keys::modifier_key("lctrl") && keys::modifier_key("Left GUI") &&
                !keys::modifier_key("Return"),
            "Shift, Ctrl, Alt and the system key are modifiers, in any spelling");
    require(keys::refused_pick_key("Escape") && keys::refused_pick_key("F12") && !keys::refused_pick_key("Q"),
            "the fixed keys are refused too");
    const auto slot = keys::pick_with(read, "v");
    require(slot && slot->row == 12 && slot->which == 0 && !keys::pick_with(read, "None") && !keys::pick_with(read, "Q"),
            "a key is found on its row in any case, no key on none");
    const auto modern = layout_of(modern_keys);
    require(keys::field_with(modern, "Q") == 8u && !keys::field_with(modern, "None") && !keys::field_with(modern, "1"),
            "a key is found on its field, no key never");
    auto both = layout_of(modern_keys);
    both[12] = "Tab";
    require(keys::field_with(both, "Tab") == 12u && keys::field_with(both, "Tab", {15u}) == 15u,
            "a field the page shows names the key before one it does not");
    keys::PickKeys edited = keys::default_pick_keys();
    require(keys::change_pick_key(edited, 1, 1, "Delete", modern).kind == Kind::cleared && edited[1][1] == "None",
            "Delete takes a key away");
    require(keys::change_pick_key(edited, 0, 0, "F3", modern).kind == Kind::fixed &&
                keys::change_pick_key(edited, 0, 0, "Right Shift", modern).kind == Kind::modifier && edited[0][0] == "1",
            "a fixed key and a modifier are refused and change nothing");
    const auto taken = keys::change_pick_key(edited, 10, 0, "Q", modern);
    require(taken.kind == Kind::taken && taken.field == 8u && edited[10][0] == "None", "a key an action has is refused");
    const auto swap = keys::change_pick_key(edited, 10, 0, "2", modern);
    require(swap.kind == Kind::assigned && swap.swapped && swap.swapped->row == 1 && swap.given == "None" &&
                edited[10][0] == "2" && edited[1][0] == "None",
            "a power takes a weapon's key and the weapon gets the power's, here none");
    require(keys::change_pick_key(edited, 10, 0, "2", modern).kind == Kind::unchanged, "a key on its own place stays");
    edited[0][0] = "Q";
    edited[1][0] = "Z";
    const auto kept = keys::change_pick_key(edited, 0, 0, "Z", modern);
    require(kept.kind == Kind::assigned && kept.given == "None" && edited[1][0] == "None" && edited[0][0] == "Z",
            "a swap never hands on a key an action has, the other row is left with none");
    require(keys::change_pick_key(edited, 15, 0, "K", modern).kind == Kind::unchanged, "a row past the last changes nothing");
    require(keys::pick_key_list(read, 0, 0, keys::weapon_count).size() == 10 &&
                keys::pick_key_list(read, 1, 0, keys::weapon_count)[2] == "Keypad 3" &&
                keys::pick_key_list(read, 0, keys::weapon_count, keys::psionic_count) ==
                    std::vector<std::string>{"C", "X", "V", "None", "None"},
            "the lists are written back in the game's order");
}
void taking_keys(const fs::path& folder) {
    namespace keys = disruptor::settings;
    KeyBindings pad(folder / "keybinds.ini");
    pad.use(layout_of(classic_keys));
    pad.clear("up");
    require(pad.key("up") == "None" && pad.key("ls_up") == "None" && pad.key("down") == "Down",
            "clearing a direction clears its stick twin, nothing else");
    pad.use(layout_of(modern_keys));
    keys::PickKeys picks = keys::default_pick_keys();
    give_to_pick(pad, picks, 10, 0, "l1");
    require(picks[10][0] == "Q" && pad.key("l1") == "None", "Take it: Choose weapon gives Q to Shock and is left with Shock's none");
    give_to_action(pad, picks, "r1", "Q");
    require(pad.key("r1") == "Q" && picks[10][0] == "R", "and Choose psionic takes Q back, Shock gets its R");
    give_to_pick(pad, picks, 1, 0, "triangle");
    require(picks[1][0] == "E" && pad.key("triangle") == "2", "a weapon's own key goes to the action that gives its key");
    pad.use(layout_of(classic_keys));
    picks = keys::default_pick_keys();
    give_to_pick(pad, picks, 11, 1, "up");
    require(picks[11][1] == "Up" && pad.key("up") == "None" && pad.key("ls_up") == "None",
            "an arrow given away leaves its stick twin too");
    pad.use(layout_of(classic_keys));
    picks = keys::default_pick_keys();
    give_to_action(pad, picks, "select", "1");
    require(pad.key("select") == "1" && picks[0][0] == "None", "a modifier the action had is never handed to a weapon");
    pad.use(layout_of(modern_keys));
    picks = keys::default_pick_keys();
    picks[10][0] = "Q";
    give_to_pick(pad, picks, 11, 0, "l1");
    require(picks[11][0] == "Q" && picks[10][0] == "None" && pad.key("l1") == "None",
            "Q idle on Shock while Choose weapon has it goes to Drain alone");
    pad.use(layout_of(modern_keys));
    picks = keys::default_pick_keys();
    picks[10][0] = "Q";
    give_to_action(pad, picks, "r1", "Q");
    require(pad.key("r1") == "R" && pad.key("l1") == "Q" && picks[10][0] == "Q",
            "an action takes no key from a pick while another action has it");
    pad.release("None");
    require(pad.key("l3") == "None", "releasing no key changes nothing");

    keys::PickKeys saved = keys::default_pick_keys();
    saved[12][0] = "H";
    saved[3][1] = "=";
    auto layout = layout_of(modern_keys);
    layout[10] = "J";
    const fs::path file = folder / "keys-1.ini";
    require(save_profile(file, {layout, saved}), "a profile must be written");
    const auto known = [](std::string_view name) { return name != "Nonsense"; };
    const auto loaded = load_profile(file, known);
    require(loaded && loaded->layout == layout && loaded->picks == saved, "and read back as it was, an = key included");
    require(!load_profile(folder / "keys-2.ini", known), "a profile never saved does not load");
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

void second_keys_read() {
    namespace keys = disruptor::settings;
    const auto known = [](std::string_view name) { return name != "Nonsense"; };
    const keys::KeyLayout modern = keys::default_second_keys(true), classic = keys::default_second_keys(false);
    require(modern[*keys::key_field("cross")] == "Left click" && modern[*keys::key_field("square")] == "Right click" &&
                modern[*keys::key_field("l1")] == "Mouse 4" && modern[*keys::key_field("r1")] == "Mouse 5" &&
                modern[*keys::key_field("up")] == "None" && classic == layout_of(std::array<const char*, 24>{
                    "None", "None", "None", "None", "None", "None", "None", "None", "None", "None", "None", "None",
                    "None", "None", "None", "None", "None", "None", "None", "None", "None", "None", "None", "None"}),
            "the modern scheme's second keys are its mouse buttons, the classic one has none");
    require(keys::second_keys_from({}, false, true, known) == modern && keys::second_keys_from({}, false, false, known) == classic,
            "without a list each scheme has its own");
    std::vector<std::string> list(24, "None");
    list[0] = "Up";
    list[4] = "Nonsense";
    list[6] = "right CLICK";
    list.resize(10);
    const keys::KeyLayout read = keys::second_keys_from(list, true, true, known);
    require(read[0] == "Up" && read[4] == "None" && read[6] == "right CLICK" && read[8] == "None" && read[12] == "None" &&
                keys::second_keys_from(list, true, false, known) == read,
            "a written list stays whatever the scheme: an unknown name and a missing entry are no key");
    require(keys::mouse_button("Mouse 5") == 5 && !keys::mouse_button("Middle click"),
            "the middle button, which takes and frees the mouse, is no second key");
    std::vector<std::string> sticks(24, "None");
    sticks[*keys::key_field("ls_up")] = "Up";
    sticks[*keys::key_field("l3")] = "Up";
    const keys::KeyLayout without_sticks = keys::second_keys_from(sticks, true, true, known);
    require(without_sticks[*keys::key_field("ls_up")] == "None" && without_sticks[*keys::key_field("l3")] == "Up",
            "a stick's field, which no button presses, takes no second key");

    const keys::SecondCodes codes = keys::second_codes(read, [](std::string_view name) { return name == "Up" ? 82 : 0; });
    require(codes[0] == 82 && codes[6] == -3 && codes[4] == 0 && keys::any_mouse(codes) &&
                !keys::any_mouse(keys::second_codes(classic, [](std::string_view) { return 0; })),
            "a key becomes its scancode and a mouse button its number below zero");
    std::array<std::uint8_t, 512> held{};
    held[82] = 1;
    require(keys::second_press(codes, held.data(), held.size(), 0) == (1u << 4) &&
                keys::second_press(codes, nullptr, 0, 1u << 2) == (1u << 15) &&
                keys::second_press(codes, nullptr, 0, 1u << 1) == 0 && keys::second_press(codes, held.data(), 10, 0) == 0,
            "a held second key presses its field's button, the right button Square, the middle one nothing");
}

void second_column(const fs::path& folder) {
    namespace keys = disruptor::settings;
    KeyBindings bindings(folder / "second.ini");
    bindings.use(layout_of(modern_keys));
    bindings.use_second(keys::default_second_keys(true));
    require(!bindings.assign("cross", 0, "Left click") && bindings.key("cross") == "Return" &&
                bindings.key("cross", 1) == "Left click",
            "a mouse button never becomes a first key");
    auto swapped = bindings.assign("cross", 1, "Right click");
    require(swapped && swapped->field == "square" && swapped->which == 1 && bindings.key("square", 1) == "Left click" &&
                bindings.key("cross", 1) == "Right click",
            "two second keys swap");
    swapped = bindings.assign("up", 1, "Q");
    require(swapped && swapped->field == "l1" && swapped->which == 0 && bindings.key("l1") == "None" &&
                bindings.key("up", 1) == "Q",
            "a first key moves to a second one, and its action takes the old second key");
    swapped = bindings.assign("triangle", 1, "E");
    require(swapped && swapped->field == "triangle" && swapped->which == 0 && bindings.key("triangle") == "None" &&
                bindings.key("triangle", 1) == "E",
            "an action's own first key may become its second");
    swapped = bindings.assign("cross", 1, "W");
    require(swapped && swapped->field == "up" && bindings.key("up") == "None" && bindings.key("cross", 1) == "W",
            "a mouse button that would go first leaves that place with no key");
    require(bindings.holder("Q") && bindings.holder("Q")->field == "up" && bindings.holder("Q")->which == 1 &&
                !bindings.holder("None") && !bindings.holder("Right click"),
            "the holder of a key is found among the second keys too");
    bindings.release("Left click");
    bindings.clear("up", 1);
    require(bindings.key("square", 1) == "None" && bindings.key("up", 1) == "None" && bindings.key("down") == "S",
            "release and clear reach the second keys");

    const auto known = [](std::string_view name) { return name != "Nonsense"; };
    const fs::path profile = folder / "keys-1.ini";
    require(save_profile(profile, {bindings.layout(), keys::default_pick_keys(), bindings.second()}),
            "a profile with second keys is written");
    const auto loaded = load_profile(profile, known);
    require(loaded && loaded->second && *loaded->second == bindings.second() && loaded->layout == bindings.layout(),
            "and read back with them");
    require(save_profile(profile, {bindings.layout(), keys::default_pick_keys(), std::nullopt}) &&
                !load_profile(profile, known)->second,
            "a profile saved without second keys has none, so loading it keeps those in use");
    fs::remove(profile);

    keys::PickKeys picks = keys::default_pick_keys();
    bindings.assign("square", 1, "Z");
    const keys::PickKeyChange change = keys::change_pick_key(picks, 2, 0, "Z", bindings.layout(), {}, bindings.second());
    require(change.kind == keys::PickKeyChange::Kind::taken && keys::key_fields[change.field] == std::string_view("square") &&
                change.column == 1,
            "a weapon cannot take a key an action has as its second");
    give_to_pick(bindings, picks, 2, 0, "square", 1);
    require(picks[2][0] == "Z" && bindings.key("square", 1) == "3" && !bindings.holder("Z"),
            "Take it gives a second key to the weapon, and the action takes the weapon's old key");
    give_to_action(bindings, picks, "square", "Z", 1);
    require(bindings.key("square", 1) == "Z" && picks[2][0] == "3", "and back to the action's second key");
}

} // namespace

int main() {
    const fs::path folder = fs::temp_directory_path() / "disruptor-key-bindings-test";
    try {
        fs::create_directories(folder);
        schemes();
        reserved();
        pick_keys();
        stick_twins(folder);
        failed_files(folder);
        file_round_trip(folder);
        taking_keys(folder);
        second_keys_read();
        second_column(folder);
        fs::remove_all(folder);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        fs::remove_all(folder);
        return 1;
    }
    std::cout << "launcher key bindings: PASS\n";
    return 0;
}
