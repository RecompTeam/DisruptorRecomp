#include "../../src/disruptor_restart.cpp"

#include <fstream>
#include <iostream>
#include <string>

/*
 * One program in three roles, told apart by its first argument and by whether a restart started it:
 *   run <file>   starts "game <file>", waits for it, then waits for the second game to write its line
 *   game <file>  the first time: writes its process id, arms the restart and exits
 *                started by the restart: writes whether the first game had ended, and exits
 *   linger <ms>  stays for that long
 */
#ifdef _WIN32

namespace {

std::string lines_of(const std::string &file) {
    std::ifstream in(file);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/* Two games may say their line at the same moment, and only an append the system makes whole keeps both. */
void say(const std::string &file, const std::string &line) {
    const HANDLE out = CreateFileA(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(out, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(out);
}

bool still_runs(unsigned long process) {
    const HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, process);
    if (!handle) return false;
    const bool running = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT;
    CloseHandle(handle);
    return running;
}

int game(const std::string &file) {
    if (!disruptor_restart_started_this()) {
        const bool chained = lines_of(file).find("first ") != std::string::npos;
        say(file, "first " + std::to_string(GetCurrentProcessId()) + "\n");
        /* A game that does not know a restart started it must not start a third, or a defect here would never stop. */
        if (chained) return 0;
        /* Registered first, so it runs last: the first game lingers after it has started the second. */
        std::atexit([] { Sleep(700); });
        if (!disruptor_restart_arm() || !disruptor_restart_arm()) return 3;
        return 0;
    }
    const std::string before = lines_of(file);
    const unsigned long first = before.rfind("first ", 0) == 0 ? std::stoul(before.substr(6)) : 0;
    char left[8] = {};
    size_t length = 0;
    getenv_s(&length, left, sizeof left, kEarlier);
    say(file, std::string("second ") + (first != 0 && !still_runs(first) ? "after" : "beside") + " the first, name " + (length == 0 ? "cleared" : "left") + "\n");
    return 0;
}

bool waits_only_for_a_game_that_ends(const std::string &program) {
    std::string command = "\"" + program + "\" linger 700";
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION started{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &started)) return false;
    const bool early = earlier_game_ended(started.dwProcessId, 50), late = earlier_game_ended(started.dwProcessId, 10000);
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    if (early || !late) std::cerr << "FAIL: a game that still runs when the wait is over has not ended, one that ends within it has\n";
    const bool none = earlier_game_ended(0, 0) && earlier_game_ended(0xFFFFFFFCu, 0);
    if (!none) std::cerr << "FAIL: a number no process has is a game that is gone\n";
    return !early && late && none;
}

int run(const std::string &program, const std::string &file) {
    if (!waits_only_for_a_game_that_ends(program)) return 1;
    std::remove(file.c_str());
    std::string command = "\"" + program + "\" game \"" + file + "\"";
    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION started{};
    if (!CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &started)) return 2;
    WaitForSingleObject(started.hProcess, 20000);
    DWORD code = 1;
    GetExitCodeProcess(started.hProcess, &code);
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
    std::string seen;
    for (int tries = 0; tries < 100 && seen.find("second ") == std::string::npos; ++tries) {
        Sleep(100);
        seen = lines_of(file);
    }
    /* A second restart, if there is one, starts as late as the first: give it time to say so. */
    Sleep(600);
    seen = lines_of(file);
    std::remove(file.c_str());
    const bool once = seen.find("second ") != std::string::npos && seen.find("second ") == seen.rfind("second ") && seen.find("first ") == seen.rfind("first ");
    if (code != 0 || !once || seen.find("second after the first, name cleared") == std::string::npos) {
        std::cerr << "FAIL: an armed game must start once more when it exits, after it has ended, and the new one must not start a third\n" << seen;
        return 1;
    }
    std::cout << "disruptor restart: PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "game") return game(argv[2]);
    if (argc == 3 && std::string(argv[1]) == "run") return run(argv[0], argv[2]);
    if (argc == 3 && std::string(argv[1]) == "linger") return Sleep(std::stoul(argv[2])), 0;
    return 2;
}

#else

int main() {
    if (disruptor_restart_arm() != 0 || disruptor_restart_started_this() != 0) return 1;
    std::cout << "disruptor restart: PASS (a program cannot start itself again here)\n";
    return 0;
}

#endif
