#pragma once

#include <filesystem>

// Velopack replaces the program folder on update and deletes it on uninstall, so installed copies and AppImages keep
// the player's files in the user's data folder, portable copies beside the program folder, other copies with the program.
namespace disruptor::user_folder {

enum class Copy { plain, installed, portable };

constexpr const char* kChosen = "PSX_USER_DIR";             // a whole path that wins over the rule
constexpr const char* kInstalledName = "DisruptorRecompData"; // under the user's local application or XDG data

// `current` beside Update.exe is installed (portable with `.portable` in its root), as is an AppImage's usr/bin.
Copy copy_of(const std::filesystem::path& program);

// `chosen` is PSX_USER_DIR, `local` the local application data or $XDG_DATA_HOME. Throws on a relative `chosen` or no `local`.
std::filesystem::path folder_for(const std::filesystem::path& program, const std::filesystem::path& chosen,
                                 const std::filesystem::path& local);

// Made when missing and seeded. Throws on failure, so files never fall back into a folder an update replaces.
std::filesystem::path prepare(const std::filesystem::path& program);

// Copies the shipped settings.toml and keybinds.ini that `user` lacks. Throws on failure.
void seed(const std::filesystem::path& program, const std::filesystem::path& user);

// `path` rebased from `program` to `user`, other paths as they are. The framework's rule, so `program` must exist.
std::filesystem::path moved_out(const std::filesystem::path& path, const std::filesystem::path& program,
                                const std::filesystem::path& user);

// The player's own `name` in `user` when there is one, else the one shipped in `program`.
std::filesystem::path own_or_shipped(const std::filesystem::path& program, const std::filesystem::path& user,
                                     const std::filesystem::path& name);

// Both return `name` alone before prepare() ran.
std::filesystem::path in_user_folder(const std::filesystem::path& name);
std::filesystem::path own_or_shipped(const std::filesystem::path& name);

} // namespace disruptor::user_folder
