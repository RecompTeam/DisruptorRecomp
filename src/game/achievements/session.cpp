#include "game/achievements/session.h"

#include "game/achievements/guest_memory.h"

#include "rc_client.h"
#include "rc_error.h"

#include <array>
#include <cctype>
#include <utility>

namespace disruptor::achievements {

namespace {

// rc_client's own warning achievements, as for an unknown client, count from this id up.
constexpr std::uint32_t kWarnings = 101000001;

std::string text_of(const char* text) { return text ? text : ""; }

} // namespace

struct SessionCallbacks {
    static Session& of(const rc_client_t* client) { return *static_cast<Session*>(rc_client_get_userdata(client)); }

    static uint32_t RC_CCONV read_memory(uint32_t address, uint8_t* buffer, uint32_t count, rc_client_t* client) {
        const GuestMemory& memory = of(client).memory_;
        return read_guest(address, buffer, count, memory.ram, memory.scratchpad);
    }

    static void RC_CCONV server_call(const rc_api_request_t* request, rc_client_server_callback_t callback,
                                     void* callback_data, rc_client_t* client) {
        Request sent{request->url, request->post_data ? request->post_data : "",
                     request->content_type ? request->content_type : ""};
        of(client).requests_.send(std::move(sent), [callback, callback_data](const Response& response) {
            rc_api_server_response_t answer{};
            answer.body = response.body.c_str();
            answer.body_length = response.body.size();
            answer.http_status_code = response.status;
            callback(&answer, callback_data);
        });
    }

    static void RC_CCONV message(const char* text, const rc_client_t* client) { of(client).log(text); }

    static void RC_CCONV logged_in(int result, const char* error, rc_client_t* client, void*) {
        Session& session = of(client);
        if (result == RC_OK) {
            const rc_client_user_t* user = rc_client_get_user_info(client);
            session.notify({NoticeKind::signed_in, user ? text_of(user->display_name) : "", {}, {}});
            session.begin_load();
            return;
        }
        if (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN || result == RC_ACCESS_DENIED) {
            session.notify({NoticeKind::sign_in_refused, error ? error : rc_error_str(result), {}, {}});
            return;
        }
        session.retry(true);
    }

    static void RC_CCONV game_loaded(int result, const char* error, rc_client_t* client, void*) {
        Session& session = of(client);
        const rc_client_game_t* game = rc_client_get_game_info(client);
        if (result == RC_OK && game && game->id == 0) {
            session.notify({NoticeKind::game_unknown, "RetroAchievements does not know this disc", {}, {}});
            return;
        }
        if (result == RC_OK) {
            std::array<char, 256> badge{};
            if (game) rc_client_game_get_image_url(game, badge.data(), badge.size());
            session.stale_ = true;
            session.notify({NoticeKind::game_found, game ? text_of(game->title) : "", session.progress(), badge.data()});
            return;
        }
        if (result == RC_NO_GAME_LOADED || result == RC_NOT_FOUND) {
            session.notify({NoticeKind::game_unknown, error ? error : rc_error_str(result), {}, {}});
            return;
        }
        session.retry(false);
    }

    static Notice about(NoticeKind kind, const rc_client_achievement_t* achievement, std::string text) {
        return {kind,
                text_of(achievement->title),
                std::move(text),
                text_of(achievement->badge_url),
                achievement->points,
                achievement->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL};
    }

    static void RC_CCONV event(const rc_client_event_t* event, rc_client_t* client) {
        Session& session = of(client);
        switch (event->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
            session.stale_ = true;
            if (event->achievement->id < kWarnings)
                session.notify(about(NoticeKind::unlocked, event->achievement, text_of(event->achievement->description)));
            break;
        case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
        case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
            session.stale_ = true;
            session.notify(about(NoticeKind::progress, event->achievement, event->achievement->measured_progress));
            break;
        case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
            session.notify({NoticeKind::progress_hidden, {}, {}, {}});
            break;
        case RC_CLIENT_EVENT_GAME_COMPLETED: {
            const rc_client_game_t* game = rc_client_get_game_info(client);
            std::array<char, 256> badge{};
            if (game) rc_client_game_get_image_url(game, badge.data(), badge.size());
            session.notify({NoticeKind::mastered, game ? text_of(game->title) : "", {}, badge.data()});
            break;
        }
        case RC_CLIENT_EVENT_DISCONNECTED: session.notify({NoticeKind::offline, {}, {}, {}}); break;
        case RC_CLIENT_EVENT_RECONNECTED: session.notify({NoticeKind::online, {}, {}, {}}); break;
        case RC_CLIENT_EVENT_RESET: session.log("the server asked for the game to be reset"); break;
        case RC_CLIENT_EVENT_SERVER_ERROR:
            if (event->server_error && event->server_error->error_message)
                session.log(std::string("server error: ") + event->server_error->error_message);
            break;
        default: break;
        }
    }
};

void Session::ClientDeleter::operator()(rc_client_t* client) const { rc_client_destroy(client); }

Session::Session(Account account, std::string hash, std::unique_ptr<Transport> transport, GuestMemory memory, Log log,
                 SessionOptions options)
    : account_(std::move(account)), hash_(std::move(hash)), memory_(memory), log_(std::move(log)),
      unofficial_(options.unofficial),
      client_(rc_client_create(&SessionCallbacks::read_memory, &SessionCallbacks::server_call)),
      requests_(std::move(transport)) {
    rc_client_t* client = client_.get();
    rc_client_set_userdata(client, this);
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_INFO, &SessionCallbacks::message);
    rc_client_set_event_handler(client, &SessionCallbacks::event);
    rc_client_set_allow_background_memory_reads(client, 0);
    rc_client_set_hardcore_enabled(client, options.hardcore ? 1 : 0);
    rc_client_set_encore_mode_enabled(client, options.encore ? 1 : 0);
    rc_client_set_unofficial_enabled(client, options.unofficial ? 1 : 0);
    rc_client_set_spectator_mode_enabled(client, options.spectator ? 1 : 0);
    begin_login();
}

// rcheevos 12.5 raises a mastery still pending while it unloads, after it has let go of the game, and reads the
// game it let go: one more frame raises it first, with no answer left to set another.
Session::~Session() {
    requests_.close();
    if (loaded()) rc_client_do_frame(client_.get());
}

void Session::begin_login() {
    rc_client_begin_login_with_token(client_.get(), account_.username.c_str(), account_.token.c_str(),
                                     &SessionCallbacks::logged_in, nullptr);
}

void Session::begin_load() {
    rc_client_begin_load_game(client_.get(), hash_.c_str(), &SessionCallbacks::game_loaded, nullptr);
}

void Session::retry(bool login) {
    const std::lock_guard lock(retry_mutex_);
    retry_at_ = frames_ + kRetryFrames;
    retry_login_ = login;
}

void Session::frame() {
    bool due = false;
    bool login = false;
    std::uint32_t frame = 0;
    {
        const std::lock_guard lock(retry_mutex_);
        frame = ++frames_;
        if (retry_at_ != 0 && frames_ >= retry_at_) {
            due = true;
            login = retry_login_;
            retry_at_ = 0;
        }
    }
    if (due) login ? begin_login() : begin_load();
    if (!loaded()) {
        rc_client_idle(client_.get());
        return;
    }
    rc_client_do_frame(client_.get());
    std::uint32_t left = watching_.load();
    while (left > 0 && !watching_.compare_exchange_weak(left, left - 1)) {}
    if (stale_.exchange(false) || (left > 0 && frame % kRefreshFrames == 0)) refresh();
}

bool Session::loaded() const { return rc_client_is_game_loaded(client_.get()) != 0; }

std::string Session::progress() const {
    if (!loaded()) return {};
    rc_client_user_game_summary_t summary{};
    rc_client_get_user_game_summary(client_.get(), &summary);
    return std::to_string(summary.num_unlocked_achievements) + " of " + std::to_string(summary.num_core_achievements) +
           " unlocked";
}

void Session::refresh() {
    std::vector<Entry> fresh;
    const int category =
        unofficial_ ? RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE_AND_UNOFFICIAL : RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE;
    if (rc_client_achievement_list_t* list =
            rc_client_create_achievement_list(client_.get(), category, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE)) {
        for (const rc_client_achievement_bucket_t& bucket : std::span(list->buckets, list->num_buckets)) {
            for (const rc_client_achievement_t* achievement : std::span(bucket.achievements, bucket.num_achievements)) {
                if (achievement->id >= kWarnings) continue;
                const bool unlocked = achievement->unlocked != RC_CLIENT_ACHIEVEMENT_UNLOCKED_NONE;
                fresh.push_back({achievement->id, text_of(achievement->title), text_of(achievement->description),
                                 text_of(achievement->badge_url), text_of(achievement->badge_locked_url),
                                 achievement->measured_progress, achievement->measured_percent, achievement->points,
                                 unlocked, achievement->category == RC_CLIENT_ACHIEVEMENT_CATEGORY_UNOFFICIAL});
            }
        }
        rc_client_destroy_achievement_list(list);
    }
    const std::lock_guard lock(list_mutex_);
    list_ = std::move(fresh);
}

std::vector<Entry> Session::achievements() const {
    const std::lock_guard lock(list_mutex_);
    return list_;
}

void Session::watch() { watching_.store(kWatchFrames); }

void Session::end_hardcore() { rc_client_set_hardcore_enabled(client_.get(), 0); }

bool Session::hardcore() const { return rc_client_get_hardcore_enabled(client_.get()) != 0; }

std::vector<std::uint8_t> Session::save_progress() {
    if (!loaded()) return {};
    std::vector<std::uint8_t> progress(rc_client_progress_size(client_.get()));
    if (progress.empty() ||
        rc_client_serialize_progress_sized(client_.get(), progress.data(), progress.size()) != RC_OK)
        return {};
    return progress;
}

bool Session::load_progress(std::span<const std::uint8_t> progress) {
    if (!loaded()) return false;
    stale_ = true;
    return rc_client_deserialize_progress_sized(client_.get(), progress.empty() ? nullptr : progress.data(),
                                                progress.size()) == RC_OK;
}

void Session::sign_out() {
    if (loaded()) rc_client_do_frame(client_.get());
    rc_client_logout(client_.get());
    {
        const std::lock_guard lock(list_mutex_);
        list_.clear();
    }
    notify({NoticeKind::signed_out, {}, {}, {}});
}

std::vector<Notice> Session::take_notices() {
    const std::lock_guard lock(notices_mutex_);
    return std::exchange(notices_, {});
}

void Session::notify(Notice notice) {
    if (notice.kind != NoticeKind::progress && notice.kind != NoticeKind::progress_hidden)
        log(notice.title.empty() ? notice.text : notice.title + (notice.text.empty() ? "" : ": " + notice.text));
    const std::lock_guard lock(notices_mutex_);
    notices_.push_back(std::move(notice));
}

void Session::log(std::string_view line) const {
    if (!log_) return;
    // The server names the player as it spells the name, which may differ in case from the account's.
    const auto lower = [](std::string text) {
        for (char& symbol : text) symbol = static_cast<char>(std::tolower(static_cast<unsigned char>(symbol)));
        return text;
    };
    std::string kept(line);
    for (const std::string* secret : {&account_.token, &account_.username}) {
        if (secret->empty()) continue;
        const std::string sought = lower(*secret);
        std::string folded = lower(kept);
        for (std::size_t at = folded.find(sought); at != std::string::npos; at = folded.find(sought, at + 8)) {
            kept.replace(at, sought.size(), "<player>");
            folded.replace(at, sought.size(), "<player>");
        }
    }
    log_(kept);
}

} // namespace disruptor::achievements
