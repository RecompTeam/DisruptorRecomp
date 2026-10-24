#pragma once

#include "game/achievements/account.h"
#include "game/achievements/requests.h"

#include <functional>
#include <string>

namespace disruptor::achievements {

struct LoginOutcome {
    bool signed_in = false;
    bool answered = false;  // the server answered, so `error` is its refusal and not the network's
    Account account;
    std::string display_name;
    std::string error;  // the server's own words when it gave any
};

// Trades the password for a sign-in token. `done` runs on the requests' thread, or at once without a name or password.
void begin_login(Requests& requests, const std::string& username, const std::string& password,
                 std::function<void(LoginOutcome)> done);
// The server's answer to a login request, apart so it can be checked without a network.
[[nodiscard]] LoginOutcome login_outcome(const Response& response);

} // namespace disruptor::achievements
