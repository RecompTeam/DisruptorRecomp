#pragma once

#include "disc_import.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace disruptor::launcher {

std::filesystem::path executable_directory();
// Throws with what the player should do when a file the game needs is missing.
void check_package(const std::filesystem::path& program, const DiscProfile& profile);
// This process's environment for CreateProcessW, with `given` set and `dropped` taken out.
std::wstring environment_block(std::span<const std::pair<std::wstring, std::wstring>> given,
                               std::span<const wchar_t* const> dropped = {});

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
    void* process_;
    unsigned long long started_;
};

void open_folder(const std::filesystem::path& folder);
void show_in_explorer(const std::filesystem::path& file);

} // namespace disruptor::launcher
