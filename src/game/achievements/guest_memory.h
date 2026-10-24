#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace disruptor::achievements {

// RetroAchievements' PlayStation addresses: the 2 MB of main RAM from 0, then the 1 KB scratchpad.
inline constexpr std::uint32_t kRamSize = 0x200000;
inline constexpr std::uint32_t kScratchpadAddress = 0x200000;
inline constexpr std::uint32_t kScratchpadSize = 0x400;

// Copies up to `count` bytes at an RA address, across the seam between the two, and returns how many it copied.
std::uint32_t read_guest(std::uint32_t address, std::uint8_t* buffer, std::uint32_t count,
                         std::span<const std::uint8_t> ram, std::span<const std::uint8_t> scratchpad);

// The 2048 bytes of user data of a disc's sectors, by absolute sector.
class DiscSectors {
public:
    virtual ~DiscSectors() = default;
    virtual bool read(std::uint32_t sector, std::uint8_t* user_data) = 0;
    virtual std::uint32_t first_sector() = 0;
};

// rc_hash's PlayStation hash: the boot file's name as SYSTEM.CNF writes it, then the executable.
[[nodiscard]] std::optional<std::string> playstation_hash(DiscSectors& disc);

} // namespace disruptor::achievements
