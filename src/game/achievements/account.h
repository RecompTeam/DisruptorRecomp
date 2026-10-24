#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace disruptor::achievements {

// Apart from settings.toml, which a problem report carries: this file must never leave the machine.
inline constexpr const char* kAccountFile = "retroachievements.toml";

struct Account {
    std::string username;
    std::string token;  // the server's sign-in token, the password is never kept
};

// Windows seals the token for the signed-in Windows user (DPAPI), elsewhere the file is readable by its owner alone.
[[nodiscard]] std::optional<Account> read_account(const std::filesystem::path& folder);
// Throws std::runtime_error when the file cannot be written.
void write_account(const std::filesystem::path& folder, const Account& account);
// False when the file is still there.
[[nodiscard]] bool erase_account(const std::filesystem::path& folder);
// The player's page on retroachievements.org.
[[nodiscard]] std::string profile_address(std::string_view player);
// The game's page there: the US disc's hash is set 8876, and every region runs the US executable.
inline constexpr const char* kGamePage = "https://retroachievements.org/game/8876";

} // namespace disruptor::achievements
