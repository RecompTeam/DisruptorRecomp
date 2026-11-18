#pragma once

#include "disc_import.h"
#include "region_disc.h"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace disruptor::launcher {

namespace platform {
class Process;
}

std::filesystem::path executable_directory();
// Throws with what the player should do when a file the game needs is missing.
void check_package(const std::filesystem::path& program, const DiscProfile& profile);
void check_disc_location(const std::filesystem::path& program, const std::filesystem::path& mount);
VerifiedDisc import_player_disc(const std::filesystem::path& program, const std::filesystem::path& data,
                                const std::filesystem::path& selected, const std::atomic_bool& cancelled,
                                const Progress& progress = {});
std::filesystem::path checked_installed_disc(const std::filesystem::path& program, const std::filesystem::path& data);

// The game started from the program folder with the player's files in `data`, its output in data/startup.log.
class GameProcess {
public:
    GameProcess(const std::filesystem::path& program, const std::filesystem::path& data, const VerifiedDisc& disc,
                const std::optional<RegionDisc>& language);
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
