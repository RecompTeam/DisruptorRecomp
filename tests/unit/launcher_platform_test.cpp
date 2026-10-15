#include "launcher/launcher_settings.h"
#include "launcher/platform.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace disruptor::launcher;

namespace {

using namespace std::chrono_literals;

constexpr const char* kVariable = "DISRUPTOR_PLATFORM_TEST";

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read(const fs::path& file) {
    std::ostringstream text;
    text << std::ifstream(file, std::ios::binary).rdbuf();
    return text.str();
}

void say(const std::string& text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

std::vector<std::string> arguments_of(int count, char** values) {
#ifdef _WIN32
    (void)count;
    (void)values;
    int wide_count = 0;
    wchar_t** wide_values = CommandLineToArgvW(GetCommandLineW(), &wide_count);
    std::vector<std::string> arguments;
    for (int at = 0; at < wide_count; ++at) arguments.push_back(narrow(wide_values[at]));
    LocalFree(wide_values);
    return arguments;
#else
    return {values, values + count};
#endif
}

unsigned long own_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// The modes the test starts itself in.
int child(const std::vector<std::string>& arguments) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const std::string& mode = arguments[1];
    if (mode == "--echo") {
        for (std::size_t at = 2; at < arguments.size(); ++at) say("[" + arguments[at] + "]\n");
        for (const auto& [name, value] : platform::environment())
            if (name == kVariable) say("env=" + value + "\n");
        say("cwd=" + utf8(fs::current_path()) + "\n");
        return 7;
    }
    if (mode == "--sleep") {
        say("pid=" + std::to_string(own_id()) + "\n");
        std::this_thread::sleep_for(60s);
        return 0;
    }
    if (mode == "--spawn") {
#ifdef _WIN32
        std::wstring command = L"\"" + platform::executable_path().wstring() + L"\" --sleep";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION started{};
        if (!CreateProcessW(platform::executable_path().c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &started))
            return 2;
        say("pid=" + std::to_string(started.dwProcessId) + "\n");
        CloseHandle(started.hThread);
        CloseHandle(started.hProcess);
#else
        const pid_t helper = fork();
        if (helper == 0) {
            sleep(60);
            _exit(0);
        }
        if (helper < 0) return 2;
        say("pid=" + std::to_string(helper) + "\n");
#endif
        return 0;
    }
    return 3;
}

// The number a child wrote after "pid=", once it has.
unsigned long written_id(const fs::path& output) {
    for (int tries = 0; tries < 200; ++tries) {
        const std::string text = read(output);
        const std::size_t at = text.find("pid=");
        if (at != std::string::npos && text.find('\n', at) != std::string::npos) return std::stoul(text.substr(at + 4));
        std::this_thread::sleep_for(50ms);
    }
    throw std::runtime_error("the child never said its number");
}

// Whether the process is gone within `most`: no such process, or one that has ended and waits to be reaped.
bool gone(unsigned long id, std::chrono::milliseconds most) {
    const auto until = std::chrono::steady_clock::now() + most;
    while (true) {
#ifdef _WIN32
        const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(id));
        if (!process) return true;
        const bool ended = WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
        CloseHandle(process);
        if (ended) return true;
#else
        if (kill(static_cast<pid_t>(id), 0) != 0) return true;
        std::ifstream status("/proc/" + std::to_string(id) + "/stat");
        std::string pid, name, state;
        if (status >> pid >> name >> state && state == "Z") return true;
#endif
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(50ms);
    }
}

void end(unsigned long id) {
#ifdef _WIN32
    if (const HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(id))) {
        TerminateProcess(process, 1);
        CloseHandle(process);
    }
#else
    kill(static_cast<pid_t>(id), SIGKILL);
#endif
}

platform::Launch launch_of(const fs::path& folder, const std::string& mode, const std::string& output) {
    platform::Launch launch;
    launch.executable = platform::executable_path();
    launch.arguments = {mode};
    launch.folder = folder;
    launch.environment = platform::environment();
    launch.output = folder / output;
    return launch;
}

void arguments_environment_and_folder(const fs::path& folder) {
    const std::vector<std::string> given = {"plain", "with space", "quote\"inside", "trailing\\", "", "\xE6\x97\xA5\xE6\x9C\xAC \xC3\xBC",
                                            "back\\\\\"slash", "a\\b c\\"};
    platform::Launch launch = launch_of(folder, "--echo", "echo.log");
    launch.arguments.insert(launch.arguments.end(), given.begin(), given.end());
    launch.environment = platform::with(platform::environment(), {{kVariable, "value=1 \xC3\xBC"}});
    launch.heading = "heading line\n";
    platform::Process process(launch);
    const std::optional<int> code = process.wait(30s);
    require(code == 7 && process.ended() == 7, "the program's exit code comes back, and again when asked again");
    const std::string text = read(launch.output);
    std::string expected = "heading line\n";
    for (const std::string& argument : given) expected += "[" + argument + "]\n";
    expected += "env=value=1 \xC3\xBC\n";
    if (!text.starts_with(expected)) std::cerr << "received:\n" << text << "expected:\n" << expected;
    require(text.starts_with(expected), "every argument arrives whole and in order, after the heading, with the variable given");
    const std::size_t at = text.find("cwd=");
    require(at != std::string::npos && fs::equivalent(from_utf8(text.substr(at + 4, text.find('\n', at) - at - 4)), folder),
            "the program runs in the folder given");
}

void refused_starts(const fs::path& folder) {
    platform::Launch launch = launch_of(folder, "--echo", "refused.log");
    launch.executable = folder / "no-such-program";
    bool refused = false;
    try {
        platform::Process process(launch);
    } catch (const std::system_error&) {
        refused = true;
    }
    require(refused, "a program that is not there is refused when it is started, not reported as an exit code later");
#ifndef _WIN32
    launch.executable = folder / "not-executable";
    std::ofstream(launch.executable) << "#!/bin/sh\nexit 0\n";
    chmod(launch.executable.c_str(), 0644);
    refused = false;
    try {
        platform::Process process(launch);
    } catch (const std::system_error&) {
        refused = true;
    }
    require(refused, "a file that cannot be executed is refused when it is started");
#endif
}

void stopping(const fs::path& folder) {
    platform::Launch launch = launch_of(folder, "--sleep", "stop.log");
    launch.ends_with_launcher = true;
    platform::Process process(launch);
    const unsigned long id = written_id(launch.output);
    require(process.stop() && process.ended().has_value() && gone(id, 5s), "stop ends the program and says so");

    launch.output = folder / "owned.log";
    unsigned long owned = 0;
    {
        platform::Process ending(launch);
        owned = written_id(launch.output);
    }
    require(gone(owned, 10s), "a program started to end with the launcher ends when its handle goes");

    launch.ends_with_launcher = false;
    launch.output = folder / "free.log";
    unsigned long free = 0;
    {
        platform::Process staying(launch);
        free = written_id(launch.output);
    }
    const bool stayed = !gone(free, 500ms);
    end(free);
    require(stayed, "a program not tied to the launcher keeps running when its handle goes");

    launch = launch_of(folder, "--spawn", "spawn.log");
    launch.ends_with_launcher = true;
    platform::Process spawning(launch);
    const unsigned long helper = written_id(launch.output);
    require(spawning.wait(30s) == 0, "the program that starts a helper ends by itself");
    const bool stopped = spawning.stop() && gone(helper, 10s);
    if (!stopped) end(helper);
    require(stopped, "stop reaches a helper the program started, after the program itself has ended");

    launch.output = folder / "spawn-owned.log";
    unsigned long left = 0;
    {
        platform::Process leaving(launch);
        left = written_id(launch.output);
        require(leaving.wait(30s) == 0, "the program that starts a helper ends by itself again");
    }
    const bool ended_with = gone(left, 10s);
    if (!ended_with) end(left);
    require(ended_with, "a helper of a program tied to the launcher ends when the handle goes, its program long ended");
}

void one_instance(const fs::path& folder) {
    std::optional<platform::Instance> first(std::in_place, folder);
    require(first->first(), "the first launcher of a folder holds it");
    {
        const platform::Instance second(folder);
        require(!second.first(), "a second launcher of the same folder is refused");
    }
    first.reset();
    const platform::Instance third(folder);
    require(third.first(), "once the first is gone the folder is free again");
}

} // namespace

int main(int count, char** values) {
    const std::vector<std::string> arguments = arguments_of(count, values);
    if (arguments.size() > 1) return child(arguments);
    const fs::path folder =
        fs::temp_directory_path() /
        ("disruptor-launcher-platform-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(folder);
    int result = 0;
    try {
        arguments_environment_and_folder(folder);
        refused_starts(folder);
        stopping(folder);
        one_instance(folder);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return result;
}
