#ifndef DISRUPTOR_LANGUAGE_DISCS_H
#define DISRUPTOR_LANGUAGE_DISCS_H

/* Synthetic discs for the language tests. The disc code is included whole, so that a test reaches its internals. */

#include "../../src/disruptor_language_disc.cpp"

namespace {

using disruptor::Bytes;
using disruptor::kRawSector;
using disruptor::kSectorData;

/* An image held in memory. Every sector's subheader carries the image's tag, so a read shows where it came from. */
class MemoryImage final : public disruptor::DiscImage {
  public:
    explicit MemoryImage(uint8_t tag) : tag_(tag) {}
    bool raw_sector(uint32_t lba, uint8_t *raw) override {
        if (lba >= sectors()) return false;
        std::memset(raw, 0, kRawSector);
        raw[12] = static_cast<uint8_t>(lba >> 8);
        raw[13] = static_cast<uint8_t>(lba);
        raw[15] = 2;
        raw[16] = tag_;
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(lba * kSectorData), kSectorData, raw + 24);
        return true;
    }
    uint32_t sectors() override { return static_cast<uint32_t>(data.size() / kSectorData); }
    void put(uint32_t lba, const Bytes &bytes) {
        if (data.size() < lba * kSectorData + bytes.size()) data.resize((lba * kSectorData + bytes.size() + kSectorData - 1) / kSectorData * kSectorData);
        std::copy(bytes.begin(), bytes.end(), data.begin() + static_cast<std::ptrdiff_t>(lba * kSectorData));
    }
    Bytes data;

  private:
    uint8_t tag_;
};

struct Entry {
    std::string name;
    uint32_t lba;
    Bytes bytes;
};

/* A volume with a root directory at sector 22 and the files where they are said to be. */
void write_volume(MemoryImage &image, const std::vector<Entry> &files, uint32_t tail) {
    Bytes descriptor(kSectorData), root(kSectorData);
    descriptor[0] = 1;
    std::memcpy(descriptor.data() + 1, "CD001", 5);
    disruptor::put_le32(descriptor.data() + 158, 22);
    disruptor::put_le32(descriptor.data() + 166, kSectorData);
    size_t at = 0;
    uint32_t end = 0;
    for (const Entry &file : files) {
        const std::string name = file.name + ";1";
        const size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
        root[at] = static_cast<uint8_t>(length);
        disruptor::put_le32(root.data() + at + 2, file.lba);
        disruptor::put_le32(root.data() + at + 10, static_cast<uint32_t>(file.bytes.size()));
        root[at + 32] = static_cast<uint8_t>(name.size());
        std::memcpy(root.data() + at + 33, name.data(), name.size());
        at += length;
        image.put(file.lba, file.bytes);
        end = std::max(end, file.lba + static_cast<uint32_t>((file.bytes.size() + kSectorData - 1) / kSectorData));
    }
    image.put(16, descriptor);
    image.put(22, root);
    image.put(end, Bytes(tail * kSectorData, 0x7E));
}

Bytes filled(size_t size, uint8_t value) { return Bytes(size, value); }

/* A menu piece: both fonts, then `fill` bytes, a table of offsets and its strings, a 0xFF and zeros to a sector's end. */
Bytes menu_like(const std::vector<std::string> &texts, size_t table_at, uint8_t colour, size_t more_signs, const std::string &changed_letters) {
    Bytes piece(table_at, 0);
    const auto glyph = [&](size_t at, size_t size, size_t seed, uint8_t shade) {
        for (size_t pixel = 0; pixel < size; ++pixel)
            piece[at + pixel] = (pixel * 7 + seed * 13) % 5 < 2 ? static_cast<uint8_t>(shade + (pixel + seed) % 3) : uint8_t{0};
    };
    size_t at = disruptor::kFontC;
    for (size_t slot = 0; slot < disruptor::kGlyphsC + more_signs; ++slot) {
        const bool sign = slot >= disruptor::kSignsC && slot < disruptor::kSignsC + more_signs;
        const size_t seed = sign ? 98 + slot - disruptor::kSignsC : slot - (slot >= disruptor::kSignsC ? more_signs : 0);
        glyph(at, disruptor::kGlyphC, seed, colour);
        at += disruptor::kGlyphC;
    }
    for (size_t slot = 0; slot < disruptor::kGlyphsB; ++slot) {
        const bool changed = slot < 26 && changed_letters.find(static_cast<char>('a' + slot)) != std::string::npos;
        glyph(at, disruptor::kGlyphB, changed ? 201 + slot : 100 + slot, colour);
        at += disruptor::kGlyphB;
    }
    size_t cursor = table_at + 4 * texts.size();
    piece.resize(cursor);
    for (size_t index = 0; index < texts.size(); ++index) {
        disruptor::put_le32(piece.data() + table_at + 4 * index, static_cast<uint32_t>(cursor));
        piece.insert(piece.end(), texts[index].begin(), texts[index].end());
        piece.push_back(0);
        cursor += texts[index].size() + 1;
    }
    piece.push_back(0xFF);
    piece.resize((piece.size() + kSectorData - 1) / kSectorData * kSectorData + kSectorData);
    return piece;
}

std::vector<std::string> numbered(const char *prefix, size_t count) {
    std::vector<std::string> out;
    for (size_t index = 0; index < count; ++index) out.push_back(prefix + std::to_string(index));
    return out;
}

const size_t kTableAt = 0x62800;

void put_string(Bytes &exe, uint32_t address, const std::string &text) {
    std::copy(text.begin(), text.end(), exe.begin() + static_cast<std::ptrdiff_t>(address - 0x80010000u + 0x800u));
}

/* Two small discs with what the layout looks at: a head table, a menu piece, a level, speech, an executable, movies. */
struct Discs {
    MemoryImage home{'U'}, other{'F'};
    Bytes home_wad, other_wad;
    uint32_t home_menu = 0, other_menu = 0, home_level = 0, other_level = 0;
};

Bytes level_like(uint32_t second, uint32_t later, uint32_t pictures, uint8_t value) {
    Bytes piece(pictures + second + later, value);
    disruptor::put_le32(piece.data(), 0x1234);
    disruptor::put_le32(piece.data() + 4, second);
    std::fill(piece.begin() + pictures, piece.begin() + pictures + second, static_cast<uint8_t>(value + 1));
    std::fill(piece.begin() + pictures + second, piece.end(), static_cast<uint8_t>(value + 2));
    return piece;
}

void make_discs(Discs &discs, bool with_movie = true, const char *other_boot = "SLES_005.64", size_t hint_bytes = 0x5000, size_t other_hint_bytes = 0) {
    const std::vector<disruptor::Language> &known = disruptor::languages();
    const auto named = std::find_if(known.begin(), known.end(), [&](const disruptor::Language &one) { return std::string(one.boot) == other_boot; });
    const disruptor::Language &language = named != known.end() ? *named : known.front();
    if (other_hint_bytes == 0) other_hint_bytes = language.hints == disruptor::Hints::kTall ? 2 * hint_bytes : hint_bytes;
    const auto wad = [&](bool french, uint32_t &menu_at, uint32_t &level_at) {
        Bytes file(disruptor::kHeadWords * 4, 0);
        const auto piece = [&](uint32_t index, const Bytes &bytes) {
            disruptor::put_le32(file.data() + 4 * index, static_cast<uint32_t>(file.size()));
            file.insert(file.end(), bytes.begin(), bytes.end());
            return static_cast<uint32_t>(file.size() - bytes.size());
        };
        piece(1, filled(0x1000, french ? 0xA1 : 0x51));
        level_at = piece(163, level_like(french ? 0x1000 : 0x1800, 0x800, french ? 0x74800 : 0x73800, french ? 0xB0 : 0x60));
        menu_at = piece(306, menu_like(numbered(french ? "FR MENU " : "US MENU ", french ? 92 : 90), kTableAt + (french ? 0x800 : 0), french ? 0x40 : 0x10,
                                       french ? language.more_signs : 0, french ? "yz" : ""));
        piece(311, filled(0x800, french ? 0xA2 : 0x52));
        piece(316, filled(french ? 0x1800 : 0x1000, french ? 0xA3 : 0x53));
        piece(319, filled(0x800, french ? 0xA4 : 0x54));
        disruptor::put_le32(file.data() + 4 * 320, french ? 0x6F0 : 0x5E0);
        piece(352, filled(french ? 0x800 : 0x1000, french ? 0xA5 : 0x55));
        piece(385, filled(0x800, french ? 0xA7 : 0x57));
        piece(401, filled(0x800, french ? 0xA8 : 0x58));
        piece(386, filled(french ? other_hint_bytes : hint_bytes, french ? 0xA6 : 0x56));
        piece(400, filled(french ? other_hint_bytes : hint_bytes, french ? 0xA9 : 0x59));
        disruptor::put_le32(file.data() + 4 * 350, 0xFFFFFFFFu);
        return file;
    };
    discs.home_wad = wad(false, discs.home_menu, discs.home_level);
    discs.other_wad = wad(true, discs.other_menu, discs.other_level);
    Bytes home_exe(0x62000, 0), other_exe(0x62800, 0);
    std::map<uint32_t, uint32_t> pairs;
    for (const disruptor::Pair &pair : language.strings) pairs[pair.home] = pair.other;
    for (const auto &[mine, theirs] : pairs) put_string(home_exe, mine, "US" + std::to_string(mine & 0xFFFF));
    for (const disruptor::Pair &pair : language.strings) put_string(other_exe, pair.other, "FR" + std::to_string(pair.other & 0xFFFF));
    std::fill_n(other_exe.begin() + static_cast<std::ptrdiff_t>(language.movie_tables - 0x80010000u + 0x800u), 0xA0, uint8_t{0x2C});
    /* The second control's name stands right of the pad, and the other disc's is too wide to start where the US one does. */
    disruptor::put_le32(home_exe.data() + (disruptor::kNamePlaces - 0x80010000u + 0x800u) + 8, 228);
    if (language.text) other_exe[language.widths[1] - 0x80010000u + 0x800u + 26 + ('F' - 'A')] = 100;
    /* A disc without words has a menu call at another x all the same: no pack may be asked of it. */
    if (!language.text) {
        for (size_t word = 0; word < 16; ++word) {
            const uint32_t filler = 0x01095021u, call = 0x0C000000u;
            disruptor::put_le32(home_exe.data() + 0x1000 + 4 * word, word == 12 ? 0x3405001Cu : word == 14 ? call | (disruptor::kMenuDraw >> 2 & 0x03FFFFFFu) : word == 15 ? 0u : filler);
            disruptor::put_le32(other_exe.data() + 0x1000 + 4 * word, word == 12 ? 0x3405000Eu : word == 14 ? call | (language.menu_draw >> 2 & 0x03FFFFFFu) : word == 15 ? 0u : filler);
        }
    }
    write_volume(discs.home, {{"SLUS_002.24", 24, home_exe}, {"WAD.IN", 220, discs.home_wad}, {"LICENSEA.DAT", 600, filled(0x700, 0x58)},
                              {"MOVIE0.STR", 601, filled(0x1800, 0x59)}, {"MOVIE3.STR", 604, filled(0x1000, 0x5A)}}, 2);
    std::vector<Entry> other_files = {{"MOVIE3.STR", 23, filled(0x1800, 0xAA)}, {other_boot, 30, other_exe}, {"WAD.IN", 230, discs.other_wad}};
    if (with_movie) other_files.push_back({"MOVIE0.STR", 900, filled(0x2800, 0xA9)});
    write_volume(discs.other, other_files, 1);
}

}  // namespace

#endif
