#include "game/achievements/game_client.h"

#include "game/achievements/account.h"
#include "game/achievements/badges.h"
#include "game/achievements/chime.h"
#include "game/achievements/guest_memory.h"
#include "game/achievements/policy.h"
#include "game/achievements/progress_file.h"
#include "game/achievements/session.h"
#include "game/achievements/sound_clip.h"
#include "game/achievements/user_agent.h"
#include "game/settings/user_folder.h"
#include "game_user_settings.h"
#include "debug_server.h"
#include "iso_overlay.h"
#include "iso_reader.h"
#include "lockstep.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

extern "C" uint8_t* memory_get_ram_ptr(void);
extern "C" uint8_t* memory_get_scratchpad_ptr(void);

namespace {

namespace achievements = disruptor::achievements;
namespace fs = std::filesystem;

// The image's own sectors, past any language overlay and mod patch, so neither changes the hash.
class GameDisc final : public achievements::DiscSectors {
public:
    explicit GameDisc(void* handle) : reader_(static_cast<PS1::ISOReader*>(handle)) {}

    bool read(std::uint32_t sector, std::uint8_t* user_data) override { return reader_->ReadSector(sector, user_data); }

    std::uint32_t first_sector() override { return reader_->TrackStartLBA(1); }

private:
    PS1::ISOReader* reader_;
};

enum class Stage { waiting, running, failed };

std::atomic<bool> g_enabled{true};
std::atomic<bool> g_retry{false};
std::atomic<bool> g_hardcore{false};
achievements::RunRecord g_record;
std::atomic<bool> g_sign_out{false};
// The emulation side's alone, as are the session's calls that need the guest memory.
Stage g_stage = Stage::waiting;
std::string g_hash;
// A state loaded before its progress could go back: the session was off or not yet loaded.
bool g_restore_due = false;
std::string g_restore_state;
// Kept to the program's end on purpose: an exit must not wait out a request in flight.
std::atomic<achievements::Session*> g_session{nullptr};
std::atomic<achievements::Badges*> g_badges{nullptr};
bool g_prefetched = false;
achievements::Chime g_chime;

std::mutex g_mutex;  // what follows, shared with the menu and the overlay
std::string g_status = "Not started";
std::string g_title;
std::string g_player;
achievements::Presentation g_shown;
achievements::SessionOptions g_options;
std::vector<achievements::Notice> g_popups;
std::optional<achievements::Notice> g_indicator;

void say(std::string_view line) { std::fprintf(stderr, "achievements: %.*s\n", static_cast<int>(line.size()), line.data()); }

void show(std::string line) {
    const std::lock_guard lock(g_mutex);
    g_status = std::move(line);
}

void pop(achievements::Notice notice) {
    const std::lock_guard lock(g_mutex);
    g_popups.push_back(std::move(notice));
}

void pop_line(std::string_view line) { pop({achievements::NoticeKind::message, "RetroAchievements", std::string(line), {}}); }

achievements::Presentation shown() {
    const std::lock_guard lock(g_mutex);
    return g_shown;
}

void take(achievements::Notice notice) {
    using achievements::NoticeKind;
    const achievements::Presentation presentation = shown();
    switch (notice.kind) {
    case NoticeKind::signed_in: {
        const std::lock_guard lock(g_mutex);
        g_player = notice.title;
        g_status = "Signed in, looking for the game";
        return;
    }
    case NoticeKind::sign_in_refused:
        show("Sign in again on the launcher's Achievements page");
        pop_line("Sign in again on the launcher's Achievements page");
        return;
    case NoticeKind::signed_out: {
        const std::lock_guard lock(g_mutex);
        g_player.clear();
        g_indicator.reset();
        g_status = "Signed out: sign in on the launcher's Achievements page";
        return;
    }
    case NoticeKind::game_unknown: show("RetroAchievements does not know this disc"); return;
    case NoticeKind::offline:
        show("Offline: unlocks are sent when the connection is back");
        if (presentation.popups) pop(std::move(notice));
        return;
    case NoticeKind::progress:
    case NoticeKind::progress_hidden: {
        const std::lock_guard lock(g_mutex);
        if (notice.kind == NoticeKind::progress && presentation.progress) g_indicator = std::move(notice);
        else g_indicator.reset();
        return;
    }
    case NoticeKind::game_found: {
        const std::lock_guard lock(g_mutex);
        g_title = notice.title;
    }
        [[fallthrough]];
    default: break;
    }
    if (notice.kind == NoticeKind::unlocked && presentation.sound) g_chime.start(achievements::Chime::Tune::unlock);
    if (notice.kind == NoticeKind::mastered && presentation.sound) g_chime.start(achievements::Chime::Tune::mastery);
    std::string title;
    {
        const std::lock_guard lock(g_mutex);
        title = g_title;
    }
    show(title + ": " + g_session.load()->progress());
    if (presentation.popups) pop(std::move(notice));
}

// The player's own unlock sound, chosen on the launcher's Achievements page: the chime when there is none or it cannot play.
void load_unlock_sound(const fs::path& folder) {
    std::string why;
    std::optional<achievements::Clip> clip = achievements::load_unlock_sound(folder, why);
    if (!clip) {
        g_chime.use(nullptr);
        if (!why.empty()) say("the unlock sound " + std::string(achievements::kUnlockSoundFile) + " does not play, " + why + ": the chime plays instead");
        return;
    }
    say("the unlock sound is the player's own, " + std::to_string(clip->frames() * 1000 / clip->rate) + " ms");
    g_chime.use(std::make_shared<const achievements::Clip>(std::move(*clip)));
}

void start() {
    void* disc = iso_game_disc();
    if (!disc) return;
    g_stage = Stage::failed;
    const fs::path folder = disruptor::user_folder::in_user_folder(achievements::kAccountFile).parent_path();
    const std::optional<achievements::Account> account = achievements::read_account(folder);
    if (!account) {
        say("no account, sign in on the launcher's Achievements page");
        show("Not signed in: sign in on the launcher, then switch RetroAchievements off and on");
        return;
    }
    GameDisc reader(disc);
    const std::optional<std::string> hash = achievements::playstation_hash(reader);
    if (!hash) {
        say("the disc's executable could not be read, so the game cannot be identified");
        show("The disc could not be identified");
        return;
    }
    say("disc " + *hash);
    g_hash = *hash;
    achievements::SessionOptions options;
    {
        const std::lock_guard lock(g_mutex);
        options = g_options;
    }
#if DISRUPTOR_DEV_TOOLS
    const char* hardcore = std::getenv("PSX_DISRUPTOR_ACHIEVEMENTS_HARDCORE");
    options.hardcore = hardcore && hardcore[0] == '1';
    const char* spectator = std::getenv("PSX_DISRUPTOR_ACHIEVEMENTS_SPECTATOR");
    options.spectator = spectator && spectator[0] == '1';
#endif
    if (options.hardcore && !g_record.may_start_hardcore()) {
        options.hardcore = false;
        say("hardcore needs a run watched from its start, so this one goes on in casual");
        pop_line("Hardcore needs a fresh start of the game: this run goes on in casual");
    }
    g_record.session_started();
    load_unlock_sound(folder);
    if (!g_badges.load())
        g_badges = new achievements::Badges(folder / "achievements" / "badges",
                                            achievements::platform_transport(achievements::user_agent()));
    const achievements::GuestMemory memory{{memory_get_ram_ptr(), achievements::kRamSize},
                                           {memory_get_scratchpad_ptr(), achievements::kScratchpadSize}};
    g_session = new achievements::Session(*account, *hash, achievements::platform_transport(achievements::user_agent()),
                                          memory, &say, options);
    g_hardcore = g_session.load()->hardcore();
    g_prefetched = false;
    g_stage = Stage::running;
    show("Signing in");
}

// Unlock pictures and the list's grey ones, fetched while the game plays so that most popups and the list find theirs.
void prefetch() {
    achievements::Badges* badges = g_badges.load();
    if (g_prefetched || !badges) return;
    const std::vector<achievements::Entry> list = g_session.load()->achievements();
    if (list.empty()) return;
    g_prefetched = true;
    for (const achievements::Entry& entry : list) {
        (void)badges->png(entry.badge);
        if (!entry.unlocked && !entry.locked_badge.empty()) (void)badges->png(entry.locked_badge);
    }
}

void guard_hardcore() {
    if (g_record.take_lapse() && g_hardcore.exchange(false)) {
        g_session.load()->end_hardcore();
        say("RetroAchievements was off for a while: achievements go on in casual");
        pop_line("RetroAchievements was off for a while: achievements go on in casual");
    }
    if (!g_hardcore.load()) return;
    const bool debugging = debug_server_is_connected() != 0;
    if (!debugging && g_ls_replay_active == 0) return;
    const achievements::Act act = debugging ? achievements::Act::debug_tools : achievements::Act::input_replay;
    g_session.load()->end_hardcore();
    g_hardcore = false;
    say(achievements::explain(act));
    pop_line(achievements::explain(act));
}

void sign_out() {
    const fs::path folder = disruptor::user_folder::in_user_folder(achievements::kAccountFile).parent_path();
    const bool forgotten = achievements::erase_account(folder);
    if (g_stage == Stage::running) {
        achievements::Session* session = g_session.load();
        session->sign_out();
        for (const achievements::Notice& notice : session->take_notices()) take(notice);
    }
    g_hardcore = false;
    g_stage = Stage::failed;
    if (forgotten) {
        show("Signed out: sign in on the launcher's Achievements page");
        return;
    }
    say("the account file could not be removed, so the next start signs in again");
    show("Signed out for this run only: the account file could not be removed");
    pop_line("The account file could not be removed: the next start signs in again");
}

// Puts back the counts of the state loaded last, once the session can take them, before it checks another frame.
void restore() {
    if (!g_restore_due || !g_enabled.load() || g_stage != Stage::running) return;
    achievements::Session* session = g_session.load();
    if (!session->loaded()) return;
    g_restore_due = false;
    std::optional<std::vector<std::uint8_t>> progress;
    if (!g_restore_state.empty()) {
        const fs::path state(g_restore_state);
        if (const std::optional<std::string> digest = achievements::state_digest(state))
            progress = achievements::read_progress(achievements::progress_file_for(state), g_hash, *digest);
    }
    const bool restored = session->load_progress(progress ? *progress : std::vector<std::uint8_t>{});
    say(progress && restored ? "the state's achievement progress was restored"
                             : "the state has no achievement progress of its own, so every count starts over");
}

} // namespace

namespace disruptor::achievements {

void configure(const PSXRecompV4::GameUserSettings& settings) {
    const std::lock_guard lock(g_mutex);
    g_shown.popups = settings.achievements_popups;
    g_shown.seconds = PSXRecompV4::kDisruptorPopupTimes[std::clamp(settings.achievements_popup_time, 0,
                                                                    PSXRecompV4::kDisruptorPopupTimeCount - 1)];
    g_shown.corner = std::clamp(settings.achievements_corner, 0, PSXRecompV4::kDisruptorPopupCornerCount - 1);
    g_shown.progress = settings.achievements_progress;
    g_shown.sound = settings.achievements_sound;
    g_options.encore = settings.achievements_encore;
    g_options.unofficial = settings.achievements_unofficial;
    if (!g_shown.progress) g_indicator.reset();
}

Presentation presentation() { return shown(); }

std::vector<Notice> take_popups() {
    const std::lock_guard lock(g_mutex);
    return std::exchange(g_popups, {});
}

std::optional<Notice> progress_indicator() {
    const std::lock_guard lock(g_mutex);
    return g_indicator;
}

std::vector<Entry> achievement_list() {
    Session* session = g_enabled.load() ? g_session.load() : nullptr;
    if (!session) return {};
    session->watch();
    return session->achievements();
}

std::shared_ptr<const std::string> badge(const std::string& url) {
    Badges* badges = g_badges.load();
    return badges && !url.empty() ? badges->png(url) : nullptr;
}

std::string player() {
    const std::lock_guard lock(g_mutex);
    return g_player;
}

} // namespace disruptor::achievements

extern "C" {

void disruptor_achievements_configure(int enabled) {
    if (!enabled) {
        g_record.switched_off();
        const std::lock_guard lock(g_mutex);
        g_indicator.reset();
    }
    g_enabled.store(enabled != 0);
    if (enabled) g_retry.store(true);
}

void disruptor_achievements_frame(void) {
    if (g_sign_out.exchange(false)) sign_out();
    if (!g_enabled.load()) return;
    if (g_retry.exchange(false) && g_stage == Stage::failed) g_stage = Stage::waiting;
    if (g_stage == Stage::waiting) start();
    if (g_stage != Stage::running) return;
    guard_hardcore();
    restore();
    achievements::Session* session = g_session.load();
    session->frame();
    for (const achievements::Notice& notice : session->take_notices()) take(notice);
    prefetch();
}

int disruptor_achievements_enabled(void) { return g_enabled.load() ? 1 : 0; }

int disruptor_achievements_hardcore(void) { return g_enabled.load() && g_hardcore.load() ? 1 : 0; }

int disruptor_achievements_allows(int act) {
    const auto asked = static_cast<achievements::Act>(act);
    if (achievements::judge(disruptor_achievements_hardcore() != 0, asked) == achievements::Verdict::allowed) {
        g_record.allowed(asked);
        return 1;
    }
    pop_line(achievements::explain(asked));
    return 0;
}

void disruptor_achievements_state_saving(const char *state) {
    if (state && *state) achievements::erase_progress(achievements::progress_file_for(fs::path(state)));
}

void disruptor_achievements_state_saved(const char *state, int written) {
    if (!written || !state || !*state || !g_enabled.load() || g_stage != Stage::running) return;
    const fs::path file = achievements::progress_file_for(fs::path(state));
    const std::vector<std::uint8_t> progress = g_session.load()->save_progress();
    const std::optional<std::string> digest = achievements::state_digest(fs::path(state));
    if (!progress.empty() && digest) (void)achievements::write_progress(file, g_hash, *digest, progress);
}

void disruptor_achievements_state_loaded(const char *state) {
    g_restore_state = state ? state : "";
    g_restore_due = true;
    restore();
}

void disruptor_achievements_sign_out(void) { g_sign_out.store(true); }

void disruptor_achievements_audio(int16_t *stereo, int frames, int rate) { g_chime.mix(stereo, frames, rate); }

void disruptor_achievements_status(char *line, int size) {
    if (!line || size <= 0) return;
    std::string text;
    if (!g_enabled.load()) {
        text = "Off";
    } else {
        const std::lock_guard lock(g_mutex);
        text = g_status;
    }
    std::snprintf(line, static_cast<std::size_t>(size), "%s", text.c_str());
}

} // extern "C"
