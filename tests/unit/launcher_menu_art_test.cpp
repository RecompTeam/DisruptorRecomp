#include "../support/disruptor_language_discs.h"

#include "launcher/menu_art.h"

#include <array>
#include <iostream>

using disruptor::launcher::menu_scenes;
using disruptor::launcher::Picture;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
}

constexpr uint32_t kPiece = 0x63000;  // longer than the pictures and the palette that follow them in a menu piece
constexpr uint32_t kPicturesAt = 0x11000;
constexpr uint32_t kPaletteAt = 0x621B0;

// A menu piece whose pixel at (x, y) of picture `scene` is (x + y + scene + seed) & 0xFF and whose palette word n is
// n * 0x81 + seed, so that every entry differs.
Bytes menu_piece(uint8_t seed, uint16_t palette_step = 0x81) {
    Bytes piece(kPiece, 0);
    for (uint32_t scene = 0; scene < 4; ++scene)
        for (uint32_t y = 0; y < 240; ++y)
            for (uint32_t x = 0; x < 320; ++x)
                piece[kPicturesAt + scene * 76800 + y * 320 + x] = static_cast<uint8_t>(x + y + scene + seed);
    for (uint32_t index = 0; index < 256; ++index) {
        const auto word = static_cast<uint16_t>((index * palette_step + seed) & 0x7FFF);
        piece[kPaletteAt + 2 * index] = static_cast<uint8_t>(word);
        piece[kPaletteAt + 2 * index + 1] = static_cast<uint8_t>(word >> 8);
    }
    return piece;
}

uint32_t rgba(uint16_t word) {
    const auto eight = [](uint32_t five) { return (five << 3) | (five >> 2); };
    return eight(word & 31u) | (eight((word >> 5) & 31u) << 8) | (eight((word >> 10) & 31u) << 16) | 0xFF000000u;
}

// WAD.IN with its head table and the three menu pieces where entries 306 to 308 point.
MemoryImage disc_with(const std::vector<Bytes>& pieces, uint32_t wad_cut = 0) {
    Bytes wad(disruptor::kHeadWords * 4, 0);
    for (size_t index = 0; index < pieces.size(); ++index) {
        disruptor::put_le32(wad.data() + 4 * (306 + index), static_cast<uint32_t>(wad.size()));
        wad.insert(wad.end(), pieces[index].begin(), pieces[index].end());
    }
    if (wad_cut) wad.resize(wad.size() - wad_cut);
    MemoryImage image(1);
    write_volume(image, {{"SLUS_002.24", 24, filled(0x800, 1)}, {"WAD.IN", 40, wad}}, 4);
    return image;
}

void test_colours() {
    Bytes known = menu_piece(0);
    const std::array<uint16_t, 4> words = {0x7FFF, 0x001F, 0x0000, 0x4210};
    for (size_t index = 0; index < words.size(); ++index) {
        known[kPaletteAt + 2 * index] = static_cast<uint8_t>(words[index]);
        known[kPaletteAt + 2 * index + 1] = static_cast<uint8_t>(words[index] >> 8);
    }
    for (size_t pixel = 0; pixel < 4; ++pixel) known[kPicturesAt + pixel] = static_cast<uint8_t>(pixel);
    MemoryImage disc = disc_with({known, menu_piece(10), menu_piece(20)});
    const std::vector<Picture> scenes = menu_scenes(disc);
    expect(scenes.size() == 12 && scenes[0].pixels[0] == 0xFFFFFFFFu && scenes[0].pixels[1] == 0xFF0000FFu &&
               scenes[0].pixels[2] == 0xFF000000u && scenes[0].pixels[3] == 0xFF848484u,
           "white, pure red, black and mid grey come out as eight bits a channel, red lowest, opaque");
}

void test_scenes() {
    MemoryImage disc = disc_with({menu_piece(0), menu_piece(10), menu_piece(20)});
    const std::vector<Picture> scenes = menu_scenes(disc);
    expect(scenes.size() == 12, "three pieces of four pictures give twelve scenes");
    if (scenes.size() != 12) return;
    bool sized = true;
    for (const Picture& scene : scenes) sized &= scene.width == 320 && scene.height == 240 && scene.pixels.size() == 76800;
    expect(sized, "each scene is 320 by 240");
    const auto colour = [](uint8_t seed, uint32_t index) { return rgba(static_cast<uint16_t>((index * 0x81 + seed) & 0x7FFF)); };
    expect(scenes[0].pixels[0] == colour(0, 0) && scenes[1].pixels[0] == colour(0, 1) && scenes[3].pixels[320 * 239 + 319] == colour(0, (319 + 239 + 3) & 0xFF),
           "the first piece's pictures come first, each pixel through that piece's palette");
    expect(scenes[5].pixels[2] == colour(10, (2 + 1 + 10) & 0xFF) && scenes[11].pixels[321] == colour(20, (1 + 1 + 3 + 20) & 0xFF),
           "the later pieces follow with their own palettes");
    bool every = true;
    for (uint32_t scene = 0; scene < 12; ++scene)
        for (uint32_t y = 0; y < 240; ++y)
            for (uint32_t x = 0; x < 320; ++x)
                every &= scenes[scene].pixels[y * 320 + x] ==
                         colour(static_cast<uint8_t>(10 * (scene / 4)), (x + y + scene % 4 + 10 * (scene / 4)) & 0xFF);
    expect(every, "every pixel of every scene is its piece's palette entry");
}

void test_refusals() {
    MemoryImage few = disc_with({menu_piece(0), menu_piece(10, 0), menu_piece(20)});
    expect(menu_scenes(few).empty(), "a palette of one colour is no menu palette, and no scene is given");
    MemoryImage cut = disc_with({menu_piece(0), menu_piece(10), menu_piece(20)}, 0x1000);
    expect(menu_scenes(cut).empty(), "a piece that ends before its palette gives nothing");
    MemoryImage two = disc_with({menu_piece(0), menu_piece(10)});
    expect(menu_scenes(two).empty(), "a missing third piece gives nothing");
    // The next piece starts with what reads as a palette where the cut one's would run on into it.
    Bytes cut_short = menu_piece(10);
    cut_short.resize(0x62000);
    Bytes after = menu_piece(20);
    for (uint32_t index = 0; index < 256; ++index) {
        after[kPaletteAt - 0x62000 + 2 * index] = static_cast<uint8_t>(index);
        after[kPaletteAt - 0x62000 + 2 * index + 1] = static_cast<uint8_t>(index >> 4);
    }
    MemoryImage short_middle = disc_with({menu_piece(0), cut_short, after});
    expect(menu_scenes(short_middle).empty(), "a middle piece that ends before its palette gives nothing, though the next piece follows");
    MemoryImage short_head(3);
    write_volume(short_head, {{"SLUS_002.24", 24, filled(0x800, 1)}, {"WAD.IN", 40, filled(0x1000, 0)}}, 4);
    expect(menu_scenes(short_head).empty(), "a WAD.IN shorter than its head table gives nothing");
    MemoryImage none(2);
    write_volume(none, {{"SLUS_002.24", 24, filled(0x800, 1)}}, 4);
    expect(menu_scenes(none).empty(), "a disc without WAD.IN gives nothing");
    expect(menu_scenes(std::filesystem::path("C:/no/such/disc.bin")).empty(), "a missing file gives nothing");
}

} // namespace

int main() {
    test_colours();
    test_scenes();
    test_refusals();
    if (g_failures == 0) std::cout << "Launcher menu art tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
