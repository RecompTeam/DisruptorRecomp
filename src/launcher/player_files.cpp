#include "player_files.h"

#include "disc_import.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr std::size_t kept_backups = 5;
constexpr std::array<std::string_view, 10> earlier_entries = {
    "settings.toml", "keybinds.ini", "input.ini", "launcher.ini", "mouse-aim.ini", "saves", "capsules", "mods",
    "input/Disruptor (USA).cue", "input/Disruptor (USA).bin"};

std::string text(const fs::path& path) {
    const std::u8string name = path.u8string();
    return {name.begin(), name.end()};
}

// What is at `path`, a link itself and not what it points to. Throws when that cannot be told.
fs::file_type kind(const fs::path& path) {
    std::error_code failed;
    const fs::file_status status = fs::symlink_status(path, failed);
    if (status.type() == fs::file_type::not_found) return fs::file_type::not_found;
    if (failed) throw fs::filesystem_error("Cannot look at", path, failed);
    return status.type();
}

bool present(const fs::path& path) { return kind(path) != fs::file_type::not_found; }

std::optional<std::string> whole(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(file)), {});
}

// True when `inner` is `outer` or lies in it, links and other spellings of one folder seen through.
bool within(const fs::path& inner, const fs::path& outer) {
    std::error_code failed;
    for (fs::path folder = fs::weakly_canonical(inner, failed); !failed && !folder.empty(); folder = folder.parent_path()) {
        if (fs::equivalent(folder, outer, failed)) return true;
        failed.clear();
        if (folder == folder.parent_path()) break;
    }
    return false;
}

// Plain files and folders only: a link is neither followed nor brought.
std::uintmax_t size_of(const fs::path& path) {
    const fs::file_type type = kind(path);
    if (type == fs::file_type::regular) return fs::file_size(path);
    if (type != fs::file_type::directory) return 0;
    std::uintmax_t bytes = 0;
    for (const auto& entry : fs::directory_iterator(path)) bytes += size_of(entry.path());
    return bytes;
}

struct Copying {
    const std::atomic_bool& cancelled;
    const CopyProgress& progress;
    std::uintmax_t total;
    std::uintmax_t copied = 0;
    std::vector<char> buffer = std::vector<char>(4 << 20);
};

void check(const Copying& copying) {
    if (copying.cancelled) throw std::runtime_error("Cancelled. Nothing was brought over.");
}

void copy_file(const fs::path& from, const fs::path& to, Copying& copying) {
    std::ifstream input(from, std::ios::binary);
    std::ofstream output(to, std::ios::binary | std::ios::trunc);
    if (!input || !output) throw std::runtime_error("Cannot copy " + text(from) + " to " + text(to) + ".");
    while (input) {
        check(copying);
        input.read(copying.buffer.data(), static_cast<std::streamsize>(copying.buffer.size()));
        const std::streamsize got = input.gcount();
        if (got <= 0) break;
        output.write(copying.buffer.data(), got);
        if (!output) throw std::runtime_error("Cannot write " + text(to) + ". Is the disc full?");
        copying.copied += static_cast<std::uintmax_t>(got);
        if (copying.progress) copying.progress(copying.copied, copying.total);
    }
    if (input.bad()) throw std::runtime_error("Cannot read " + text(from) + ".");
    output.close();
    if (output.fail()) throw std::runtime_error("Cannot write " + text(to) + ". Is the disc full?");
}

void copy_tree(const fs::path& from, const fs::path& to, Copying& copying) {
    const fs::file_type type = kind(from);
    if (type == fs::file_type::regular) {
        fs::create_directories(to.parent_path());
        copy_file(from, to, copying);
    } else if (type == fs::file_type::directory) {
        fs::create_directories(to);
        for (const auto& entry : fs::directory_iterator(from)) copy_tree(entry.path(), to / entry.path().filename(), copying);
    }
}

void move(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    std::error_code failed;
    fs::rename(from, to, failed);
    if (failed) throw std::runtime_error("Cannot move " + text(from) + " to " + text(to) + ": " + failed.message());
}

// Two real folders merge entry by entry. Anything else takes the place of what is there, which is kept in `replaced`.
void merge(const fs::path& from, const fs::path& to, const fs::path& replaced) {
    const fs::file_type there = kind(to);
    if (kind(from) == fs::file_type::directory && there == fs::file_type::directory) {
        for (const auto& entry : fs::directory_iterator(from))
            merge(entry.path(), to / entry.path().filename(), replaced / entry.path().filename());
        fs::remove(from);
        return;
    }
    if (there != fs::file_type::not_found) move(to, replaced);
    move(from, to);
}

std::vector<fs::path> made_backups(const fs::path& backups, std::string_view prefix) {
    std::vector<fs::path> made;
    if (!present(backups)) return made;
    for (const auto& entry : fs::directory_iterator(backups)) {
        const std::string name = entry.path().filename().string();
        if (entry.symlink_status().type() == fs::file_type::directory && name.starts_with(prefix) && !name.ends_with(".partial"))
            made.push_back(entry.path());
    }
    std::sort(made.begin(), made.end());
    return made;
}

// `wanted`, or `wanted-2` and on when a folder of that name is there already: nothing kept earlier is written into.
fs::path unused(const fs::path& wanted) {
    fs::path chosen = wanted;
    for (int number = 2; present(chosen); ++number) {
        chosen = wanted;
        chosen += "-" + std::to_string(number);
    }
    return chosen;
}

void keep_newest(const fs::path& backups, std::string_view prefix) {
    const std::vector<fs::path> made = made_backups(backups, prefix);
    for (std::size_t index = 0; index + kept_backups < made.size(); ++index) fs::remove_all(made[index]);
}

} // namespace

std::string installed_version(const fs::path& program) {
    std::ifstream file(program / "sq.version", std::ios::binary);
    const std::string read((std::istreambuf_iterator<char>(file)), {});
    const auto begin = read.find("<version>");
    const auto end = read.find("</version>");
    if (begin == std::string::npos || end == std::string::npos || end < begin) return {};
    return read.substr(begin + 9, end - begin - 9);
}

fs::path back_up_saves(const fs::path& data, std::string_view version, std::string_view stamp) {
    if (!present(data / "saves")) return {};
    const fs::path backups = data / "backups";
    const fs::path copy = backups / ("saves-" + std::string(stamp) + "-" + std::string(version.empty() ? "unknown" : version));
    fs::path partial = copy;
    partial += ".partial";
    fs::remove_all(partial);
    fs::create_directories(backups);
    fs::copy(data / "saves", partial, fs::copy_options::recursive | fs::copy_options::copy_symlinks);
    fs::rename(partial, copy);
    keep_newest(backups, "saves-");
    return copy;
}

EarlierCopy look_at_copy(const fs::path& chosen, const fs::path& program, const fs::path& data) {
    EarlierCopy copy{fs::absolute(chosen).lexically_normal()};
    if (present(copy.folder / ".portable") && kind(copy.folder / "data") == fs::file_type::directory) copy.folder /= "data";
    if (kind(copy.folder) != fs::file_type::directory) throw std::runtime_error(text(copy.folder) + " is not a folder.");
    if (within(copy.folder, data) || within(data, copy.folder) || within(copy.folder, program) || within(program, copy.folder))
        throw std::runtime_error("That is this copy's own folder. Choose the folder of the earlier copy.");
    const bool disc_here = !find_installed_disc(data).empty();
    std::vector<fs::path> names(earlier_entries.begin(), earlier_entries.end());
    for (const DiscProfile& profile : supported_discs())
        names.push_back(installed_cue(fs::path(), profile).parent_path());
    for (const fs::path& name : names) {
        const fs::path found = copy.folder / name;
        if (!present(found)) continue;
        if (disc_here && *name.begin() == "input") continue;
        if (name == "mouse-aim.ini") {
            const auto theirs = whole(found);
            if (theirs && theirs == whole(program / name)) continue;
        }
        copy.entries.push_back(name);
        copy.bytes += size_of(found);
    }
    if (copy.entries.empty())
        throw std::runtime_error("There are no saves or settings in " + text(copy.folder) +
                                 ". Choose the folder that holds the earlier copy's saves folder.");
    return copy;
}

Brought bring_over(const EarlierCopy& copy, const fs::path& data, std::string_view stamp,
                   const std::atomic_bool& cancelled, const CopyProgress& progress) {
    const fs::path staging = unused(data / (".bring-" + std::string(stamp)));
    Copying copying{cancelled, progress, copy.bytes};
    try {
        for (const fs::path& entry : copy.entries) copy_tree(copy.folder / entry, staging / entry, copying);
        check(copying);
    } catch (...) {
        std::error_code failed;
        fs::remove_all(staging, failed);
        throw;
    }
    Brought brought;
    const fs::path replaced = unused(data / "backups" / ("replaced-" + std::string(stamp)));
    try {
        for (const fs::path& entry : copy.entries) {
            merge(staging / entry, data / entry, replaced / entry);
            brought.entries.push_back(entry);
        }
    } catch (const std::exception& error) {
        throw std::runtime_error(std::string(error.what()) + ". What did not come over is in " + text(staging) +
                                 (present(replaced) ? ", and the files it took the place of are in " + text(replaced) : std::string()) + ".");
    }
    fs::remove_all(staging);
    if (present(replaced)) brought.replaced = replaced;
    return brought;
}

} // namespace disruptor::launcher
