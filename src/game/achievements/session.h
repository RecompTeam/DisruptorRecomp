#pragma once

#include "game/achievements/account.h"
#include "game/achievements/requests.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct rc_client_t;

namespace disruptor::achievements {

enum class NoticeKind {
    signed_in,
    sign_in_refused,  // the token is no longer good: sign in again in the launcher
    signed_out,
    game_found,
    game_unknown,
    unlocked,
    mastered,
    offline,
    online,
    progress,         // an achievement's count moved: `text` is how far it is
    progress_hidden,
    message,          // the client's own word to the player, in `text`
};

struct Notice {
    NoticeKind kind;
    std::string title;
    std::string text;
    std::string badge;  // the picture's address, empty when there is none
    std::uint32_t points = 0;
    bool unofficial = false;
};

struct GuestMemory {
    std::span<const std::uint8_t> ram;
    std::span<const std::uint8_t> scratchpad;
};

// What a session does besides unlocking, fixed when it starts.
struct SessionOptions {
    bool hardcore = false;
    bool encore = false;      // achievements already unlocked trigger again
    bool unofficial = false;  // the set's unofficial ones too, which the server does not record
    bool spectator = false;   // unlocks shown and never sent, for a developer build's pictures
};

// An achievement as the menu lists it.
struct Entry {
    std::uint32_t id = 0;
    std::string title;
    std::string description;
    std::string badge;
    std::string locked_badge;  // the grey one shown until it is unlocked
    std::string measured;  // "41/51" while it counts towards one, otherwise empty
    float percent = 0.0f;
    std::uint32_t points = 0;
    bool unlocked = false;
    bool unofficial = false;
};

// One run's RetroAchievements session over rc_client. Signs in with the account's token, loads the game by its disc
// hash, then evaluates achievements once a guest frame.
class Session {
public:
    using Log = std::function<void(std::string_view line)>;

    Session(Account account, std::string hash, std::unique_ptr<Transport> transport, GuestMemory memory, Log log,
            SessionOptions options = {});
    // The queued requests are dropped, so unlocks not yet sent are lost. On the memory's thread, the memory still there.
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // On the thread that owns the guest memory, once a guest frame.
    void frame();
    [[nodiscard]] bool loaded() const;
    // "12 of 52 unlocked" once the game is loaded, otherwise empty. Any thread.
    [[nodiscard]] std::string progress() const;
    // What happened since the last call, oldest first. Any thread.
    [[nodiscard]] std::vector<Notice> take_notices();
    // The set as last seen, refreshed a few times a second while someone watches. Any thread.
    [[nodiscard]] std::vector<Entry> achievements() const;
    // Asks for fresh counts for the next two seconds. Any thread.
    void watch();

    // The memory's thread, as are the three below. Ending hardcore needs no restart, starting it does.
    void end_hardcore();
    [[nodiscard]] bool hardcore() const;
    // The hit counts a save state needs to go on from where it was, empty before the game is loaded.
    [[nodiscard]] std::vector<std::uint8_t> save_progress();
    // Puts back what `save_progress` gave. Empty starts every achievement's counting over.
    bool load_progress(std::span<const std::uint8_t> progress);
    void sign_out();

private:
    struct ClientDeleter {
        void operator()(rc_client_t* client) const;
    };
    static constexpr std::uint32_t kRetryFrames = 60 * 60;
    static constexpr std::uint32_t kWatchFrames = 120;
    static constexpr std::uint32_t kRefreshFrames = 20;

    void begin_login();
    void begin_load();
    void retry(bool login);
    void notify(Notice notice);
    void log(std::string_view line) const;
    void refresh();

    Account account_;
    std::string hash_;
    GuestMemory memory_;
    Log log_;
    std::mutex notices_mutex_;
    std::vector<Notice> notices_;
    std::mutex retry_mutex_;
    std::uint32_t frames_ = 0;
    std::uint32_t retry_at_ = 0;  // a frame number, 0 for none
    bool retry_login_ = false;
    bool unofficial_ = false;
    std::atomic<bool> stale_{true};
    std::atomic<std::uint32_t> watching_{0};  // frames left
    mutable std::mutex list_mutex_;
    std::vector<Entry> list_;
    std::unique_ptr<rc_client_t, ClientDeleter> client_;
    // Last, so it is the first to go: its thread calls into the client while it runs.
    Requests requests_;

    friend struct SessionCallbacks;
};

} // namespace disruptor::achievements
