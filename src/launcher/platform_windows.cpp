#include "platform.h"

#include "launcher_flow.h"
#include "launcher_settings.h"
#include "report.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace disruptor::launcher::platform {

namespace {

class Handle {
public:
    explicit Handle(HANDLE value = nullptr) : value_(value == INVALID_HANDLE_VALUE ? nullptr : value) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE get() const { return value_; }
    void reset(HANDLE value = nullptr) {
        if (value_) CloseHandle(value_);
        value_ = value == INVALID_HANDLE_VALUE ? nullptr : value;
    }

private:
    HANDLE value_;
};

std::system_error last_error(const std::string& what) {
    return {static_cast<int>(GetLastError()), std::system_category(), what};
}

std::wstring block_of(const Variables& variables) {
    std::wstring block;
    for (const auto& [name, value] : variables) {
        block.append(wide(name)).append(L"=").append(wide(value));
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

std::string registry_text(const wchar_t* name) {
    std::array<wchar_t, 128> value{};
    DWORD bytes = static_cast<DWORD>(sizeof(wchar_t) * (value.size() - 1));
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name, RRF_RT_REG_SZ, nullptr,
                     value.data(), &bytes) != ERROR_SUCCESS)
        return "?";
    return narrow(value.data());
}

} // namespace

struct Process::State {
    Handle process;
    Handle job;
};

Variables environment() {
    Variables variables;
    wchar_t* const strings = GetEnvironmentStringsW();
    for (const wchar_t* line = strings; line && *line; line += std::wcslen(line) + 1) {
        const std::wstring_view text(line);
        const std::size_t equals = text.find(L'=', 1);
        variables.emplace_back(narrow(text.substr(0, equals)), equals == std::wstring_view::npos ? std::string() : narrow(text.substr(equals + 1)));
    }
    if (strings) FreeEnvironmentStringsW(strings);
    return variables;
}

Variables with(Variables base, const Variables& given, const std::vector<std::string>& dropped) {
    const auto same = [](const std::string& one, const std::string& other) { return _wcsicmp(wide(one).c_str(), wide(other).c_str()) == 0; };
    std::erase_if(base, [&](const auto& pair) {
        return std::ranges::any_of(given, [&](const auto& set) { return same(pair.first, set.first); }) ||
               std::ranges::any_of(dropped, [&](const std::string& name) { return same(pair.first, name); });
    });
    base.insert(base.end(), given.begin(), given.end());
    return base;
}

Process::Process(const Launch& launch) : state_(std::make_unique<State>()) {
    std::wstring command = quote(launch.executable.wstring());
    for (const std::string& argument : launch.arguments) command += L" " + quote(wide(argument));
    std::wstring environment = block_of(launch.environment);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    const Handle output(launch.output.empty()
                            ? CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr)
                            : CreateFileW(launch.output.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                                          FILE_ATTRIBUTE_NORMAL, nullptr));
    const Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!output.get() || !input.get()) throw last_error("cannot open " + (launch.output.empty() ? std::string("NUL") : utf8(launch.output)));
    DWORD written = 0;
    if (!launch.heading.empty())
        WriteFile(output.get(), launch.heading.data(), static_cast<DWORD>(launch.heading.size()), &written, nullptr);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get();
    startup.hStdOutput = startup.hStdError = output.get();
    PROCESS_INFORMATION process{};
    const DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT | (launch.ends_with_launcher ? CREATE_SUSPENDED : 0);
    if (!CreateProcessW(launch.executable.c_str(), command.data(), nullptr, nullptr, TRUE, flags, environment.data(),
                        launch.folder.empty() ? nullptr : launch.folder.c_str(), &startup, &process))
        throw last_error("cannot start " + utf8(launch.executable.filename()));
    state_->process.reset(process.hProcess);
    const Handle thread(process.hThread);
    if (!launch.ends_with_launcher) return;
    // The job ends the program with the launcher, should the launcher itself end first.
    state_->job.reset(CreateJobObjectW(nullptr, nullptr));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!state_->job.get() || !SetInformationJobObject(state_->job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
        !AssignProcessToJobObject(state_->job.get(), state_->process.get())) {
        const std::system_error failed = last_error("cannot hold " + utf8(launch.executable.filename()) + " to the launcher's life");
        TerminateProcess(state_->process.get(), 1);
        WaitForSingleObject(state_->process.get(), 10000);
        throw failed;
    }
    ResumeThread(thread.get());
}

Process::~Process() = default;

std::optional<int> Process::ended() { return wait(std::chrono::milliseconds(0)); }

std::optional<int> Process::wait(std::chrono::milliseconds most) {
    // Asked of the handle and not of the code: a program may exit with 259, which reads as STILL_ACTIVE.
    if (WaitForSingleObject(state_->process.get(), static_cast<DWORD>(most.count())) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 1;
    GetExitCodeProcess(state_->process.get(), &code);
    return static_cast<int>(code);
}

bool Process::stop() {
    if (state_->job.get()) TerminateJobObject(state_->job.get(), 1);
    else TerminateProcess(state_->process.get(), 1);
    return WaitForSingleObject(state_->process.get(), 10000) == WAIT_OBJECT_0;
}

fs::path executable_path() {
    std::wstring buffer(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!size || size >= buffer.size()) throw std::runtime_error("Cannot find the launcher's folder");
    buffer.resize(size);
    return fs::path(buffer);
}

fs::path game_executable(const fs::path& program) { return program / "DisruptorRecompiled.exe"; }

Moved move_new(const fs::path& from, const fs::path& to) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH)) return Moved::done;
    const DWORD error = GetLastError();
    return error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? Moved::taken : Moved::failed;
}

void open_folder(const fs::path& folder) { ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

void show_file(const fs::path& file) {
    const std::wstring shown = L"/select,\"" + file.wstring() + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", shown.c_str(), nullptr, SW_SHOWNORMAL);
}

void write_standard(std::string_view text, bool error) {
    DWORD written = 0;
    WriteFile(GetStdHandle(error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE), text.data(), static_cast<DWORD>(text.size()),
              &written, nullptr);
}

void unset(const std::string& name) { SetEnvironmentVariableW(wide(name).c_str(), nullptr); }

bool animations_enabled() {
    BOOL enabled = TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled;
}

Instance::Instance(const fs::path& data) {
    std::wstring key = fs::weakly_canonical(data).wstring();
    for (wchar_t& letter : key) letter = static_cast<wchar_t>(std::towlower(letter));
    const std::wstring name = L"Local\\DisruptorLauncher-" + std::to_wstring(std::hash<std::wstring>{}(key));
    held_ = CreateMutexW(nullptr, TRUE, name.c_str());
    first_ = held_ && GetLastError() != ERROR_ALREADY_EXISTS;
}

Instance::~Instance() {
    if (held_) CloseHandle(static_cast<HANDLE>(held_));
}

std::vector<std::string> system_lines() {
    std::vector<std::string> lines;
    lines.push_back("Windows: " + windows_name(registry_text(L"ProductName"), registry_text(L"CurrentBuildNumber")) + ", version " +
                    registry_text(L"DisplayVersion") + ", build " + registry_text(L"CurrentBuildNumber"));
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
        lines.push_back("Display: " + std::to_string(mode.dmPelsWidth) + " by " + std::to_string(mode.dmPelsHeight) + " at " +
                        std::to_string(mode.dmDisplayFrequency) + " Hz");
    std::set<std::string> adapters;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
        if (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) adapters.insert(narrow(device.DeviceString));
    for (const auto& adapter : adapters) lines.push_back("Graphics: " + adapter);
    return lines;
}

} // namespace disruptor::launcher::platform
