#pragma once

#include "disc_import.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>

namespace disruptor::launcher {

namespace platform {
class Process;
}

std::filesystem::path executable_directory();
// Throws with what the player should do when a file the game needs is missing.
void check_package(const std::filesystem::path& program, const DiscProfile& profile);

// The game started from the program folder with the player's files in `data`, its output in data/startup.log.
class GameProcess {
public:
    GameProcess(const std::filesystem::path& program, const std::filesystem::path& data, const VerifiedDisc& disc);
    ~GameProcess();
    GameProcess(const GameProcess&) = delete;
    GameProcess& operator=(const GameProcess&) = delete;

    std::optional<unsigned long> ended() const;
    unsigned long long seconds() const;

private:
    std::unique_ptr<platform::Process> process_;
    std::chrono::steady_clock::time_point started_;
};

} // namespace disruptor::launcher
