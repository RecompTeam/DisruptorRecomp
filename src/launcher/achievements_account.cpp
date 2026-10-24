#include "achievements_account.h"

#include "game/achievements/account.h"
#include "game/achievements/login.h"
#include "game/achievements/user_agent.h"

#include <exception>
#include <utility>

namespace disruptor::launcher {

AchievementsAccount::AchievementsAccount(std::filesystem::path folder, MakeTransport make_transport)
    : folder_(std::move(folder)), make_transport_(std::move(make_transport)) {
    if (!make_transport_)
        make_transport_ = [] { return achievements::platform_transport(achievements::user_agent()); };
    if (const auto account = achievements::read_account(folder_)) shown_ = {State::signed_in, account->username, {}};
}

AchievementsAccount::Shown AchievementsAccount::shown() const {
    const std::lock_guard lock(mutex_);
    return shown_;
}

bool AchievementsAccount::busy() const {
    const std::lock_guard lock(mutex_);
    return shown_.state == State::signing_in;
}

void AchievementsAccount::sign_in(const std::string& name, const std::string& password) {
    {
        const std::lock_guard lock(mutex_);
        if (shown_.state == State::signing_in) return;
        shown_ = {State::signing_in, name, {}};
        if (!requests_) requests_ = std::make_unique<achievements::Requests>(make_transport_());
    }
    achievements::begin_login(*requests_, name, password, [this](achievements::LoginOutcome outcome) {
        Shown next{State::signed_out, {}, std::move(outcome.error)};
        if (outcome.signed_in) {
            try {
                achievements::write_account(folder_, outcome.account);
                next = {State::signed_in, outcome.display_name, {}};
            } catch (const std::exception& error) {
                next.message = error.what();
            }
        }
        const std::lock_guard lock(mutex_);
        shown_ = std::move(next);
    });
}

void AchievementsAccount::sign_out() {
    const std::lock_guard lock(mutex_);
    if (shown_.state == State::signing_in) return;
    if (!achievements::erase_account(folder_)) {
        shown_.message = "The account file could not be removed, so the game would still sign in";
        return;
    }
    shown_ = {};
}

} // namespace disruptor::launcher
