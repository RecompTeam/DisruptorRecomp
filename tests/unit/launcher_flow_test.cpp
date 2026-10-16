#include "choices.h"
#include "launcher_flow.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
#ifdef _WIN32
const fs::path kRoot = L"C:\\Games\\Disruptor";
const fs::path kImage = L"D:\\\u0414\u0438\u0441\u043A\u0438\\Disruptor (France).cue";
const std::string kGerman = "E:\\Disruptor (Germany).cue", kGone = "F:\\gone.cue", kUnlisted = "G:\\unlisted.cue";
#else
const fs::path kRoot = "/games/Disruptor";
const fs::path kImage = u8"/media/\u0414\u0438\u0441\u043A\u0438/Disruptor (France).cue";
const std::string kGerman = "/media/e/Disruptor (Germany).cue", kGone = "/media/f/gone.cue", kUnlisted = "/media/g/unlisted.cue";
#endif
const fs::path kHome = kRoot / "disc";

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
bool starts(const std::wstring& text, const wchar_t* with) { return text.rfind(with, 0) == 0; }
bool ends(const std::wstring& text, const std::wstring& with) {
    return text.size() >= with.size() && text.compare(text.size() - with.size(), with.size(), with) == 0;
}

// Stands in for the discs and writes down what a task asked of them.
struct Shelf {
    std::vector<std::string> calls;
    std::vector<std::wstring> reports;
    std::optional<RegionDisc> kind;       // what an image turns out to be
    std::string unreadable;               // a path whose look throws
    const char* refuse_import = nullptr;
    const char* refuse_package = nullptr;
    const char* refuse_check = nullptr;
    std::atomic_bool cancelled{false};
    bool cancel_in_import = false;
    DiscProfile profile{"SLUS-00224", L"Disruptor (USA)", 0, "", L"game.toml"};

    VerifiedDisc read(const char* how, const fs::path& source) {
        calls.push_back(std::string(how) + " " + utf8(source));
        if (refuse_import) throw std::runtime_error(refuse_import);
        if (cancel_in_import) cancelled = true;
        return {&profile, kHome, kHome / "Disruptor.cue", nullptr};
    }
    Discs discs() {
        Discs discs;
        discs.import = [this](const fs::path& source) { return read("import", source); };
        discs.verify = [this](const fs::path& source) { return read("verify", source); };
        discs.check_package = [this](const VerifiedDisc& disc) {
            calls.push_back("package");
            require(disc.profile == &profile, "the package is checked for the disc that was just read");
            if (refuse_package) throw std::runtime_error(refuse_package);
        };
        discs.look = [this](const fs::path& image) {
            calls.push_back("look " + utf8(image));
            if (utf8(image) == unreadable) throw std::runtime_error("The share is gone.");
            return kind;
        };
        discs.check = [this](const fs::path& home, const fs::path& image) {
            calls.push_back("check " + utf8(home) + " | " + utf8(image));
            if (refuse_check) throw std::runtime_error(refuse_check);
            return RegionDisc{"French", true};
        };
        discs.report = [this](const std::wstring& text, unsigned percent) { reports.push_back(text + L" " + std::to_wstring(percent)); };
        return discs;
    }
    Outcome run(Request request) { return disruptor::launcher::run(std::move(request), discs(), cancelled); }
    bool asked(const std::vector<std::string>& expected) const { return calls == expected; }
};

void test_the_game_disc_is_read_and_checked() {
    Shelf verify;
    Outcome outcome = verify.run({Task::verify, kHome / "Disruptor.cue", false, {}, kGerman, {kGerman}});
    require(outcome.task == Task::verify && outcome.error.empty() && outcome.disc.profile == &verify.profile && !outcome.launch,
            "an installed disc must come back verified");
    require(verify.asked({"verify " + utf8(kHome / "Disruptor.cue"), "package", "look " + kGerman}) && verify.reports.empty(),
            "without Play the language disc is described and not laid out");
    require(outcome.regions.size() == 1 && outcome.regions.count(kGerman) == 1, "every listed disc is described on the worker");

    Shelf import;
    outcome = import.run({Task::import, kImage, true, {}, "", {}});
    require(outcome.disc.profile && outcome.launch && import.asked({"import " + utf8(kImage), "package"}),
            "an import copies the image, and English needs no second disc");

    Shelf incomplete;
    incomplete.refuse_package = "The build is incomplete.";
    outcome = incomplete.run({Task::verify, kHome, true, {}, kGerman, {}});
    require(outcome.error == L"The build is incomplete." && !outcome.disc.profile && outcome.language_error.empty() &&
                incomplete.asked({"verify " + utf8(kHome), "package"}),
            "an incomplete package must refuse the disc before any language disc is read");

    Shelf unreadable;
    unreadable.refuse_import = "That is not the USA disc.";
    outcome = unreadable.run({Task::import, kImage, true, {}, kGerman, {kGerman}});
    require(outcome.error == L"That is not the USA disc." && !outcome.disc.profile &&
                unreadable.asked({"import " + utf8(kImage), "look " + kGerman}) && outcome.regions.size() == 1,
            "a refused image leaves no disc, and the listed discs are still described");
}

void test_play_checks_the_language_disc_again() {
    Shelf fitting;
    Outcome outcome = fitting.run({Task::verify, kHome, true, {}, kGerman, {kGerman}});
    require(outcome.error.empty() && outcome.language_error.empty() && outcome.disc.profile,
            "a fitting language disc must let the game start");
    require(fitting.asked({"verify " + utf8(kHome), "package", "check " + utf8(kHome) + " | " + kGerman, "look " + kGerman}),
            "before Play the language disc is laid over the disc that was just verified");
    require(fitting.reports == std::vector<std::wstring>{L"Checking the language disc... 100"}, "and the window is told so");

    Shelf moved;
    moved.refuse_check = "The image is gone.";
    outcome = moved.run({Task::verify, kHome, true, {}, kGerman, {}});
    require(outcome.error.empty() && outcome.disc.profile && outcome.language_error == L"The language disc cannot be used. The image is gone.",
            "a language disc that no longer fits keeps the game disc and is reported apart");

    Shelf cancelled;
    cancelled.cancel_in_import = true;
    outcome = cancelled.run({Task::import, kImage, true, {}, kGerman, {}});
    require(starts(outcome.error, L"Cancelled.") && !outcome.disc.profile && cancelled.asked({"import " + utf8(kImage), "package"}),
            "a cancelled import gives no disc and reads no language disc");
}

void test_an_image_is_taken_for_what_it_is() {
    Shelf game;
    Outcome outcome = game.run({Task::take, kImage, true, kHome, "", {kGerman}});
    require(outcome.task == Task::take && outcome.error.empty() && outcome.image == kImage && outcome.launch && !outcome.language &&
                game.asked({"look " + utf8(kImage), "look " + kGerman}),
            "an image that is no disc of another region is left for the import, untouched");

    Shelf early;
    early.kind = RegionDisc{"French", true};
    outcome = early.run({Task::take, kImage, false, {}, "", {}});
    require(outcome.task == Task::take && starts(outcome.error, L"That is a disc of another region.") && early.asked({"look " + utf8(kImage)}),
            "a language disc before the game disc is refused and is not laid out");

    Shelf language;
    language.kind = RegionDisc{"French", true};
    outcome = language.run({Task::take, kImage, false, kHome, "", {kGerman}});
    require(outcome.task == Task::language && outcome.error.empty() && outcome.language && outcome.language->language == "French",
            "a disc of another region becomes the language disc");
    require(language.asked({"look " + utf8(kImage), "check " + utf8(kHome) + " | " + utf8(kImage), "look " + kGerman, "look " + utf8(kImage)}) &&
                outcome.regions.count(utf8(kImage)) == 1 && outcome.regions.count(kGerman) == 1,
            "it is laid over the installed disc and described with the listed ones");

    Shelf unfit;
    unfit.refuse_check = "The disc does not fit.";
    outcome = unfit.run({Task::language, kImage, false, kHome, "", {kGerman}});
    require(outcome.task == Task::language && outcome.error == L"The disc does not fit." && !outcome.language &&
                unfit.asked({"check " + utf8(kHome) + " | " + utf8(kImage), "look " + kGerman}) && outcome.regions.count(utf8(kImage)) == 0,
            "a disc that does not fit is not named and not listed");

    Shelf look;
    outcome = look.run({Task::look, {}, false, kHome, kGerman, {kGerman, utf8(kImage)}});
    require(outcome.task == Task::look && outcome.error.empty() && look.asked({"look " + kGerman, "look " + utf8(kImage)}) &&
                outcome.regions.size() == 2 && !outcome.regions.at(kGerman),
            "a look only describes the listed discs, and an image it cannot name has no description");

    Shelf gone;
    gone.kind = RegionDisc{"French", true};
    gone.unreadable = kGerman;
    outcome = gone.run({Task::look, {}, false, kHome, kGerman, {kGerman, utf8(kImage)}});
    require(outcome.error.empty() && outcome.regions.size() == 2 && !outcome.regions.at(kGerman) && outcome.regions.at(utf8(kImage)),
            "a listed disc whose look fails is one that cannot be named, and the others are still described");
}

Outcome made(Task task, bool launch = false, const wchar_t* error = L"", const wchar_t* language_error = L"") {
    Outcome outcome;
    outcome.task = task;
    outcome.launch = launch;
    outcome.error = error;
    outcome.language_error = language_error;
    return outcome;
}
bool same(const Step& step, DiscAfter disc, bool import_next, bool save_language, bool launch) {
    return step.disc == disc && step.import_next == import_next && step.save_language == save_language && step.launch == launch;
}

void test_the_window_acts_on_an_outcome() {
    Step step = after(made(Task::look), false);
    require(same(step, DiscAfter::kept, false, false, false) && step.status.empty(), "a look changes nothing and says nothing");
    step = after(made(Task::look), true);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Cancelled.", "a cancelled look says so");

    step = after(made(Task::take, true), false);
    require(same(step, DiscAfter::kept, true, false, false) && step.status.empty(), "an image that is no language disc goes on to the import");
    step = after(made(Task::take, true), true);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Cancelled.", "unless the look was cancelled");
    step = after(made(Task::take, true, L"Install the USA disc first."), false);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Install the USA disc first.",
            "a refused image keeps the installed disc and is not imported");

    Outcome language = made(Task::language);
    language.language = RegionDisc{"German", true};
    step = after(language, false);
    require(same(step, DiscAfter::kept, false, true, false) && starts(step.status, L"German disc: ") &&
                ends(step.status, L"\nThe image is used where it is, so keep it there."),
            "a checked language disc is named in the settings and described");
    step = after(language, true);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"Cancelled. The language is as it was.",
            "a cancelled check changes nothing");
    step = after(made(Task::language), false);
    require(same(step, DiscAfter::kept, false, false, false), "an outcome without a disc names none");
    step = after(made(Task::language, false, L"The disc does not fit."), false);
    require(same(step, DiscAfter::kept, false, false, false) && step.status == L"The disc does not fit.", "a refused language disc changes nothing");

    step = after(made(Task::verify, true, L"The image is damaged."), false);
    require(same(step, DiscAfter::cleared, false, false, false) && step.status == L"The image is damaged.", "a failed check forgets the disc");
    step = after(made(Task::import, true), false);
    require(same(step, DiscAfter::taken, false, false, true) && step.status == L"Game data is installed and ready. Press Play to start.",
            "a verified disc is taken and the game is started when that was asked");
    step = after(made(Task::verify, false), false);
    require(same(step, DiscAfter::taken, false, false, false), "and is not started when it was not");
    step = after(made(Task::verify, true), true);
    require(same(step, DiscAfter::taken, false, false, false), "nor after Cancel");
    step = after(made(Task::verify, true, L"", L"The language disc cannot be used."), false);
    require(same(step, DiscAfter::taken, false, false, false) &&
                step.status == L"The language disc cannot be used.\nChoose English or another disc, then press Play.",
            "a language disc that no longer fits keeps the game from starting");
}

void test_the_running_status() {
    const std::wstring plain = running_text(L"", false);
    require(plain == L"Disruptor is running. You can close this launcher and keep playing.", "a plain start says the game runs");
    const std::wstring unwritten = running_text(L"", true), both = running_text(L"PSX_VSYNC", true);
    require(starts(unwritten, plain.c_str()) && unwritten.find(L"settings.toml cannot be written") != std::wstring::npos &&
                running_text(L"PSX_VSYNC", false).find(L"settings.toml") == std::wstring::npos,
            "a settings file that could not be completed is named, and only then");
    require(ends(both, L"run: PSX_VSYNC") && both.find(L"settings.toml cannot be written") != std::wstring::npos &&
                unwritten.find(L"PSX_") == std::wstring::npos,
            "the variables that replace settings are named, and only when there are some");
}

void test_the_status_line() {
    const std::wstring problem = L"Cannot read settings.toml.";
    require(shown_status(L"", L"").empty(), "a game closed as usual leaves no status line");
    require(shown_status(L"", problem) == problem, "an unreadable settings file still shows when nothing else is said");
    require(shown_status(L"Verified.", problem) == L"Verified.\n" + problem, "a settings problem goes under what was said");
    require(shown_status(problem, problem) == problem, "the settings problem is not said twice");
    require(shown_status(L"Verified.", L"") == L"Verified.", "readable settings add nothing");
}

void test_the_language_box() {
    const std::string french = utf8(kImage), missing = kGone;
    LanguageList list = language_list({}, {}, "");
    require(list.labels == std::vector<std::wstring>{L"English (the US disc alone)"} && list.selected == 0 && starts(list.note, L"Add a French"),
            "without discs the box offers English and says how to add one");

    Regions regions{{french, RegionDisc{"French", true}}, {missing, std::nullopt}};
    list = language_list({kGerman, french, missing}, regions, french);
    require(list.labels == std::vector<std::wstring>{L"English (the US disc alone)", L"Disc image (Disruptor (Germany).cue)",
                                                     L"French (Disruptor (France).cue)", L"Unknown or missing (gone.cue)"},
            "a disc is listed by its language and file name, one not looked at yet as a disc image");
    require(list.selected == 2 && starts(list.note, L"French disc: ") && ends(list.note, L" The image is used where it is."),
            "the disc in use is selected and described");
    list = language_list({kGerman, french, missing}, regions, missing);
    require(list.selected == 3 && starts(list.note, L"This image is missing"), "a missing disc in use is called missing");
    list = language_list({kGerman, french, missing}, regions, kGerman);
    require(list.selected == 1 && list.note.empty(), "a disc not looked at yet has no note");
    list = language_list({kGerman}, regions, kUnlisted);
    require(list.selected == 0 && starts(list.note, L"Add a French"), "a disc in use that is not listed leaves English selected");
}

void test_the_shown_version() {
    require(shown_version("v0.1.0-alpha.5") == "v0.1.0-alpha.5", "a release shows its tag alone");
    require(shown_version("v0.3.1") == "v0.3.1" && shown_version("v0.3.1-4-g43bcf0e") == "v0.3.1 (dev)",
            "a tag without a label too");
    require(shown_version("v0.1.0-alpha.4-76-g43bcf0e") == "v0.1.0-alpha.4 (dev)" &&
                shown_version("v0.1.0-alpha.4-76-g43bcf0e-dirty") == "v0.1.0-alpha.4 (dev)" &&
                shown_version("v0.1.0-alpha.5-dirty") == "v0.1.0-alpha.5 (dev)",
            "a build after a tag or with changes shows the tag it follows as a dev build");
    require(shown_version("v1.2.3-rc-1-2-gabc") == "v1.2.3-rc-1 (dev)", "a tag with dashes keeps them");
    require(shown_version("43bcf0e") == "dev build" && shown_version("unknown") == "dev build" && shown_version("") == "dev build",
            "a build without a tag says so");
}

bool valid_utf8(const std::string& text) {
    for (std::size_t at = 0; at < text.size();) {
        const auto lead = static_cast<unsigned char>(text[at]);
        const std::size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (length == 0 || at + length > text.size()) return false;
        for (std::size_t next = 1; next < length; ++next)
            if ((static_cast<unsigned char>(text[at + next]) & 0xC0) != 0x80) return false;
        at += length;
    }
    return true;
}

void test_fitted_text() {
    // One unit a code point: what a font of even letters would measure.
    const auto points = [](const std::string& text) {
        return static_cast<float>(std::count_if(text.begin(), text.end(), [](char byte) { return (static_cast<unsigned char>(byte) & 0xC0) != 0x80; }));
    };
    require(fitted("short", 10.0f, points) == "short", "a text that fits stays whole");
    for (const std::string letter : {"\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x8e\xae"}) {
        const std::string text = "ab" + letter + letter + letter + "cd";
        for (float room = 1.0f; room < 7.0f; room += 1.0f) {
            const std::string cut = fitted(text, room, points);
            require(valid_utf8(cut) && points(cut) <= room, "a cut text is whole code points and fits, two, three and four bytes alike");
            require(cut.ends_with("\xe2\x80\xa6"), "a cut text ends with an ellipsis");
        }
    }
    require(fitted("anything", 0.5f, points).empty(), "where not even the ellipsis fits nothing is left");
}

void test_the_title_bar() {
    const TitleBar bar{40.0f, 862.0f, 6.0f};
    const auto at = [&](float x, float y, bool maximized = false) { return hit_at(x, y, 1000.0f, 700.0f, bar, maximized); };
    require(at(500.0f, 20.0f) == Hit::drag && at(6.0f, 39.0f) == Hit::drag && at(861.0f, 20.0f) == Hit::drag,
            "the bar left of its buttons moves the window");
    require(at(862.0f, 20.0f) == Hit::client && at(500.0f, 40.0f) == Hit::client && at(500.0f, 400.0f) == Hit::client,
            "the buttons and everything under the bar are the window's own");
    require(at(500.0f, 0.0f) == Hit::top && at(930.0f, 5.0f) == Hit::top && at(500.0f, 699.0f) == Hit::bottom &&
                at(0.0f, 300.0f) == Hit::left && at(999.0f, 300.0f) == Hit::right,
            "a press near a side resizes the window, over a button at the top edge too");
    require(at(994.0f, 300.0f) == Hit::right && at(993.0f, 300.0f) == Hit::client && at(500.0f, 694.0f) == Hit::bottom &&
                at(500.0f, 693.0f) == Hit::client && at(5.0f, 300.0f) == Hit::left && at(6.0f, 300.0f) == Hit::client,
            "each side reaches exactly as far in as the edge");
    require(at(0.0f, 0.0f) == Hit::top_left && at(11.0f, 2.0f) == Hit::top_left && at(2.0f, 11.0f) == Hit::top_left &&
                at(999.0f, 0.0f) == Hit::top_right && at(988.0f, 3.0f) == Hit::top_right &&
                at(0.0f, 699.0f) == Hit::bottom_left && at(999.0f, 699.0f) == Hit::bottom_right && at(997.0f, 690.0f) == Hit::bottom_right,
            "near a corner a press takes the corner");
    require(at(12.0f, 2.0f) == Hit::top && at(2.0f, 12.0f) == Hit::left, "the corner reaches twice the edge and no further");
    require(at(0.0f, 0.0f, true) == Hit::drag && at(500.0f, 0.0f, true) == Hit::drag && at(999.0f, 0.0f, true) == Hit::client &&
                at(0.0f, 300.0f, true) == Hit::client && at(500.0f, 699.0f, true) == Hit::client,
            "a maximized window is not resized by its sides and its corner button reaches the screen's corner");
    require(at(-1.0f, 20.0f) == Hit::client && at(500.0f, 700.0f) == Hit::client, "a point outside the window is no part of it");
}

void test_the_launcher_choices() {
    const fs::path file = fs::temp_directory_path() / "disruptor-launcher-choices-test.ini";
    fs::remove(file);
    LauncherChoices none = read_choices(file);
    require(none.check_for_updates && !none.window, "without the file updates are checked and no place is known");
    LauncherChoices chosen;
    chosen.check_for_updates = false;
    chosen.window = WindowPlace{-1200, 40, 1100, 760, true};
    require(write_choices(file, chosen), "the choices are written");
    const LauncherChoices back = read_choices(file);
    require(!back.check_for_updates && back.window && back.window->x == -1200 && back.window->y == 40 &&
                back.window->width == 1100 && back.window->height == 760 && back.window->maximized,
            "they read back, a place on a monitor left of the main one included");
    for (const char* broken : {"window = 10 10 50 50 0", "window = 10 10 1000", "window = 10 10 1000 700 2", "window = a b c d e",
                               "window = 2147483647 10 1000 700 0", "window = 10 -2147483648 1000 700 0",
                               "window = 1000001 10 1000 700 0"}) {
        std::ofstream(file, std::ios::binary) << "check_for_updates = true\n" << broken << "\n";
        require(!read_choices(file).window, "a place too small, cut short, not numbers or beyond any desktop is forgotten");
    }
    fs::remove(file);
}

void test_the_command_line() {
    require(quote(L"a b") == L"\"a b\"" && quote(L"") == L"\"\"" && quote(L"C:\\a\\b") == L"\"C:\\a\\b\"",
            "an argument is quoted, a backslash inside it stays single");
    require(quote(L"C:\\a\\") == L"\"C:\\a\\\\\"" && quote(L"a\"b") == L"\"a\\\"b\"" && quote(L"a\\\"b") == L"\"a\\\\\\\"b\"",
            "backslashes before a quote are doubled and the quote escaped");
    DiscProfile profile{"SLUS-00224", L"Disruptor (USA)", 0, "", L"game.toml"};
    const fs::path program = L"C:\\Games folder\\Disruptor";
    const fs::path data = L"C:\\Users\\Player\\AppData\\Local\\DisruptorRecompData";
    const VerifiedDisc disc{&profile, data / "disc", data / "disc" / "Disruptor (USA).cue", nullptr};
    require(game_arguments(program, disc) ==
                std::vector<std::string>{"--no-launcher", "--game", utf8(program / "game.toml"), "--disc",
                                         utf8(data / "disc" / "Disruptor (USA).cue")},
            "the game is started without its own launcher, its config named in the program folder and the disc "
            "wherever the player's files are");
}

void test_the_sliders() {
    size_t logarithmic = 0, linear = 0;
    for (const Option& option : options()) {
        if (option.kind != Kind::slider) continue;
        if (option.unit != Unit::thousandths) {
            ++linear;
            require(to_position(option, option.lowest, option.highest) == option.lowest &&
                        from_position(option, option.highest, option.highest) == option.highest,
                    "a plain slider's position is its value");
            continue;
        }
        ++logarithmic;
        require(to_position(option, option.lowest, option.highest) == 0 && to_position(option, option.highest, option.highest) == slider_steps &&
                    from_position(option, 0, option.highest) == option.lowest && from_position(option, slider_steps, option.highest) == option.highest,
                "a sensitivity slider runs from its lowest value to its highest");
        int before = option.lowest - 1;
        for (int position = 0; position <= slider_steps; ++position) {
            const int value = from_position(option, position, option.highest);
            require(value >= before && value >= option.lowest && value <= option.highest, "a later position is never a lower value");
            require(position < 60 || to_position(option, value, option.highest) == position, "a position past the crowded low end keeps its value");
            before = value;
        }
        require(to_position(option, option.initial, option.highest) > 0 && to_position(option, option.initial, option.highest) < slider_steps,
                "the start value lies inside the slider");
    }
    require(logarithmic == 2 && linear >= 3, "the table must hold the sliders this test measures");
    const Option* sensitivity = find_option("disruptor.horizontal_sensitivity");
    const Option* volume = find_option("audio.master_volume");
    const Option* rate = find_option("video.frame_interpolation_fps");
    const Option* reach = find_option("disruptor.occlusion_reach");
    require(sensitivity && volume && rate && reach && value_text(*sensitivity, 80) == L"1.0x" && value_text(*sensitivity, 40) == L"0.50x" &&
                value_text(*sensitivity, 2000) == L"25x" &&
                value_text(*volume, 55) == L"55%" && value_text(*rate, 120) == L"120 FPS" && value_text(*reach, 240) == L"240",
            "a value is shown in its unit");
    const Option* deadzone = find_option("controller.deadzone");
    disruptor::settings::Settings stick;
    require(deadzone && disruptor::settings::stored(stick, *deadzone) == 37 && value_text(*deadzone, 37) == L"37%" &&
                deadzone->initial == static_cast<int>(std::lround(stick.deadzone * 100.0 / 32767.0)),
            "a fresh installation's dead zone is the runtime's, 12000 of 32767, shown as 37%");
    deadzone->set(stick, 50);
    require(deadzone->saved(stick) && stick.deadzone == 16384 && deadzone->get(stick) == 50,
            "a share of the stick is saved in the stick's raw units and reads back");
    deadzone->set(stick, deadzone->initial);
    require(stick.deadzone == 12000, "the start share, which a reset writes, is the runtime's own 12000");
}
} // namespace

int main() {
    try {
        test_the_game_disc_is_read_and_checked();
        test_play_checks_the_language_disc_again();
        test_an_image_is_taken_for_what_it_is();
        test_the_window_acts_on_an_outcome();
        test_the_running_status();
        test_the_status_line();
        test_the_language_box();
        test_the_command_line();
        test_the_shown_version();
        test_fitted_text();
        test_the_title_bar();
        test_the_launcher_choices();
        test_the_sliders();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "launcher flow: PASS\n";
    return 0;
}
