#include "game/achievements/user_agent.h"

#include "rc_client.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

#include <array>
#include <cctype>

#ifndef DISRUPTOR_CLIENT_VERSION
#define DISRUPTOR_CLIENT_VERSION "0.0.0"
#endif

namespace disruptor::achievements {
namespace {

std::string system_name() {
#ifdef _WIN32
    using Query = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    // GetVersionEx answers 6.2 to a program without a manifest that names Windows 10, RtlGetVersion does not.
    const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    if (!query || query(&version) != 0) return "Windows";
    return "Windows " + std::to_string(version.dwMajorVersion) + "." + std::to_string(version.dwMinorVersion) + "." +
           std::to_string(version.dwBuildNumber);
#else
    utsname name{};
    if (uname(&name) != 0) return "Linux";
    return std::string(name.sysname) + " " + numeric_version(name.release);
#endif
}

} // namespace

std::string numeric_version(std::string_view text) {
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);
    if (text.empty() || !std::isdigit(static_cast<unsigned char>(text.front()))) return "0.0.0";
    std::size_t end = 0;
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) ||
                                 (text[end] == '.' && end + 1 < text.size() &&
                                  std::isdigit(static_cast<unsigned char>(text[end + 1])))))
        ++end;
    return std::string(text.substr(0, end));
}

std::string user_agent(std::string_view version, std::string_view system) {
    std::array<char, 64> library{};
    rc_client_get_user_agent_clause(nullptr, library.data(), library.size());
    return std::string(kClientName) + "/" + numeric_version(version) + " (" + std::string(system) + ") " +
           library.data();
}

std::string user_agent() { return user_agent(DISRUPTOR_CLIENT_VERSION, system_name()); }

} // namespace disruptor::achievements
