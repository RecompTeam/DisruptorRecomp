#include "memcard.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

std::vector<uint8_t> read_all(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

long entries(const fs::path &dir) { return std::distance(fs::directory_iterator(dir), fs::directory_iterator{}); }

}  // namespace

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("memcard_publish_" + std::to_string(stamp));
    fs::create_directories(dir);
    const fs::path card = dir / "card1.mcd";
    const std::string card_text = card.string();
    const MemcardSlotConfig slots[2] = {{card_text.c_str(), 1}, {nullptr, 0}};

    memcard_init_slots(dir.string().c_str(), slots);
    expect(memcard_is_present(0) == 1, "a missing card is made and mounted");
    expect(fs::file_size(card) == MEMCARD_SIZE, "the new card is written whole");
    expect(entries(dir) == 1, "making a card leaves no temporary beside it");

    std::vector<uint8_t> sector(MEMCARD_SECTOR_SIZE, 0xAB);
    expect(memcard_write_sector(0, 5, sector.data()) == 0, "a sector of a mounted card takes a write");
    memcard_flush(0);
    expect(memcard_last_flush_result(0) == 0, "a flush that reaches the file reports success");
    const std::vector<uint8_t> written = read_all(card);
    expect(written.size() == MEMCARD_SIZE && written[5 * MEMCARD_SECTOR_SIZE] == 0xAB &&
               written[6 * MEMCARD_SECTOR_SIZE - 1] == 0xAB,
           "the flushed card holds the written sector");
    expect(entries(dir) == 1, "a flush leaves no temporary beside the card");

    fs::remove(card);
    fs::create_directory(card);
    std::ofstream(card / "kept") << "kept";
    std::fill(sector.begin(), sector.end(), uint8_t{0xCD});
    expect(memcard_write_sector(0, 6, sector.data()) == 0, "the card in memory still takes a write");
    memcard_flush(0);
    expect(memcard_last_flush_result(0) == -1, "a flush that cannot replace the file reports failure");
    expect(fs::is_directory(card) && fs::exists(card / "kept"), "a failed flush leaves what stands at the path");
    expect(entries(dir) == 1, "a failed flush removes its temporary");

    fs::remove_all(card);
    memcard_flush(0);
    expect(memcard_last_flush_result(0) == 0, "a card left dirty by a failed flush is written by the next one");
    const std::vector<uint8_t> retried = read_all(card);
    expect(retried.size() == MEMCARD_SIZE && retried[5 * MEMCARD_SECTOR_SIZE] == 0xAB &&
               retried[6 * MEMCARD_SECTOR_SIZE] == 0xCD,
           "the retried card holds both writes");

    const fs::path formatted = dir / "formatted.mcd";
    expect(memcard_format_file(formatted.string().c_str()) == 0, "formatting a new file succeeds");
    expect(fs::file_size(formatted) == MEMCARD_SIZE, "the formatted card is whole");
    expect(entries(dir) == 2, "formatting leaves no temporary");

    const std::vector<uint8_t> image(MEMCARD_SIZE, 0x5A);
    fs::remove_all(card);
    expect(memcard_import_raw(0, image.data()) == 0, "an import that reaches the file reports success");
    fs::remove(card);
    fs::create_directory(card);
    expect(memcard_import_raw(0, image.data()) == -1, "an import that cannot replace the file reports failure");
    fs::remove_all(card);
    memcard_flush(0);
    expect(memcard_last_flush_result(0) == 0 && read_all(card) == image,
           "the failed import is written by the next flush");

#ifndef _WIN32
    /* Windows cannot build this case: a held card refuses the replace as well as the read. */
    const fs::path locked = dir / "locked.mcd";
    std::ofstream(locked, std::ios::binary).write(reinterpret_cast<const char *>(image.data()), 100);
    const std::string locked_text = locked.string();
    const MemcardSlotConfig locked_slots[2] = {{locked_text.c_str(), 1}, {nullptr, 0}};
    fs::permissions(locked, fs::perms::none);
    memcard_init_slots(dir.string().c_str(), locked_slots);
    fs::permissions(locked, fs::perms::owner_read | fs::perms::owner_write);
    if (geteuid() != 0) {
        expect(memcard_is_present(0) == 0, "a card that cannot be read is not mounted");
        expect(fs::file_size(locked) == 100, "a card that cannot be read is never replaced by a blank one");
    }
#endif

    std::error_code ignored;
    fs::remove_all(dir, ignored);
    if (g_failures == 0) std::cout << "memcard publish: PASS\n";
    return g_failures == 0 ? 0 : 1;
}
