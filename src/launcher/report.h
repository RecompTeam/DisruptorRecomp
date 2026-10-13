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
/// Taken are the game's log of the last two runs, the mouse log, the settings, the framework's notes of how the last
/// run ended and of its freezes, each with user names cut out, and `system` as `system.txt` with a line for every
/// file: its size and when it was written, or that there is none. A freeze dump is taken when its name says it was
/// made since the last run began, as the log's first line tells, and the newest two of those. A link is never
/// followed. The disc image, the BIOS and saves are never taken. With `archive` the folder becomes a ZIP of the
/// same name when Windows' own tar can make one.
///
/// Throws std::runtime_error when the folder cannot be written.
Report make_report(const std::filesystem::path& root, std::string_view system, std::string_view stamp, bool archive = true);

/// Windows' own product name with the 10 of a Windows 11 put right: the registry still says 10 there, and its builds
/// begin at 22000.
std::string windows_name(std::string_view product, std::string_view build);

/// The build's version, the Windows version, the display and the graphics adapters, a line each.
std::string system_note();

/// Now as `YYYY-MM-DD HH:MM:SS UTC`.
std::string utc_now();

/// The line the launcher heads the game's log with: when the run began. A report tells the run's freeze dumps by it.
std::string started_line();

/// The local time as `YYYYMMDD-HHMMSS`, for a report's name.
std::string report_stamp();

} // namespace disruptor::launcher
