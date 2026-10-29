#include "game/settings/game_mark.h"

#ifdef _WIN32
#include <cwctype>
#include <functional>
#include <string>
#include <system_error>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace disruptor::game_mark {

#ifdef _WIN32

namespace {

// The mutex lives while any game has a handle to it: one name per folder, however the folder is written.
std::wstring name_of(const fs::path& user) {
    std::error_code failed;
    fs::path folder = fs::weakly_canonical(user, failed);
    if (failed) folder = user;
    std::wstring key = folder.wstring();
    for (wchar_t& letter : key) letter = static_cast<wchar_t>(std::towlower(letter));
    return L"Local\\DisruptorGame-" + std::to_wstring(std::hash<std::wstring>{}(key));
}

} // namespace

Mark::Mark(const fs::path& user) : handle_(CreateMutexW(nullptr, FALSE, name_of(user).c_str())) {}

Mark::~Mark() {
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
}

bool marked(const fs::path& user) {
    const HANDLE mark = OpenMutexW(SYNCHRONIZE, FALSE, name_of(user).c_str());
    if (mark) CloseHandle(mark);
    return mark || GetLastError() == ERROR_ACCESS_DENIED;
}

#else

// Games share the lock, a look for one takes it whole: it fails while any game holds it.
Mark::Mark(const fs::path& user) : descriptor_(open((user / ".game.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600)) {
    if (descriptor_ < 0) return;
    int taken = 0;
    do taken = flock(descriptor_, LOCK_SH);
    while (taken != 0 && errno == EINTR);
    if (taken != 0) {
        close(descriptor_);
        descriptor_ = -1;
    }
}

Mark::~Mark() {
    if (descriptor_ >= 0) close(descriptor_);
}

bool marked(const fs::path& user) {
    const int descriptor = open((user / ".game.lock").c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) return false;
    const bool held = flock(descriptor, LOCK_EX | LOCK_NB) != 0 && errno == EWOULDBLOCK;
    close(descriptor);
    return held;
}

#endif

} // namespace disruptor::game_mark
