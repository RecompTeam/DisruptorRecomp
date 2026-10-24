#pragma once

#include "game/achievements/requests.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace disruptor::launcher {

// The RetroAchievements sign-in of the Achievements page. The game signs in with the account file it leaves.
class AchievementsAccount {
public:
    enum class State { signed_out, signing_in, signed_in };
    struct Shown {
        State state = State::signed_out;
        std::string name;
        std::string message;  // why the last sign-in or sign-out failed
    };
    using MakeTransport = std::function<std::unique_ptr<achievements::Transport>()>;

    // `make_transport` is the network unless a test gives its own.
    explicit AchievementsAccount(std::filesystem::path folder, MakeTransport make_transport = {});

    Shown shown() const;
    bool busy() const;
    // The password goes into this one request and is not kept.
    void sign_in(const std::string& name, const std::string& password);
    void sign_out();

private:
    std::filesystem::path folder_;
    MakeTransport make_transport_;
    mutable std::mutex mutex_;
    Shown shown_;
    std::unique_ptr<achievements::Requests> requests_;  // made at the first sign-in: no thread until one is needed
};

} // namespace disruptor::launcher
