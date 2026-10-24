#include "game/achievements/requests.h"

#include <windows.h>
#include <winhttp.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace disruptor::achievements {
namespace {

constexpr int kTimeoutMilliseconds = 15000;
constexpr std::size_t kLargestBody = 16u << 20;

std::wstring widen(std::string_view text) {
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

class Handle {
public:
    explicit Handle(HINTERNET handle) : handle_(handle) {}
    ~Handle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_;
};

Response failure(int status, std::string_view what) {
    const DWORD code = GetLastError();
    return Response{status, {}, std::string(what) + " (WinHTTP error " + std::to_string(code) + ")"};
}

class WinHttp final : public Transport {
public:
    explicit WinHttp(const std::string& user_agent)
        : session_(WinHttpOpen(widen(user_agent).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                               WINHTTP_NO_PROXY_BYPASS, 0)) {
        if (!session_) return;
        WinHttpSetTimeouts(session_.get(), kTimeoutMilliseconds, kTimeoutMilliseconds, kTimeoutMilliseconds,
                           kTimeoutMilliseconds);
        DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        ready_ = WinHttpSetOption(session_.get(), WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never)) != FALSE;
    }

    Response perform(const Request& request) override {
        if (!session_ || !ready_) return failure(kFailed, "WinHTTP did not start");
        const std::wstring url = widen(request.url);
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) return failure(kFailed, "The address is not a URL");
        const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        const std::wstring path = std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength) +
                                  std::wstring(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        const Handle connection(WinHttpConnect(session_.get(), host.c_str(), parts.nPort, 0));
        if (!connection) return failure(kRetryable, "No connection");
        const bool post = !request.body.empty();
        const Handle call(WinHttpOpenRequest(connection.get(), post ? L"POST" : L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
        if (!call) return failure(kRetryable, "No request");
        const std::wstring headers = post ? L"Content-Type: " + widen(request.content_type) : std::wstring();
        const auto size = static_cast<DWORD>(request.body.size());
        void* body = post ? const_cast<char*>(request.body.data()) : WINHTTP_NO_REQUEST_DATA;
        if (!WinHttpSendRequest(call.get(), headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                static_cast<DWORD>(headers.size()), body, size, size, 0) ||
            !WinHttpReceiveResponse(call.get(), nullptr))
            return failure(kRetryable, "No answer");
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (!WinHttpQueryHeaders(call.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
            return failure(kRetryable, "No status");
        Response response{static_cast<int>(status), {}, {}};
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(call.get(), &available)) return failure(kRetryable, "The answer broke off");
            if (available == 0) break;
            if (response.body.size() + available > kLargestBody) return failure(kFailed, "The answer is too large");
            const std::size_t had = response.body.size();
            response.body.resize(had + available);
            DWORD read = 0;
            if (!WinHttpReadData(call.get(), response.body.data() + had, available, &read))
                return failure(kRetryable, "The answer broke off");
            response.body.resize(had + read);
        }
        return response;
    }

private:
    Handle session_;
    bool ready_ = false;
};

} // namespace

std::unique_ptr<Transport> platform_transport(const std::string& user_agent) {
    return std::make_unique<WinHttp>(user_agent);
}

} // namespace disruptor::achievements
