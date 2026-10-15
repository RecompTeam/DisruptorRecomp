#pragma once

#include "game/settings/options.h"

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::launcher {

using settings::find_option;
using settings::Kind;
using settings::Option;
using settings::options;
using settings::Settings;
using settings::stored;
using settings::Unit;

enum class Page { display, enhancements, controls, audio, experimental, troubleshooting };
// A row of the settings pages. The rows of a page are shown in the order they are listed. A row with a group starts a
// group of that name, a child row stands indented under the row it needs, right above it.
struct Placement {
    const char* key;
    Page page;
    const char* group = nullptr;
    bool child = false;
};

std::span<const Placement> layout();
// Whether a release's settings.toml names the row: for these the game's own value is not the one a release runs with.
bool shipped(const Option& option);
// A row still being tried on a page other than Experimental: it carries a tag beside its name.
bool experimental(const Option& option);
std::span<const wchar_t* const> page_labels();

// Bytes that are not UTF-8 come out as U+FFFD, a lone surrogate going back the same way.
std::wstring wide(std::string_view utf8_text);
std::string narrow(std::wstring_view text);
std::string utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& text);
// The PSX_ variables of this process, which the game it starts lets replace settings for that run.
std::wstring environment_overrides();

// settings.toml through the game's own reader and writer: a change rereads the file, so a key the game saved since is kept.
class SettingsFile {
public:
    // `program` holds game.toml, the folder of `path` when empty.
    explicit SettingsFile(std::filesystem::path path, std::filesystem::path program = {});
    // False when the file is not valid TOML: nothing is written then, a save would replace it with defaults.
    bool load();
    bool readable() const { return readable_; }

    void set_display_rate(double hertz) { display_rate_ = hertz; }
    int highest(const Option& option) const;
    int shown(const Option& option) const;
    bool applies(const Option& option) const;

    bool set(const Option& option, int value);
    // The rows of `page` back to what a new installation has.
    bool reset(Page page);
    // Writes each shipped row the file does not name: for those the game's own value is not the one shown here.
    bool complete();

    // UTF-8 paths. An empty language disc means the US disc alone.
    const std::string& language_disc() const { return settings_.language_disc; }
    // Player 1's device where settings.toml names one, empty where game.toml's default applies.
    std::string player_device() const { return settings_.has_p1_device ? settings_.p1_device : std::string(); }
    std::vector<std::string> language_discs() const;
    bool choose_language_disc(const std::string& path);
    bool forget_language_disc(const std::string& path);

    std::filesystem::path saves_folder() const;

private:
    template <typename Change> bool change(Change&& alter);
    static void put(Settings& settings, const Option& option, int value);

    std::filesystem::path path_;
    std::filesystem::path program_;
    Settings settings_;
    bool readable_ = true;
    double display_rate_ = 0.0;
};

} // namespace disruptor::launcher
