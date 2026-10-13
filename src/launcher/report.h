#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace disruptor::launcher {

/// `text` with the name of every Windows user folder cut out of its paths: `C:\Users\Name\x` becomes `C:\Users\<user>\x`.
std::string without_user_names(std::string_view text);

struct Report {
    std::filesystem::path path;  // the archive, or the folder where no archive could be made
    bool archived = false;
};

/// Gathers what a bug report needs from the build folder `root` into `reports\disruptor-report-<stamp>`.
///
/// Taken are the game's log of the last two runs, the mouse log and the settings, each with user names cut out, and
/// `system` as `system.txt`. The disc image, the BIOS and saves are never taken. With `archive` the folder becomes a
/// ZIP of the same name when Windows' own tar can make one.
///
/// Throws std::runtime_error when the folder cannot be written.
Report make_report(const std::filesystem::path& root, std::string_view system, std::string_view stamp, bool archive = true);

/// The build's version, the Windows version, the display and the graphics adapters, a line each.
std::string system_note();

/// The local time as `YYYYMMDD-HHMMSS`, for a report's name.
std::string report_stamp();

} // namespace disruptor::launcher
