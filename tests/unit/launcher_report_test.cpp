#include "launcher/report.h"

#include <array>
#include <chrono>
#include <ctime>
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

void test_user_names_are_cut() {
    expect(without_user_names(R"(disc C:\Users\Alice\Games\d.cue loaded)") == R"(disc C:\Users\<user>\Games\d.cue loaded)",
           "a user folder's name is cut out of a path");
    expect(without_user_names(R"(language_disc = "D:\\Users\\Bob Smith\\d.cue")") == R"(language_disc = "D:\\Users\\<user>\\d.cue")",
           "and out of a path as a settings file writes it, with a space in the name");
    expect(without_user_names("a /users/carol/x and C:/USERS/Dave") == "a /users/<user>/x and C:/USERS/<user>",
           "whatever the slashes and the case, to the end of the text");
    expect(without_user_names(R"(one C:\Users\Eve\a two C:\Users\Eve\b)") == R"(one C:\Users\<user>\a two C:\Users\<user>\b)",
           "every time it occurs");
    expect(without_user_names(R"(path = "C:\Users\Frank")" "\r\nnext") == R"(path = "C:\Users\<user>")" "\r\nnext",
           "a name ends at a quote and at a line's end");
    expect(without_user_names(R"(C:\Games\Users.txt and powerusers\x and C:\Users\)") == R"(C:\Games\Users.txt and powerusers\x and C:\Users\)",
           "what is no user folder is left alone, and so is a folder with no name after it");
    expect(without_user_names(R"(C:\Users\Gina)" "\r\nnext") == R"(C:\Users\<user>)" "\r\nnext",
           "a name with no quote after it ends at the line's end too");
    expect(without_user_names(R"(C:\Users\O'Connor\Games)") == R"(C:\Users\<user>\Games)",
           "an apostrophe is part of a name");
    expect(without_user_names("") .empty() && without_user_names("users") == "users", "short texts pass through");
}

void test_a_report_takes_logs_and_settings_only() {
    const fs::path root = fs::temp_directory_path() / "disruptor-report-test";
    fs::remove_all(root);
    const long long began = static_cast<long long>(std::time(nullptr));
    const std::string heading = started_line();
    const auto dump = [](long long seconds) { return "psx_freeze_dump_psx-runtime_" + std::to_string(seconds) + ".json"; };
    put(root / "startup.log", heading + R"(psxrecomp: loaded C:\Users\Alice\Games\game.toml)" "\n");
    put(root / "startup.previous.log", "the run before\n");
    put(root / "disruptor-mouse-aim.log", "event=ready\n");
    put(root / "settings.toml", R"(language_disc = "C:\\Users\\Alice\\d.cue")" "\n");
    put(root / "bios" / "openbios.bin", "bios");
    put(root / "saves" / "card.mcd", "save");
    put(root / "disc" / "Disruptor.bin", "disc");
    put(root / "DisruptorRecompiled.exe", "exe");
    put(root / "psx_last_run_report.json", R"({"reason": "seh", "module": "C:\\Users\\Alice\\x.dll"})");
    put(root / "psx_freeze_heartbeat.json", "{}");
    for (const long long made : {began - 3600, began - 1, began + 10, began + 20, began + 30}) put(root / dump(made), "{}");
    put(root / "psx_freeze_dump_private.json", "a file that only begins like a dump");
    put(root / ("psx_freeze_dump_psx-runtime_" + std::to_string(began + 40) + "x.json"), "no number");
    put(root / ("psx_freeze_dump_psx-runtime_" + std::to_string(began + 41) + ".txt"), "no JSON");
    put(root / ("psx_freeze_dump_psx-runtime_1" + std::to_string(began + 42) + "999.json"), "a number too long for a time");
    put(root / ("other_dump_psx-runtime_" + std::to_string(began + 43) + ".json"), "another name");
    put(root / ("psx_freeze_dump_" + std::to_string(began + 44) + ".json"), "no backend named");
    fs::create_directories(root / dump(began + 50));
    put(root / "elsewhere" / "private.json", "a file a link leads to");
    std::error_code no_link;
    fs::create_symlink(root / "elsewhere" / "private.json", root / dump(began + 60), no_link);

    const Report plain = make_report(root, R"(system of C:\Users\Alice)", "20261013-010203", false);
    expect(!plain.archived && plain.path == root / "reports" / "disruptor-report-20261013-010203" && fs::is_directory(plain.path),
           "without an archive the report is a folder named by its stamp under reports");
    expect(read(plain.path / "startup.log") == heading + R"(psxrecomp: loaded C:\Users\<user>\Games\game.toml)" "\n",
           "the log is taken with the user's name cut out");
    expect(read(plain.path / "settings.toml") == R"(language_disc = "C:\\Users\\<user>\\d.cue")" "\n", "and so are the settings");
    const std::string system = read(plain.path / "system.txt");
    expect(system.rfind(R"(system of C:\Users\<user>)" "\r\nReport made 20", 0) == 0, "and the system note, which says when the report was made");
    expect(system.find("\r\nstartup.log: " + std::to_string(49 + heading.size()) + " bytes, written 20") != std::string::npos &&
               system.find(" UTC\r\nstartup.previous.log: 15 bytes, written 20") != std::string::npos &&
               system.find(dump(began + 30) + ": 2 bytes, written 20") < system.find(dump(began + 20) + ": 2 bytes, written 20") &&
               system.find("Freeze dumps:") == std::string::npos,
           "the note lists every file with its size and when it was written, the dumps newest first");
    expect(read(plain.path / "psx_last_run_report.json") == R"({"reason": "seh", "module": "C:\\Users\\<user>\\x.dll"})" &&
               read(plain.path / "psx_freeze_heartbeat.json") == "{}",
           "how the last run ended and the freeze heartbeat are taken, a user's name cut out of them too");
    expect(fs::exists(plain.path / dump(began + 30)) && fs::exists(plain.path / dump(began + 20)) && !fs::exists(plain.path / dump(began + 10)),
           "of the freeze dumps made since the run began the newest two");
    expect(read(plain.path / "startup.previous.log") == "the run before\n" && read(plain.path / "disruptor-mouse-aim.log") == "event=ready\n",
           "the run before and the mouse log are taken");
    size_t files = 0;
    for (const auto& entry : fs::recursive_directory_iterator(plain.path)) files += entry.is_regular_file();
    expect(files == 9, "nothing else is taken: no BIOS, no save, no disc image, no executable, no file or folder or link that only looks like a dump");

    fs::remove(root / dump(began + 30));
    fs::remove(root / dump(began + 20));
    const Report bounded = make_report(root, "system", "bounded", false);
    expect(fs::exists(bounded.path / dump(began + 10)) && !fs::exists(bounded.path / dump(began - 1)) && !fs::exists(bounded.path / dump(began - 3600)),
           "a dump from before the run began is left out though there is room for it");

    if (!no_link) {
        fs::remove(root / "settings.toml");
        fs::create_symlink(root / "elsewhere" / "private.json", root / "settings.toml", no_link);
        const Report linked = make_report(root, "system", "linked", false);
        expect(!no_link && !fs::exists(linked.path / "settings.toml") &&
                   read(linked.path / "system.txt").find("\r\nsettings.toml: none\r\n") != std::string::npos,
               "a link where a file of the report should be is not followed");
        fs::remove(root / "settings.toml");
        put(root / "settings.toml", "muted = true\n");
    }

    fs::remove(root / "startup.previous.log");
    fs::remove(root / "disruptor-mouse-aim.log");
    fs::remove(root / "psx_last_run_report.json");
    for (const std::string& unheaded : {std::string("a log the launcher did not head\n"), "launcher: started 2026-1x-13 04:09:07 UTC\r\n" + heading,
                                        "LAUNCHER: STARTED " + heading.substr(18), "\n" + heading,
                                        heading.substr(0, heading.size() - 12)}) {
        put(root / "startup.log", unheaded);
        const Report sparse = make_report(root, "system", "sparse", false);
        expect(fs::exists(sparse.path / "startup.log") && !fs::exists(sparse.path / "startup.previous.log") &&
                   !fs::exists(sparse.path / "disruptor-mouse-aim.log"),
               "a file the build folder does not have is not made up");
        const std::string sparse_note = read(sparse.path / "system.txt");
        expect(sparse_note.rfind("system\r\nReport made 20", 0) == 0 && sparse_note.find("\r\nstartup.previous.log: none\r\n") != std::string::npos &&
                   sparse_note.find("\r\npsx_last_run_report.json: none\r\n") != std::string::npos,
               "and the note says there is none");
        expect(!fs::exists(sparse.path / dump(began + 10)) &&
                   sparse_note.find("\r\nFreeze dumps: none taken, the log does not say when its run began\r\n") != std::string::npos,
               "a log that does not begin with a whole start line tells no run: no dump is taken, and the note says why");
    }
    put(root / "startup.log", heading + "the last run\n");

    const Report packed = make_report(root, "system", "packed");
    const fs::path folder = root / "reports" / "disruptor-report-packed";
    if (packed.archived)
        expect(packed.path == fs::path(folder).concat(".zip") && fs::file_size(packed.path) > 0 && !fs::exists(folder),
               "with Windows' tar the report is one ZIP and its folder is gone");
    else
        expect(packed.path == folder && fs::exists(folder / "startup.log") && !fs::exists(fs::path(folder).concat(".zip")),
               "without it the folder stays and no half-made ZIP is left");

    std::string long_log = std::string(50, 'x') + R"( C:\Users\Alice\secret)" "\n";
    while (long_log.size() < 4 * 1024 * 1024 + 62) long_log += "filler\n";
    long_log.resize(4 * 1024 * 1024 + 62);
    put(root / "startup.log", long_log);
    put(root / "reports" / "disruptor-report-long" / "left-over.txt", "from an earlier report of the same second");
    const Report cut = make_report(root, "system", "long", false);
    const std::string kept = read(cut.path / "startup.log");
    expect(kept.size() < 4 * 1024 * 1024 && kept.find("secret") == std::string::npos && kept.find("filler\n") < 8,
           "of a long log the end is taken, from a whole line on: a line cut inside a user's name is left out");
    expect(read(cut.path / "system.txt").find("\r\nstartup.log: 4194366 bytes, written 20") != std::string::npos &&
               read(cut.path / "system.txt").find(" UTC, its end taken\r\n") != std::string::npos,
           "and the note says so beside the log's whole size");
    expect(!fs::exists(cut.path / "left-over.txt"), "and what an older report left in the folder does not ride along");

    bool refused = false;
    put(root / "blocked", "a file where the reports folder would be");
    try {
        (void)make_report(root / "blocked", "system", "refused", false);
    } catch (const std::runtime_error&) {
        refused = true;
    }
    expect(refused, "a folder that cannot be made is an error, not a silent nothing");
    fs::remove_all(root);
}

void test_the_notes_have_their_shape() {
    const std::string stamp = report_stamp();
    expect(stamp.size() == 15 && stamp[8] == '-' && stamp.find_first_not_of("0123456789-") == std::string::npos,
           "a stamp is a date and a time of digits");
    const auto universal = [] {
        const std::time_t now = std::time(nullptr);
        std::tm parts{};
        gmtime_s(&parts, &now);
        std::array<char, 32> text{};
        std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M:%S UTC", &parts);
        return std::string(text.data());
    };
    const std::string before = universal();
    const std::string made = utc_now();
    const std::string after = universal();
    expect(before <= made && made <= after, "the time of now is the universal one, whatever zone the machine is in");
    expect(made.size() == 23 && made[4] == '-' && made[10] == ' ' && made[13] == ':' && made.ends_with(" UTC") &&
               made.find_first_not_of("0123456789-: UTC") == std::string::npos,
           "the time of now is a date, a time and UTC");
    const std::string line = started_line();
    expect(line.rfind("launcher: started 20", 0) == 0 && line.size() == 43 && line.ends_with(" UTC\r\n"),
           "the line that heads a log says when the run began");
    const std::string note = system_note();
    expect(note.rfind("Disruptor Recompiled ", 0) == 0 && note.find("Windows: ") != std::string::npos,
           "the system note begins with the build's version and names the Windows version");
}

} // namespace

int main() {
    test_user_names_are_cut();
    test_a_report_takes_logs_and_settings_only();
    test_the_notes_have_their_shape();
    if (g_failures) return 1;
    std::cout << "launcher report: PASS\n";
    return 0;
}
