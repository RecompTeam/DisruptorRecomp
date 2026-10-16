#pragma once

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>

namespace disruptor::launcher {

// A region needs its own verified image and runtime configuration: a serial alone enables nothing.
struct DiscProfile {
    const char* id;
    const wchar_t* label;
    uint64_t size;
    const char* sha256;
    const wchar_t* game_config;
};

std::span<const DiscProfile> supported_discs();
using Progress = std::function<void(const std::wstring&, unsigned)>;

struct VerifiedDisc {
    const DiscProfile* profile = nullptr;
    std::filesystem::path data;
    std::filesystem::path cue;
    // Held while the game runs: no writes or deletion between verification and start.
    std::shared_ptr<void> read_lock;
};

std::filesystem::path resolve_image(const std::filesystem::path& selected);
VerifiedDisc verify_disc(const std::filesystem::path& selected,
                         const std::atomic_bool& cancelled,
                         const Progress& progress = {},
                         std::span<const DiscProfile> profiles = supported_discs());
VerifiedDisc import_disc(const std::filesystem::path& selected,
                         const std::filesystem::path& package,
                         const std::atomic_bool& cancelled,
                         const Progress& progress = {},
                         std::span<const DiscProfile> profiles = supported_discs());
std::filesystem::path installed_cue(const std::filesystem::path& package,
                                    const DiscProfile& profile);
std::filesystem::path find_installed_disc(const std::filesystem::path& package);
std::wstring error_text(const std::exception& error);

} // namespace disruptor::launcher
