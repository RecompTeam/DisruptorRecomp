#include "session.h"

#include "disc_import.h"
#include "game_process.h"
#include "platform.h"
#include "region_disc.h"
#include "report.h"
#include "startup_log.h"
#include "game/settings/game_mark.h"

#include <cwchar>
#include <exception>
#include <format>
#include <stdexcept>
#include <utility>

namespace fs = std::filesystem;

namespace disruptor::launcher {

namespace {

constexpr size_t log_lines = 8;

} // namespace

Session::Session(fs::path program, fs::path data, Activity& activity)
    : program_(std::move(program)), data_(std::move(data)), activity_(activity),
      settings_(data_ / "settings.toml", program_) {}

Session::~Session() {
    cancelled_ = true;
    if (worker_.joinable()) worker_.join();
}

void Session::open(double display_rate) {
    settings_.set_display_rate(display_rate);
    settings_.load();
    activity_.add("Your saves and settings are in " + utf8(data_));
    watch_other_game();
    if (!settings_.readable()) say(settings_error());
    check_installed_disc();
    look();
}

void Session::check_installed_disc() {
    try {
        auto installed = find_installed_disc(data_);
        if (!installed.empty()) {
            bool legacy = installed.parent_path() != installed_cue(data_, supported_discs().front()).parent_path();
            begin(legacy ? Task::import : Task::verify, installed, false);
        } else if (settings_.readable()) {
            say(L"No game data installed\nChoose your disc image to get started\n"
                L"It is copied and verified, and your original files are kept\n"
                L"Played an earlier copy? Bring saves brings its saves, settings and disc here");
        }
    } catch (const std::exception& error) { say(error_text(error)); }
}

void Session::poll() {
    watch_other_game();
    std::unique_ptr<Outcome> outcome;
    std::unique_ptr<StateUpgrade> upgrade;
    {
        std::scoped_lock lock(handed_mutex_);
        if (handed_update_) {
            if (!cancelled_) show(handed_update_->first);
            progress_ = handed_update_->second;
            handed_update_.reset();
        }
        outcome = std::move(handed_outcome_);
        upgrade = std::move(handed_upgrade_);
    }
    std::unique_ptr<Earlier> earlier;
    {
        std::scoped_lock lock(handed_mutex_);
        earlier = std::move(handed_earlier_);
    }
    if (outcome) finish(*outcome);
    if (upgrade) finish_upgrade(*upgrade);
    if (earlier) finish_earlier(*earlier);
    if (game_) {
        if (const auto code = game_->ended()) game_ended(*code);
    }
}

void Session::begin(Task task, fs::path source, bool launch) {
    if (worker_.joinable()) worker_.join();
    Request request{task, std::move(source), launch, verified_.data, settings_.language_disc(), settings_.language_discs()};
    if (task == Task::import || task == Task::verify) verified_ = {}; // release previous read locks before replacing data
    cancelled_ = false;
    busy_ = true;
    progress_ = 0;
    if (task != Task::look)
        say(task == Task::import ? L"Preparing to import your image..."
            : task == Task::verify ? L"Checking installed game data..."
            : task == Task::take ? L"Looking at the image..." : L"Checking the language disc...");
    worker_ = std::thread([this, request = std::move(request)]() mutable {
        unsigned previous = 101;
        std::wstring previous_status;
        const auto report = [this, &previous, &previous_status](const std::wstring& text, unsigned percent) {
            if (previous == percent && previous_status == text) return;
            previous = percent;
            previous_status = text;
            std::scoped_lock lock(handed_mutex_);
            handed_update_ = {text, percent};
        };
        Discs discs;
        discs.import = [&](const fs::path& image) { return import_disc(image, data_, cancelled_, report); };
        discs.verify = [&](const fs::path& cue) { return verify_disc(cue, cancelled_, report); };
        discs.check_package = [&](const VerifiedDisc& disc) { check_package(program_, *disc.profile); };
        discs.look = [](const fs::path& image) { return region_disc(image); };
        discs.check = [](const fs::path& home, const fs::path& image) { return check_region_disc(home, image); };
        discs.report = report;
        auto outcome = std::make_unique<Outcome>(run(std::move(request), discs, cancelled_));
        std::scoped_lock lock(handed_mutex_);
        handed_outcome_ = std::move(outcome);
    });
}

void Session::finish(Outcome& outcome) {
    if (worker_.joinable()) worker_.join();
    busy_ = false;
    if (closing_) {
        closed_ = true;
        return;
    }
    progress_ = 0;
    regions_ = std::move(outcome.regions);
    Step step = after(outcome, cancelled_);
    if (step.import_next) {
        begin(Task::import, outcome.image, outcome.launch);
        return;
    }
    if (step.disc == DiscAfter::cleared) verified_ = {};
    else if (step.disc == DiscAfter::taken) verified_ = std::move(outcome.disc);
    if (step.save_language && game_running())
        step.status = L"The game started meanwhile, so the language is not saved\nChoose it again once the game closes";
    else if (step.save_language && !settings_.choose_language_disc(utf8(outcome.image))) step.status = settings_error();
    if (!step.status.empty()) say(step.status);
    if (step.launch && !game_running()) launch();
    else if (verified_.profile && !upgraded_) begin_upgrade();
}

void Session::begin_upgrade() {
    upgraded_ = true;
    if (game_running()) return;
    if (worker_.joinable()) worker_.join();
    cancelled_ = false;
    busy_ = true;
    progress_ = 0;
    auto upgrade = std::make_unique<StateUpgrade>();
    upgrade->lines.reserve(16);
    worker_ = std::thread([this, disc = verified_, regions = regions_, upgrade = std::move(upgrade)]() mutable {
        const auto report = [this](const std::wstring& text, unsigned percent) {
            std::scoped_lock lock(handed_mutex_);
            handed_update_ = {text, percent};
        };
        try {
            *upgrade = upgrade_states(program_, data_, disc, settings_, regions, cancelled_, report);
        } catch (...) {
            upgrade->carried = upgrade->refused = 0;
            upgrade->untried = 1;
        }
        std::scoped_lock lock(handed_mutex_);
        handed_upgrade_ = std::move(upgrade);
    });
}

void Session::finish_upgrade(StateUpgrade& upgrade) {
    if (worker_.joinable()) worker_.join();
    busy_ = false;
    if (closing_) {
        closed_ = true;
        return;
    }
    progress_ = 0;
    for (const std::string& line : upgrade.lines) activity_.add(line);
    if (cancelled_) {
        say(L"Trying the save states was cancelled: they are tried again next time\nPress Play to start");
        return;
    }
    if (upgrade.carried + upgrade.refused + upgrade.untried == 0) return;
    std::string text;
    if (upgrade.carried > 0) text += std::format("{} save state(s) of an earlier version now load on this one. ", upgrade.carried);
    if (upgrade.refused > 0)
        text += std::format("{} do not: they stay as they were, and their originals are in saves\\old-states. ", upgrade.refused);
    if (upgrade.untried > 0) text += std::format("{} could not be tried and are tried again next time. ", upgrade.untried);
    say(wide(text + "Activity says which\nPress Play to start"));
}

void Session::begin_earlier(std::function<Earlier()> work) {
    if (worker_.joinable()) worker_.join();
    cancelled_ = false;
    busy_ = true;
    progress_ = 0;
    worker_ = std::thread([this, work = std::move(work)] {
        auto earlier = std::make_unique<Earlier>();
        try {
            *earlier = work();
        } catch (const std::exception& error) {
            earlier->error = error.what();
        }
        std::scoped_lock lock(handed_mutex_);
        handed_earlier_ = std::move(earlier);
    });
}

void Session::look_at_earlier(const fs::path& folder) {
    if (busy_ || game_running()) return;
    earlier_.reset();
    say(L"Looking at the earlier copy...");
    begin_earlier([this, folder] { return Earlier{look_at_copy(folder, program_, data_), std::nullopt, {}}; });
}

void Session::bring_earlier() {
    if (busy_ || game_running() || !earlier_) return;
    const EarlierCopy copy = std::move(*earlier_);
    earlier_.reset();
    verified_ = {}; // a disc that comes over must not be held open
    say(L"Bringing your files over...");
    begin_earlier([this, copy] {
        const auto copied = [this](std::uintmax_t done, std::uintmax_t total) {
            const unsigned percent = total ? static_cast<unsigned>(done * 100 / total) : 100;
            std::scoped_lock lock(handed_mutex_);
            handed_update_ = {L"Bringing your files over...", percent};
        };
        return Earlier{std::nullopt, bring_over(copy, data_, report_stamp(), cancelled_, copied), {}};
    });
}

void Session::keep_earlier() {
    earlier_.reset();
    say(L"Nothing was brought over");
}

void Session::finish_earlier(Earlier& earlier) {
    if (worker_.joinable()) worker_.join();
    busy_ = false;
    if (closing_) {
        closed_ = true;
        return;
    }
    progress_ = 0;
    if (!earlier.error.empty()) {
        say(wide(earlier.error));
        if (!verified_.profile) check_installed_disc();
        return;
    }
    if (earlier.copy) {
        earlier_ = std::move(earlier.copy);
        say(L"Found the files of an earlier copy\nBring them here?");
        return;
    }
    if (!earlier.brought) return;
    for (const fs::path& entry : earlier.brought->entries) activity_.add("Brought over: " + utf8(entry));
    if (!earlier.brought->replaced.empty())
        activity_.add("The files they took the place of are in " + utf8(earlier.brought->replaced));
    settings_.load();
    upgraded_ = false;
    say(L"Your files came over from the earlier copy");
    check_installed_disc();
}

void Session::launch() {
    try {
        game_ = std::make_unique<GameProcess>(program_, data_, verified_);
        say(running_text(environment_overrides()));
    } catch (const std::exception& error) {
        say(error_text(error));
    }
}

void Session::game_ended(unsigned long code) {
    const unsigned long long seconds = game_->seconds();
    game_.reset();
    settings_.load(); // the in-game menu writes the same file
    if (code == 0) {
        show({});
        activity_.add("Game closed");
    } else {
        wchar_t heading[160];
        swprintf(heading, 160, L"The game exited with an error (code 0x%08lX)\n"
                 L"If a runtime DLL is missing, install the Visual C++ x64 Redistributable", code);
        const std::wstring tail = log_tail(data_ / "startup.log", log_lines);
        say(tail.empty() ? std::wstring(heading) + L"\nHelp in the side bar has the troubleshooting steps"
                         : std::wstring(heading) + L"\nThe end of startup.log:\n" + tail);
    }
    note_exit(data_ / "startup.log", code, seconds);
    look();
}

void Session::watch_other_game() {
    if (game_ || game_mark::marked(data_) == other_game_) return;
    other_game_ = !other_game_;
    if (other_game_) {
        say(running_text({}));
        return;
    }
    settings_.load(); // the in-game menu writes the same file
    show({});
    activity_.add("Game closed");
    look();
}

void Session::look() {
    if (!busy_ && !settings_.language_discs().empty()) begin(Task::look, {}, false);
}

void Session::take_image(const fs::path& image) {
    if (!busy_ && !game_running()) begin(Task::take, image, auto_play);
}

void Session::add_language_disc(const fs::path& image) {
    if (busy_ || game_running()) return;
    if (verified_.profile) begin(Task::language, image, false);
    else say(L"Install the USA disc first: it is the game, and the other disc gives it a language");
}

void Session::choose_language(std::size_t index) {
    if (!editable()) return;
    const auto discs = settings_.language_discs();
    if (index == 0) {
        saved(settings_.choose_language_disc({}), L"The game is in English");
    } else if (index > discs.size()) {
        return;
    } else if (verified_.profile) {
        begin(Task::language, from_utf8(discs[index - 1]), false);
    } else {
        say(L"Install the USA disc first: it is the game, and the other disc gives it a language");
    }
}

void Session::remove_language(std::size_t index) {
    if (!editable()) return;
    const auto discs = settings_.language_discs();
    if (index > 0 && index <= discs.size())
        saved(settings_.forget_language_disc(discs[index - 1]), L"The disc is off the list\nIts file is left as it is");
}

void Session::play() {
    if (playable()) begin(Task::verify, verified_.mount, true);
}

void Session::cancel() {
    if (!busy_) return;
    cancelled_ = true;
    say(L"Cancelling...");
}

void Session::make_problem_report() {
    try {
        const Report report = make_report(data_, system_note(), report_stamp());
        platform::show_file(report.path);
        say((report.archived ? L"Report saved as " : L"Report saved in the folder ") + report.path.wstring() +
            L"\nAttach it to a bug report at github.com/RecompTeam/DisruptorRecomp/issues with the level and the steps\n"
            L"It holds the game's logs, your settings and a note of the system, with user names cut out of paths\n"
            L"Your disc image, BIOS and saves are never included");
    } catch (const std::exception& error) {
        say(wide(error.what()));
    }
}

void Session::open_saves() const { platform::open_folder(settings_.saves_folder()); }

void Session::start_settings_over() {
    if (busy_ || game_running()) return;
    fs::path kept;
    if (settings_.start_over(kept)) {
        write_failure_.clear();
        say(L"settings.toml could not be read, so it is kept as " + wide(utf8(kept.filename())) +
            (settings_.readable() ? L"\nThe settings are those of a new installation"
                                  : L"\nA new settings.toml came meanwhile and cannot be read either"));
    } else if (settings_.readable()) {
        say(L"settings.toml can be read again");
    } else {
        say(L"settings.toml could not be moved aside\nClose any program that has it open, then try again");
    }
}

void Session::reset_page(Page page) {
    if (editable()) saved(settings_.reset(page), L"The settings on this page are those of a new installation again");
}

void Session::set(const Option& option, int value) {
    if (!editable()) return;
    saved(settings_.set(option, value));
    // A change of scheme carries the keys along where they are the other scheme's own layout.
    if (std::string_view(option.key) != "disruptor.modern_controls") return;
    load_keys();
    const KeyLayout next = settings::layout_for_scheme(keys_.layout(), value == 1);
    if (next == keys_.layout()) return;
    keys_.use(next);
    save_keys();
}

bool Session::set_pick_keys(const settings::PickKeys& keys) {
    if (!editable()) return false;
    const bool written = settings_.set_pick_keys(keys);
    saved(written);
    return written;
}

void Session::load_keys() {
    keys_.load();
    keys_.use_second(settings_.second_keys(settings_.shown(*find_option("disruptor.modern_controls")) == 1));
}

// A file that could not be written is read again, so the page shows what the game will use.
bool Session::save_keys() {
    KeyBindings written(data_ / "keybinds.ini");
    written.load();
    if (!keys_.save()) {
        write_failure_ = L"Cannot write keybinds.ini\nCheck that the file and its folder can be written";
        say(write_failure_);
        load_keys();
        return false;
    }
    write_failure_.clear();
    const bool modern = settings_.shown(*find_option("disruptor.modern_controls")) == 1;
    if (keys_.second() == settings_.second_keys(modern)) return true;
    if (editable() && settings_.set_second_keys(keys_.second(), modern)) return true;
    written.save(); // a first key that moved to a second one goes back with it
    saved(false);
    load_keys();
    return false;
}

bool Session::store_keys(const KeyLayout& undo, const settings::PickKeys& picks, const KeyLayout& undo_second) {
    if (!save_keys()) return false;
    if (set_pick_keys(picks)) return true;
    keys_.use(undo);
    keys_.use_second(undo_second);
    save_keys();
    return false;
}

bool Session::save_pads() {
    if (pads_.save()) {
        write_failure_.clear();
        return true;
    }
    write_failure_ = L"Cannot write input.ini\nCheck that the file and its folder can be written";
    say(write_failure_);
    pads_.load();
    return false;
}

bool Session::close() {
    if (!busy_) return true;
    closing_ = true;
    cancelled_ = true;
    say(L"Finishing cancellation...");
    return false;
}

std::wstring Session::disc_text() const {
    return verified_.profile ? std::wstring(verified_.profile->label) + L", verified"
                             : L"Not installed\nBrowse to your USA disc image (SLUS-00224) or drop it on this window";
}

LanguageList Session::languages() const {
    return language_list(settings_.language_discs(), regions_, settings_.language_disc());
}

void Session::say(const std::wstring& text) {
    show(text);
    activity_.add(utf8(fs::path(text)));
}

void Session::show(const std::wstring& text) {
    status_ = shown_status(text, settings_.readable() ? L"" : settings_error());
}

void Session::saved(bool written, const std::wstring& done) {
    if (written) write_failure_.clear();
    else if (settings_.readable()) write_failure_ = settings_error();
    if (!written) say(settings_error());
    else if (!done.empty()) say(done);
}

const wchar_t* Session::settings_error() const {
    return settings_.readable()
        ? L"Cannot write settings.toml\nCheck that the file and its folder can be written"
        : L"settings.toml is not valid TOML, so it is left as it is\nFix or delete it, then reopen the launcher";
}

} // namespace disruptor::launcher
