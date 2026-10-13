#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace disruptor::launcher {

// The last lines of the game's startup.log, empty when there is none.
std::wstring log_tail(const std::filesystem::path& log, size_t lines);

// Adds the launcher's line for a game that ended to its log: the exit code and how long it ran.
void note_exit(const std::filesystem::path& log, unsigned long code, unsigned long long seconds);

} // namespace disruptor::launcher
