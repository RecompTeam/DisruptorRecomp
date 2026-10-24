#include "launcher_settings.h"
#include "startup_log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
const std::string french = "C:\\Games \"old\"\\Disruptor (France).cue";
#ifdef _WIN32
const std::string german = "D:\\\xD0\x94\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB8\\Disruptor (Germany).cue";
#else
const std::string german = "/media/\xD0\x94\xD0\xB8\xD1\x81\xD0\xBA\xD0\xB8/Disruptor (Germany).cue";
#endif

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
    out.close();
    require(static_cast<bool>(out), "fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
bool has(const fs::path& path, std::string_view text) { return read(path).find(text) != std::string::npos; }
const Option& option(const char* key) {
    const Option* found = find_option(key);
    require(found != nullptr, "an option the test names is not in the table");
    return *found;
}
int other_value(const SettingsFile& settings, const Option& one) {
    if (one.kind == Kind::toggle) return 1 - one.initial;
    if (one.kind == Kind::choice) return (one.initial + 1) % static_cast<int>(one.choices.size());
    return one.initial == one.lowest + 7 ? one.lowest + 8 : std::min(one.lowest + 7, settings.highest(one));
}

void table() {
    std::set<std::string> keys;
    for (const auto& one : options()) {
        require(keys.insert(one.key).second, "two options have one key");
        require(std::string_view(one.key).find('.') != std::string_view::npos, "a key lacks its section");
        if (one.kind == Kind::choice)
            require(!one.choices.empty() && one.initial >= 0 && one.initial < static_cast<int>(one.choices.size()) &&
                    one.initial <= one.highest && one.highest < static_cast<int>(one.choices.size()), "a choice starts outside its list");
        if (one.kind == Kind::choice && one.highest < static_cast<int>(one.choices.size()) - 1)
            require(std::string_view(one.key) == "disruptor.draw_distance" || std::string_view(one.key) == "disruptor.fog_shape",
                    "only the draw distance and the fog shape offer fewer choices than they name");
        require(!one.notice || (one.kind == Kind::choice && one.highest < static_cast<int>(one.choices.size()) - 1),
                "a notice stands only beside choices that are not all offered");
        if (one.kind == Kind::toggle) require(one.initial == 0 || one.initial == 1, "a toggle starts as neither");
        for (const char* need : one.needs)
            require(option(need).kind == Kind::toggle && &option(need) != &one, "a row needs no toggle");
        require(one.hint && (!*one.hint || (std::strlen(one.hint) >= 20 && std::strlen(one.hint) <= 240)),
                "a row's description is empty or of 20 to 240 characters");
    }
}

// The in-game menu saves a staged value by get and set from one settings to another.
void carried() {
    for (const auto& one : options()) {
        std::set<int> values{one.initial};
        for (int value = one.lowest; value <= one.highest; ++value) values.insert(value);
        for (const int value : values) {
            Settings staged{}, file{};
            one.set(staged, value);
            one.set(file, one.get(staged));
            require(one.saved(file) && one.get(file) == value, "a staged value is not carried whole");
        }
    }
    const Option& aspect = option("video.aspect_ratio");
    const int shapes[][3] = {{4, 3, 0}, {16, 9, 0}, {21, 9, 0}, {32, 9, 0}, {32, 9, 1}};
    for (const auto& shape : shapes) {
        Settings staged{}, file{};
        staged.has_aspect_ratio = staged.has_adaptive_view = true;
        staged.aspect_num = shape[0];
        staged.aspect_den = shape[1];
        staged.adaptive_view = shape[2] != 0;
        aspect.set(file, aspect.get(staged));
        require(file.has_aspect_ratio && file.has_adaptive_view && file.aspect_num == shape[0] &&
                file.aspect_den == shape[1] && file.adaptive_view == staged.adaptive_view,
                "an aspect the menu stages is not carried whole");
    }
    Settings probe{};
    option("video.vsync").set(probe, 0);
    option("video.supersampling").set(probe, 3);
    option("disruptor.horizontal_sensitivity").set(probe, 83);
    require(probe.vsync == -1 && probe.supersampling == 4 && probe.horizontal_sensitivity == 0.083,
            "the menu's own values are not the table's");
    const Option& turn = option("disruptor.horizontal_sensitivity");
    Settings typed{}, written{};
    typed.has_horizontal_sensitivity = true;
    typed.horizontal_sensitivity = 0.0834;
    turn.set(written, turn.get(typed));
    require(written.has_horizontal_sensitivity && written.horizontal_sensitivity == 0.083,
            "a sensitivity is not written in whole thousandths");
}

// The rule both front ends ask: every toggle an option needs is on, and so is what those need.
void needed() {
    const auto on = [](std::set<std::string> keys) {
        return [keys = std::move(keys)](const Option& toggle) { return keys.contains(toggle.key); };
    };
    const Option& smooth = option("disruptor.high_precision_camera");
    const Option& reach = option("disruptor.occlusion_reach");
    require(disruptor::settings::applies(option("video.vsync"), on({})), "an option that needs nothing always applies");
    require(disruptor::settings::applies(smooth, on({"disruptor.mouse_aim", "disruptor.geometry_correction"})) &&
            !disruptor::settings::applies(smooth, on({"disruptor.mouse_aim"})) &&
            !disruptor::settings::applies(smooth, on({"disruptor.geometry_correction"})),
            "an option applies only with every toggle it needs");
    require(disruptor::settings::applies(reach, on({"disruptor.ambient_occlusion", "disruptor.geometry_correction"})) &&
            !disruptor::settings::applies(reach, on({"disruptor.ambient_occlusion"})) &&
            !disruptor::settings::applies(reach, on({"disruptor.geometry_correction"})),
            "and only when what it needs applies itself");
}

// A settings.toml written before the list, as replay capsules carry it too, read by the game's own reader.
void antialiasing_from_file(const fs::path& folder) {
    const Option& list = option("disruptor.multisampling");
    const std::pair<const char*, int> files[] = {
        {"[disruptor]\nmultisampling = 8\nsample_shading = true\n", 7},
        {"[disruptor]\nmultisampling = 16\n", 4},
        {"[disruptor]\nsample_shading = true\n", 0},
        {"[disruptor]\nmultisampling = 5\nsample_shading = true\n", 0},
    };
    for (const auto& [contents, level] : files) {
        const fs::path file = folder / "antialiasing.toml";
        write(file, contents);
        const Settings loaded = PSXRecompV4::load_user_settings(file);
        require(!loaded.parse_error && list.saved(loaded) && list.get(loaded) == level,
                "a file of the two keys from before the list reads as the level they make");
        SettingsFile settings(file);
        require(settings.load() && settings.shown(list) == level, "and the launcher shows that level");
    }
}

void separate_vertical_from_file(const fs::path& folder) {
    const Option& separate = option("disruptor.separate_vertical_speed");
    const std::pair<const char*, int> files[] = {
        {"[disruptor]\nvertical_sensitivity = 0.120\n", 1},
        {"[disruptor]\nhorizontal_sensitivity = 0.200\nvertical_sensitivity = 0.200\n", 0},
        {"[disruptor]\nhorizontal_sensitivity = 0.200\n", 0},
        {"[disruptor]\nvertical_sensitivity = 0.080\n", 0},
        {"[disruptor]\nvertical_sensitivity = 0.120\nseparate_vertical_speed = false\n", 0},
        {"[disruptor]\nseparate_vertical_speed = true\n", 1},
    };
    for (const auto& [contents, shown] : files) {
        const fs::path file = folder / "separate.toml";
        write(file, contents);
        SettingsFile settings(file);
        require(settings.load() && settings.shown(separate) == shown,
                "a file from before the switch keeps a vertical speed of its own where it set one unlike the "
                "horizontal, and the switch once saved decides");
    }
    SettingsFile settings(folder / "separate.toml");
    require(settings.load() && settings.set(separate, 1) && settings.shown(separate) == 1 &&
                settings.set(separate, 0) && settings.shown(separate) == 0,
            "the switch writes both ways");
    const Option& horizontal = option("disruptor.horizontal_sensitivity");
    write(folder / "separate.toml", "[disruptor]\nvertical_sensitivity = 0.120\n");
    SettingsFile older(folder / "separate.toml");
    require(older.load() && older.shown(separate) == 1 && older.set(horizontal, 120) && older.shown(separate) == 1 &&
                older.set(horizontal, 150) && older.shown(separate) == 1 && has(folder / "separate.toml", "separate_vertical_speed = true"),
            "a speed written once writes down the switch the older file implied, so the next speeds leave it");
}

void draw_distance_list() {
    const Option& list = option("disruptor.draw_distance");
    require(list.kind == Kind::choice && list.initial == 0 && list.choices.size() == 4 && list.needs.empty() &&
                std::string_view(list.choices[0]) == "Original",
            "the draw distance is a list of four that starts at the original and needs nothing");
    Settings fresh;
    require(!list.saved(fresh) && list.get(fresh) == 0, "a file that does not name it draws at the original distance");
    const int percents[] = {100, 125, 150, 200};
    for (int index = 0; index < 4; ++index) {
        Settings chosen;
        list.set(chosen, index);
        require(list.saved(chosen) && chosen.draw_distance == percents[index] && list.get(chosen) == index,
                "each choice is written as its percent and read back as itself");
    }
    Settings odd;
    odd.has_draw_distance = true;
    odd.draw_distance = 110;
    require(list.get(odd) == -1, "a percent the list does not have is no choice");
    require(list.highest == 1 && !list.notice, "the original distance and 1.25x are offered, with no notice of more");
    Settings quarter;
    list.set(quarter, 1);
    require(stored(quarter, list) == 1, "1.25x is kept as chosen");
    Settings farther;
    list.set(farther, 2);
    require(stored(farther, list) == 0, "a longer distance saved by an older build starts at the original");
}

void fog_shape_list() {
    const Option& list = option("disruptor.fog_shape");
    require(list.kind == Kind::choice && list.initial == 0 && list.choices.size() == 3 && list.needs.size() == 1 &&
                std::string_view(list.needs[0]) == "disruptor.smooth_fade" && std::string_view(list.choices[0]) == "Original",
            "the fog shape is a list of three that starts at the original and needs the smooth fade");
    for (int index = 0; index < 3; ++index) {
        Settings chosen;
        list.set(chosen, index);
        require(chosen.has_fog_shape && chosen.fog_shape == index && list.get(chosen) == index, "each shape is written as its index");
    }
    require(list.highest == 0 && list.notice && std::string_view(list.notice) == "Other shapes come in a later release",
            "only the original fog is offered for now, and the notice says why");
    Settings late;
    list.set(late, 2);
    require(stored(late, list) == 0, "another shape saved by a test build starts at the original");
}

void antialiasing_list() {
    const Option& list = option("disruptor.multisampling");
    require(list.kind == Kind::choice && list.initial == 0 && list.choices.size() == 9 &&
                std::string_view(list.choices[0]) == "Off" && std::string_view(list.choices[1]) == "2x MSAA" &&
                std::string_view(list.choices[4]) == "16x MSAA" && std::string_view(list.choices[5]) == "2x SSAA" &&
                std::string_view(list.choices[8]) == "16x SSAA",
            "anti-aliasing is one list: off, four counts of MSAA, the same four of SSAA");
    Settings fresh;
    require(!list.saved(fresh) && list.get(fresh) == 0, "a file that names neither key is off");
    const int samples[] = {0, 2, 4, 8, 16};
    for (int level = 0; level <= 8; ++level) {
        Settings chosen;
        list.set(chosen, level);
        require(chosen.has_multisampling && chosen.has_sample_shading && list.saved(chosen) &&
                    chosen.multisampling == samples[level > 4 ? level - 4 : level] && chosen.sample_shading == (level > 4) &&
                    list.get(chosen) == level,
                "each level is written as the two saved keys and read back as itself");
    }
    Settings old;
    old.has_multisampling = old.has_sample_shading = true;
    old.multisampling = 8;
    old.sample_shading = true;
    require(list.get(old) == 7, "a file of the two settings before the list reads as the level they make");
    old.sample_shading = false;
    require(list.get(old) == 3, "and without shading as that count of MSAA");
    old.multisampling = 0;
    old.sample_shading = true;
    require(list.get(old) == 0, "shading without samples is off, as the renderer has it");
    Settings shading_only;
    shading_only.has_sample_shading = true;
    require(list.saved(shading_only), "either key saved counts as the list saved");
    Settings odd;
    odd.has_multisampling = true;
    odd.multisampling = 3;
    require(list.get(odd) == 0, "a count the list does not have reads as off");
    list.set(odd, 42);
    require(odd.multisampling == 16 && odd.sample_shading, "a level past the list is its last");
    list.set(odd, -3);
    require(odd.multisampling == 0 && !odd.sample_shading, "and one before it is off");
}

void described() {
    Settings settings;
    const Option& logging = *find_option("disruptor.detailed_logging");
    const Option& volume = *find_option("audio.master_volume");
    const Option& wheels = *find_option("disruptor.selection_wheels");
    require(wheels.kind == Kind::toggle && wheels.initial == 0 && stored(settings, wheels) == 0,
            "one option controls both wheels and starts disabled");
    const Option& numbers = *find_option("disruptor.number_keys");
    require(numbers.kind == Kind::toggle && numbers.initial == 0 && stored(settings, numbers) == 0,
            "the number keys pick no weapon until their option is on");
    const Option& use = *find_option("disruptor.psionic_keys_use");
    require(use.kind == Kind::toggle && use.initial == 0 && stored(settings, use) == 0 && use.needs.size() == 1 &&
                std::string_view(use.needs[0]) == "disruptor.number_keys" && !experimental(use),
            "a power's key only picks the power at first, and uses it only under the weapon and psionic keys");
    require(experimental(wheels) && !experimental(numbers) && experimental(*find_option("disruptor.vertical_look")) &&
                !experimental(*find_option("disruptor.modern_controls")),
            "the wheels and the vertical look carry the experimental tag, the number keys and the scheme do not");
    require(logging.kind == Kind::toggle && logging.initial == 0 && stored(settings, logging) == 0 && stored(settings, volume) == 100,
            "an option the file does not name has a fresh installation's value, and the detailed log starts off");
    logging.set(settings, 1);
    volume.set(settings, 40);
    require(stored(settings, logging) == 1 && stored(settings, volume) == 40, "a named one has the file's");
    const std::string text = disruptor::settings::describe(settings);
    size_t at = 0, count = 0;
    for (const auto& option : options()) {
        const std::string entry = std::string(count++ ? " " : "") + option.key + "=" + std::to_string(stored(settings, option));
        require(text.compare(at, entry.size(), entry) == 0, "the settings line names every option with its value, in the table's order");
        at += entry.size();
    }
    require(at == text.size() && text.find(" audio.master_volume=40 ") != std::string::npos &&
                text.find(" disruptor.detailed_logging=1 ") != std::string::npos,
            "and nothing else");
}

void placements() {
    std::set<std::string> placed;
    for (const auto& row : layout()) {
        require(find_option(row.key) != nullptr, "a row names no option");
        require(placed.insert(row.key).second, "an option is on two rows");
        require(static_cast<size_t>(row.page) < page_labels().size(), "a row is on no page");
    }
    require(placed.size() == options().size(), "an option is on no page");
    require(page_labels().size() == 7 && std::wstring_view(page_labels()[5]) == L"Troubleshooting" &&
                std::wstring_view(page_labels()[6]) == L"Achievements",
            "the sixth page is Troubleshooting and the last Achievements");
    for (const auto& row : layout())
        require((std::string_view(row.key) == "disruptor.detailed_logging") == (row.page == Page::troubleshooting),
                "the Troubleshooting page holds the detailed log's switch and nothing else");
    for (int page = 0; page < static_cast<int>(page_labels().size()); ++page)
        require(page_rows_shown(static_cast<Page>(page), true) &&
                    page_rows_shown(static_cast<Page>(page), false) == (static_cast<Page>(page) != Page::experimental),
                "a developer build shows every page's rows, a release all but Experimental's");
    require(wide("caf\xC3\xA9") == L"café" && wide("").empty(), "an option's text is read as UTF-8");
    require(wide("a\xFF" "b\xC3") == L"a\uFFFDb\uFFFD" && wide("\xE0\x80\x80") == L"\uFFFD\uFFFD\uFFFD" &&
                wide("\xED\xA0\x80") == L"\uFFFD\uFFFD\uFFFD",
            "bytes that are not UTF-8, overlong forms and surrogates come out as U+FFFD, one a byte");
    for (const char* text : {"caf\xC3\xA9", "\xF0\x9F\x8E\xAE game", "\xE6\x97\xA5\xE6\x9C\xAC", "plain"})
        require(narrow(wide(text)) == text, "UTF-8 goes to wide and back unchanged, beyond the basic plane too");
    const auto rows = layout();
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (!rows[index].child) continue;
        const auto& needs = option(rows[index].key).needs;
        bool found = false;
        for (std::size_t above = index; above-- > 0 && rows[above].page == rows[index].page;) {
            found = found || std::find(needs.begin(), needs.end(), std::string_view(rows[above].key)) != needs.end();
            if (!rows[above].child) break;
        }
        require(!rows[index].group && found, "a child row stands under a row of its group that it needs");
    }
    std::vector<std::string> enhancements;
    for (const auto& row : rows)
        if (row.page == Page::enhancements) enhancements.emplace_back(row.key);
    require(enhancements.size() == 16 && enhancements.front() == "disruptor.frame_unlock" && rows[5].group &&
                std::string_view(rows[5].key) == "disruptor.frame_unlock",
            "Enhancements opens with its frame rate group, 60 FPS gameplay first");
}

void absent(const fs::path& folder) {
    SettingsFile settings(folder / "none" / "settings.toml");
    require(settings.load() && settings.readable(), "a missing file must read as defaults");
    for (const auto& one : options()) {
        if (std::string_view(one.key) == "video.frame_interpolation_fps") continue;
        require(settings.shown(one) == one.initial, "a missing key must show what a new installation has");
    }
    const Option& rate = option("video.frame_interpolation_fps");
    require(settings.highest(rate) == 320 && settings.shown(rate) == 320, "an unknown display must cap the rate at 320");
    settings.set_display_rate(143.9);
    require(settings.highest(rate) == 320 && settings.shown(rate) == 320, "a slower display must leave the rate its 320");
    settings.set_display_rate(359.9);
    require(settings.highest(rate) == 360 && settings.shown(rate) == 360, "the rate must reach a faster display's own");
    require(!fs::exists(folder / "none"), "reading must write nothing");
    require(settings.saves_folder() == folder / "none", "saves are beside the settings by default");
}

void one_change(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    write(file, "[video]\nrenderer = \"opengl\"\n\n[controller]\np1_device = \"keyboard\"\n\n"
                "[memcard]\ndir = \"cards\"\n\n[disruptor]\nhigh_precision_camera = true\n");
    SettingsFile settings(file);
    require(settings.load(), "the fixture must load");
    require(settings.saves_folder() == folder / "cards", "a relative saves folder is beside the settings");
#ifdef _WIN32
    constexpr const char* absolute_saves = "D:/Saves";
#else
    constexpr const char* absolute_saves = "/srv/Saves";
#endif
    write(file, std::string("[memcard]\ndir = \"") + absolute_saves + "\"\n");
    require(settings.load() && settings.saves_folder() == fs::path(absolute_saves), "an absolute saves folder is itself");
    write(file, "[video]\nrenderer = \"opengl\"\n\n[controller]\np1_device = \"keyboard\"\n\n"
                "[memcard]\ndir = \"cards\"\n\n[disruptor]\nhigh_precision_camera = true\n");
    require(settings.load(), "the fixture must load again");
    require(settings.set(option("disruptor.hud_scale"), 70), "a change must be saved");
    require(has(file, "hud_scale = 70") && !has(file, "mouse_aim") && !has(file, "supersampling"),
            "one change must write one key");
    require(has(file, "renderer          = \"opengl\"") && has(file, "p1_device = \"keyboard\"") &&
            has(file, "high_precision_camera = true") && has(file, "dir     = \"cards\""),
            "keys the launcher does not show must be kept");

    write(file, read(file) + "frame_unlock = true\n");
    require(settings.set(option("audio.muted"), 1) && settings.shown(option("disruptor.frame_unlock")) == 1,
            "a change must start from the file as it is now");
    fs::remove(file);
}

void pick_keys_saved(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    require(settings.load() && settings.pick_lists().weapons.empty() && settings.pick_lists().psionics_second.empty(),
            "a new file lists no keys");
    disruptor::settings::PickKeys keys = disruptor::settings::default_pick_keys();
    keys[0][0] = "Z";
    keys[9][1] = "None";
    keys[12][0] = "H";
    require(settings.set_pick_keys(keys), "the keys must be written");
    SettingsFile again(file);
    require(again.load(), "the file must load again");
    const disruptor::settings::PickLists lists = again.pick_lists();
    require(lists.weapons.size() == 10 && lists.weapons[0] == "Z" && lists.weapons_second[9] == "None" &&
                lists.weapons_second[0] == "Keypad 1" && lists.psionics.size() == 5 && lists.psionics[2] == "H" &&
                lists.psionics_second.size() == 5 && lists.psionics_second[0] == "None",
            "and read back as written, the powers' lists and no key included");
    require(again.set(option("disruptor.number_keys"), 1) && again.pick_lists().weapons[0] == "Z" &&
                again.pick_lists().psionics[2] == "H",
            "another setting keeps them");
    fs::remove(file);
}
void round_trip(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    for (const auto& one : options()) {
        SettingsFile settings(file);
        settings.set_display_rate(240.0);
        require(settings.load(), "the file must load");
        if (one.kind == Kind::choice && one.highest < static_cast<int>(one.choices.size()) - 1) {
            require(settings.set(one, one.highest + 1) && settings.shown(one) == one.initial,
                    "a choice past the offered ones, written by an older build, shows as the first");
            fs::remove(file);
            continue;
        }
        const int value = other_value(settings, one);
        require(value != settings.shown(one), "the test value must differ from the one shown");
        require(settings.set(one, value) && settings.shown(one) == value, "a set value must be the one shown");
        SettingsFile again(file);
        again.set_display_rate(240.0);
        require(again.load() && again.shown(one) == value, "a set value must be read back");
        fs::remove(file);
    }
}

void whole_range(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    settings.set_display_rate(1000.0);
    require(settings.set(option("video.frame_interpolation"), 1), "in-between frames must switch on");
    for (const auto& one : options()) {
        const int highest = settings.highest(one);
        const int step = one.kind == Kind::slider ? highest - one.lowest : 1;
        for (int value = one.lowest; value <= highest; value += step) {
            SettingsFile again(file);
            again.set_display_rate(1000.0);
            const bool saved = settings.set(one, value);
            const int save_error = errno;
            const bool loaded = again.load();
            if (!saved || !loaded || again.shown(one) != value)
                throw std::runtime_error(std::string("settings round-trip failed for ") + one.key +
                    "=" + std::to_string(value) + ": saved=" + std::to_string(saved) +
                    ", loaded=" + std::to_string(loaded) + ", actual=" + std::to_string(again.shown(one)) +
                    ", error=" + std::to_string(save_error));
        }
    }
    fs::remove(file);
}

void dependants(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    const Option& geometry = option("disruptor.geometry_correction");
    const Option& textures = option("disruptor.perspective_textures");
    const Option& frames = option("video.frame_interpolation");
    const Option& rate = option("video.frame_interpolation_fps");
    require(settings.set(geometry, 1) && settings.shown(textures) == 1, "switching a toggle on must leave its dependants");
    require(settings.set(frames, 1) && settings.applies(textures) && settings.applies(frames) && settings.applies(rate),
            "rows apply while what they need is on");
    require(settings.set(geometry, 0), "geometry must switch off");
    require(settings.shown(textures) == 0 && settings.shown(frames) == 0 && !settings.applies(textures) &&
            !settings.applies(frames) && !settings.applies(rate), "what needs geometry must go off with it");
    require(has(file, "perspective_textures = false") && has(file, "frame_interpolation = false"),
            "a switched off dependant must be written, its default is on");
    require(settings.set(geometry, 1) && settings.shown(textures) == 0 && settings.applies(textures) &&
            !settings.applies(rate), "switching geometry back on must switch nothing else");
    const Option& aim = option("disruptor.mouse_aim");
    const Option& smooth = option("disruptor.high_precision_camera");
    const Option& gaps = option("disruptor.gap_filling");
    const Option& by_depth = option("disruptor.depth_sorted_world");
    const Option& occlusion = option("disruptor.ambient_occlusion");
    const Option& reach = option("disruptor.occlusion_reach");
    require(settings.set(aim, 1) && settings.set(smooth, 1) && settings.applies(smooth) && settings.set(aim, 0) &&
            settings.shown(smooth) == 0 && !settings.applies(smooth) && has(file, "high_precision_camera = false"),
            "smooth turning needs mouse aim and goes off with it");
    require(settings.set(aim, 1) && settings.applies(smooth) && settings.set(smooth, 1) &&
            has(file, "high_precision_camera = true") && settings.set(geometry, 0) && settings.shown(smooth) == 0 &&
            !settings.applies(smooth) && has(file, "high_precision_camera = false") && settings.set(aim, 0),
            "and geometry, and goes off with that too");
    require(settings.shown(gaps) == 0 && has(file, "gap_filling = false"),
            "gap filling, on in a fresh installation, went off with geometry above and was written so");
    require(settings.set(geometry, 1) && settings.set(gaps, 1) && settings.set(by_depth, 1) && settings.set(occlusion, 1) &&
            settings.applies(gaps) && settings.applies(by_depth) && settings.applies(reach),
            "the new rows apply with geometry on");
    require(settings.set(geometry, 0) && settings.shown(gaps) == 0 && settings.shown(by_depth) == 0 &&
            settings.shown(occlusion) == 0 && !settings.applies(gaps) && !settings.applies(reach) &&
            has(file, "gap_filling = false"), "gap filling, the depth-sorted world and the occlusion go off with geometry");
    require(settings.set(geometry, 1) && settings.set(occlusion, 1) && settings.applies(reach) && settings.set(occlusion, 0) &&
            !settings.applies(reach), "the occlusion's sliders need its switch");
    write(file, "[video]\nframe_interpolation = true\n\n[disruptor]\ngeometry_correction = false\n");
    require(settings.load() && settings.shown(frames) == 1 && !settings.applies(frames) && !settings.applies(rate),
            "a row whose need does not apply itself does not apply either");
    const std::string before = read(file);
#ifdef _WIN32
    const fs::path locked = file;
#else
    const fs::path locked = file.parent_path();  // a rename replaces a read-only file here: the folder refuses it
#endif
    fs::permissions(locked, fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove);
    const bool written = settings.set(geometry, 1);
    fs::permissions(locked, fs::perms::owner_write, fs::perm_options::add);
#ifdef _WIN32
    const bool refusable = true;
#else
    const bool refusable = geteuid() != 0;  // root writes into any folder, so there this save cannot be made to fail
#endif
    require(!refusable || (!written && settings.readable() && settings.shown(geometry) == 0 && read(file) == before),
            "a save that fails must be reported and must not be shown as done");
    fs::remove(file);
}

void reset_to_new(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    SettingsFile settings(file);
    require(settings.set(option("disruptor.hud_scale"), 60) && settings.set(option("audio.master_volume"), 30) &&
                settings.set(option("disruptor.geometry_correction"), 0) && settings.set(option("video.aspect_ratio"), 1),
            "changes must be saved");

    require(settings.choose_language_disc(french) && settings.reset(Page::display), "a reset must be saved");
    require(settings.shown(option("disruptor.hud_scale")) == option("disruptor.hud_scale").initial &&
                settings.shown(option("video.aspect_ratio")) == option("video.aspect_ratio").initial &&
                settings.shown(option("audio.master_volume")) == 30 && settings.shown(option("disruptor.geometry_correction")) == 0,
            "a reset of one page leaves the other pages as they were");
    for (const auto& row : layout())
        require(settings.reset(row.page), "every page can be reset");
    for (const auto& one : options())
        require(settings.shown(one) == (std::string_view(one.key) == "video.frame_interpolation_fps" ? settings.highest(one) : one.initial),
                "every page reset shows what a new installation has");
    require(has(file, "mouse_aim = true") && has(file, "master_volume = 100") && has(file, "frame_interpolation_fps = 0") &&
            settings.language_disc() == french, "a reset writes the page's rows and leaves the language");
    fs::remove(file);
}

void unreadable(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const std::string broken = "[video\nsupersampling = 2\n";
    write(file, broken);
    SettingsFile settings(file);
    require(!settings.load() && !settings.readable(), "a file that is not TOML must be reported");
    require(!settings.set(option("audio.muted"), 1) && !settings.reset(Page::audio) &&
            !settings.choose_language_disc(french) && !settings.forget_language_disc(french),
            "nothing may be saved over a file that could not be read");
    require(read(file) == broken, "the unreadable file must be left as it is");
    write(file, "[video]\nsupersampling = 2\n");
    require(settings.set(option("audio.muted"), 1) && settings.readable() &&
            settings.shown(option("video.supersampling")) == 1, "a mended file must be taken again");
    fs::remove(file);
}

void started_over(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const fs::path first = folder / "settings.unreadable.toml", second = folder / "settings.unreadable-2.toml";
    const std::string broken = "[video\nsupersampling = 2\n", later = "[audio\nmuted = true\n";
    write(file, "[video]\nsupersampling = 2\n");
    SettingsFile settings(file);
    fs::path kept;
    require(!settings.start_over(kept) && read(file) == "[video]\nsupersampling = 2\n",
            "a file that reads is never moved aside");
    write(file, broken);
    require(!settings.load(), "the broken file is unreadable");
    require(settings.start_over(kept) && kept == first && read(first) == broken && !fs::exists(file) && settings.readable() &&
                settings.shown(option("video.supersampling")) == option("video.supersampling").initial,
            "an unreadable file is kept whole under its own name and the settings are a new installation's");
    write(file, later);
    require(settings.start_over(kept) && kept == second && read(second) == later && read(first) == broken,
            "a second unreadable file takes the next free name and the first one stays");
    require(settings.set(option("audio.muted"), 1) && has(file, "muted = true"), "after starting over a change writes a new file");
    fs::remove(file);
    fs::remove(first);
    fs::remove(second);
}

void aspects(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const Option& aspect = option("video.aspect_ratio");
    write(file, "[video]\naspect_ratio = \"16:10\"\n");
    SettingsFile settings(file);
    require(settings.load() && settings.shown(aspect) == -1, "a ratio outside the list must select nothing");
    write(file, "[video]\naspect_ratio = \"64:27\"\n");
    require(settings.load() && settings.shown(aspect) == -1, "64:27 is not 21:9");
    write(file, "[video]\naspect_ratio = \"8:6\"\n");
    require(settings.load() && settings.shown(aspect) == 0, "a ratio is matched by its value");
    write(file, "[video]\naspect_ratio = \"32:9\"\nadaptive_view = true\n");
    require(settings.load() && settings.shown(aspect) == 4, "the adaptive view is the last choice");
    require(settings.set(aspect, 2) && has(file, "aspect_ratio      = \"21:9\"") && has(file, "adaptive_view     = false"),
            "a fixed ratio must switch the adaptive view off");
    require(settings.set(aspect, 4) && has(file, "aspect_ratio      = \"32:9\"") && has(file, "adaptive_view     = true"),
            "matching the window is 32:9 with the adaptive view on");
    write(file, "[video]\nadaptive_view = true\n");
    require(settings.load() && settings.shown(aspect) == 4, "the adaptive view alone is the game matching its window");
    write(file, "[video]\nadaptive_view = false\n");
    require(settings.load() && settings.shown(aspect) == 0, "without a ratio and the adaptive view the game is 4:3");
    write(file, "[video]\nsupersampling = 9\nvsync = \"immediate\"\n");
    require(settings.load() && settings.shown(option("video.supersampling")) == 3 && settings.shown(option("video.vsync")) == 1,
            "a scale the game refuses is shown as the game's own");
    fs::remove(file);
}

// The row shows what the in-game control shows: both go through disruptor_present_rate_shown.
void rates(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    const Option& rate = option("video.frame_interpolation_fps");
    write(file, "[video]\nframe_interpolation_fps = 400\n");
    SettingsFile settings(file);
    settings.set_display_rate(144.0);
    require(settings.load() && settings.shown(rate) == 320, "a rate over the control's top is shown as the top");
    settings.set_display_rate(480.0);
    require(settings.shown(rate) == 400 && settings.highest(rate) == 480, "a rate the display reaches is shown as it is");
    write(file, "[video]\nframe_interpolation_fps = 300\n");
    settings.set_display_rate(144.0);
    require(settings.load() && settings.shown(rate) == 300, "a rate over the display's and under 320 is shown as it is");
    write(file, "[video]\nframe_interpolation_fps = 0\n");
    settings.set_display_rate(60.0);
    require(settings.load() && settings.shown(rate) == 320, "the display's own rate is shown as the top of the control");
    fs::remove(file);
}

void set_variable(const char* name, const char* value) {
#ifdef _WIN32
    SetEnvironmentVariableA(name, value);
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

void overrides() {
    set_variable("PSX_LAUNCHER_TEST_ONE", nullptr);
    set_variable("psx_launcher_test_two", nullptr);
    const std::wstring before = environment_overrides();
    require(before.find(L"LAUNCHER_TEST") == std::wstring::npos, "a variable that is not set is not named");
    set_variable("PSX_LAUNCHER_TEST_ONE", "1");
    set_variable("psx_launcher_test_two", "a=b");
    set_variable("NOT_PSX_LAUNCHER_TEST", "1");
    const std::wstring named = environment_overrides();
    set_variable("PSX_LAUNCHER_TEST_ONE", nullptr);
    set_variable("psx_launcher_test_two", nullptr);
    set_variable("NOT_PSX_LAUNCHER_TEST", nullptr);
    require(named.find(L"PSX_LAUNCHER_TEST_ONE") != std::wstring::npos &&
            named.find(L"psx_launcher_test_two") != std::wstring::npos && named.find(L"NOT_PSX") == std::wstring::npos &&
            named.find(L'=') == std::wstring::npos && named.find(L", ") != std::wstring::npos,
            "the PSX_ variables are named, whatever their case, and nothing else");
}

void languages(const fs::path& folder) {
    const fs::path file = folder / "settings.toml";
    write(file, "[disruptor]\nlanguage_disc = 'C:\\Games \"old\"\\Disruptor (France).cue'\n");
    SettingsFile settings(file);
    require(settings.load() && settings.language_disc() == french && settings.language_discs() == std::vector{french},
            "a disc named by the in-game menu must be offered");
    require(settings.choose_language_disc(german) && settings.language_disc() == german &&
            settings.language_discs() == (std::vector{french, german}) && has(file, "language_discs = [\"C:"),
            "choosing another disc must keep the one the in-game menu named");
    require(settings.choose_language_disc(french) && settings.choose_language_disc(french) &&
            settings.language_discs() == (std::vector{french, german}), "a disc is listed once");
    require(settings.choose_language_disc({}) && settings.language_disc().empty() &&
            settings.language_discs() == (std::vector{french, german}), "English keeps the discs for later");
    SettingsFile again(file);
    require(again.load() && again.language_disc().empty() && again.language_discs() == (std::vector{french, german}),
            "the discs must be read back, letters outside ASCII included");
    require(again.choose_language_disc(german) && again.forget_language_disc(french) &&
            again.language_disc() == german && again.language_discs() == std::vector{german},
            "forgetting another disc leaves the language");
    require(again.forget_language_disc(german) && again.language_disc().empty() && again.language_discs().empty() &&
            has(file, "language_disc = \"\"") && has(file, "language_discs = []"), "forgetting the disc in use is English");
    require(utf8(from_utf8(german)) == german && from_utf8(german).filename() == L"Disruptor (Germany).cue",
            "paths are UTF-8 in the file");
    fs::remove(file);
}

void log(const fs::path& folder) {
    const fs::path file = folder / "startup.log";
    require(log_tail(file, 3).empty(), "no log is no tail");
    write(file, "one\ntwo\r\n\r\nthree\nfour\n");
    require(log_tail(file, 3) == L"two\r\nthree\r\nfour\r\n", "the tail is the last lines that say something");
    require(log_tail(file, 9) == L"one\r\ntwo\r\nthree\r\nfour\r\n", "a short log is shown whole");
    write(file, "caf\xC3\xA9\n");
    require(log_tail(file, 1) == L"caf\u00E9\r\n", "the log is UTF-8");
    write(file, "caf\xE9\n");
    require(log_tail(file, 1) == L"caf�\r\n", "a log that is not UTF-8 is still shown");
    write(file, std::string(20000, 'x') + "\xD0\x94\nlast\n");
    require(log_tail(file, 5) == L"last\r\n", "a line cut by the read is not shown");
    write(file, "the game's last line\n");
    note_exit(file, 0, 125);
    require(log_tail(file, 2) == L"the game's last line\r\nlauncher: the game ended with code 0x00000000 after 125 s\r\n",
            "the launcher adds how the game ended to its log");
    std::string raw(200, '\0');
    raw.resize(static_cast<size_t>(std::ifstream(file, std::ios::binary).read(raw.data(), 200).gcount()));
    require(raw == "the game's last line\nlauncher: the game ended with code 0x00000000 after 125 s\r\n",
            "straight after a whole line, with no empty one between");
    write(file, "cut mid-li");
    note_exit(file, 0xC0000005ul, 3);
    require(log_tail(file, 2) == L"cut mid-li\r\nlauncher: the game ended with code 0xC0000005 after 3 s\r\n",
            "on a line of its own where the game was cut mid-line");
    fs::remove(file);
    note_exit(file, 1, 0);
    require(log_tail(file, 2) == L"launcher: the game ended with code 0x00000001 after 0 s\r\n", "and in a log that was not there");
    fs::remove(file);
}

// The game fills in each of its own options the file does not name, and the window shows what it runs with.
void defaults_filled(const fs::path& folder) {
    const Settings filled = disruptor::settings::with_defaults(Settings{});
    for (const auto& one : options()) {
        const bool ours = std::string_view(one.key).starts_with("disruptor.");
        require(one.saved(filled) == ours, "only the game's own options are filled in, the runtime's keep game.toml's");
        if (ours) require(one.get(filled) == one.initial, "a filled option holds its initial value");
    }
    Settings chosen;
    option("disruptor.smooth_fade").set(chosen, 0);
    option("disruptor.horizontal_sensitivity").set(chosen, 300);
    const Settings kept = disruptor::settings::with_defaults(chosen);
    require(stored(kept, option("disruptor.smooth_fade")) == 0 && stored(kept, option("disruptor.horizontal_sensitivity")) == 300,
            "what the file names stays as it was");

    const fs::path file = folder / "defaults.toml";
    fs::remove(file);
    SettingsFile fresh(file);
    require(fresh.load(), "a missing file is read as empty");
    for (const auto& one : options())
        if (std::string_view(one.key).starts_with("disruptor."))
            require(fresh.shown(one) == one.get(filled), "the window shows each of the game's rows as the game runs it");
    require(!fs::exists(file), "and nothing is written for it");

    Settings shading;
    shading.has_sample_shading = shading.sample_shading = true;
    const Settings half = disruptor::settings::with_defaults(shading);
    require(!half.has_multisampling && half.multisampling == 0 && half.sample_shading,
            "a file naming one half of anti-aliasing keeps it, the other half is off as the list starts");
}

// The game reads its saves folder from game.toml when settings.toml names none.
void saves_beside_game(const fs::path& folder) {
    const fs::path source = DISRUPTOR_SOURCE_DIR;
    const fs::path package = folder / "package";
    fs::create_directories(package);
    fs::copy_file(source / "game.toml", package / "game.toml", fs::copy_options::overwrite_existing);
    write(package / "settings.toml", "");
    SettingsFile settings(package / "settings.toml");
    require(settings.load() && settings.saves_folder() == package / "saves", "the saves are where game.toml puts them");
    write(package / "settings.toml", "[memcard]\ndir = \"cards\"\n");
    require(settings.load() && settings.saves_folder() == package / "cards", "and settings.toml's own folder wins");
    fs::remove_all(package);
}

// The runtime's own keys are not filled in: game.toml must give the game the values the table starts from.
void runtime_defaults() {
    const fs::path source = DISRUPTOR_SOURCE_DIR;
    const auto game = PSXRecompV4::load_game_config(source / "game.toml");
    Settings as_game;
    as_game.has_supersampling = as_game.has_aspect_ratio = as_game.has_frame_interpolation = true;
    as_game.supersampling = game.runtime.video_supersampling;
    as_game.aspect_num = game.runtime.video_aspect_num;
    as_game.aspect_den = game.runtime.video_aspect_den;
    as_game.frame_interpolation = game.runtime.video_frame_interpolation;
    for (const char* key : {"video.supersampling", "video.aspect_ratio", "video.frame_interpolation"})
        require(option(key).get(as_game) == option(key).initial, "a row starts from another value than game.toml gives the game");
}
} // namespace

int main() {
    const fs::path folder = fs::temp_directory_path() /
                            ("disruptor-launcher-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        fs::remove_all(folder);
        fs::create_directories(folder);
        table();
        carried();
        needed();
        placements();
        antialiasing_list();
        draw_distance_list();
        fog_shape_list();
        described();
        absent(folder);
        antialiasing_from_file(folder);
        separate_vertical_from_file(folder);
        one_change(folder);
        saves_beside_game(folder);
        round_trip(folder);
    pick_keys_saved(folder);
        whole_range(folder);
        dependants(folder);
        reset_to_new(folder);
        unreadable(folder);
    started_over(folder);
        aspects(folder);
        rates(folder);
        overrides();
        languages(folder);
        log(folder);
        defaults_filled(folder);
        runtime_defaults();
        std::cout << "Launcher settings, reset, language discs and log tail tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return result;
}
