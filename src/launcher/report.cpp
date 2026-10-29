#include "report.h"

#include "build_version.h"
#include "launcher_settings.h"
#include "platform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {

constexpr std::array<const char*, 6> gathered = {"startup.log", "startup.previous.log", "disruptor-mouse-aim.log", "settings.toml",
                                                 "psx_last_run_report.json", "psx_freeze_heartbeat.json"};
constexpr std::string_view freeze_dump = "psx_freeze_dump_";
constexpr size_t most_freeze_dumps = 2;
constexpr std::string_view started = "launcher: started ";
constexpr std::streamoff most_bytes = 4 * 1024 * 1024;  // of a log's end: GitHub takes attachments of 25 MB
// Folders whose next part is a user's name: Windows' C:\Users\<name>, Linux's /home/<name>, macOS's /Users/<name>.
constexpr std::array<std::string_view, 2> user_parents = {"users", "home"};

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
    if (!output) throw std::runtime_error("Cannot write the report\nCheck that the folder of your files can be written");
}

std::uint32_t crc32(std::string_view bytes) {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char byte : bytes) {
        crc ^= static_cast<unsigned char>(byte);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void put16(std::string& out, std::uint32_t value) {
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void put32(std::string& out, std::uint32_t value) {
    put16(out, value & 0xFFFF);
    put16(out, value >> 16);
}

// The report's folder as a ZIP of stored files under the folder's own name, as every system's file manager opens.
bool write_zip(const fs::path& folder, const fs::path& archive) {
    std::vector<fs::path> files;
    std::error_code failed;
    for (fs::directory_iterator entry(folder, failed), end; !failed && entry != end; entry.increment(failed))
        if (entry->is_regular_file(failed)) files.push_back(entry->path());
    if (failed) return false;
    std::ranges::sort(files);
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    const std::uint32_t dos_time = static_cast<std::uint32_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
    const std::uint32_t dos_date = static_cast<std::uint32_t>(((std::max(local.tm_year, 80) - 80) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
    std::string zip, directory;
    for (const fs::path& file : files) {
        std::ifstream input(file, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (!input && !input.eof()) return false;
        const std::string name = utf8(folder.filename()) + "/" + utf8(file.filename());
        const std::uint32_t crc = crc32(bytes), size = static_cast<std::uint32_t>(bytes.size()), offset = static_cast<std::uint32_t>(zip.size());
        std::string header;
        put32(header, 0x04034B50);
        put16(header, 20);
        put16(header, 0x0800);  // the name is UTF-8
        put16(header, 0);       // stored
        put16(header, dos_time);
        put16(header, dos_date);
        put32(header, crc);
        put32(header, size);
        put32(header, size);
        put16(header, static_cast<std::uint32_t>(name.size()));
        put16(header, 0);
        zip += header + name + bytes;
        put32(directory, 0x02014B50);
        put16(directory, 20);
        directory.append(header, 4, 26);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, offset);
        directory += name;
    }
    std::string end;
    put32(end, 0x06054B50);
    put32(end, 0);
    put16(end, static_cast<std::uint32_t>(files.size()));
    put16(end, static_cast<std::uint32_t>(files.size()));
    put32(end, static_cast<std::uint32_t>(directory.size()));
    put32(end, static_cast<std::uint32_t>(zip.size()));
    put16(end, 0);
    std::ofstream output(archive, std::ios::binary);
    output << zip << directory << end;
    output.close();
    return static_cast<bool>(output);
}

std::string utc_text(std::chrono::system_clock::time_point time) {
    return std::format("{:%Y-%m-%d %H:%M:%S} UTC", std::chrono::floor<std::chrono::seconds>(time));
}

std::string listed(const fs::path& file) {
    std::error_code failed;
    const std::uintmax_t bytes = fs::file_size(file, failed);
    const fs::file_time_type written = fs::last_write_time(file, failed);
    if (failed) return "none";
    return std::to_string(bytes) + " bytes, written " + utc_text(std::chrono::clock_cast<std::chrono::system_clock>(written)) +
           (bytes > static_cast<std::uintmax_t>(most_bytes) ? ", its end taken" : "");
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
    const std::chrono::year_month_day day{std::chrono::year(parts[0]), std::chrono::month(static_cast<unsigned>(parts[1])),
                                          std::chrono::day(static_cast<unsigned>(parts[2]))};
    if (!day.ok() || parts[3] > 23 || parts[4] > 59 || parts[5] > 60) return std::nullopt;
    const auto began = std::chrono::sys_days(day) + std::chrono::hours(parts[3]) + std::chrono::minutes(parts[4]) +
                       std::chrono::seconds(parts[5]);
    const std::int64_t seconds = began.time_since_epoch().count();
    return seconds < 0 ? std::nullopt : std::optional(seconds);
}

// The unix time in a dump's name, which the framework writes as `psx_freeze_dump_<backend>_<seconds>.json`.
std::optional<std::int64_t> dump_time(std::string_view name) {
    constexpr std::string_view ending = ".json";
    if (!name.starts_with(freeze_dump) || !name.ends_with(ending)) return std::nullopt;
    const size_t end = name.size() - ending.size(), under = name.rfind('_', end);
    if (under < freeze_dump.size() || end - under < 2 || end - under > 13) return std::nullopt;
    std::int64_t seconds = 0;
    for (size_t at = under + 1; at < end; ++at) {
        if (name[at] < '0' || name[at] > '9') return std::nullopt;
        seconds = seconds * 10 + (name[at] - '0');
    }
    return seconds;
}

// The newest dumps made since the last run began: an older run's would be read as this one's.
std::vector<fs::path> freeze_dumps(const fs::path& root, std::int64_t began) {
    std::vector<std::pair<std::int64_t, fs::path>> found;
    std::error_code failed;
    for (fs::directory_iterator entry(root, failed), end; !failed && entry != end; entry.increment(failed)) {
        const fs::path name = entry->path().filename();
        const std::optional<std::int64_t> made = dump_time(utf8(name));
        if (made && *made >= began && plain_file(entry->path())) found.emplace_back(*made, name);
    }
    std::ranges::sort(found, std::greater{});
    if (found.size() > most_freeze_dumps) found.resize(most_freeze_dumps);
    std::vector<fs::path> names;
    for (auto& dump : found) names.push_back(std::move(dump.second));
    return names;
}

bool user_parent_at(std::string_view text, size_t at, std::string_view folder) {
    return at > 0 && separates(text[at - 1]) && text.size() - at > folder.size() &&
           std::equal(folder.begin(), folder.end(), text.begin() + static_cast<std::ptrdiff_t>(at),
                      [](char wanted, char found) { return std::tolower(static_cast<unsigned char>(found)) == wanted; }) &&
           separates(text[at + folder.size()]);
}

} // namespace

std::string without_user_names(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    size_t at = 0;
    while (at < text.size()) {
        const auto parent = std::ranges::find_if(user_parents, [&](std::string_view folder) { return user_parent_at(text, at, folder); });
        if (parent == user_parents.end()) {
            out += text[at++];
            continue;
        }
        size_t name = at + parent->size();
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
    fs::remove_all(folder, failed);  // one of the same second: what is packed is what is written here and nothing older
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
        note += utf8(name) + ": " + (present ? listed(root / name) : "none") + "\r\n";
        if (present) write(folder / name, without_user_names(end_of(root / name)));
    }
    write(folder / "system.txt", without_user_names(note));
    const fs::path zip = fs::path(folder).concat(".zip");
    if (!archive || !write_zip(folder, zip)) {
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
    for (const std::string& line : platform::system_lines()) note += line + "\r\n";
    return note;
}

std::string utc_now() { return utc_text(std::chrono::system_clock::now()); }

std::string started_line() { return std::string(started) + utc_now() + "\r\n"; }

std::string report_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::array<char, 32> text{};
    std::strftime(text.data(), text.size(), "%Y%m%d-%H%M%S", &local);
    return text.data();
}

} // namespace disruptor::launcher
