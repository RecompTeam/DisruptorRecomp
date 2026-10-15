#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::launcher {

// The version Velopack installed in `program`, from its sq.version. Empty when there is none.
std::string installed_version(const std::filesystem::path& program);
// Copies `data/saves` to `data/backups/saves-<stamp>-<version>`, whole or not at all, and keeps the newest five copies.
// Empty when there are no saves.
std::filesystem::path back_up_saves(const std::filesystem::path& data, std::string_view version, std::string_view stamp);

// The player's files of an earlier copy: an unzipped release or a build folder, or a portable copy's data folder.
struct EarlierCopy {
    std::filesystem::path folder;
    std::vector<std::filesystem::path> entries; // relative to `folder`, in the order they are brought
    std::uintmax_t bytes = 0;
};
// What `chosen` holds that is the player's: settings, key bindings, saves, recordings, mods and the imported disc. A
// mouse-aim.ini comes along only when it differs from the one `program` ships, and no disc when `data` has one. Throws
// when `chosen` is `data` or `program`, holds one or lies in one, or has nothing of the player's.
EarlierCopy look_at_copy(const std::filesystem::path& chosen, const std::filesystem::path& program,
                         const std::filesystem::path& data);

struct Brought {
    std::vector<std::filesystem::path> entries;
    std::filesystem::path replaced; // where the files they took the place of are kept, empty when none were
};
using CopyProgress = std::function<void(std::uintmax_t copied, std::uintmax_t total)>;
// Copies the copy's entries into a staging folder in `data`, then moves each into place. A file already in `data`
// that one takes the place of goes to `data/backups/replaced-<stamp>` first, a folder of its own that is never pruned.
// A cancel or a failed copy leaves `data` as it was. Throws with the staging folder named when a move fails.
Brought bring_over(const EarlierCopy& copy, const std::filesystem::path& data, std::string_view stamp,
                   const std::atomic_bool& cancelled, const CopyProgress& progress);

} // namespace disruptor::launcher
