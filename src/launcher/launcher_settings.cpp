#include "launcher_settings.h"

#include "disc_import.h"
#include "game/frame/present_rate.h"
#include "game/settings/user_folder.h"

#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
constexpr const wchar_t* pages[] = {L"Display", L"Enhancements", L"Controls", L"Audio", L"Experimental", L"Troubleshooting"};
constexpr const char* rate_key = "video.frame_interpolation_fps";

constexpr Placement placed[] = {
    {"video.fullscreen", Page::display},
    {"video.aspect_ratio", Page::display},
    {"video.supersampling", Page::display},
    {"video.vsync", Page::display},
    {"disruptor.hud_scale", Page::display},
    {"disruptor.frame_unlock", Page::enhancements, "Frame rate"},
    {"video.frame_interpolation", Page::enhancements},
    {"video.frame_interpolation_fps", Page::enhancements, nullptr, true},
    {"disruptor.geometry_correction", Page::enhancements, "Picture"},
    {"disruptor.perspective_textures", Page::enhancements},
    {"disruptor.gap_filling", Page::enhancements},
    {"disruptor.smooth_fade", Page::enhancements},
    {"disruptor.depth_sorted_world", Page::enhancements},
    {"disruptor.ambient_occlusion", Page::enhancements, "Shading"},
    {"disruptor.occlusion_reach", Page::enhancements, nullptr, true},
    {"disruptor.occlusion_strength", Page::enhancements, nullptr, true},
    {"disruptor.skip_intro", Page::enhancements, "Startup"},
    {"disruptor.modern_controls", Page::controls, "Scheme"},
    {"disruptor.mouse_aim", Page::controls, "Mouse"},
    {"disruptor.high_precision_camera", Page::controls, nullptr, true},
    {"disruptor.horizontal_sensitivity", Page::controls},
    {"disruptor.invert_horizontal", Page::controls},
    {"disruptor.vertical_look", Page::controls, "Vertical look"},
    {"disruptor.invert_vertical", Page::controls, nullptr, true},
    {"disruptor.separate_vertical_speed", Page::controls, nullptr, true},
    {"disruptor.vertical_sensitivity", Page::controls, nullptr, true},
    {"controller.deadzone", Page::controls, "Gamepad"},
    {"audio.master_volume", Page::audio},
    {"audio.muted", Page::audio},
    {"disruptor.multisampling", Page::experimental},
    {"disruptor.detailed_logging", Page::troubleshooting},
};


constexpr const char* shipped_keys[] = {
    "disruptor.mouse_aim", "disruptor.modern_controls", "disruptor.vertical_look", "disruptor.geometry_correction",
    "disruptor.perspective_textures", "disruptor.smooth_fade", "disruptor.depth_sorted_world", "disruptor.frame_unlock",
    "video.frame_interpolation"};

bool listed(const std::vector<std::string>& discs, const std::string& path) {
    return std::find(discs.begin(), discs.end(), path) != discs.end();
}
} // namespace

std::span<const Placement> layout() { return placed; }
std::span<const wchar_t* const> page_labels() { return pages; }

std::wstring wide(std::string_view utf8_text) {
    const int bytes = static_cast<int>(utf8_text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8_text.data(), bytes, nullptr, 0);
    std::wstring text(static_cast<size_t>(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, utf8_text.data(), bytes, text.data(), size);
    return text;
}

std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

fs::path from_utf8(const std::string& text) { return fs::path(std::u8string(text.begin(), text.end())); }

std::wstring environment_overrides() {
    std::wstring names;
    wchar_t* const block = GetEnvironmentStringsW();
    for (const wchar_t* entry = block; entry && *entry; entry += wcslen(entry) + 1) {
        const std::wstring_view variable(entry);
        if (_wcsnicmp(entry, L"PSX_", 4) != 0) continue;
        if (!names.empty()) names += L", ";
        names += variable.substr(0, variable.find(L'='));
    }
    if (block) FreeEnvironmentStringsW(block);
    return names;
}

SettingsFile::SettingsFile(fs::path path, fs::path program) : path_(std::move(path)), program_(std::move(program)) {}

bool SettingsFile::load() {
    settings_ = PSXRecompV4::load_user_settings(path_);
    readable_ = !settings_.parse_error;
    if (!readable_) settings_ = Settings{};
    return readable_;
}

int SettingsFile::highest(const Option& option) const {
    return std::string_view(option.key) == rate_key ? disruptor_present_rate_top(display_rate_) : option.highest;
}

int SettingsFile::shown(const Option& option) const {
    const int value = stored(settings_, option);
    if (option.kind == Kind::toggle) return value != 0;
    if (option.kind == Kind::choice) return value >= 0 && value <= option.highest ? value : -1;
    if (std::string_view(option.key) == rate_key) return disruptor_present_rate_shown(value, highest(option));
    return std::clamp(value, option.lowest, highest(option));
}

bool SettingsFile::applies(const Option& option) const {
    return settings::applies(option, [this](const Option& needed) { return shown(needed) != 0; });
}

void SettingsFile::put(Settings& settings, const Option& option, int value) {
    option.set(settings, value);
    if (option.kind != Kind::toggle || value != 0) return;
    const auto named = [&](const char* key) { return std::string_view(key) == option.key; };
    for (const auto& other : options()) {
        if (other.kind == Kind::toggle && std::any_of(other.needs.begin(), other.needs.end(), named) &&
            stored(settings, other) != 0)
            put(settings, other, 0);
    }
}

template <typename Change> bool SettingsFile::change(Change&& alter) {
    Settings fresh = PSXRecompV4::load_user_settings(path_);
    readable_ = !fresh.parse_error;
    if (!readable_) return false;
    alter(fresh);
    if (!PSXRecompV4::save_user_settings(path_, fresh)) return false;
    settings_ = std::move(fresh);
    return true;
}

bool SettingsFile::set(const Option& option, int value) {
    return change([&](Settings& settings) { put(settings, option, value); });
}

bool SettingsFile::reset(Page page) {
    return change([page](Settings& settings) {
        for (const auto& option : options()) {
            const auto row = std::ranges::find_if(placed, [&](const Placement& one) { return std::string_view(one.key) == option.key; });
            if (row != std::end(placed) && row->page == page) option.set(settings, option.initial);
        }
    });
}

bool experimental(const Option& option) { return std::string_view(option.key) == "disruptor.vertical_look"; }

bool shipped(const Option& option) {
    const auto same = [&](const char* key) { return std::string_view(key) == option.key; };
    return std::any_of(std::begin(shipped_keys), std::end(shipped_keys), same);
}

bool SettingsFile::complete() {
    if (!load()) return false;
    const auto whole = [this](const Option& option) { return !shipped(option) || option.saved(settings_); };
    if (std::all_of(options().begin(), options().end(), whole)) return true;
    return change([](Settings& settings) {
        for (const auto& option : options())
            if (shipped(option) && !option.saved(settings)) option.set(settings, option.initial);
    });
}

std::vector<std::string> SettingsFile::language_discs() const {
    auto discs = settings_.language_discs;
    if (!settings_.language_disc.empty() && !listed(discs, settings_.language_disc))
        discs.push_back(settings_.language_disc);
    return discs;
}

bool SettingsFile::choose_language_disc(const std::string& path) {
    return change([&](Settings& settings) {
        for (const std::string& disc : {settings.language_disc, path}) {
            if (disc.empty() || listed(settings.language_discs, disc)) continue;
            settings.has_language_discs = true;
            settings.language_discs.push_back(disc);
        }
        settings.has_language_disc = true;
        settings.language_disc = path;
    });
}

bool SettingsFile::forget_language_disc(const std::string& path) {
    return change([&](Settings& settings) {
        std::erase(settings.language_discs, path);
        settings.has_language_discs = true;
        if (settings.language_disc != path) return;
        settings.has_language_disc = true;
        settings.language_disc.clear();
    });
}

// As the game decides: the settings' folder, else game.toml's, else the build folder itself.
fs::path SettingsFile::saves_folder() const {
    const fs::path beside = path_.parent_path();
    if (settings_.has_memcard_dir) return beside / settings_.memcard_dir;
    const fs::path program = program_.empty() ? beside : program_;
    const fs::path game = program / supported_discs().front().game_config;
    std::error_code missing;
    if (!fs::is_regular_file(game, missing)) return beside;
    try {
        const auto config = PSXRecompV4::load_game_config(game);
        if (config.runtime.has_memcard_dir) return user_folder::moved_out(config.runtime.memcard_dir, program, beside);
    } catch (const std::exception&) {
    }
    return beside;
}

} // namespace disruptor::launcher
