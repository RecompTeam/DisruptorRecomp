#include "game/settings/user_folder.h"

#include "user_dir.h"

#include <array>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

namespace fs = std::filesystem;

namespace disruptor::user_folder {

namespace {

constexpr std::array<const char*, 1> shipped = {"keybinds.ini"};

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

bool redirects(const fs::path& path, fs::file_type type) {
    if (type == fs::file_type::symlink) return true;
#ifdef _WIN32
    WIN32_FIND_DATAW info{};
    const HANDLE search = FindFirstFileW(path.c_str(), &info);
    if (search == INVALID_HANDLE_VALUE) return false;
    FindClose(search);
    return (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && IsReparseTagNameSurrogate(info.dwReserved0);
#else
    (void)path;
    return false;
#endif
}

void check_ancestors(fs::path path, const fs::path& program, const fs::path& requested) {
    if (program.empty()) return;
    while (!path.empty()) {
        std::error_code failed;
        if (fs::equivalent(path, program, failed))
            throw std::runtime_error("Move your player data outside the program folder before updating: " + text(requested));
        if (path == path.root_path()) break;
        path = path.parent_path();
    }
}

#ifdef _WIN32
fs::path substituted_drive(const fs::path& path) {
    const auto drive = path.root_name().wstring();
    if (drive.size() != 2 || drive.back() != L':') return {};
    std::wstring buffer(32768, L'\0');
    if (!QueryDosDeviceW(drive.c_str(), buffer.data(), static_cast<DWORD>(buffer.size())))
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), "Cannot resolve drive");
    const std::wstring_view target(buffer.c_str());
    if (target.starts_with(L"\\??\\UNC\\")) return fs::path(std::wstring(L"\\\\") + std::wstring(target.substr(8)));
    if (target.starts_with(L"\\??\\")) return fs::path(target.substr(4));
    return {};
}
#endif

fs::path physical_directory(const fs::path& path, const fs::path& protected_program, unsigned links = 0) {
    if (links > 40) throw std::runtime_error("Too many links in player data path: " + text(path));
    fs::path resolved = path.root_path();
#ifdef _WIN32
    if (const fs::path mapped = substituted_drive(path); !mapped.empty())
        resolved = physical_directory(mapped, protected_program, links + 1);
#endif
    check_ancestors(resolved, protected_program, path);
    const fs::path drive_root = resolved;
    for (const auto& part : path.relative_path()) {
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!fs::is_directory(resolved)) throw std::runtime_error("Cannot resolve the parent of " + text(resolved));
            if (resolved != drive_root) resolved = resolved.parent_path();
            continue;
        }
        resolved /= part;
        const auto type = kind(resolved);
        if (type == fs::file_type::not_found) continue;
        if (redirects(resolved, type)) {
            const auto target = fs::read_symlink(resolved);
            resolved = physical_directory(resolved.parent_path() / target, protected_program, links + 1);
            if (!fs::exists(resolved)) throw std::runtime_error("Broken player data link in " + text(path));
        }
        resolved = fs::canonical(resolved);
        if (!protected_program.empty() && fs::equivalent(resolved, protected_program))
            throw std::runtime_error("Move your player data outside the program folder before updating: " + text(path));
    }
    check_ancestors(resolved, protected_program, path);
    return resolved;
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
        return chosen;
    }
    switch (copy_of(program)) {
    case Copy::installed:
        if (local.empty()) throw std::runtime_error("The system did not say where this user's application data is");
        return (local / kInstalledName).lexically_normal();
    case Copy::portable: return (folder_of(program).parent_path() / "data").lexically_normal();
    case Copy::plain:
#ifndef _WIN32
        if (regular(program / ".portable")) return program / "data";
#endif
        break;
    }
    return program;
}

void require_update_safe_folder(const fs::path& program, const fs::path& user) {
    if (copy_of(program) != Copy::plain) (void)physical_directory(user, program);
}

void require_no_installed_inputs(const fs::path& program) {
    const fs::path input = program / "input";
    const bool has_input = fs::exists(input);
    for (const auto& entry : fs::recursive_directory_iterator(program)) {
        const auto type = entry.symlink_status().type();
        const bool link = redirects(entry.path(), type);
        if (!link && type == fs::file_type::directory) continue;
        const auto name = entry.path().lexically_relative(program);
        if (!link && name == fs::path("bios/openbios.bin")) continue;
        auto extension = text(name.extension());
        for (char& letter : extension)
            if (letter >= 'A' && letter <= 'Z') letter = static_cast<char>(letter - 'A' + 'a');
        if (link || (has_input && fs::equivalent(program / *name.begin(), input)) || extension == ".bin" || extension == ".cue" || extension == ".chd" ||
            extension == ".iso" || extension == ".img")
            throw std::runtime_error("Move the existing input files or links outside the program folder before updating: " + text(entry.path()));
    }
}

fs::path ensure_directory(const fs::path& program, const fs::path& chosen) {
    const fs::path protected_program = copy_of(program) == Copy::plain ? fs::path{} : program;
    const fs::path user = physical_directory(folder_for(program, chosen.empty() ? chosen_folder() : chosen, local_application_data()), protected_program);
    std::error_code failed;
    fs::create_directories(user, failed);
    if (failed || !fs::is_directory(user, failed))
        throw std::runtime_error("Cannot make the folder for your saves and settings, " + text(user) +
                                 (failed ? ": " + failed.message() : std::string()));
    const fs::path final = fs::canonical(user);
    require_update_safe_folder(program, final);
    return final;
}

void activate(const fs::path& program, const fs::path& user) {
    seed(program, user);
    Prepared& state = prepared();
    std::scoped_lock lock(state.mutex);
    state.program = program;
    state.user = user;
}

fs::path prepare(const fs::path& program, const fs::path& chosen) {
    const fs::path user = ensure_directory(program, chosen);
    activate(program, user);
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
