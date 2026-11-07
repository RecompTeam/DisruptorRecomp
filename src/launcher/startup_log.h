#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace disruptor::launcher {

// How the game's log begins the line it writes once its first picture is on the screen.
inline constexpr std::string_view first_frame_line = "disruptor: first frame ";

// Reads what the game adds to its startup.log until the line of its first picture comes.
class FirstFrameWatch {
public:
    // True from the call that finds the line on. A log not there yet, or shorter than before, is read from its start.
    bool seen(const std::filesystem::path& log);

private:
    std::uintmax_t read_ = 0;
    std::string line_; // the line not ended yet
    bool seen_ = false;
};

// The last lines of the game's startup.log, empty when there is none.
std::wstring log_tail(const std::filesystem::path& log, size_t lines);

// Adds the launcher's line for a game that ended to its log: the exit code and how long it ran.
void note_exit(const std::filesystem::path& log, unsigned long code, unsigned long long seconds);

} // namespace disruptor::launcher
