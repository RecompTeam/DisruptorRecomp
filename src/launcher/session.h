#pragma once

#include "achievements_account.h"
#include "activity.h"
#include "key_bindings.h"
#include "pad_bindings.h"
#include "launcher_flow.h"
#include "launcher_settings.h"
#include "player_files.h"
#include "state_upgrade.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace disruptor::launcher {

class GameProcess;

// The launcher without its window: the disc tasks on a worker, the game's process and the settings file. The program
// is in `program`, the player's files in `data`.
class Session {
public:
    Session(std::filesystem::path program, std::filesystem::path data, Activity& activity);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Reads the settings and checks the installed disc, as the window opens.
    void open(double display_rate);
    // Takes what the worker and the game did since the last call: once a frame.
    void poll();

    void take_image(const std::filesystem::path& image);
    void add_language_disc(const std::filesystem::path& image);
    // 0 is English, then the listed discs in their order.
    void choose_language(std::size_t index);
    void remove_language(std::size_t index);
    void play();
    void cancel();
    void make_problem_report();
    // Looks at the folder of an earlier copy on the worker; earlier() then holds what it found, for the player's word.
    void look_at_earlier(const std::filesystem::path& folder);
    void bring_earlier();
    void keep_earlier();
    const std::optional<EarlierCopy>& earlier() const { return earlier_; }
    void open_saves() const;
    void reset_page(Page page);
    // An unreadable settings.toml kept aside under another name, the settings a new installation's.
    void start_settings_over();
    // Why the last write of a settings file failed, empty once one succeeds: the page it was made on shows it.
    const std::wstring& write_failure() const { return write_failure_; }
    void set(const Option& option, int value);
    bool set_pick_keys(const settings::PickKeys& keys);
    // Player 1's keys, read again with load_keys() before they are shown.
    KeyBindings& keys() { return keys_; }
    // keybinds.ini's first keys and settings.toml's second keys, as the scheme in the file makes them.
    void load_keys();
    bool save_keys();
    // The keys held now and `picks` together: a failed write of the picks puts `undo` and `undo_second` back.
    bool store_keys(const KeyLayout& undo, const settings::PickKeys& picks, const KeyLayout& undo_second);
    PadBindings& pads() { return pads_; }
    AchievementsAccount& achievements() { return achievements_; }
    bool save_pads();
    // True when the window may close now. A running task is cancelled first, and closed() turns true when it ends.
    bool close();
    bool closed() const { return closed_; }

    bool busy() const { return busy_; }
    bool cancellable() const { return busy_ && !closing_; }
    // A game this launcher started, or one started before it opened or beside it on the same player's files.
    bool game_running() const { return game_ != nullptr || other_game_; }
    bool editable() const { return !busy_ && !game_running() && settings_.readable(); }
    bool playable() const { return !busy_ && !game_running() && verified_.profile; }
    const SettingsFile& settings() const { return settings_; }
    const std::wstring& status() const { return status_; }
    unsigned progress() const { return progress_; }
    std::wstring disc_text() const;
    LanguageList languages() const;
    const VerifiedDisc& verified() const { return verified_; }
    const std::filesystem::path& program() const { return program_; }
    const std::filesystem::path& data() const { return data_; }

    bool auto_play = true;

private:
    void begin(Task task, std::filesystem::path source, bool launch);
    void finish(Outcome& outcome);
    // Save states of an earlier build are tried on this one once a session, after the disc is verified.
    void begin_upgrade();
    void finish_upgrade(StateUpgrade& upgrade);
    struct Earlier {
        std::optional<EarlierCopy> copy;
        std::optional<Brought> brought;
        std::string error;
    };
    void begin_earlier(std::function<Earlier()> work);
    void finish_earlier(Earlier& earlier);
    void check_installed_disc();
    void launch();
    void look();
    void game_ended(unsigned long code);
    void watch_other_game();
    void say(const std::wstring& text);
    void show(const std::wstring& text);
    void saved(bool written, const std::wstring& done = {});
    const wchar_t* settings_error() const;

    std::filesystem::path program_;
    std::filesystem::path data_;
    KeyBindings keys_{data_ / "keybinds.ini"};
    PadBindings pads_{data_ / "input.ini"};
    AchievementsAccount achievements_{data_};
    Activity& activity_;
    SettingsFile settings_;
    std::thread worker_;
    std::atomic_bool cancelled_{false};
    bool busy_ = false, closing_ = false, closed_ = false, upgraded_ = false;
    std::unique_ptr<GameProcess> game_;
    bool other_game_ = false;
    VerifiedDisc verified_;
    Regions regions_; // filled by the worker only: an image on a share that is gone must not stall the window
    std::wstring status_;
    std::wstring write_failure_;
    unsigned progress_ = 0;
    std::mutex handed_mutex_;
    std::optional<std::pair<std::wstring, unsigned>> handed_update_;
    std::unique_ptr<Outcome> handed_outcome_;
    std::unique_ptr<StateUpgrade> handed_upgrade_;
    std::unique_ptr<Earlier> handed_earlier_;
    std::optional<EarlierCopy> earlier_;
};

} // namespace disruptor::launcher
