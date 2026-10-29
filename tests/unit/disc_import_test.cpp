#include "chd_image.h"
#include "disc_import.h"
#ifdef _WIN32
#include <windows.h>
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
constexpr DiscProfile fixture[] = {
    {"TEST-USA", L"Test USA", 3,
     "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
     L"game.toml"},
};
std::atomic_bool cancelled{false};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void write(const fs::path& path, const std::string& contents) {
    std::ofstream out(path, std::ios::binary);
    out << contents;
    out.close();
    require(static_cast<bool>(out), "fixture write failed");
}
std::string read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
template<typename Action> void fails(Action action, const char* message) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    require(failed, message);
}
void no_staging(const fs::path& package) {
    for (const auto& entry : fs::directory_iterator(package / "input/discs"))
        require(!entry.path().filename().string().starts_with(".import-"), "partial staging data remains");
}

// tools/make_chd_fixtures.py made these with chdman 0.289: one_track holds 10 sectors of this pattern.
const fs::path chd_fixtures = fs::path(DISRUPTOR_SOURCE_DIR) / "tests" / "fixtures" / "chd";
constexpr DiscProfile chd_fixture[] = {
    {"TEST-CHD", L"Test CHD", 23520, "ed56c734afd9102844f40703ee2f7cf04e7997643539c75115b554de8140aaa3", L"game.toml"},
};
std::string chd_pattern() {
    std::string bytes(23520, '\0');
    for (size_t at = 0; at < bytes.size(); ++at) bytes[at] = static_cast<char>((at * 31 + 7) % 256);
    return bytes;
}

void chd_tests(const fs::path& root) {
    const auto chd = root / fs::path(u8"Диск ü.chd");
    fs::copy_file(chd_fixtures / "one_track.chd", chd);
    {
        ChdImage image(chd);
        require(image.sectors() == 10 && image.refusal().empty(), "the fixture CHD did not open");
        std::string sectors;
        std::array<uint8_t, 2352> raw{};
        for (uint32_t lba = 0; lba < 10; ++lba) {
            require(image.raw_sector(lba, raw.data()), "a sector of the CHD did not read");
            sectors.append(reinterpret_cast<const char*>(raw.data()), raw.size());
        }
        require(sectors == chd_pattern() && !image.raw_sector(10, raw.data()), "the CHD's sectors are not the BIN's");
    }
    require(resolve_image(chd) == chd && open_image(chd)->sectors() == 10, "a CHD is its own data");
    const auto bin = root / "pattern.bin";
    write(bin, chd_pattern());
    {
        auto from_bin = verify_disc(bin, cancelled, {}, chd_fixture);
        auto from_chd = verify_disc(chd, cancelled, {}, chd_fixture);
        require(from_bin.profile == &chd_fixture[0] && from_chd.profile == &chd_fixture[0], "the CHD and its BIN differ");
        require(from_chd.data == chd && from_chd.mount == chd, "the game is not started with the CHD");
#ifdef _WIN32
        HANDLE writer = CreateFileW(chd.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
        require(writer == INVALID_HANDLE_VALUE, "a verified CHD must remain locked against writes");
#endif
    }
    fails([&] { verify_disc(chd, cancelled); }, "production catalog accepted the synthetic CHD");
    for (const char* name : {"two_tracks.chd", "cooked.chd"}) {
        const auto refused = root / name;
        fs::copy_file(chd_fixtures / name, refused);
        fails([&] { verify_disc(refused, cancelled, {}, chd_fixture); }, "a CHD of another layout verified");
    }
    fails([&] { open_image(root / "two_tracks.chd"); }, "a CHD of two tracks opened");
    // Metadata the game's reader cannot parse is refused although the sectors decode to the profile's.
    std::string unparsed = read(chd);
    const auto field = unparsed.find("SUBTYPE:");
    require(field != std::string::npos, "the fixture has no track metadata");
    unparsed[field] = 'X';
    const auto malformed = root / "malformed.chd";
    write(malformed, unparsed);
    fails([&] { verify_disc(malformed, cancelled, {}, chd_fixture); }, "a CHD the game would not mount verified");

    const auto package = root / "chd package";
    fs::create_directories(package / "input/discs");
    cancelled = false;
    fails([&] {
        import_disc(chd, package, cancelled,
            [](const std::wstring& status, unsigned) { if (status.starts_with(L"Verifying")) cancelled = true; }, chd_fixture);
    }, "cancel during the CHD's decoding succeeded");
    cancelled = false;
    no_staging(package);
    fails([&] { import_disc(root / "two_tracks.chd", package, cancelled, {}, chd_fixture); }, "a CHD of another layout installed");
    no_staging(package);
    const auto folder = installed_cue(package, chd_fixture[0]).parent_path();
    {
        auto imported = import_disc(chd, package, cancelled, {}, chd_fixture);
        require(imported.mount == folder / "disc.chd" && imported.data == imported.mount, "the CHD was not installed as itself");
    }
    require(read(folder / "disc.chd") == read(chd) && !fs::exists(folder / "disc.cue") && !fs::exists(folder / "disc.bin"),
            "the installed CHD is not the player's file");
    const auto production = root / "production package" / "input" / "discs" / "SLUS-00224";
    fs::create_directories(production);
    fs::copy_file(chd, production / "disc.chd");
    require(find_installed_disc(root / "production package") == production / "disc.chd", "an installed CHD is not found");
    no_staging(package);

    // A stamp that names other bytes is checked by decoding, and a damaged CHD fails either way.
    const auto stamp = folder / "disc.chd.verified";
    const std::string written = read(stamp);
    write(stamp, std::string(64, '0') + "\n" + chd_fixture[0].sha256 + "\n");
    { auto decoded = verify_disc(folder / "disc.chd", cancelled, {}, chd_fixture); }
    write(stamp, written);
    std::string damaged = read(folder / "disc.chd");
    // Byte 400 is compressed sectors: the fixture's metadata ends at 229 and its map starts at 839.
    damaged[400] = static_cast<char>(damaged[400] ^ 0x5A);
    write(folder / "disc.chd", damaged);
    fails([&] { verify_disc(folder / "disc.chd", cancelled, {}, chd_fixture); }, "a damaged CHD verified");
    { auto repaired = import_disc(chd, package, cancelled, {}, chd_fixture); }
    require(read(folder / "disc.chd") == read(chd) && read(stamp) == written, "the damaged CHD was not replaced");
}
} // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = fs::current_path() / ("disruptor-disc-test-" + std::to_string(stamp));
    fs::create_directories(root);
    try {
        const auto source = root / fs::path(u8"Disc ü 日本.bin");
        write(source, "abc");
        auto unicode = source.filename().u8string();
        const std::string filename(reinterpret_cast<const char*>(unicode.data()), unicode.size());
        const auto cue = root / "original.CUE";
        write(cue, "\xef\xbb\xbfREM test\r\nFILE \"" + filename +
              "\" BINARY\r\n TRACK 01 MODE2/2352\r\n INDEX 01 00:00:00\r\n");
        require(resolve_image(cue) == source, "Unicode CUE did not resolve its named image");
        {
            auto verified = verify_disc(cue, cancelled, {}, fixture);
            require(verified.profile == &fixture[0], "known SHA-256 was not recognized");
#ifdef _WIN32
            HANDLE writer = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            require(writer == INVALID_HANDLE_VALUE, "verified data must remain locked against writes");
            writer = CreateFileW(cue.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            require(writer == INVALID_HANDLE_VALUE, "verified CUE must remain locked against writes");
#endif
        }
        const auto package = root / fs::path(u8"Build with spaces ü 日本");
        {
            auto imported = import_disc(cue, package, cancelled, {}, fixture);
            require(imported.mount == installed_cue(package, fixture[0]), "wrong install path");
            require(read(imported.data) == "abc", "installed bytes differ from source");
            require(resolve_image(imported.mount) == imported.data, "generated CUE is not self-contained");
        }
        require(read(source) == "abc" && fs::exists(cue), "original files were changed");
        no_staging(package);

        // The extension does not set the format: raw bytes named ISO pass, cooked images fail the hash.
        const auto iso = root / "raw.iso";
        write(iso, "abc");
        { auto verified = verify_disc(iso, cancelled, {}, fixture); }
        const auto bad = root / "wrong.bin";
        write(bad, "abd");
        fails([&] { import_disc(bad, package, cancelled, {}, fixture); }, "wrong hash was installed");
        require(read(installed_cue(package, fixture[0]).parent_path() / "disc.bin") == "abc",
                "failed import replaced the good installation");
        no_staging(package);
        write(bad, "ab");
        fails([&] { verify_disc(bad, cancelled, {}, fixture); }, "truncated image verified");
        write(bad, "abc");
        fails([&] { verify_disc(bad, cancelled); }, "production catalog accepted synthetic game data");

        const auto invalid = root / "invalid.cue";
        for (const auto& layout : {
            "FILE \"missing.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n",
            "FILE \"raw.iso\" BINARY\nTRACK 01 MODE1/2048\nINDEX 01 00:00:00\n",
            "FILE \"raw.iso\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:02:00\n",
            "FILE \"raw.iso\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 02 AUDIO\n",
            "FILE \"raw.iso\" BINARY\nTRACK 01 MODE2/2352\n",
            "FILE \"raw.iso\" BINARY\nFILE \"wrong.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n"
        }) {
            write(invalid, layout);
            fails([&] { resolve_image(invalid); }, "invalid CUE was accepted");
        }
        cancelled = true;
        fails([&] { import_disc(source, package, cancelled, {}, fixture); }, "pre-cancelled import succeeded");
        cancelled = false;
        fails([&] {
            import_disc(source, package, cancelled,
                [](const std::wstring&, unsigned) { cancelled = true; }, fixture);
        }, "cancel during copy succeeded");
        cancelled = false;
        require(read(installed_cue(package, fixture[0]).parent_path() / "disc.bin") == "abc",
                "cancellation changed installed data");
        no_staging(package);
        fails([&] {
            import_disc(source, package, cancelled,
                [](const std::wstring& status, unsigned) {
                    if (status.starts_with(L"Verifying")) cancelled = true;
                }, fixture);
        }, "cancel during destination verification succeeded");
        cancelled = false;
        no_staging(package);

        // Repair preserves user files and the previous installation in a backup.
        const auto destination = installed_cue(package, fixture[0]).parent_path();
        write(destination / "keep.txt", "keep me");
        write(destination / "disc.bin", "abd");
        { auto repaired = import_disc(source, package, cancelled, {}, fixture); }
        require(read(destination / "disc.bin") == "abc", "repair failed");
        bool backup_found = false;
        for (const auto& entry : fs::directory_iterator(destination.parent_path())) {
            if (entry.path().filename().string().starts_with(".previous-")) {
                backup_found = read(entry.path() / "keep.txt") == "keep me" &&
                               read(entry.path() / "disc.bin") == "abd";
            }
        }
        require(backup_found, "repair lost previous game data or user files");
        { auto again = import_disc(destination / "disc.cue", package, cancelled, {}, fixture); }
        require(read(destination / "disc.bin") == "abc", "import from installed location failed");
        unsigned backup_count = 0;
        for (const auto& entry : fs::directory_iterator(destination.parent_path()))
            if (entry.path().filename().string().starts_with(".previous-")) ++backup_count;
        require(backup_count == 1, "reimporting verified data created an unnecessary backup");
        // Python's hashlib: lengths around the 64-byte block, its 56-byte padding limit and past a 1 MiB read.
        constexpr std::pair<std::uint64_t, const char*> vectors[] = {
            {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
            {55, "8aa994584139d128848eeebc4e815639ba5ab6e6e39574195a63ac4f14f7c43b"},
            {56, "ad574708f75c044c9b85de64cb568ee7711ff4f36448c6242f053ba8f6cc2b63"},
            {63, "280ed3e8ff1df845b2e7dfe6ac6cee817bef20e783cc65abc41b818b4d2fe076"},
            {64, "c6ab9724ade5b6a7a1edfffb12f3aa9181351355af8fd08c919952ad211339dd"},
            {65, "788367c73c7ddf4c53f65e68cc0d943e6227ab55b0e78ba63ace822b1c6301c0"},
            {119, "3d610547d68216dedf7435a4fb6260353911f6b3fd3f18805ddb8be285d726fe"},
            {120, "1f80156a804cb7862ad113e8200e9d74499723e7c7854d5f48776d3148e09656"},
            {1048577, "0d09f3eabb5c78e5d435c41e3b3755f1c4d038657f739d0be0083c24e71235df"},
        };
        for (const auto& [length, digest] : vectors) {
            std::string bytes(static_cast<size_t>(length), '\0');
            for (size_t at = 0; at < bytes.size(); ++at) bytes[at] = static_cast<char>((at * 31 + 7) % 256);
            const auto image = root / ("vector-" + std::to_string(length) + ".bin");
            write(image, bytes);
            const DiscProfile profile[] = {{"TEST-VECTOR", L"Vector", length, digest, L"game.toml"}};
            require(verify_disc(image, cancelled, {}, profile).profile == &profile[0],
                    "SHA-256 must match hashlib at every block boundary");
        }
        chd_tests(root);
        fs::remove_all(root);
        std::cout << "Disc verification, CUE and CHD reading, import, cancellation, repair and Unicode tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
