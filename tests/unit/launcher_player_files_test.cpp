#include "launcher/player_files.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace disruptor::launcher;

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

std::string thrown(const auto& call) {
    try {
        call();
    } catch (const std::exception& error) {
        return error.what();
    }
    return {};
}

fs::path fresh(const char* name) {
    const fs::path folder = fs::temp_directory_path() / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

bool has(const EarlierCopy& copy, const fs::path& entry) {
    return std::find(copy.entries.begin(), copy.entries.end(), entry) != copy.entries.end();
}

// An unzipped release the player has played: the program's files beside their own.
fs::path earlier_release(const fs::path& folder) {
    put(folder / "DisruptorRecompiled.exe", "old game");
    put(folder / "game.toml", "shipped");
    put(folder / "mouse-aim.ini", "shipped tuning");
    put(folder / "settings.toml", "their settings");
    put(folder / "keybinds.ini", "their keys");
    put(folder / "saves" / "card1.mcd", "their card");
    put(folder / "saves" / "German" / "state_80048CE4_slot00.pst", "their german state");
    put(folder / "capsules" / "1" / "input.bin", "capsule");
    put(folder / "input" / "discs" / "SLUS-00224" / "disc.cue", "cue");
    put(folder / "input" / "discs" / "SLUS-00224" / "disc.bin", "the disc");
    put(folder / "input" / "discs" / ".previous-SLUS-00224-1" / "disc.bin", "a broken disc");
    put(folder / "cache" / "SLUS-00224" / "x", "cache");
    put(folder / "startup.log", "log");
    return folder;
}

void test_version_and_backups() {
    const fs::path root = fresh("disruptor-player-files-test-backups");
    put(root / "current" / "sq.version", "<package><metadata><version>0.1.0-alpha.6</version></metadata></package>");
    expect(installed_version(root / "current") == "0.1.0-alpha.6", "the installed version is read from sq.version");
    expect(installed_version(root).empty(), "a folder without sq.version has no version");
    const fs::path data = root / "data";
    put(data / "saves" / "German" / "state_80048CE4_slot00.pst", "german state");
    put(data / "backups" / "saves-20261015-000000-0.1.0-alpha.5.partial" / "card1.mcd", "half a copy");
    const fs::path first = back_up_saves(data, "0.1.0-alpha.6", "20261015-010000");
    expect(first == data / "backups" / "saves-20261015-010000-0.1.0-alpha.6", "a copy is named by when and from what version");
    expect(read(first / "German" / "state_80048CE4_slot00.pst") == "german state", "and holds the saves whole");
    for (int hour = 2; hour <= 7; ++hour) back_up_saves(data, "0.1.0-alpha.6", "20261015-0" + std::to_string(hour) + "0000");
    int kept = 0;
    for (const auto& entry : fs::directory_iterator(data / "backups"))
        kept += entry.is_directory() && !entry.path().filename().string().ends_with(".partial") ? 1 : 0;
    expect(kept == 5 && !fs::exists(first) && fs::exists(data / "backups" / "saves-20261015-070000-0.1.0-alpha.6"),
           "the newest five whole copies are kept, a half copy counts for none");
    fs::remove_all(data / "saves");
    expect(back_up_saves(data, "0.1.0-alpha.6", "20261015-080000").empty(), "no saves, no copy");
    fs::remove_all(root);
}

void test_looking_at_a_copy() {
    const fs::path root = fresh("disruptor-player-files-test-look");
    const fs::path program = root / "install" / "current";
    put(program / "mouse-aim.ini", "shipped tuning");
    put(program / "settings.toml", "shipped settings");
    const fs::path data = root / "data";
    fs::create_directories(data);
    const fs::path old = earlier_release(root / "alpha.5");
    const EarlierCopy copy = look_at_copy(old, program, data);
    expect(copy.folder == old, "the chosen folder is the copy");
    expect(has(copy, "settings.toml") && has(copy, "keybinds.ini") && has(copy, "saves") && has(copy, "capsules") &&
               has(copy, fs::path("input/discs/SLUS-00224")),
           "its settings, key bindings, saves, recordings and disc come along");
    expect(!has(copy, "mouse-aim.ini") && !has(copy, "DisruptorRecompiled.exe") && !has(copy, "game.toml") &&
               !has(copy, "cache") && !has(copy, "startup.log") && !has(copy, fs::path("input/discs")),
           "the shipped tuning, the program, its cache, the logs and a disc kept from a repair do not");
    expect(copy.bytes == std::string("their settingstheir keystheir cardtheir german statecapsulecuethe disc").size(),
           "the bytes to copy are counted");
    put(old / "mouse-aim.ini", "their tuning");
    expect(has(look_at_copy(old, program, data), "mouse-aim.ini"), "a tuning the player changed comes along");
    put(data / "input" / "discs" / "SLUS-00224" / "disc.cue", "cue");
    expect(!has(look_at_copy(old, program, data), fs::path("input/discs/SLUS-00224")), "no disc comes when this copy has one");

    const fs::path portable = root / "portable";
    put(portable / ".portable", "");
    put(portable / "Update.exe", "updater");
    put(portable / "data" / "saves" / "card1.mcd", "portable card");
    expect(look_at_copy(portable, program, data).folder == portable / "data", "a portable copy's files are in its data folder");

    expect(!thrown([&] { look_at_copy(data, program, data); }).empty(), "this copy's own files are refused");
    expect(!thrown([&] { look_at_copy(data / "input", program, data); }).empty(), "and a folder in them");
    expect(!thrown([&] { look_at_copy(root, program, data); }).empty(), "and a folder that holds them");
    expect(!thrown([&] { look_at_copy(program, program, data); }).empty(), "and this copy's program folder");
    fs::create_directories(root / "empty");
    expect(thrown([&] { look_at_copy(root / "empty", program, data); }).find("no saves or settings") != std::string::npos,
           "a folder with nothing of the player's says so");
    expect(!thrown([&] { look_at_copy(root / "missing", program, data); }).empty(), "a folder that is not there is refused");
    fs::remove_all(root);
}

void test_bringing_over() {
    const fs::path root = fresh("disruptor-player-files-test-bring");
    const fs::path program = root / "program";
    fs::create_directories(program);
    const fs::path data = root / "data";
    put(data / "settings.toml", "my settings");
    put(data / "saves" / "card1.mcd", "my card");
    put(data / "saves" / "only-here.mcd", "kept");
    const fs::path old = earlier_release(root / "alpha.5");
    const EarlierCopy copy = look_at_copy(old, program, data);
    std::atomic_bool cancelled{false};
    std::uintmax_t last = 0;
    const Brought brought = bring_over(copy, data, "20261015-090000", cancelled, [&](std::uintmax_t done, std::uintmax_t total) {
        if (done == total) last = done;
    });
    expect(last == copy.bytes, "the progress reaches the whole");
    expect(read(data / "settings.toml") == "their settings" && read(data / "saves" / "card1.mcd") == "their card" &&
               read(data / "saves" / "German" / "state_80048CE4_slot00.pst") == "their german state" &&
               read(data / "input" / "discs" / "SLUS-00224" / "disc.bin") == "the disc",
           "the earlier copy's files are in place");
    expect(read(data / "saves" / "only-here.mcd") == "kept", "a file only this copy has stays");
    expect(brought.replaced == data / "backups" / "replaced-20261015-090000" &&
               read(brought.replaced / "settings.toml") == "my settings" && read(brought.replaced / "saves" / "card1.mcd") == "my card",
           "the files they took the place of are kept");
    expect(!fs::exists(data / ".bring-20261015-090000"), "no staging folder is left");
    expect(read(old / "settings.toml") == "their settings" && read(old / "saves" / "card1.mcd") == "their card",
           "the earlier copy stays as it was");

    cancelled = true;
    put(old / "settings.toml", "newer settings");
    expect(thrown([&] { bring_over(copy, data, "20261015-100000", cancelled, {}); }).starts_with("Cancelled"),
           "a cancel stops the copy");
    expect(read(data / "settings.toml") == "their settings" && !fs::exists(data / ".bring-20261015-100000"),
           "and leaves this copy's files as they were");

    cancelled = false;
    for (int round = 0; round < 6; ++round) bring_over(copy, data, "20261015-120000", cancelled, {});
    int archives = 0;
    for (const auto& entry : fs::directory_iterator(data / "backups"))
        archives += entry.path().filename().string().starts_with("replaced-") ? 1 : 0;
    expect(archives == 7 && fs::exists(data / "backups" / "replaced-20261015-120000-6") &&
               read(data / "backups" / "replaced-20261015-090000" / "settings.toml") == "my settings",
           "every bringing keeps its own folder of what it replaced, the same stamp included, and none is pruned");

    HANDLE held = CreateFileW((data / "keybinds.ini").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const std::string failed = thrown([&] { bring_over(copy, data, "20261015-110000", cancelled, {}); });
    if (held != INVALID_HANDLE_VALUE) CloseHandle(held);
    expect(held != INVALID_HANDLE_VALUE && failed.find(".bring-20261015-110000") != std::string::npos &&
               fs::exists(data / ".bring-20261015-110000" / "keybinds.ini"),
           "a move that fails names the staging folder, and what did not come over is still there");
    bring_over(copy, data, "20261015-110000", cancelled, {});
    expect(fs::exists(data / ".bring-20261015-110000" / "keybinds.ini") && !fs::exists(data / ".bring-20261015-110000-2"),
           "a later bringing with the same stamp leaves that staging folder alone and cleans up its own");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_version_and_backups();
    test_looking_at_a_copy();
    test_bringing_over();
    if (g_failures == 0) std::cout << "Launcher player files tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
