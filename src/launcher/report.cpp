#include "report.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#ifndef DISRUPTOR_BUILD_VERSION
#define DISRUPTOR_BUILD_VERSION "unknown"
#endif

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {

constexpr std::array<const char*, 6> gathered = {"startup.log", "startup.previous.log", "disruptor-mouse-aim.log", "settings.toml",
                                                 "psx_last_run_report.json", "psx_freeze_heartbeat.json"};
constexpr std::wstring_view freeze_dump = L"psx_freeze_dump_";
constexpr size_t most_freeze_dumps = 2;
constexpr std::string_view started = "launcher: started ";
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

std::string utc_text(const SYSTEMTIME& time) {
    std::array<char, 40> text{};
    std::snprintf(text.data(), text.size(), "%04u-%02u-%02u %02u:%02u:%02u UTC", time.wYear, time.wMonth, time.wDay,
                  time.wHour, time.wMinute, time.wSecond);
    return text.data();
}

std::string listed(const fs::path& file) {
    WIN32_FILE_ATTRIBUTE_DATA found{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &found)) return "none";
    SYSTEMTIME written{};
    FileTimeToSystemTime(&found.ftLastWriteTime, &written);
    const std::uint64_t bytes = (std::uint64_t{found.nFileSizeHigh} << 32) | found.nFileSizeLow;
    return std::to_string(bytes) + " bytes, written " + utc_text(written) +
           (bytes > static_cast<std::uint64_t>(most_bytes) ? ", its end taken" : "");
}

bool plain_file(const fs::path& file) {
    std::error_code missing;
    return fs::is_regular_file(fs::symlink_status(file, missing));
}

// The unix time the last run began, from the line the launcher heads the log with.
std::optional<std::int64_t> run_began(const fs::path& log) {
    constexpr std::array<size_t, 6> places = {0, 5, 8, 11, 14, 17}, widths = {4, 2, 2, 2, 2, 2};  // YYYY-MM-DD HH:MM:SS
    std::string head(started.size() + 19, '\0');
    std::ifstream input(log, std::ios::binary);
    input.read(head.data(), static_cast<std::streamsize>(head.size()));
    if (!plain_file(log) || !head.starts_with(started)) return std::nullopt;
    std::array<int, 6> parts{};
    for (size_t part = 0; part < parts.size(); ++part)
        for (size_t at = 0; at < widths[part]; ++at) {
            const char digit = head[started.size() + places[part] + at];
            if (digit < '0' || digit > '9') return std::nullopt;
            parts[part] = parts[part] * 10 + (digit - '0');
        }
    std::tm time{};
    time.tm_year = parts[0] - 1900;
    time.tm_mon = parts[1] - 1;
    time.tm_mday = parts[2];
    time.tm_hour = parts[3];
    time.tm_min = parts[4];
    time.tm_sec = parts[5];
    const std::int64_t began = _mkgmtime64(&time);
    return began < 0 ? std::nullopt : std::optional(began);
}

// The unix time in a dump's name, which the framework writes as `psx_freeze_dump_<backend>_<seconds>.json`.
std::optional<std::int64_t> dump_time(const std::wstring& name) {
    constexpr std::wstring_view ending = L".json";
    if (!name.starts_with(freeze_dump) || !name.ends_with(ending)) return std::nullopt;
    const size_t end = name.size() - ending.size(), under = name.rfind(L'_', end);
    if (under < freeze_dump.size() || end - under < 2 || end - under > 13) return std::nullopt;
    std::int64_t seconds = 0;
    for (size_t at = under + 1; at < end; ++at) {
        if (name[at] < L'0' || name[at] > L'9') return std::nullopt;
        seconds = seconds * 10 + (name[at] - L'0');
    }
    return seconds;
}

// The newest dumps made since the last run began: an older run's would be read as this one's.
std::vector<fs::path> freeze_dumps(const fs::path& root, std::int64_t began) {
    std::vector<std::pair<std::int64_t, fs::path>> found;
    std::error_code failed;
    for (fs::directory_iterator entry(root, failed), end; !failed && entry != end; entry.increment(failed)) {
        const fs::path name = entry->path().filename();
        const std::optional<std::int64_t> made = dump_time(name.native());
        if (made && *made >= began && plain_file(entry->path())) found.emplace_back(*made, name);
    }
    std::ranges::sort(found, std::greater{});
    if (found.size() > most_freeze_dumps) found.resize(most_freeze_dumps);
    std::vector<fs::path> names;
    for (auto& dump : found) names.push_back(std::move(dump.second));
    return names;
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
    std::vector<fs::path> names(gathered.begin(), gathered.end());
    const std::optional<std::int64_t> began = run_began(root / "startup.log");
    if (began)
        for (auto& dump : freeze_dumps(root, *began)) names.push_back(std::move(dump));
    std::string note(system);
    if (!note.empty() && note.back() != '\n') note += "\r\n";
    note += "Report made " + utc_now() + "\r\n";
    if (!began) note += "Freeze dumps: none taken, the log does not say when its run began\r\n";
    for (const fs::path& name : names) {
        const bool present = plain_file(root / name);
        note += narrow(name.native()) + ": " + (present ? listed(root / name) : "none") + "\r\n";
        if (present) write(folder / name, without_user_names(end_of(root / name)));
    }
    write(folder / "system.txt", without_user_names(note));
    const fs::path zip = fs::path(folder).concat(".zip");
    if (!archive || !run_tar(folder, zip)) {
        fs::remove(zip, failed);
        return {folder, false};
    }
    fs::remove_all(folder, failed);
    return {zip, true};
}

std::string windows_name(std::string_view product, std::string_view build) {
    constexpr std::string_view ten = "Windows 10";
    std::string name(product);
    int number = 0;
    const auto read = std::from_chars(build.data(), build.data() + build.size(), number);
    if (read.ec == std::errc() && number >= 22000 && name.starts_with(ten)) name.replace(ten.size() - 2, 2, "11");
    return name;
}

std::string system_note() {
    std::string note = "Disruptor Recompiled " DISRUPTOR_BUILD_VERSION "\r\n";
    note += "Windows: " + windows_name(windows_value(L"ProductName"), windows_value(L"CurrentBuildNumber")) + ", version " +
            windows_value(L"DisplayVersion") + ", build " +
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

std::string utc_now() {
    SYSTEMTIME now{};
    GetSystemTime(&now);
    return utc_text(now);
}

std::string started_line() { return std::string(started) + utc_now() + "\r\n"; }

std::string report_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::array<char, 32> text{};
    std::strftime(text.data(), text.size(), "%Y%m%d-%H%M%S", &local);
    return text.data();
}

} // namespace disruptor::launcher
