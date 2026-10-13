#include "report.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>
#include <fstream>
#include <set>
#include <stdexcept>
#include <system_error>

#ifndef DISRUPTOR_BUILD_VERSION
#define DISRUPTOR_BUILD_VERSION "unknown"
#endif

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {

constexpr std::array<const char*, 4> gathered = {"startup.log", "startup.previous.log", "disruptor-mouse-aim.log", "settings.toml"};
constexpr std::streamoff most_bytes = 4 * 1024 * 1024;  // of a log's end: GitHub takes attachments of 25 MB

bool separates(char letter) { return letter == '\\' || letter == '/'; }

std::string end_of(const fs::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const std::streamoff size = input.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - most_bytes);
    std::string text(static_cast<size_t>(size - start), '\0');
    input.seekg(start);
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0)));
    if (start > 0) text.erase(0, std::min(text.find('\n'), text.size() - 1) + 1);  // a line cut in two could begin inside a user's name
    return text;
}

void write(const fs::path& file, std::string_view text) {
    std::ofstream output(file, std::ios::binary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("Cannot write the report. Put the extracted build in a writable Games folder.");
}

bool run_tar(const fs::path& folder, const fs::path& archive) {
    std::wstring system(MAX_PATH, L'\0');
    system.resize(GetSystemDirectoryW(system.data(), MAX_PATH));
    const fs::path tar = fs::path(system) / "tar.exe";
    std::error_code missing;
    if (!fs::exists(tar, missing)) return false;
    std::wstring command = L"\"" + tar.wstring() + L"\" -a -c -f \"" + archive.filename().wstring() + L"\" \"" +
                           folder.filename().wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(tar.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        folder.parent_path().c_str(), &startup, &process))
        return false;
    DWORD code = 1;
    const bool ended = WaitForSingleObject(process.hProcess, 60000) == WAIT_OBJECT_0;
    if (ended) GetExitCodeProcess(process.hProcess, &code);
    else if (TerminateProcess(process.hProcess, 1)) WaitForSingleObject(process.hProcess, 5000);  // its half-made ZIP is open until it is gone
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    std::error_code unreadable;
    return ended && code == 0 && fs::file_size(archive, unreadable) > 0 && !unreadable;
}

std::string narrow(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::string windows_value(const wchar_t* name) {
    std::array<wchar_t, 128> value{};
    DWORD bytes = static_cast<DWORD>(sizeof(wchar_t) * (value.size() - 1));
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", name, RRF_RT_REG_SZ, nullptr,
                     value.data(), &bytes) != ERROR_SUCCESS)
        return "?";
    return narrow(value.data());
}

} // namespace

std::string without_user_names(std::string_view text) {
    constexpr std::string_view folder = "users";
    std::string out;
    out.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        const bool begins = at > 0 && separates(text[at - 1]) && text.size() - at > folder.size() &&
                            std::equal(folder.begin(), folder.end(), text.begin() + static_cast<std::ptrdiff_t>(at),
                                       [](char wanted, char found) { return std::tolower(static_cast<unsigned char>(found)) == wanted; }) &&
                            separates(text[at + folder.size()]);
        if (!begins) {
            out += text[at++];
            continue;
        }
        size_t name = at + folder.size();
        while (name < text.size() && separates(text[name])) ++name;
        size_t end = name;
        while (end < text.size() && !separates(text[end]) && text[end] != '"' && text[end] != '\r' && text[end] != '\n') ++end;
        out.append(text, at, name - at);
        if (end > name) out += "<user>";
        at = end;
    }
    return out;
}

Report make_report(const fs::path& root, std::string_view system, std::string_view stamp, bool archive) {
    const fs::path folder = root / "reports" / ("disruptor-report-" + std::string(stamp));
    std::error_code failed;
    fs::remove_all(folder, failed);  // one of the same second: what tar packs is what is written here and nothing older
    fs::create_directories(folder, failed);
    write(folder / "system.txt", without_user_names(system));
    for (const char* name : gathered) {
        std::error_code missing;
        if (fs::is_regular_file(root / name, missing)) write(folder / name, without_user_names(end_of(root / name)));
    }
    const fs::path zip = fs::path(folder).concat(".zip");
    if (!archive || !run_tar(folder, zip)) {
        fs::remove(zip, failed);
        return {folder, false};
    }
    fs::remove_all(folder, failed);
    return {zip, true};
}

std::string system_note() {
    std::string note = "Disruptor Recompiled " DISRUPTOR_BUILD_VERSION "\r\n";
    note += "Windows: " + windows_value(L"ProductName") + ", version " + windows_value(L"DisplayVersion") + ", build " +
            windows_value(L"CurrentBuildNumber") + "\r\n";
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
        note += "Display: " + std::to_string(mode.dmPelsWidth) + " by " + std::to_string(mode.dmPelsHeight) + " at " +
                std::to_string(mode.dmDisplayFrequency) + " Hz\r\n";
    std::set<std::string> adapters;
    DISPLAY_DEVICEW device{};
    device.cb = sizeof(device);
    for (DWORD index = 0; EnumDisplayDevicesW(nullptr, index, &device, 0); ++index)
        if (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) adapters.insert(narrow(device.DeviceString));
    for (const auto& adapter : adapters) note += "Graphics: " + adapter + "\r\n";
    return note;
}

std::string report_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::array<char, 32> text{};
    std::strftime(text.data(), text.size(), "%Y%m%d-%H%M%S", &local);
    return text.data();
}

} // namespace disruptor::launcher
