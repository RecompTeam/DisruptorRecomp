#pragma once

#include <filesystem>

// Where a copy of the program keeps the player's files, said once for the launcher and for the game. Velopack replaces
// its program folder on every update and deletes the whole install folder on uninstall, so an installed copy keeps them
// in the user's own application data and a portable copy beside its program folder. Any other copy keeps them with the
// program, as a build folder or an unzipped release always has.
namespace disruptor::user_folder {

enum class Copy { plain, installed, portable };

constexpr const char* kChosen = "PSX_USER_DIR";             // a whole path that wins over the rule
constexpr const char* kInstalledName = "DisruptorRecompData"; // under the user's local application data

// Velopack's `current` beside Update.exe is an installed copy, or a portable one with `.portable` in its root.
Copy copy_of(const std::filesystem::path& program);

// The folder for the player's files of the copy in `program`. `chosen` is what PSX_USER_DIR says, `local` the user's
// local application data. Throws when `chosen` is not a whole path or an installed copy has no `local`.
std::filesystem::path folder_for(const std::filesystem::path& program, const std::filesystem::path& chosen,
                                 const std::filesystem::path& local);

// The folder for this process, made when missing and given the shipped defaults it lacks. Throws when that fails: the
// files must never fall back into a folder an update replaces.
std::filesystem::path prepare(const std::filesystem::path& program);

// Copies each shipped default, settings.toml and keybinds.ini, that `user` does not have. Throws when one cannot be,
// or when either folder cannot be looked at.
void seed(const std::filesystem::path& program, const std::filesystem::path& user);

// `path` with the part of it inside `program` placed in `user`, any other path as it is. The framework's own rule, which
// asks the file system whether the path lies inside, so the program folder must exist.
std::filesystem::path moved_out(const std::filesystem::path& path, const std::filesystem::path& program,
                                const std::filesystem::path& user);

// The player's own `name` in `user` when there is one, else the one shipped in `program`.
std::filesystem::path own_or_shipped(const std::filesystem::path& program, const std::filesystem::path& user,
                                     const std::filesystem::path& name);

// `name` in the folder prepare() made for this process, or `name` alone before it ran.
std::filesystem::path in_user_folder(const std::filesystem::path& name);
// The player's own `name` or the shipped one, for this process, or `name` alone before prepare() ran.
std::filesystem::path own_or_shipped(const std::filesystem::path& name);

} // namespace disruptor::user_folder
