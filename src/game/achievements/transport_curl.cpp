#include "game/achievements/requests.h"

#include <dlfcn.h>

#include <cstddef>
#include <mutex>
#include <string>

namespace disruptor::achievements {
namespace {

// libcurl is opened at run time, so a system without it still plays and only loses achievements. Numbers from curl.h.
constexpr int kUrl = 10002;
constexpr int kWriteData = 10001;
constexpr int kPostFields = 10015;
constexpr int kUserAgent = 10018;
constexpr int kHttpHeader = 10023;
constexpr int kWriteFunction = 20011;
constexpr int kTimeout = 13;
constexpr int kConnectTimeout = 78;
constexpr int kNoSignal = 99;
constexpr int kPostFieldSize = 60;
constexpr int kResponseCode = 0x200002;
constexpr long kGlobalAll = 3;
constexpr long kTimeoutSeconds = 15;
constexpr std::size_t kLargestBody = 16u << 20;

struct Slist;

struct Curl {
    void* (*easy_init)();
    int (*easy_setopt)(void*, int, ...);
    int (*easy_perform)(void*);
    int (*easy_getinfo)(void*, int, ...);
    void (*easy_cleanup)(void*);
    const char* (*easy_strerror)(int);
    int (*global_init)(long);
    Slist* (*slist_append)(Slist*, const char*);
    void (*slist_free_all)(Slist*);
};

template <typename Function>
bool take(void* library, const char* name, Function& function) {
    function = reinterpret_cast<Function>(dlsym(library, name));
    return function != nullptr;
}

bool open_curl(Curl& curl) {
    void* library = dlopen("libcurl.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!library) library = dlopen("libcurl-gnutls.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!library) return false;
    return take(library, "curl_easy_init", curl.easy_init) && take(library, "curl_easy_setopt", curl.easy_setopt) &&
           take(library, "curl_easy_perform", curl.easy_perform) &&
           take(library, "curl_easy_getinfo", curl.easy_getinfo) &&
           take(library, "curl_easy_cleanup", curl.easy_cleanup) &&
           take(library, "curl_easy_strerror", curl.easy_strerror) &&
           take(library, "curl_global_init", curl.global_init) &&
           take(library, "curl_slist_append", curl.slist_append) &&
           take(library, "curl_slist_free_all", curl.slist_free_all) && curl.global_init(kGlobalAll) == 0;
}

struct Answer {
    std::string body;
    bool too_large = false;
};

std::size_t collect(char* data, std::size_t size, std::size_t count, void* answer_pointer) {
    auto& answer = *static_cast<Answer*>(answer_pointer);
    const std::size_t bytes = size * count;
    if (answer.body.size() + bytes > kLargestBody) {
        answer.too_large = true;
        return 0;
    }
    answer.body.append(data, bytes);
    return bytes;
}

class LibCurl final : public Transport {
public:
    explicit LibCurl(std::string user_agent) : user_agent_(std::move(user_agent)) {}

    Response perform(const Request& request) override {
        static Curl curl{};
        static bool opened = false;
        static std::once_flag once;
        std::call_once(once, [] { opened = open_curl(curl); });
        if (!opened) return Response{kFailed, {}, "libcurl is not installed"};
        void* call = curl.easy_init();
        if (!call) return Response{kRetryable, {}, "libcurl did not start a request"};
        Answer answer;
        Slist* headers = nullptr;
        curl.easy_setopt(call, kUrl, request.url.c_str());
        curl.easy_setopt(call, kUserAgent, user_agent_.c_str());
        curl.easy_setopt(call, kWriteFunction, &collect);
        curl.easy_setopt(call, kWriteData, &answer);
        curl.easy_setopt(call, kNoSignal, 1L);
        curl.easy_setopt(call, kTimeout, kTimeoutSeconds);
        curl.easy_setopt(call, kConnectTimeout, kTimeoutSeconds);
        if (!request.body.empty()) {
            headers = curl.slist_append(nullptr, ("Content-Type: " + request.content_type).c_str());
            curl.easy_setopt(call, kHttpHeader, headers);
            curl.easy_setopt(call, kPostFieldSize, static_cast<long>(request.body.size()));
            curl.easy_setopt(call, kPostFields, request.body.c_str());
        }
        const int result = curl.easy_perform(call);
        long status = 0;
        curl.easy_getinfo(call, kResponseCode, &status);
        curl.easy_cleanup(call);
        curl.slist_free_all(headers);
        if (answer.too_large) return Response{kFailed, {}, "The answer is too large"};
        if (result != 0) return Response{kRetryable, {}, curl.easy_strerror(result)};
        return Response{static_cast<int>(status), std::move(answer.body), {}};
    }

private:
    std::string user_agent_;
};

} // namespace

std::unique_ptr<Transport> platform_transport(const std::string& user_agent) {
    return std::make_unique<LibCurl>(user_agent);
}

} // namespace disruptor::achievements
