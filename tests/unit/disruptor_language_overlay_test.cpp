#include "../support/disruptor_language_discs.h"

#include "iso_reader.h"
#include "mod_runtime.h"

#include <iostream>
#include <map>

namespace {

std::map<std::string, MemoryImage *> g_images;
int g_patched = 0;

}  // namespace

/* The image reader, over the synthetic images named in g_images instead of files. */
namespace PS1 {

struct CHDState {};

ISOReader::ISOReader() : is_open_(false) {}
ISOReader::~ISOReader() = default;

bool ISOReader::Open(const std::string &filename) {
    bin_path_ = filename;
    is_open_ = g_images.count(filename) != 0;
    return is_open_;
}

void ISOReader::Close() { is_open_ = false; }

bool ISOReader::ReadRawSector(uint32_t lba, uint8_t *buffer) { return is_open_ && g_images.at(bin_path_)->raw_sector(lba, buffer); }

bool ISOReader::ReadSector(uint32_t lba, uint8_t *buffer) {
    uint8_t raw[kRawSector];
    if (!ReadRawSector(lba, raw)) return false;
    std::memcpy(buffer, raw + 24, kSectorData);
    return true;
}

uint32_t ISOReader::GetSectorCount() { return is_open_ ? g_images.at(bin_path_)->sectors() : 0; }
int ISOReader::TrackCount() const { return 1; }
uint32_t ISOReader::TrackStartLBA(int) const { return 0; }
uint32_t ISOReader::TrackPregapLBA(int) const { return 0; }
bool ISOReader::TrackIsAudio(int) const { return false; }

}  // namespace PS1

void mod_runtime_patch_disc_sector(uint32_t, int, uint8_t *, uint32_t) { ++g_patched; }

/* The save states' folder and key, as the runtime keeps them. */
namespace {
std::string g_states = "saves";
uint32_t g_states_bios = 7, g_states_entry = 0;
int g_states_moves = 0;
}  // namespace

extern "C" void savestate_configure(const char *dir, uint32_t bios_checksum, uint32_t entry_pc) {
    g_states = dir;
    g_states_bios = bios_checksum;
    g_states_entry = entry_pc;
    ++g_states_moves;
}
extern "C" const char *savestate_dir(void) { return g_states.c_str(); }
extern "C" void savestate_get_integrity(uint32_t *bios_checksum, uint32_t *entry_pc) {
    *bios_checksum = g_states_bios;
    *entry_pc = g_states_entry;
}

#include "../../psxrecomp-overlay/runtime/src/iso_reader_c.cpp"
#include "../../src/disruptor_language_source.cpp"

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

Bytes own(MemoryImage &image, uint32_t lba) {
    Bytes raw(kRawSector);
    return image.raw_sector(lba, raw.data()) ? raw : Bytes();
}

Bytes data_of(const Bytes &raw) { return raw.size() == kRawSector ? Bytes(raw.begin() + 24, raw.begin() + 24 + kSectorData) : Bytes(); }

Bytes through(void *handle, uint32_t lba) {
    Bytes raw(kRawSector);
    return iso_read_raw_sector(handle, lba, raw.data(), static_cast<int>(kRawSector)) ? raw : Bytes();
}

Bytes data_through(void *handle, uint32_t lba) {
    Bytes data(kSectorData);
    return iso_read_sector(handle, lba, data.data(), static_cast<int>(kSectorData)) ? data : Bytes();
}

bool hands_a_pack(const Bytes &wanted) {
    const uint8_t *data = nullptr;
    uint32_t size = 0;
    return disruptor_language_disc_pack(&data, &size) == 1 && Bytes(data, data + size) == wanted;
}

/* A process keeps the one language disc it was given, so a disc with tall hints has a run of its own. */
int tall() {
    Discs japanese;
    make_discs(japanese, true, "SLPS_008.04");
    g_images["game"] = &japanese.home;
    g_images["language"] = &japanese.other;
    disruptor_language_set_disc("language");
    expect(disruptor_language_disc_tall_hints() == 0, "no hints are tall before a disc is laid out");
    g_states.clear();
    g_states_entry = 0x80048CE4u;
    void *game = iso_open("game");
    expect(game && iso_sector_count(game) > japanese.home.sectors() && disruptor_language_disc_tall_hints() == 1, "a disc laid out with hints twice as tall says so");
    expect(g_states_moves == 0 && g_states.empty(), "with save states switched off there is no folder to give them");
    g_states = "saves";
    expect(iso_sector_count(game) != 0 && g_states_moves == 1 && g_states == "saves/Japanese" && g_states_bios == 7 && g_states_entry == 0x80048CE4u,
           "the states of a laid out disc go to a folder named by its language, under the same key");
    iso_close(game);
    expect(disruptor_language_disc_tall_hints() == 0, "until its image is closed");
    if (g_failures != 0) return 1;
    std::cout << "disruptor language overlay, tall hints: PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "tall") return tall();
    Discs discs, lacking;
    make_discs(discs);
    make_discs(lacking, false);
    g_images["game"] = &discs.home;
    g_images["language"] = &lacking.other;
    disruptor::LanguageDisc wanted;
    std::string why;
    expect(wanted.build(discs.home, discs.other, why) && wanted.sectors() != discs.home.sectors(), "the two discs must lay out, to another length than the game disc's");

    void *first = iso_open("game");
    expect(first && iso_sector_count(first) == discs.home.sectors() && through(first, 16) == own(discs.home, 16) && data_through(first, 220) == data_of(own(discs.home, 220)),
           "with no language disc named the game disc is read as it is");

    disruptor_language_set_disc("");
    disruptor_language_set_disc("language");
    bool untouched = iso_sector_count(first) == discs.home.sectors() && !hands_a_pack(wanted.pack());
    for (uint32_t lba = 0; lba < discs.home.sectors(); ++lba) untouched = untouched && through(first, lba) == own(discs.home, lba);
    expect(untouched, "a language disc that is refused changes nothing: length, pack and every sector are the game disc's");
    g_states_entry = 0x80048CE4u;
    expect(iso_sector_count(first) == discs.home.sectors() && g_states_moves == 0 && g_states == "saves", "and its save states stay where the game disc's are");
    g_states_entry = 0;

    g_images["language"] = &discs.other;
    void *second = iso_open("game");
    expect(second && iso_game_disc() == second, "the disc in the drive is the image opened last");
    disruptor_language_settle();
    expect(hands_a_pack(wanted.pack()) && g_states_moves == 0 && g_states == "saves",
           "the disc is laid out when asked, before any read, and save states that are not set up yet are left alone");
    g_states_entry = 0x80048CE4u;
    disruptor_language_settle();
    expect(g_states_moves == 1 && g_states == "saves/French" && g_states_bios == 7 && g_states_entry == 0x80048CE4u,
           "once they are, the states of the laid out disc go to a folder named by its language, under the same key, with no read of the disc");
    bool served = second && iso_sector_count(second) == wanted.sectors();
    for (uint32_t lba = 0; lba < wanted.sectors(); ++lba) {
        Bytes raw(kRawSector);
        served = served && wanted.raw_sector(lba, raw.data()) && through(second, lba) == raw && data_through(second, lba) == data_of(raw);
    }
    expect(served, "a language disc that fits is served sector for sector, raw and as data, to its own length");
    expect(!through(second, 17).empty() && iso_sector_count(second) != 0 && g_states_moves == 1 && g_states == "saves/French", "and they are moved once");
    expect(through(second, 16) != own(discs.home, 16) && hands_a_pack(wanted.pack()), "and it differs from the game disc and brings its pack");
    expect(disruptor_language_disc_tall_hints() == 0, "its hints are as tall as the US ones");
    expect(through(second, wanted.sectors()).empty() && data_through(second, wanted.sectors()).empty(), "nothing is read past its end");

    Bytes raw(kRawSector);
    expect(iso_read_raw_sector_plain(second, 16, raw.data()) == 1 && raw == own(discs.home, 16) && iso_sector_count_plain(second) == discs.home.sectors(),
           "a plain read of the game disc's handle goes past the language disc");
    void *plain = iso_open_plain("game");
    expect(plain && through(plain, 16) == own(discs.home, 16) && iso_sector_count(plain) == discs.home.sectors(), "an image opened plain is never stood in for");
    expect(iso_game_disc() == second, "and it is not the disc in the drive");
    iso_close(plain);

    const int patched = g_patched;
    expect(!through(second, 300).empty() && g_patched == patched + 1, "the runtime's own sector patches still see a sector the language disc gave");

    discs.other.data.resize(700 * kSectorData);
    uint32_t lost = 0;
    while (lost < wanted.sectors() && wanted.raw_sector(lost, raw.data())) ++lost;
    expect(lost < discs.home.sectors() && !own(discs.home, lost).empty() && through(second, lost).empty() && data_through(second, lost).empty() && g_patched == patched + 1,
           "a sector the language disc cannot give is a failed read, never the game disc's sector at that place");

    const auto no_pack = [] {
        const uint8_t *data = nullptr;
        uint32_t size = 0;
        return disruptor_language_disc_pack(&data, &size) == 0;
    };
    iso_close(first);
    expect(hands_a_pack(wanted.pack()), "the layout stands while the image it was made for is open, whatever else is closed");
    expect(iso_game_disc() == second, "and that image stays the disc in the drive");
    iso_close(second);
    expect(no_pack(), "closing that image ends its layout");
    expect(iso_game_disc() == nullptr, "and leaves no disc in the drive");
    disruptor_language_settle();
    expect(no_pack(), "with no disc in the drive there is nothing to lay out");

    Discs again;
    make_discs(again);
    MemoryImage unrelated('E');
    unrelated.put(40, Bytes(kSectorData, 0));
    g_images["game"] = &again.home;
    g_images["language"] = &again.other;
    g_images["unrelated"] = &unrelated;
    alignas(PS1::ISOReader) static unsigned char slot[sizeof(PS1::ISOReader)];
    auto *reader = new (slot) PS1::ISOReader();
    reader->Open("game");
    expect(kOverlay.sectors(reader) == wanted.sectors() && kOverlay.read(reader, 16, raw.data()) == 1, "a handle is laid out when it is first asked about");
    kOverlay.closed(reader);
    reader->~ISOReader();
    reader = new (slot) PS1::ISOReader();
    reader->Open("unrelated");
    expect(kOverlay.sectors(reader) == 0 && kOverlay.read(reader, 16, raw.data()) == 0 && no_pack(), "an image opened where a closed one was is looked at afresh");
    reader->~ISOReader();
    if (g_failures != 0) return 1;
    std::cout << "disruptor language overlay: PASS\n";
    return 0;
}
