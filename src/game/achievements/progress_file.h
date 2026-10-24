#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::achievements {

// A save state's achievement progress lives in a file beside it: rcheevos' hit counts, the disc hash they are for
// and the digest of the very state they were saved with, so a file left from another state is never taken.
[[nodiscard]] std::filesystem::path progress_file_for(const std::filesystem::path& state);
// The state file's MD5 in hex, nothing when it cannot be read.
[[nodiscard]] std::optional<std::string> state_digest(const std::filesystem::path& state);

// Replaces the file. On failure no file is left where it could be.
bool write_progress(const std::filesystem::path& file, std::string_view hash, std::string_view state,
                    std::span<const std::uint8_t> progress);
// Nothing when the file is missing, damaged, for another disc or for another state.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_progress(const std::filesystem::path& file,
                                                                     std::string_view hash, std::string_view state);
void erase_progress(const std::filesystem::path& file);

} // namespace disruptor::achievements
