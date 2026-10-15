#include "../support/disruptor_language_discs.h"

#include "region_disc.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;
using namespace disruptor::launcher;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

fs::path dump(MemoryImage& image, const fs::path& folder, const char* name) {
    fs::create_directories(folder);
    const fs::path data = folder / (std::string(name) + ".bin");
    std::ofstream out(data, std::ios::binary);
    std::vector<uint8_t> raw(kRawSector);
    for (uint32_t lba = 0; lba < image.sectors(); ++lba) {
        require(image.raw_sector(lba, raw.data()), "a synthetic sector could not be read");
        out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    }
    out.close();
    require(static_cast<bool>(out), "fixture write failed");
    std::ofstream cue(folder / (std::string(name) + ".cue"), std::ios::binary);
    cue << "FILE \"" << name << ".bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n";
    return data;
}

std::string refusal(const fs::path& home, const fs::path& other) {
    try {
        check_region_disc(home, other);
    } catch (const std::exception& error) { return error.what(); }
    return {};
}

void known(const fs::path& folder, const char* boot, const char* language, bool words) {
    Discs discs;
    make_discs(discs, true, boot);
    const fs::path home = dump(discs.home, folder, "home");
    // A folder named in Cyrillic: the image is read where the player keeps it.
    const fs::path other = dump(discs.other, folder / L"Диски", language);
    const fs::path cue = fs::path(other).replace_extension(".cue");
    for (const fs::path& selected : {other, cue}) {
        const auto found = region_disc(selected);
        require(found && found->language == language && found->words == words, "a known disc must be named by its boot file");
        const RegionDisc checked = check_region_disc(home, selected);
        require(checked.language == language && checked.words == words, "a known disc that fits must be taken");
    }
    require(!region_disc(home) && refusal(home, home).find("another region") != std::string::npos,
            "the US disc is not a language disc");
    fs::remove_all(folder);
}

void refused(const fs::path& folder) {
    Discs discs;
    make_discs(discs, true, "SLES_005.64", 0x5000, 0xA000);
    const fs::path home = dump(discs.home, folder, "home"), other = dump(discs.other, folder, "other");
    require(region_disc(other).has_value(), "a disc that does not fit is still a known one");
    const std::string why = refusal(home, other);
    require(why.find("French disc does not fit the US disc") != std::string::npos && why.find("hint") != std::string::npos,
            "a disc that does not fit must be refused with the game's reason");

    const fs::path cut = folder / "cut.bin";
    fs::copy_file(other, cut);
    fs::resize_file(cut, fs::file_size(cut) - 1000);
    require(!region_disc(cut) && refusal(home, cut).find("2352") != std::string::npos, "a cut image is not a raw one");
    require(!region_disc(folder / "none.bin") && !refusal(home, folder / "none.bin").empty() &&
            !region_disc(folder / "notes.txt") && !refusal(home, folder / "notes.txt").empty(),
            "a missing image and a file of another kind are refused");
    fs::remove_all(folder);
}
} // namespace

int main() {
    const fs::path folder = fs::temp_directory_path() /
                            ("disruptor-region-disc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int result = 0;
    try {
        fs::remove_all(folder);
        known(folder, "SLES_005.64", "French", true);
        known(folder, "SLES_005.65", "German", true);
        known(folder, "SLPS_008.04", "Japanese", false);
        refused(folder);
        require(describe({"German", true}) == L"German disc: menus, texts, movies, speech and hints." &&
                describe({"Japanese", false}).find(L"Its menus are in English") != std::wstring::npos,
                "the launcher must say what a disc gives");
        std::cout << "Region disc recognition, layout check and refusal tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        result = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return result;
}
