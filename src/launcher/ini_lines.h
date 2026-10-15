#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::launcher {

// The lines of an ini file the launcher edits in place: keybinds.ini and input.ini.
std::string trimmed(std::string_view text);
std::string lowered(std::string text);
// A missing file has no lines. One that is there and cannot be read is no file to rewrite.
std::optional<std::vector<std::string>> read_lines(const std::filesystem::path& file);
// Through a sibling file and a rename, so a failed write leaves the old file whole.
bool write_lines(const std::filesystem::path& file, const std::vector<std::string>& lines);

struct IniValue {
    std::string key;
    std::string value;
};
// How the runtime tells a section's line: keybinds.ini's by its opening bracket, input.ini's by both, comments cut.
enum class Headers { opened, closed };
// Rewrites `section`'s `key = value` lines, keys padded to `pad`, and adds the keys and the section the file lacks.
void put_section(std::vector<std::string>& lines, std::string_view section, std::span<const IniValue> values, std::size_t pad,
                 Headers headers);

} // namespace disruptor::launcher
