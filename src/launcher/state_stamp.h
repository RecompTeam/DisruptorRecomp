#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::launcher {

// What ties a save state to the build that made it: the game refuses a state whose stamp is not its own.
struct StateStamp {
    std::uint32_t codegen_hash = 0;
    std::int32_t abi_tag = 0;
    std::uint32_t codegen_version = 0;
    bool operator==(const StateStamp&) const = default;
};

// The stamp of the game built beside this launcher.
StateStamp build_stamp();
std::string stamp_text(const StateStamp& stamp);

struct SavedState {
    std::filesystem::path path;
    std::string language; // the folder of a language disc's slots, empty for the US disc's
    int slot = 0;         // 0 is the state F1 loads
    std::uint32_t entry = 0;
    StateStamp stamp;
};

// A save state's header, when the file is one in the format this build reads.
std::optional<SavedState> read_state(const std::filesystem::path& file);
// The states in `saves` and its language folders whose stamp is not this build's.
std::vector<SavedState> stale_states(const std::filesystem::path& saves);
// The name the game gives the file of `slot` for the disc whose game starts at `entry`.
std::string state_name(std::uint32_t entry, int slot);

// A file's bytes, all of them or none.
std::optional<std::string> read_whole(const std::filesystem::path& file);
// Writes the bytes and says whether every one reached the file.
bool write_whole(const std::filesystem::path& file, std::string_view bytes);
// A state's bytes with `stamp` in its header and the rest as they were. Empty when the bytes are no state.
std::string restamped(std::string bytes, const StateStamp& stamp);
// A short name for the bytes, so that a changed file is not taken for one already tried.
std::string fingerprint(std::string_view bytes);

} // namespace disruptor::launcher
