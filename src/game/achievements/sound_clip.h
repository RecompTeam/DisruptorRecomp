#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::achievements {

// A sound of the player's own for unlocks: interleaved stereo 16-bit frames at the file's rate.
struct Clip {
    int rate = 0;
    std::vector<std::int16_t> stereo;
    [[nodiscard]] std::size_t frames() const { return stereo.size() / 2; }
};

inline constexpr std::size_t kMostSoundBytes = 8u * 1024u * 1024u;
inline constexpr int kMostSoundSeconds = 10;
// Where the launcher keeps the chosen sound, in the folder of the player's files.
inline constexpr const char* kUnlockSoundFile = "achievements/unlock.wav";

// A WAV file's sound: PCM of 8, 16, 24 or 32 bits or 32-bit float, its first two channels. Empty, with `why`, otherwise.
[[nodiscard]] std::optional<Clip> read_wav(std::string_view bytes, std::string& why);

// The player's sound from `folder`: none when there is no file, and then `why` stays empty, or when it does not play.
[[nodiscard]] std::optional<Clip> load_unlock_sound(const std::filesystem::path& folder, std::string& why);
// A chosen WAV copied whole into `folder` once it reads as a sound, an earlier one kept till then: empty, or why not.
[[nodiscard]] std::string keep_unlock_sound(const std::filesystem::path& chosen, const std::filesystem::path& folder);
// The player's sound taken away, so that the chime plays: false when it could not be.
[[nodiscard]] bool drop_unlock_sound(const std::filesystem::path& folder);

} // namespace disruptor::achievements
