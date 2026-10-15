#include "game/settings/user_folder.h"

#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;
using namespace disruptor::user_folder;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

void put(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string read(const fs::path& file) {
    std::ostringstream text;
    text << std::ifstream(file, std::ios::binary).rdbuf();
    return text.str();
}

bool throws(const auto& call) {
    try {
        call();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

void choose(const char* value) {
#ifdef _WIN32
    SetEnvironmentVariableA(kChosen, value);
#else
    if (value) setenv(kChosen, value, 1);
    else unsetenv(kChosen);
#endif
}

fs::path fresh(const char* name) {
    const fs::path folder = fs::temp_directory_path() / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

// Velopack's layout: Update.exe in the root, the program in current with its sq.version.
fs::path velopack(const fs::path& root, bool portable) {
    put(root / "Update.exe", "updater");
    put(root / "current" / "sq.version", "<version>0.1.0-alpha.6</version>");
    if (portable) put(root / ".portable", "");
    return root / "current";
}

void test_kinds_of_copy() {
    const fs::path root = fresh("disruptor-user-folder-test-kinds");
    expect(copy_of(root) == Copy::plain, "a folder of its own is a plain copy");
    const fs::path program = velopack(root, false);
    expect(copy_of(program) == Copy::installed, "current beside Update.exe with its sq.version is an installed copy");
    expect(copy_of(program / "") == Copy::installed, "a trailing separator changes nothing");
    put(root / ".portable", "");
    expect(copy_of(program) == Copy::portable, "and with .portable in the root a portable one");
    fs::remove(program / "sq.version");
    expect(copy_of(program) == Copy::plain, "without sq.version it is no Velopack copy");
    put(program / "sq.version", "");
    fs::remove(root / "Update.exe");
    expect(copy_of(program) == Copy::plain, "nor without Update.exe above it");
    const fs::path named = fresh("disruptor-user-folder-test-named") / "Current";
    put(named / "sq.version", "");
    put(named.parent_path() / "Update.exe", "");
    expect(copy_of(named) == Copy::installed, "the folder's name is matched whatever its case");
    const fs::path other = named.parent_path() / "unpacked";
    put(other / "sq.version", "");
    expect(copy_of(other) == Copy::plain, "a folder of another name beside Update.exe is a plain copy");
    fs::remove_all(root);
    fs::remove_all(named.parent_path());
}

void test_where_the_files_go() {
    const fs::path root = fresh("disruptor-user-folder-test-where");
    const fs::path local = root / "local";
    const fs::path plain = root / "unzipped";
    fs::create_directories(plain);
    expect(folder_for(plain, {}, local) == plain, "a plain copy keeps the files with the program");
    const fs::path program = velopack(root / "install", false);
    expect(folder_for(program, {}, local) == local / kInstalledName, "an installed copy keeps them in local application data");
    expect(throws([&] { folder_for(program, {}, {}); }), "and stops when there is no such folder, never falling back into current");
    put(root / "install" / ".portable", "");
    expect(folder_for(program, {}, local) == root / "install" / "data", "a portable copy keeps them beside current");
    expect(folder_for(program / "", {}, local) == root / "install" / "data",
           "and a trailing separator does not put them inside current");
    const fs::path chosen = root / "chosen";
    expect(folder_for(plain, chosen, local) == chosen && folder_for(program, chosen, local) == chosen,
           "a chosen folder wins over the rule");
    expect(throws([&] { folder_for(plain, "relative", local); }), "a chosen folder must be a whole path");
    fs::remove_all(root);
}

void test_seeding() {
    const fs::path root = fresh("disruptor-user-folder-test-seed");
    const fs::path program = root / "program";
    const fs::path user = root / "user";
    put(program / "settings.toml", "shipped settings");
    put(program / "keybinds.ini", "shipped keys");
    fs::create_directories(user);
    seed(program, user);
    expect(read(user / "settings.toml") == "shipped settings" && read(user / "keybinds.ini") == "shipped keys",
           "a new folder gets the shipped settings and key bindings");
    put(user / "settings.toml", "the player's settings");
    fs::remove(user / "keybinds.ini");
    seed(program, user);
    expect(read(user / "settings.toml") == "the player's settings", "the player's own file is never replaced");
    expect(read(user / "keybinds.ini") == "shipped keys", "a missing one is given again");
    seed(program, program);
    expect(read(program / "settings.toml") == "shipped settings", "a copy whose files are its program folder is left alone");
    fs::remove(program / "keybinds.ini");
    fs::remove(user / "keybinds.ini");
    seed(program, user);
    expect(!fs::exists(user / "keybinds.ini"), "a default the program does not ship is not made up");
    bool partial = false;
    for (const auto& entry : fs::directory_iterator(user)) partial |= entry.path().filename().string().find(".partial") != std::string::npos;
    expect(!partial, "no half-made copy is left behind");
#ifdef _WIN32
    put(program / "keybinds.ini", "shipped keys");
    HANDLE held = CreateFileW((program / "keybinds.ini").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const bool stopped = throws([&] { seed(program, user); });
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    expect(held != INVALID_HANDLE_VALUE && stopped && !fs::exists(user / "keybinds.ini"),
           "a shipped default that cannot be read stops the start instead of being skipped");
#endif
    fs::remove_all(root);
}

void test_moving_out() {
    const fs::path root = fresh("disruptor-user-folder-test-move");
    const fs::path program = root / "Games" / "current";
    const fs::path sibling = root / "Games" / "current2";
    fs::create_directories(program);
    fs::create_directories(sibling);
    const fs::path user = root / "user";
    expect(moved_out(program / "saves", program, user) == user / "saves", "a folder in the program folder moves to the user's");
    expect(moved_out(program / "saves" / "German", program, user) == user / "saves" / "German", "and one deeper in it");
    expect(moved_out(program, program, user) == user && moved_out(program / "", program, user) == user,
           "the program folder itself becomes the user's");
    expect(moved_out(root / "elsewhere" / "saves", program, user) == root / "elsewhere" / "saves",
           "a folder elsewhere stays where it is");
    expect(moved_out(sibling / "saves", program, user) == sibling / "saves",
           "a folder whose name only begins like the program's is elsewhere");
    expect(moved_out(program / ".." / "current" / "saves", program, user) == user / "saves", "a roundabout spelling is seen through");
#ifdef _WIN32
    std::wstring upper = (program / "saves").wstring();
    for (wchar_t& letter : upper) letter = static_cast<wchar_t>(towupper(letter));
    expect(moved_out(upper, program, user) == user / "SAVES", "on Windows a spelling in another case is the same folder");
#endif
    fs::remove_all(root);
}

void test_prepared_folder() {
    const fs::path root = fresh("disruptor-user-folder-test-prepared");
    expect(in_user_folder("disruptor-mouse-aim.log") == "disruptor-mouse-aim.log" && own_or_shipped("mouse-aim.ini") == "mouse-aim.ini",
           "before a folder is prepared a name stays alone");
    const fs::path program = root / "program";
    put(program / "settings.toml", "shipped settings");
    put(program / "mouse-aim.ini", "shipped tuning");
    const fs::path user = root / "files of the player";
    choose(user.string().c_str());
    expect(prepare(program) == user && fs::is_directory(user), "PSX_USER_DIR names the folder, and it is made");
    expect(read(user / "settings.toml") == "shipped settings", "and given the shipped settings");
    expect(in_user_folder("disruptor-mouse-aim.log") == user / "disruptor-mouse-aim.log", "a log goes into it");
    expect(own_or_shipped("mouse-aim.ini") == program / "mouse-aim.ini", "the shipped tuning is read while the player has none");
    put(user / "mouse-aim.ini", "the player's tuning");
    expect(own_or_shipped("mouse-aim.ini") == user / "mouse-aim.ini", "and the player's own once there is one");
    choose("relative folder");
    expect(throws([&] { prepare(program); }), "a relative PSX_USER_DIR stops the start");
    choose(nullptr);
    fs::remove_all(root);
}

} // namespace

int main() {
    test_kinds_of_copy();
    test_where_the_files_go();
    test_seeding();
    test_moving_out();
    test_prepared_folder();
    if (g_failures == 0) std::cout << "User folder tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
