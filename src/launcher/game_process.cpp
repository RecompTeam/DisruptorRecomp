#include "game_process.h"

#include "game/settings/user_folder.h"
#include "launcher_flow.h"
#include "launcher_settings.h"
#include "report.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

HANDLE start_game(const fs::path& program, const fs::path& data, const VerifiedDisc& disc) {
    check_package(program, *disc.profile);
    const auto executable = program / "DisruptorRecompiled.exe";
    std::wstring command = game_command(program, disc);
    const std::pair<std::wstring, std::wstring> folder{L"PSX_USER_DIR", data.wstring()};
    std::wstring environment = environment_block({&folder, 1});
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    std::error_code kept;  // a report takes the run before as well: a crash is often seen after a second start
    fs::rename(data / "startup.log", data / "startup.previous.log", kept);
    HANDLE log = CreateFileW((data / "startup.log").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &security, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE || input == INVALID_HANDLE_VALUE) {
        if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
        throw std::runtime_error("Cannot create startup.log in " + utf8(data) + ". Check that the folder can be written.");
    }
    std::string heading = started_line();
    for (const auto line : std::views::split(system_note(), std::string_view("\r\n")))
        if (!line.empty()) heading += "launcher: " + std::string(line.begin(), line.end()) + "\r\n";
    DWORD written = 0;
    WriteFile(log, heading.data(), static_cast<DWORD>(heading.size()), &written, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input;
    startup.hStdOutput = startup.hStdError = log;
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, environment.data(),
                                        program.c_str(), &startup, &process);
    const DWORD failure = GetLastError();
    CloseHandle(log);
    CloseHandle(input);
    if (!started)
        throw std::runtime_error("Windows could not start DisruptorRecompiled.exe (error " +
            std::to_string(failure) + ").\n"
            "Check that the complete build is extracted and the Microsoft Visual C++ x64 Redistributable is installed.\n"
            "Help in the side bar has the steps.");
    CloseHandle(process.hThread);
    return process.hProcess;
}

} // namespace

fs::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("Cannot find the launcher's folder.");
    buffer.resize(size);
    return fs::path(buffer).parent_path();
}

void check_package(const fs::path& program, const DiscProfile& profile) {
    for (const auto& name : {fs::path(L"DisruptorRecompiled.exe"), fs::path(profile.game_config),
                             fs::path(L"bios/openbios.bin")}) {
        if (!fs::is_regular_file(program / name))
            throw std::runtime_error("The build is incomplete.\n"
                                     "Use Extract All on the release ZIP and keep the launcher, game executable, "
                                     "game.toml, and bios folder together.");
    }
    if (fs::file_size(program / "bios/openbios.bin") != 524288)
        throw std::runtime_error("The bundled OpenBIOS is incomplete. Extract the complete release again.");
}

std::wstring environment_block(std::span<const std::pair<std::wstring, std::wstring>> given,
                               std::span<const wchar_t* const> dropped) {
    std::wstring block;
    wchar_t* strings = GetEnvironmentStringsW();
    for (const wchar_t* line = strings; line && *line; line += std::wcslen(line) + 1) {
        const std::wstring_view text(line);
        const std::wstring name(text.substr(0, text.find(L'=', 1)));
        const auto named = [&](const wchar_t* other) { return _wcsicmp(name.c_str(), other) == 0; };
        if (std::ranges::any_of(given, [&](const auto& pair) { return named(pair.first.c_str()); }) ||
            std::ranges::any_of(dropped, named))
            continue;
        block.append(text);
        block.push_back(L'\0');
    }
    if (strings) FreeEnvironmentStringsW(strings);
    for (const auto& [name, value] : given) {
        block.append(name).append(L"=").append(value);
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

GameProcess::GameProcess(const fs::path& program, const fs::path& data, const VerifiedDisc& disc)
    : process_(start_game(program, data, disc)), started_(GetTickCount64()) {}

GameProcess::~GameProcess() { CloseHandle(process_); }

std::optional<unsigned long> GameProcess::ended() const {
    // Asked of the handle and not of the code: a game may exit with 259, which reads as STILL_ACTIVE.
    if (WaitForSingleObject(process_, 0) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 1;
    GetExitCodeProcess(process_, &code);
    return code;
}

unsigned long long GameProcess::seconds() const { return (GetTickCount64() - started_) / 1000; }

void open_folder(const fs::path& folder) {
    ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void show_in_explorer(const fs::path& file) {
    const std::wstring shown = L"/select,\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", shown.c_str(), nullptr, SW_SHOWNORMAL);
}

} // namespace disruptor::launcher
