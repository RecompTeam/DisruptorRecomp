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
// A row of the settings pages. The rows of a page are shown in the order they are listed.
struct Placement { const char* key; Page page; };

struct PresetValue { const char* key; int value; };
struct Preset { const wchar_t* label; std::span<const PresetValue> values; };

std::span<const Placement> layout();
std::span<const Preset> presets();
// Whether a release's settings.toml names the row: for these the game's own value is not the one a release runs with.
bool shipped(const Option& option);
std::span<const wchar_t* const> page_labels();

// Bytes that are not UTF-8 come out as U+FFFD.
std::wstring wide(std::string_view utf8_text);
std::string utf8(const std::filesystem::path& path);
std::filesystem::path from_utf8(const std::string& text);
// The PSX_ variables of this process, which the game it starts lets replace settings for that run.
std::wstring environment_overrides();

// settings.toml through the game's own reader and writer: a change rereads the file, so a key the game saved since is kept.
class SettingsFile {
public:
    explicit SettingsFile(std::filesystem::path path);
    // False when the file is not valid TOML: nothing is written then, a save would replace it with defaults.
    bool load();
    bool readable() const { return readable_; }

    void set_display_rate(double hertz) { display_rate_ = hertz; }
    int highest(const Option& option) const;
    int shown(const Option& option) const;
    bool applies(const Option& option) const;

    bool set(const Option& option, int value);
    bool apply(const Preset& preset);
    bool reset();
    // Writes each shipped row the file does not name: for those the game's own value is not the one shown here.
    bool complete();

    // UTF-8 paths. An empty language disc means the US disc alone.
    const std::string& language_disc() const { return settings_.language_disc; }
    std::vector<std::string> language_discs() const;
    bool choose_language_disc(const std::string& path);
    bool forget_language_disc(const std::string& path);

    std::filesystem::path saves_folder() const;

private:
    template <typename Change> bool change(Change&& alter);
    static void put(Settings& settings, const Option& option, int value);

    std::filesystem::path path_;
    Settings settings_;
    bool readable_ = true;
    double display_rate_ = 0.0;
};

} // namespace disruptor::launcher
