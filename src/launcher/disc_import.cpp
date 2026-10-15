#include "disc_import.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
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

struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit File(const fs::path& path, bool write = false) {
        handle = CreateFileW(path.c_str(), write ? GENERIC_WRITE : GENERIC_READ,
            write ? 0 : FILE_SHARE_READ, nullptr,
            write ? CREATE_NEW : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            throw std::runtime_error(write
                ? "Cannot create game data. Extract the build into a writable folder with enough free space."
                : "Cannot read the disc image. Check that it exists and is not being changed by another program.");
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
};

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
    void add(unsigned char* bytes, DWORD size) {
        if (BCryptHashData(hash, bytes, size, 0) < 0)
            throw std::runtime_error("SHA-256 verification failed.");
    }
    std::string finish() {
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
            throw std::runtime_error("SHA-256 verification failed.");
        std::string hex;
        for (const auto byte : digest) {
            hex += "0123456789abcdef"[byte >> 4];
            hex += "0123456789abcdef"[byte & 15];
        }
        return hex;
    }
};

// CUE files are usually UTF-8 or the system ANSI encoding. Installed filenames
// are ASCII, so the game's existing narrow-path loader can read them too.
std::wstring decode(const std::string& text) {
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
        DWORD read = 0;
        if (!ReadFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
            throw std::runtime_error("Cannot read the complete image. Check your drive and try again.");
        if (!read) break;
        hash.add(bytes.data(), read);
        if (output) {
            DWORD written = 0;
            if (!WriteFile(output->handle, bytes.data(), read, &written, nullptr) || written != read)
                throw std::runtime_error("Cannot copy the image. Check free space and folder permissions.");
        }
        done += read;
        report(progress, status, total ? static_cast<unsigned>(std::min<uint64_t>(100, done * 100 / total)) : 0);
    }
    if (done != total) throw std::runtime_error("The image is incomplete.");
    if (output && !FlushFileBuffers(output->handle))
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
        fs::path path = parent / (prefix + std::to_string(GetCurrentProcessId()) + "-" +
                                 std::to_string(GetTickCount64()) + "-" + std::to_string(i));
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
    DWORD got = 0;
    if (!ReadFile(source.handle, text.data(), static_cast<DWORD>(text.size()), &got, nullptr) || got != text.size())
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
    const fs::path data = selected.parent_path() / fs::path(decode(filename));
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
