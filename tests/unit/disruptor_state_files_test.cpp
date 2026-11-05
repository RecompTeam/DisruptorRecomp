#include "game/session/state_files.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

namespace files = disruptor::state_files;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void put_section(std::string &file, uint32_t tag, const std::vector<uint8_t> &bytes, bool pack) {
    std::string payload;
    if (pack) {
        files::put_le32(payload, static_cast<uint32_t>(bytes.size()));
        uLongf room = compressBound(static_cast<uLong>(bytes.size()));
        std::string packed(static_cast<size_t>(room), '\0');
        compress2(reinterpret_cast<Bytef *>(packed.data()), &room, bytes.data(), static_cast<uLong>(bytes.size()), Z_BEST_SPEED);
        packed.resize(static_cast<size_t>(room));
        payload += packed;
    } else {
        payload.assign(bytes.begin(), bytes.end());
    }
    files::put_le32(file, tag);
    files::put_le32(file, pack ? files::kPacked : 0u);
    files::put_le32(file, static_cast<uint32_t>(payload.size()));
    files::put_le32(file, 0u);
    file += payload;
}

struct Made {
    std::string file;
    std::vector<uint8_t> ram;
};

/* A state as boot_state.c writes one: the header, RAM and video memory packed, the GPU's words raw. */
Made state(uint32_t mission, uint8_t difficulty, uint32_t display_x, uint32_t display_y, uint32_t depth = 0u, bool vram = true) {
    Made made;
    files::put_le32(made.file, files::kMagic);
    for (int field = 1; field < 7; ++field) files::put_le32(made.file, 7u);
    files::put_le32(made.file, vram ? 4u : 2u);
    files::put_le32(made.file, 0u);
    made.ram.assign(files::kRamBytes, 0u);
    for (int byte = 0; byte < 4; ++byte) made.ram[(files::kMission & 0x1FFFFFu) + byte] = static_cast<uint8_t>(mission >> (8 * byte));
    made.ram[files::kDifficulty & 0x1FFFFFu] = difficulty;
    made.ram[12345] = 77u;
    put_section(made.file, 0x01u, std::vector<uint8_t>(64, 1u), false);
    put_section(made.file, files::kRamSection, made.ram, true);
    if (!vram) return made;
    std::vector<uint8_t> gpu(276, 0u);
    const auto set = [&gpu](size_t index, uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) gpu[index * 4 + byte] = static_cast<uint8_t>(value >> (8 * byte));
    };
    set(files::kHres1, 1u);
    set(files::kDepth, depth);
    set(files::kDisplayX, display_x);
    set(files::kDisplayY, display_y);
    put_section(made.file, files::kGpuSection, gpu, false);
    std::vector<uint8_t> video(files::kVramBytes, 0u);
    const auto pixel = [&video](uint32_t x, uint32_t y, uint16_t value) {
        video[(y * files::kVramWide + x) * 2] = static_cast<uint8_t>(value);
        video[(y * files::kVramWide + x) * 2 + 1] = static_cast<uint8_t>(value >> 8);
    };
    pixel(display_x, display_y, 0x001Fu);
    pixel(display_x + 319, display_y + 239, 0x7C00u);
    pixel(display_x + 1, display_y, 0x03E0u);
    put_section(made.file, files::kVramSection, video, true);
    return made;
}

void test_contents() {
    const Made made = state(3u, 2u, 0u, 240u);
    const auto read = files::read_contents(made.file);
    expect(read && read->mission == 3u && read->difficulty == 2u, "the mission and difficulty come from the state's RAM");
    expect(read && read->ram_crc == static_cast<uint32_t>(crc32(0L, made.ram.data(), static_cast<uInt>(made.ram.size()))),
           "the checksum is of the RAM as the machine held it");
    expect(read && read->display.width == 320u && read->display.height == 240u, "the display's picture is 320 by 240");
    const auto rgb = [&read](uint32_t x, uint32_t y, int channel) { return read->display.rgb[(y * 320u + x) * 3u + channel]; };
    expect(read && rgb(0, 0, 0) == 255u && rgb(0, 0, 1) == 0u && rgb(0, 0, 2) == 0u, "red is the low five bits");
    expect(read && rgb(1, 0, 1) == 255u && rgb(319, 239, 2) == 255u, "green the middle and blue the top five, from the display's corner");
    expect(read && rgb(5, 5, 0) == 0u, "the rest of the display is black");
}

void test_refused() {
    expect(!files::read_contents("not a state at all, nothing like one"), "no state, no contents");
    Made made = state(1u, 4u, 0u, 0u);
    std::string cut = made.file.substr(0, made.file.size() - 100);
    expect(!files::read_contents(cut).has_value() || files::read_contents(cut)->display.empty(),
           "a file cut short loses at least its picture");
    std::string wrong = made.file;
    wrong[0] = 'X';
    expect(!files::read_contents(wrong), "a wrong mark is no state");
    const auto movie = files::read_contents(state(1u, 4u, 0u, 0u, 1u).file);
    expect(movie && movie->display.empty() && movie->mission == 1u, "a 24-bit movie gives no picture, the words stay");
    const auto off_edge = files::read_contents(state(1u, 4u, 800u, 0u).file);
    expect(off_edge && off_edge->display.empty(), "a display past video memory's edge gives no picture");
    const auto no_video = files::read_contents(state(5u, 2u, 0u, 0u, 0u, false).file);
    expect(no_video && no_video->mission == 5u && no_video->display.empty(), "a state without video memory has only words");
}

void test_shown() {
    files::Shown shown{0xDEADBEEFu, files::Place::menus, {4u, 2u, std::vector<uint8_t>(24, 9u)}};
    shown.picture.rgb[5] = 200u;
    const std::string bytes = files::encode(shown);
    const auto back = files::decode(bytes);
    expect(back && back->ram_crc == 0xDEADBEEFu && back->place == files::Place::menus && back->picture.width == 4u &&
               back->picture.height == 2u && back->picture.rgb == shown.picture.rgb,
           "the screen's picture comes back as it was kept");
    expect(!files::decode(bytes.substr(0, bytes.size() - 3)), "a cut picture is refused");
    std::string big = bytes;
    big[12] = '\xFF';
    big[13] = '\xFF';
    expect(!files::decode(big), "a picture wider than 4096 is refused");
    std::string place = bytes;
    place[8] = 9;
    expect(!files::decode(place), "an unknown place is refused");
}

void test_bars_and_names() {
    std::vector<uint8_t> window(16u * 9u * 3u, 0u);
    window[(0u * 16u + 2u) * 3u] = 50u;
    const files::Picture game = files::without_bars(window.data(), 16u, 9u, 4, 3);
    expect(game.width == 12u && game.height == 9u && game.rgb[0] == 50u, "a 4:3 picture in a 16:9 window loses the bars beside it");
    const files::Picture tall = files::without_bars(window.data(), 16u, 9u, 32, 9);
    expect(tall.width == 16u && tall.height == 4u, "a wider picture loses bars above and below");
    expect(files::without_bars(window.data(), 16u, 9u, 0, 3).empty(), "no aspect, no picture");
    expect(files::beside("saves/scph1001/state_80048CE4_slot02.pst", ".shown") == "saves/scph1001/state_80048CE4_slot02.shown",
           "the screen's picture lies beside the state with its own ending");
    expect(files::beside("state.mcd", ".shown").empty(), "no state, no file beside it");
}

}  // namespace

/* With a state's path: what the screen would read from it, and its display's picture as a PPM beside the given name. */
int read_one(const char *state, const char *picture) {
    std::ifstream input(state, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const auto contents = files::read_contents(bytes);
    if (!contents) {
        std::cout << "no contents\n";
        return 1;
    }
    std::cout << "mission " << contents->mission << " difficulty " << int{contents->difficulty} << " ram crc " << std::hex << contents->ram_crc
              << std::dec << " picture " << contents->display.width << "x" << contents->display.height << '\n';
    std::ofstream output(picture, std::ios::binary);
    output << "P6\n" << contents->display.width << ' ' << contents->display.height << "\n255\n";
    output.write(reinterpret_cast<const char *>(contents->display.rgb.data()), static_cast<std::streamsize>(contents->display.rgb.size()));
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 3) return read_one(argv[1], argv[2]);
    test_contents();
    test_refused();
    test_shown();
    test_bars_and_names();
    if (g_failures != 0) return 1;
    std::cout << "disruptor state files: PASS\n";
    return 0;
}
