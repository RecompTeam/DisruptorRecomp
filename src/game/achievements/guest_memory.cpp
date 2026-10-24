#include "game/achievements/guest_memory.h"

#include "rc_consoles.h"
#include "rc_hash.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace disruptor::achievements {
namespace {

constexpr std::size_t kUserData = 2048;
constexpr uint32_t kFirstTrack = 1;

DiscSectors* disc_of(const rc_hash_iterator* iterator) { return static_cast<DiscSectors*>(iterator->userdata); }

void* RC_CCONV open_track(const char*, uint32_t track, const rc_hash_iterator* iterator) {
    return track == kFirstTrack || track == RC_HASH_CDTRACK_FIRST_DATA ? disc_of(iterator) : nullptr;
}

size_t RC_CCONV read_sector(void* track, uint32_t sector, void* buffer, size_t requested_bytes) {
    std::array<std::uint8_t, kUserData> data{};
    if (!static_cast<DiscSectors*>(track)->read(sector, data.data())) return 0;
    const std::size_t bytes = std::min(requested_bytes, kUserData);
    std::memcpy(buffer, data.data(), bytes);
    return bytes;
}

void RC_CCONV close_track(void*) {}

uint32_t RC_CCONV first_track_sector(void* track) { return static_cast<DiscSectors*>(track)->first_sector(); }

} // namespace

std::uint32_t read_guest(std::uint32_t address, std::uint8_t* buffer, std::uint32_t count,
                         std::span<const std::uint8_t> ram, std::span<const std::uint8_t> scratchpad) {
    const auto main_ram = ram.first(std::min<std::size_t>(kRamSize, ram.size()));
    const auto pad = scratchpad.first(std::min<std::size_t>(kScratchpadSize, scratchpad.size()));
    std::uint32_t copied = 0;
    while (copied < count) {
        const std::uint32_t at = address + copied;
        std::span<const std::uint8_t> region;
        std::uint32_t offset = 0;
        if (at < main_ram.size()) {
            region = main_ram;
            offset = at;
        } else if (at >= kScratchpadAddress && at - kScratchpadAddress < pad.size()) {
            region = pad;
            offset = at - kScratchpadAddress;
        } else {
            break;
        }
        const auto run = static_cast<std::uint32_t>(std::min<std::size_t>(count - copied, region.size() - offset));
        std::memcpy(buffer + copied, region.data() + offset, run);
        copied += run;
    }
    return copied;
}

std::optional<std::string> playstation_hash(DiscSectors& disc) {
    // Not rc_hash_initialize_iterator: it would open the path to guess the console, and destroying frees the path.
    rc_hash_iterator_t iterator{};
    iterator.path = "disc";
    iterator.userdata = &disc;
    iterator.callbacks.cdreader.open_track_iterator = open_track;
    iterator.callbacks.cdreader.read_sector = read_sector;
    iterator.callbacks.cdreader.close_track = close_track;
    iterator.callbacks.cdreader.first_track_sector = first_track_sector;
    std::array<char, 33> hash{};
    const int made = rc_hash_generate(hash.data(), RC_CONSOLE_PLAYSTATION, &iterator);
    iterator.path = nullptr;
    rc_hash_destroy_iterator(&iterator);
    if (!made) return std::nullopt;
    return std::string(hash.data());
}

} // namespace disruptor::achievements
