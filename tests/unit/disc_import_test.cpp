#include "disc_import.h"
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

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
            HANDLE writer = CreateFileW(source.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            require(writer == INVALID_HANDLE_VALUE, "verified data must remain locked against writes");
            writer = CreateFileW(cue.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);
            require(writer == INVALID_HANDLE_VALUE, "verified CUE must remain locked against writes");
        }
        const auto package = root / fs::path(u8"Build with spaces ü 日本");
        {
            auto imported = import_disc(cue, package, cancelled, {}, fixture);
            require(imported.cue == installed_cue(package, fixture[0]), "wrong install path");
            require(read(imported.data) == "abc", "installed bytes differ from source");
            require(resolve_image(imported.cue) == imported.data, "generated CUE is not self-contained");
        }
        require(read(source) == "abc" && fs::exists(cue), "original files were changed");
        no_staging(package);

        // Extension does not determine the format; identical raw bytes named
        // ISO are accepted, while actual cooked images fail the production hash.
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
        fs::remove_all(root);
        std::cout << "Disc verification, CUE parsing, import, cancellation, repair and Unicode tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
}
