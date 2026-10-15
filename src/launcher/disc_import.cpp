#include "disc_import.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;
namespace disruptor::launcher {
namespace {
constexpr DiscProfile discs[] = {
    {"SLUS-00224", L"Disruptor (USA, SLUS-00224)", 636350064,
     "3b49f9874e30c613ca9d17720716764cd76d0ac968c0acd0f53159366c0cf3a4",
     L"game.toml"},
};

void check_cancel(const std::atomic_bool& cancelled) {
    if (cancelled) throw std::runtime_error("Cancelled. Your original image has been kept.");
}

void report(const Progress& progress, const wchar_t* text, unsigned percent) {
    if (progress) progress(text, percent);
}

constexpr const char* cannot_create = "Cannot create game data. Extract the build into a writable folder with enough free space.";
constexpr const char* cannot_open = "Cannot read the disc image. Check that it exists and is not being changed by another program.";

#ifdef _WIN32
// Opened for reading, the file shares reading only: no program can change it while it is held.
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit File(const fs::path& path, bool write = false) {
        handle = CreateFileW(path.c_str(), write ? GENERIC_WRITE : GENERIC_READ,
            write ? 0 : FILE_SHARE_READ, nullptr,
            write ? CREATE_NEW : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error(write ? cannot_create : cannot_open);
    }
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    uint64_t size() const {
        LARGE_INTEGER result{};
        if (!GetFileSizeEx(handle, &result) || result.QuadPart < 0)
            throw std::runtime_error("Cannot read the image size.");
        return static_cast<uint64_t>(result.QuadPart);
    }
    bool read(unsigned char* bytes, size_t size, size_t& got) {
        DWORD read = 0;
        const bool done = ReadFile(handle, bytes, static_cast<DWORD>(size), &read, nullptr);
        got = read;
        return done;
    }
    bool write(const unsigned char* bytes, size_t size) {
        DWORD written = 0;
        return WriteFile(handle, bytes, static_cast<DWORD>(size), &written, nullptr) && written == size;
    }
    bool flush() { return FlushFileBuffers(handle); }
};
#else
// POSIX has no lock that holds other programs off, so a file changed while it is read fails its hash instead.
struct File {
    int descriptor = -1;
    explicit File(const fs::path& path, bool write = false) {
        descriptor = write ? open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644) : open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (descriptor < 0) throw std::runtime_error(write ? cannot_create : cannot_open);
#ifdef POSIX_FADV_SEQUENTIAL
        if (!write) posix_fadvise(descriptor, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
    }
    ~File() { if (descriptor >= 0) close(descriptor); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    uint64_t size() const {
        struct stat status{};
        if (fstat(descriptor, &status) != 0 || status.st_size < 0) throw std::runtime_error("Cannot read the image size.");
        return static_cast<uint64_t>(status.st_size);
    }
    bool read(unsigned char* bytes, size_t size, size_t& got) {
        got = 0;
        while (got < size) {
            const ssize_t part = ::read(descriptor, bytes + got, size - got);
            if (part < 0 && errno == EINTR) continue;
            if (part < 0) return false;
            if (part == 0) break;
            got += static_cast<size_t>(part);
        }
        return true;
    }
    bool write(const unsigned char* bytes, size_t size) {
        for (size_t done = 0; done < size;) {
            const ssize_t part = ::write(descriptor, bytes + done, size - done);
            if (part < 0 && errno == EINTR) continue;
            if (part <= 0) return false;
            done += static_cast<size_t>(part);
        }
        return true;
    }
    bool flush() { return fsync(descriptor) == 0; }
};
#endif

std::string hex_of(std::span<const unsigned char> digest) {
    std::string hex;
    for (const auto byte : digest) {
        hex += "0123456789abcdef"[byte >> 4];
        hex += "0123456789abcdef"[byte & 15];
    }
    return hex;
}

#ifdef _WIN32
struct Sha256 {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<unsigned char> object;
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Windows could not initialize SHA-256 verification.");
        DWORD size = 0, got = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&size), sizeof(size), &got, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Windows could not initialize SHA-256 verification.");
        }
        object.resize(size);
        if (BCryptCreateHash(algorithm, &hash, object.data(), size, nullptr, 0, 0) < 0) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("Windows could not initialize SHA-256 verification.");
        }
    }
    ~Sha256() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    void add(unsigned char* bytes, size_t size) {
        if (BCryptHashData(hash, bytes, static_cast<ULONG>(size), 0) < 0)
            throw std::runtime_error("SHA-256 verification failed.");
    }
    std::string finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("SHA-256 verification failed.");
        return hex_of(digest);
    }
};
#else
// FIPS 180-4.
struct Sha256 {
    static constexpr std::array<uint32_t, 64> rounds = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    std::array<uint32_t, 8> state = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<unsigned char, 64> block{};
    size_t filled = 0;
    uint64_t length = 0;

    static uint32_t rotate(uint32_t value, int by) { return (value >> by) | (value << (32 - by)); }

    void compress(const unsigned char* bytes) {
        std::array<uint32_t, 64> words{};
        for (int at = 0; at < 16; ++at)
            words[at] = uint32_t{bytes[at * 4]} << 24 | uint32_t{bytes[at * 4 + 1]} << 16 | uint32_t{bytes[at * 4 + 2]} << 8 | bytes[at * 4 + 3];
        for (int at = 16; at < 64; ++at) {
            const uint32_t low = rotate(words[at - 15], 7) ^ rotate(words[at - 15], 18) ^ (words[at - 15] >> 3);
            const uint32_t high = rotate(words[at - 2], 17) ^ rotate(words[at - 2], 19) ^ (words[at - 2] >> 10);
            words[at] = words[at - 16] + low + words[at - 7] + high;
        }
        auto [a, b, c, d, e, f, g, h] = state;
        for (int at = 0; at < 64; ++at) {
            const uint32_t first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + rounds[at] + words[at];
            const uint32_t second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + first;
            d = c;
            c = b;
            b = a;
            a = first + second;
        }
        const std::array<uint32_t, 8> added = {a, b, c, d, e, f, g, h};
        for (size_t at = 0; at < state.size(); ++at) state[at] += added[at];
    }

    void add(const unsigned char* bytes, size_t size) {
        length += size;
        if (filled) {
            const size_t taken = std::min(size, block.size() - filled);
            std::copy_n(bytes, taken, block.begin() + static_cast<std::ptrdiff_t>(filled));
            filled += taken;
            bytes += taken;
            size -= taken;
            if (filled < block.size()) return;
            compress(block.data());
            filled = 0;
        }
        for (; size >= block.size(); bytes += block.size(), size -= block.size()) compress(bytes);
        std::copy_n(bytes, size, block.begin());
        filled = size;
    }

    std::string finish() {
        const uint64_t bits = length * 8;
        const unsigned char marker = 0x80;
        add(&marker, 1);
        const unsigned char zero = 0;
        while (filled != 56) add(&zero, 1);
        std::array<unsigned char, 8> count{};
        for (int at = 0; at < 8; ++at) count[static_cast<size_t>(at)] = static_cast<unsigned char>(bits >> (56 - 8 * at));
        add(count.data(), count.size());
        std::array<unsigned char, 32> digest{};
        for (size_t at = 0; at < digest.size(); ++at) digest[at] = static_cast<unsigned char>(state[at / 4] >> (24 - 8 * (at % 4)));
        return hex_of(digest);
    }
};
#endif

// CUE files are usually UTF-8, or on Windows the system ANSI encoding and elsewhere Latin-1. Installed filenames
// are ASCII, so the game's existing narrow-path loader can read them too.
std::wstring decode(const std::string& text) {
#ifndef _WIN32
    std::wstring result;
    for (size_t at = 0; at < text.size();) {
        const auto lead = static_cast<unsigned char>(text[at]);
        const size_t length = lead < 0x80 ? 1 : (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 0;
        char32_t code = length == 1 ? lead : length == 2 ? lead & 0x1Fu : length == 3 ? lead & 0x0Fu : lead & 0x07u;
        bool whole = length > 0 && at + length <= text.size();
        for (size_t next = 1; whole && next < length; ++next) {
            whole = (static_cast<unsigned char>(text[at + next]) & 0xC0) == 0x80;
            code = (code << 6) | (static_cast<unsigned char>(text[at + next]) & 0x3Fu);
        }
        constexpr char32_t least[] = {0, 0, 0x80, 0x800, 0x10000};
        if (!whole || code < least[length] || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
            result.clear();
            for (const char letter : text) result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(letter)));
            return result;
        }
        result.push_back(static_cast<wchar_t>(code));
        at += length;
    }
    return result;
#else
    if (text.empty()) return {};
    UINT page = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    int length = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) {
        page = CP_ACP;
        flags = 0;
        length = MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), nullptr, 0);
    }
    std::wstring result(length, L'\0');
    MultiByteToWideChar(page, flags, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
#endif
}

// The file a CUE names: on POSIX its UTF-8 bytes are the path, so no wide text and no locale stands between.
fs::path cue_name(const std::string& text) {
#ifdef _WIN32
    return fs::path(decode(text));
#else
    std::u8string bytes;
    for (const wchar_t letter : decode(text)) {
        const auto code = static_cast<char32_t>(letter);
        if (code < 0x80) {
            bytes.push_back(static_cast<char8_t>(code));
        } else if (code < 0x800) {
            bytes.push_back(static_cast<char8_t>(0xC0 | (code >> 6)));
            bytes.push_back(static_cast<char8_t>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            bytes.push_back(static_cast<char8_t>(0xE0 | (code >> 12)));
            bytes.push_back(static_cast<char8_t>(0x80 | ((code >> 6) & 0x3F)));
            bytes.push_back(static_cast<char8_t>(0x80 | (code & 0x3F)));
        } else {
            bytes.push_back(static_cast<char8_t>(0xF0 | (code >> 18)));
            bytes.push_back(static_cast<char8_t>(0x80 | ((code >> 12) & 0x3F)));
            bytes.push_back(static_cast<char8_t>(0x80 | ((code >> 6) & 0x3F)));
            bytes.push_back(static_cast<char8_t>(0x80 | (code & 0x3F)));
        }
    }
    return fs::path(bytes);
#endif
}

std::string upper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

std::string hash_file(File& file, File* output, const std::atomic_bool& cancelled,
                      const Progress& progress, const wchar_t* status) {
    Sha256 hash;
    std::vector<unsigned char> bytes(1 << 20);
    const uint64_t total = file.size();
    uint64_t done = 0;
    for (;;) {
        check_cancel(cancelled);
        size_t read = 0;
        if (!file.read(bytes.data(), bytes.size(), read))
            throw std::runtime_error("Cannot read the complete image. Check your drive and try again.");
        if (!read) break;
        hash.add(bytes.data(), read);
        if (output && !output->write(bytes.data(), read))
            throw std::runtime_error("Cannot copy the image. Check free space and folder permissions.");
        done += read;
        report(progress, status, total ? static_cast<unsigned>(std::min<uint64_t>(100, done * 100 / total)) : 0);
    }
    if (done != total) throw std::runtime_error("The image is incomplete.");
    if (output && !output->flush())
        throw std::runtime_error("Could not finish writing game data. Check your drive and free space.");
    return hash.finish();
}

const DiscProfile& match_size(uint64_t size, std::span<const DiscProfile> profiles) {
    for (const auto& disc : profiles) if (disc.size == size) return disc;
    throw std::runtime_error(
        "Unsupported image size or format.\n"
        "Use the USA SLUS-00224 raw MODE2/2352 dump of 636,350,064 bytes.\n"
        "Cooked ISOs lose audio and video the game needs.\n"
        "PAL and Japanese discs are not supported yet.");
}

const DiscProfile& match_hash(uint64_t size, const std::string& hash,
                               std::span<const DiscProfile> profiles) {
    for (const auto& disc : profiles)
        if (disc.size == size && disc.sha256 == hash) return disc;
    throw std::runtime_error(
        "The full disc SHA-256 does not match a supported revision.\n"
        "The image may be modified, damaged, or from another region.\n"
        "Use an unmodified USA SLUS-00224 raw dump.\n"
        "No game data was installed and the game was not started.");
}

fs::path unique_directory(const fs::path& parent, const std::string& prefix) {
    for (unsigned i = 0; i < 1000; ++i) {
        fs::path path = parent / (prefix + std::to_string(std::random_device{}()) + "-" + std::to_string(i));
        if (fs::create_directory(path)) return path;
    }
    throw std::runtime_error("Could not create a staging folder for the disc image.");
}

struct Staging {
    fs::path path;
    ~Staging() { std::error_code ignored; if (!path.empty()) fs::remove_all(path, ignored); }
};

void write_cue(const fs::path& directory) {
    std::ofstream cue(directory / "disc.cue", std::ios::binary);
    cue << "FILE \"disc.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n";
    cue.close();
    if (!cue) throw std::runtime_error("Cannot write the disc CUE. Check folder permissions.");
}
} // namespace

std::span<const DiscProfile> supported_discs() { return discs; }
std::wstring error_text(const std::exception& error) { return decode(error.what()); }

fs::path resolve_image(const fs::path& selected) {
    const auto extension = upper(selected.extension().string());
    if (extension == ".BIN" || extension == ".ISO" || extension == ".IMG") return selected;
    if (extension != ".CUE")
        throw std::runtime_error("Select a CUE, BIN, IMG, or raw ISO disc image.");
    File source(selected);
    if (source.size() > 65536) throw std::runtime_error("The CUE is too large or invalid.");
    std::string text(static_cast<size_t>(source.size()), '\0');
    size_t got = 0;
    if (!source.read(reinterpret_cast<unsigned char*>(text.data()), text.size(), got) || got != text.size())
        throw std::runtime_error("Cannot read the CUE file.");
    if (text.starts_with("\xef\xbb\xbf")) text.erase(0, 3);
    std::istringstream lines(text);
    std::string line, filename;
    unsigned files = 0, tracks = 0, indices = 0;
    const std::regex file_re(R"cue(^\s*FILE\s+(?:"([^"]+)"|(\S+))\s+BINARY\s*$)cue", std::regex::icase);
    const std::regex track_re(R"(^\s*TRACK\s+01\s+MODE2/2352\s*$)", std::regex::icase);
    const std::regex index_re(R"(^\s*INDEX\s+01\s+00:00:00\s*$)", std::regex::icase);
    const std::regex metadata_re(R"(^\s*(?:REM|TITLE|PERFORMER|SONGWRITER|CATALOG|ISRC|CDTEXTFILE)\b.*$)", std::regex::icase);
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \r\t") == std::string::npos || std::regex_match(line, metadata_re)) continue;
        std::smatch match;
        if (std::regex_match(line, match, file_re) && files == 0 && tracks == 0) {
            filename = match[1].matched ? match[1].str() : match[2].str();
            ++files;
        } else if (std::regex_match(line, track_re) && files == 1 && tracks == 0) ++tracks;
        else if (std::regex_match(line, index_re) && tracks == 1 && indices == 0) ++indices;
        else throw std::runtime_error(
            "Unsupported CUE layout.\n"
            "The supported USA dump has one BINARY file, one MODE2/2352 track, and INDEX 01 at 00:00:00.\n"
            "Multi-track or offset images cannot be imported by this build.");
    }
    if (files != 1 || tracks != 1 || indices != 1)
        throw std::runtime_error("The CUE is incomplete. It needs FILE, TRACK 01 MODE2/2352, and INDEX 01 00:00:00.");
    const fs::path data = selected.parent_path() / cue_name(filename);
    if (!fs::is_regular_file(data))
        throw std::runtime_error("The image named by the CUE is missing. Keep the CUE and its referenced BIN together.");
    return data;
}

VerifiedDisc verify_disc(const fs::path& selected, const std::atomic_bool& cancelled,
                         const Progress& progress, std::span<const DiscProfile> profiles) {
    check_cancel(cancelled);
    // Hold both CUE and data locks: the table of contents must not change after
    // verification either. Reading the CUE again in resolve_image is safe with
    // shared read access, while all writers remain excluded.
    std::shared_ptr<File> cue_lock;
    if (upper(selected.extension().string()) == ".CUE") cue_lock = std::make_shared<File>(selected);
    const fs::path data = resolve_image(selected);
    auto file = std::make_shared<File>(data);
    const auto size = file->size();
    match_size(size, profiles);
    const auto hash = hash_file(*file, nullptr, cancelled, progress, L"Verifying the complete disc image...");
    const auto& profile = match_hash(size, hash, profiles);
    auto locks = std::make_shared<std::array<std::shared_ptr<File>, 2>>(
        std::array<std::shared_ptr<File>, 2>{file, cue_lock});
    return {&profile, data, cue_lock ? selected : fs::path{}, locks};
}

fs::path installed_cue(const fs::path& package, const DiscProfile& profile) {
    return package / "input" / "discs" / profile.id / "disc.cue";
}

VerifiedDisc import_disc(const fs::path& selected, const fs::path& package,
                         const std::atomic_bool& cancelled, const Progress& progress,
                         std::span<const DiscProfile> profiles) {
    check_cancel(cancelled);
    const auto source_path = resolve_image(selected);
    const auto parent = package / "input" / "discs";
    Staging staging;
    const DiscProfile* profile = nullptr;
    {
        File source(source_path);
        match_size(source.size(), profiles);
        fs::create_directories(parent);
        staging.path = unique_directory(parent, ".import-");
        File output(staging.path / "disc.bin", true);
        const auto hash = hash_file(source, &output, cancelled, progress, L"Copying and verifying your disc image...");
        profile = &match_hash(source.size(), hash, profiles);
    }
    write_cue(staging.path);
    // Read back the actual installed bytes, not just the source or a cached verdict.
    { const auto verified = verify_disc(staging.path / "disc.cue", cancelled, progress, profiles); }
    check_cancel(cancelled);
    const auto destination = installed_cue(package, *profile).parent_path();
    fs::path backup;
    if (fs::exists(destination)) {
        try {
            // Re-selecting a correct image should not accumulate duplicate
            // installations. Only a failed existing verification needs repair.
            return verify_disc(destination / "disc.cue", cancelled, progress, profiles);
        } catch (const std::exception&) {
            check_cancel(cancelled);
        }
        // A repaired installation preserves every previous file in a sibling
        // backup. No existing data is deleted, including unexpected user files.
        backup = unique_directory(parent, ".previous-" + std::string(profile->id) + "-");
        fs::remove(backup); // the directory we just created is empty
        fs::rename(destination, backup);
    }
    try {
        fs::rename(staging.path, destination);
        staging.path.clear();
    } catch (...) {
        if (!backup.empty()) { std::error_code ignored; fs::rename(backup, destination, ignored); }
        throw;
    }
    // Publication is complete. Cancellation after this point must not label
    // successfully installed data as an incomplete import.
    const std::atomic_bool committed{false};
    return verify_disc(destination / "disc.cue", committed, progress, profiles);
}

fs::path find_installed_disc(const fs::path& package) {
    for (const auto& profile : supported_discs()) {
        auto cue = installed_cue(package, profile);
        if (fs::exists(cue)) return cue;
    }
    // Upgrade the old manual layout automatically, without changing its files.
    for (const auto& name : {L"Disruptor (USA).cue", L"Disruptor (USA).bin"}) {
        auto path = package / "input" / name;
        if (fs::exists(path)) return path;
    }
    return {};
}
} // namespace disruptor::launcher
