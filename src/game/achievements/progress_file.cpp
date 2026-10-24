#include "game/achievements/progress_file.h"

#include "rhash/md5.h"

#include <array>
#include <fstream>
#include <iterator>
#include <system_error>

namespace disruptor::achievements {

namespace fs = std::filesystem;

namespace {

constexpr std::string_view kMagic = "DRAP2";
constexpr std::size_t kDigestLength = 32;
constexpr std::size_t kHeader = kMagic.size() + 2 * kDigestLength + 4;
constexpr std::size_t kLargest = 1 << 20;

} // namespace

fs::path progress_file_for(const fs::path& state) {
    fs::path file = state;
    file += ".achievements";
    return file;
}

std::optional<std::string> state_digest(const fs::path& state) {
    std::ifstream in(state, std::ios::binary);
    if (!in) return std::nullopt;
    md5_state_t md5;
    md5_init(&md5);
    std::array<char, 1 << 16> chunk{};
    while (in.read(chunk.data(), chunk.size()) || in.gcount() > 0)
        md5_append(&md5, reinterpret_cast<const md5_byte_t*>(chunk.data()), static_cast<int>(in.gcount()));
    if (in.bad()) return std::nullopt;
    std::array<md5_byte_t, 16> digest{};
    md5_finish(&md5, digest.data());
    std::string hex;
    for (const md5_byte_t byte : digest) {
        constexpr std::string_view kDigits = "0123456789abcdef";
        hex += kDigits[byte >> 4];
        hex += kDigits[byte & 15];
    }
    return hex;
}

void erase_progress(const fs::path& file) {
    std::error_code error;
    fs::remove(file, error);
}

bool write_progress(const fs::path& file, std::string_view hash, std::string_view state,
                    std::span<const std::uint8_t> progress) {
    erase_progress(file);
    if (hash.size() != kDigestLength || state.size() != kDigestLength || progress.empty() ||
        progress.size() > kLargest)
        return false;
    std::string bytes(kMagic);
    bytes += hash;
    bytes += state;
    for (int shift = 0; shift < 32; shift += 8) bytes += static_cast<char>((progress.size() >> shift) & 0xFF);
    bytes.append(reinterpret_cast<const char*>(progress.data()), progress.size());
    fs::path written = file;
    written += ".new";
    {
        std::ofstream out(written, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out.flush()) {
            out.close();
            erase_progress(written);
            return false;
        }
    }
    std::error_code error;
    fs::rename(written, file, error);
    if (!error) return true;
    erase_progress(written);
    return false;
}

std::optional<std::vector<std::uint8_t>> read_progress(const fs::path& file, std::string_view hash,
                                                       std::string_view state) {
    std::error_code error;
    const auto size = fs::file_size(file, error);
    if (error || size <= kHeader || size > kHeader + kLargest || hash.size() != kDigestLength ||
        state.size() != kDigestLength)
        return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() != size || !bytes.starts_with(kMagic) ||
        bytes.compare(kMagic.size(), kDigestLength, hash) != 0 ||
        bytes.compare(kMagic.size() + kDigestLength, kDigestLength, state) != 0)
        return std::nullopt;
    std::size_t length = 0;
    for (int at = 0; at < 4; ++at)
        length |= static_cast<std::size_t>(static_cast<unsigned char>(bytes[kHeader - 4 + at])) << (8 * at);
    if (length != bytes.size() - kHeader) return std::nullopt;
    return std::vector<std::uint8_t>(bytes.begin() + kHeader, bytes.end());
}

} // namespace disruptor::achievements
