#include "game/achievements/account.h"

#include "toml.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <exception>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

namespace disruptor::achievements {
namespace {

constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encode(std::string_view bytes) {
    std::string text;
    for (std::size_t at = 0; at < bytes.size(); at += 3) {
        const std::size_t left = std::min<std::size_t>(3, bytes.size() - at);
        std::uint32_t group = 0;
        for (std::size_t index = 0; index < 3; ++index)
            group = (group << 8) | (index < left ? static_cast<std::uint8_t>(bytes[at + index]) : 0u);
        for (std::size_t index = 0; index < 4; ++index)
            text += index <= left ? kAlphabet[(group >> (18 - 6 * index)) & 63u] : '=';
    }
    return text;
}

std::optional<std::string> decode(std::string_view text) {
    if (text.size() % 4 != 0) return std::nullopt;
    std::string bytes;
    for (std::size_t at = 0; at < text.size(); at += 4) {
        std::uint32_t group = 0;
        int padding = 0;
        for (std::size_t index = 0; index < 4; ++index) {
            const char symbol = text[at + index];
            const std::size_t value = kAlphabet.find(symbol);
            if (symbol == '=' && at + 4 == text.size() && index >= 2) {
                ++padding;
                group <<= 6;
                continue;
            }
            if (value == std::string_view::npos || padding) return std::nullopt;
            group = (group << 6) | static_cast<std::uint32_t>(value);
        }
        for (int index = 0; index < 3 - padding; ++index)
            bytes += static_cast<char>((group >> (16 - 8 * index)) & 0xFFu);
    }
    return bytes;
}

// Printable ASCII without quotes or backslashes is a TOML basic string as it stands.
bool plain(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char symbol) {
        return symbol > ' ' && symbol < 127 && symbol != '"' && symbol != '\\';
    });
}

#ifdef _WIN32
constexpr std::string_view kSealing = "dpapi";

std::optional<std::string> seal(std::string_view token) {
    DATA_BLOB plain_blob{static_cast<DWORD>(token.size()),
                         reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))};
    DATA_BLOB sealed{};
    if (!CryptProtectData(&plain_blob, L"RetroAchievements", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                          &sealed))
        return std::nullopt;
    std::string bytes(reinterpret_cast<const char*>(sealed.pbData), sealed.cbData);
    LocalFree(sealed.pbData);
    return encode(bytes);
}

bool write_private(const fs::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return file && file.write(text.data(), static_cast<std::streamsize>(text.size())) && file.flush();
}

std::optional<std::string> unseal(std::string_view text) {
    const std::optional<std::string> bytes = decode(text);
    if (!bytes) return std::nullopt;
    DATA_BLOB sealed{static_cast<DWORD>(bytes->size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes->data()))};
    DATA_BLOB opened{};
    if (!CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &opened))
        return std::nullopt;
    std::string token(reinterpret_cast<const char*>(opened.pbData), opened.cbData);
    SecureZeroMemory(opened.pbData, opened.cbData);
    LocalFree(opened.pbData);
    return token;
}
#else
constexpr std::string_view kSealing = "none";

std::optional<std::string> seal(std::string_view token) { return encode(token); }

std::optional<std::string> unseal(std::string_view text) { return decode(text); }

// Owner-only from its creation: a chmod after it would leave a moment for someone else to open it.
bool write_private(const fs::path& path, std::string_view text) {
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (descriptor < 0) return false;
    bool written = true;
    for (std::size_t at = 0; written && at < text.size();) {
        const ssize_t wrote = ::write(descriptor, text.data() + at, text.size() - at);
        if (wrote < 0 && errno == EINTR) continue;
        written = wrote > 0;
        if (written) at += static_cast<std::size_t>(wrote);
    }
    return ::close(descriptor) == 0 && written;
}
#endif

} // namespace

std::optional<Account> read_account(const fs::path& folder) {
    std::ifstream file(folder / kAccountFile, std::ios::binary);
    if (!file) return std::nullopt;
    try {
        const toml::value data = toml::parse(file, kAccountFile);
        if (toml::find<std::string>(data, "sealing") != kSealing) return std::nullopt;
        Account account{toml::find<std::string>(data, "username"), {}};
        const std::optional<std::string> token = unseal(toml::find<std::string>(data, "token"));
        if (!token || !plain(account.username) || !plain(*token)) return std::nullopt;
        account.token = *token;
        return account;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void write_account(const fs::path& folder, const Account& account) {
    if (!plain(account.username) || !plain(account.token))
        throw std::runtime_error("The account's name or token has characters it cannot have");
    const std::optional<std::string> sealed = seal(account.token);
    if (!sealed) throw std::runtime_error("Windows did not seal the sign-in token");
    const fs::path target = folder / kAccountFile;
    const fs::path written = folder / (std::string(kAccountFile) + ".new");
    const std::string text = "username = \"" + account.username + "\"\ntoken = \"" + *sealed + "\"\nsealing = \"" +
                             std::string(kSealing) + "\"\n";
    std::error_code error;
    fs::remove(written, error);
    if (!write_private(written, text)) {
        fs::remove(written, error);
        throw std::runtime_error("The account file cannot be written");
    }
    fs::rename(written, target, error);
    if (error) {
        fs::remove(written, error);
        throw std::runtime_error("The account file cannot be put in place");
    }
}

bool erase_account(const fs::path& folder) {
    std::error_code error;
    fs::remove(folder / kAccountFile, error);
    if (error) return false;
    const bool remains = fs::exists(folder / kAccountFile, error);
    return !error && !remains;
}

std::string profile_address(std::string_view player) {
    std::string address = "https://retroachievements.org/user/";
    for (const unsigned char letter : player) {
        if (std::isalnum(letter) || letter == '_' || letter == '-' || letter == '.') {
            address += static_cast<char>(letter);
            continue;
        }
        std::array<char, 4> escaped{};
        std::snprintf(escaped.data(), escaped.size(), "%%%02X", letter);
        address += escaped.data();
    }
    return address;
}

} // namespace disruptor::achievements
