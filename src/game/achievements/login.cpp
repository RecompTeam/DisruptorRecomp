#include "game/achievements/login.h"

#include "rc_api_user.h"
#include "rc_error.h"

#include <utility>

namespace disruptor::achievements {

LoginOutcome login_outcome(const Response& response) {
    LoginOutcome outcome;
    if (response.status < 0) {
        outcome.error = response.error.empty() ? "RetroAchievements did not answer" : response.error;
        return outcome;
    }
    outcome.answered = true;
    rc_api_server_response_t server{};
    server.body = response.body.c_str();
    server.body_length = response.body.size();
    server.http_status_code = response.status;
    rc_api_login_response_t login{};
    const int result = rc_api_process_login_server_response(&login, &server);
    if (result == RC_OK && login.response.succeeded && login.username && login.api_token) {
        outcome.signed_in = true;
        outcome.account = Account{login.username, login.api_token};
        outcome.display_name = login.display_name && *login.display_name ? login.display_name : login.username;
    } else if (login.response.error_message && *login.response.error_message) {
        outcome.error = login.response.error_message;
    } else {
        outcome.error = std::string("RetroAchievements gave an answer that cannot be read: ") + rc_error_str(result);
    }
    rc_api_destroy_login_response(&login);
    return outcome;
}

void begin_login(Requests& requests, const std::string& username, const std::string& password,
                 std::function<void(LoginOutcome)> done) {
    rc_api_login_request_t parameters{};
    parameters.username = username.c_str();
    parameters.password = password.c_str();
    rc_api_request_t api{};
    if (rc_api_init_login_request(&api, &parameters) != RC_OK) {
        rc_api_destroy_request(&api);
        LoginOutcome outcome;
        outcome.error = "A name and a password are both needed";
        done(std::move(outcome));
        return;
    }
    Request request{api.url, api.post_data ? api.post_data : "", api.content_type ? api.content_type : ""};
    rc_api_destroy_request(&api);
    requests.send(std::move(request), [done = std::move(done)](const Response& response) {
        done(login_outcome(response));
    });
}

} // namespace disruptor::achievements
