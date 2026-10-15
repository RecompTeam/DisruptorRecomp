/*
 * Starts the game again after it has closed.
 *
 * A setting that is read once at start, like the language disc, takes a
 * restart. The program that is closing starts a new one with its own command
 * line as its last act and names itself in the environment. The new one waits
 * for it to end before its main runs, where settings, saves and the disc are
 * opened, so the two never hold the same files at once. If the first has not
 * ended by then, the new one ends itself and the player starts the game.
 */

#include "game/session/restart.h"

#include "mod_plugins.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

bool g_armed = false;
bool g_started_by_restart = false;

#ifdef _WIN32
constexpr char kEarlier[] = "DISRUPTOR_RESTART_AFTER_PID";
constexpr DWORD kLongestWait = 15000;  /* milliseconds */

/* Whether a game is gone: it ended within the wait, or no process has its number any more. */
bool earlier_game_ended(unsigned long process, unsigned long longest_wait) {
    const HANDLE before = OpenProcess(SYNCHRONIZE, FALSE, process);
    if (!before) return GetLastError() == ERROR_INVALID_PARAMETER;
    const DWORD waited = WaitForSingleObject(before, longest_wait);
    CloseHandle(before);
    return waited == WAIT_OBJECT_0;
}

void start_again() {
    _putenv_s(kEarlier, std::to_string(GetCurrentProcessId()).c_str());
    /* The program is named apart from its command line: Windows takes the first word of an unquoted one with spaces for it. */
    std::wstring program(32768, L'\0'), command = GetCommandLineW();
    program.resize(GetModuleFileNameW(nullptr, program.data(), static_cast<DWORD>(program.size())));
    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION started{};
    if (program.empty() || !CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &started)) {
        std::fprintf(stderr, "disruptor: the game could not be started again (error %lu)\n", GetLastError());
        return;
    }
    CloseHandle(started.hThread);
    CloseHandle(started.hProcess);
}
#else
constexpr char kEarlier[] = "DISRUPTOR_RESTART_AFTER_PID";
constexpr unsigned long kLongestWait = 15000;  /* milliseconds */

/* Whether a game is gone: no process has its number, or it has ended and waits for its parent to be told. */
bool earlier_game_ended(unsigned long process, unsigned long longest_wait) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(longest_wait);
    while (true) {
        if (process == 0 || (kill(static_cast<pid_t>(process), 0) != 0 && errno == ESRCH)) return true;
        std::ifstream status("/proc/" + std::to_string(process) + "/stat");
        const std::string line((std::istreambuf_iterator<char>(status)), std::istreambuf_iterator<char>());
        const std::size_t name_end = line.rfind(')');
        if (name_end != std::string::npos && name_end + 2 < line.size() && line[name_end + 2] == 'Z') return true;
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void start_again() {
    setenv(kEarlier, std::to_string(getpid()).c_str(), 1);
    std::ifstream cmdline("/proc/self/cmdline", std::ios::binary);
    const std::string words((std::istreambuf_iterator<char>(cmdline)), std::istreambuf_iterator<char>());
    std::vector<std::string> arguments;
    for (std::size_t at = 0; at < words.size();) {
        const std::size_t end = words.find('\0', at);
        arguments.push_back(words.substr(at, end == std::string::npos ? std::string::npos : end - at));
        at = end == std::string::npos ? words.size() : end + 1;
    }
    std::vector<char *> argv;
    for (std::string &argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    std::fflush(nullptr);
    const pid_t started = arguments.empty() ? -1 : fork();
    if (started == 0) {
        execv("/proc/self/exe", argv.data());
        _exit(127);
    }
    if (started < 0) std::fprintf(stderr, "disruptor: the game could not be started again (error %d)\n", errno);
}
#endif

PSX_MOD_CONSTRUCTOR(wait_for_the_game_before) {
#ifdef _WIN32
    char earlier[32] = {};
    size_t length = 0;
    if (getenv_s(&length, earlier, sizeof earlier, kEarlier) != 0 || length == 0) return;
    g_started_by_restart = true;
    _putenv_s(kEarlier, "");
    if (!earlier_game_ended(std::strtoul(earlier, nullptr, 10), kLongestWait)) {
        std::fprintf(stderr, "disruptor: the game before this one has not closed, so this one does not start beside it\n");
        std::exit(1);
    }
#else
    const char *earlier = std::getenv(kEarlier);
    if (!earlier || !*earlier) return;
    g_started_by_restart = true;
    const unsigned long before = std::strtoul(earlier, nullptr, 10);
    unsetenv(kEarlier);
    if (!earlier_game_ended(before, kLongestWait)) {
        std::fprintf(stderr, "disruptor: the game before this one has not closed, so this one does not start beside it\n");
        std::exit(1);
    }
#endif
}

}  // namespace

extern "C" int disruptor_restart_arm(void) {
    if (!g_armed && std::atexit(start_again) != 0) return 0;
    g_armed = true;
    return 1;
}

extern "C" int disruptor_restart_started_this(void) { return g_started_by_restart ? 1 : 0; }
