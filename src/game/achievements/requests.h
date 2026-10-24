#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace disruptor::achievements {

// rc_api's codes for a request that got no answer: the first is tried again, the second is not.
inline constexpr int kRetryable = -2;
inline constexpr int kFailed = -1;

struct Request {
    std::string url;
    std::string body;  // empty for a GET
    std::string content_type;
};

struct Response {
    int status = kFailed;  // the HTTP status, or one of the codes above
    std::string body;
    std::string error;  // why there is no answer, never the request's own text
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual Response perform(const Request& request) = 0;
};

// WinHTTP on Windows, libcurl loaded at run time elsewhere, neither following a redirect: a request carries a
// password or a token and goes to the address rcheevos built alone. One that cannot start answers kFailed.
std::unique_ptr<Transport> platform_transport(const std::string& user_agent);

// Sends requests one at a time on a thread of its own and calls each one's `done` there, in the order they were sent.
class Requests {
public:
    using Done = std::function<void(const Response&)>;

    explicit Requests(std::unique_ptr<Transport> transport);
    // The request in flight is finished and answered, the ones still queued and any sent later are dropped.
    void close();
    ~Requests();
    Requests(const Requests&) = delete;
    Requests& operator=(const Requests&) = delete;

    void send(Request request, Done done);

private:
    void run();

    std::unique_ptr<Transport> transport_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<Request, Done>> queue_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace disruptor::achievements
