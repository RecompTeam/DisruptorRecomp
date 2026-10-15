#include "game/settings/user_folder.h"

#include "user_dir.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace disruptor::user_folder {

namespace {

constexpr std::array<const char*, 2> shipped = {"settings.toml", "keybinds.ini"};

struct Prepared {
    std::mutex mutex;
    fs::path program;
    fs::path user;
};

Prepared& prepared() {
    static Prepared state;
    return state;
}

std::string text(const fs::path& path) {
    const std::u8string name = path.u8string();
    return {name.begin(), name.end()};
}

// What is at `path` without following a link. Throws when that cannot be told.
fs::file_type kind(const fs::path& path) {
    std::error_code failed;
    const fs::file_status status = fs::symlink_status(path, failed);
    if (status.type() == fs::file_type::not_found) return fs::file_type::not_found;
    if (failed) throw std::runtime_error("Cannot look at " + text(path) + ": " + failed.message());
    return status.type();
}

bool regular(const fs::path& path) { return kind(path) == fs::file_type::regular; }

// The folder itself, without the empty last element a trailing separator leaves.
fs::path folder_of(const fs::path& program) {
    const fs::path folder = program.lexically_normal();
    return folder.has_filename() ? folder : folder.parent_path();
}

bool named_current(const fs::path& folder) {
    std::string name = text(folder.filename());
    for (char& letter : name)
        if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
    return name == "current";
}

fs::path chosen_folder() {
#ifdef _WIN32
    const DWORD size = GetEnvironmentVariableW(L"PSX_USER_DIR", nullptr, 0);
    if (size == 0) return {};
    std::wstring value(size, L'\0');
    value.resize(GetEnvironmentVariableW(L"PSX_USER_DIR", value.data(), size));
    return value;
#else
    const char* value = std::getenv(kChosen);
    return value ? fs::path(value) : fs::path();
#endif
}

fs::path local_application_data() {
#ifdef _WIN32
    PWSTR found = nullptr;
    fs::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &found)) && found) folder = found;
    CoTaskMemFree(found);
    return folder;
#else
    const char* data = std::getenv("XDG_DATA_HOME");
    if (data && *data == '/') return data;
    const char* home = std::getenv("HOME");
    return home && *home == '/' ? fs::path(home) / ".local" / "share" : fs::path();
#endif
}

} // namespace

Copy copy_of(const fs::path& program) {
    const fs::path named = folder_of(program);
    const fs::path root = named.parent_path();
    if (text(named.filename()) == "bin" && text(root.filename()) == "usr" && regular(named / "sq.version") &&
        regular(named / "UpdateNix"))
        return Copy::installed;
    if (!named_current(named) || !regular(named / "sq.version") || !regular(root / "Update.exe")) return Copy::plain;
    return kind(root / ".portable") == fs::file_type::not_found ? Copy::installed : Copy::portable;
}

fs::path folder_for(const fs::path& program, const fs::path& chosen, const fs::path& local) {
    if (!chosen.empty()) {
        if (!chosen.is_absolute())
            throw std::runtime_error(std::string(kChosen) + " must name a whole path, from the drive on: " + text(chosen));
        return chosen.lexically_normal();
    }
    switch (copy_of(program)) {
    case Copy::installed:
        if (local.empty()) throw std::runtime_error("The system did not say where this user's application data is.");
        return (local / kInstalledName).lexically_normal();
    case Copy::portable: return (folder_of(program).parent_path() / "data").lexically_normal();
    case Copy::plain: break;
    }
    return program;
}

fs::path prepare(const fs::path& program) {
    const fs::path user = folder_for(program, chosen_folder(), local_application_data());
    std::error_code failed;
    fs::create_directories(user, failed);
    if (failed || !fs::is_directory(user, failed))
        throw std::runtime_error("Cannot make the folder for your saves and settings, " + text(user) +
                                 (failed ? ": " + failed.message() : std::string()));
    seed(program, user);
    Prepared& state = prepared();
    std::scoped_lock lock(state.mutex);
    state.program = program;
    state.user = user;
    return user;
}

void seed(const fs::path& program, const fs::path& user) {
    std::error_code failed;
    if (fs::equivalent(program, user, failed)) return;
    for (const char* name : shipped) {
        const fs::path from = program / name;
        const fs::path to = user / name;
        if (kind(to) != fs::file_type::not_found || !regular(from)) continue;
        fs::path partial = to;
        partial += ".partial-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        fs::copy_file(from, partial, failed);
        std::error_code ignored;
        if (!failed && fs::symlink_status(to, ignored).type() == fs::file_type::not_found) fs::rename(partial, to, failed);
        fs::remove(partial, ignored);
        if (failed) throw std::runtime_error("Cannot copy " + std::string(name) + " into " + text(user) + ": " + failed.message());
    }
}

fs::path moved_out(const fs::path& path, const fs::path& program, const fs::path& user) {
    return psx_moved_to_user_dir(path, program, user);
}

fs::path own_or_shipped(const fs::path& program, const fs::path& user, const fs::path& name) {
    std::error_code failed;
    return fs::is_regular_file(user / name, failed) ? user / name : program / name;
}

fs::path in_user_folder(const fs::path& name) {
    Prepared& state = prepared();
    std::scoped_lock lock(state.mutex);
    return state.user.empty() ? name : state.user / name;
}

fs::path own_or_shipped(const fs::path& name) {
    Prepared& state = prepared();
    std::scoped_lock lock(state.mutex);
    return state.user.empty() ? name : own_or_shipped(state.program, state.user, name);
}

} // namespace disruptor::user_folder
