#define DISRUPTOR_CAPSULE_NO_HOST 1
#include "../../src/game/session/capsule.cpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {
using disruptor::capsule::Frame;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

bool same(const Frame &one, const Frame &other) {
    return one.pad[0] == other.pad[0] && one.pad[1] == other.pad[1] && one.byte_at == other.byte_at && one.byte == other.byte &&
           one.byte2_at == other.byte2_at && one.byte2 == other.byte2 &&
           std::memcmp(&one.pitch, &other.pitch, sizeof(one.pitch)) == 0 && one.memory == other.memory;
}

void test_frames_survive_the_file() {
    std::vector<Frame> frames(3), back;
    frames[0].pad[0] = 0xBFFE;
    frames[0].pad[1] = 0x1234;
    frames[0].byte_at = 0x80077624u;
    frames[0].byte = 0xC3;
    frames[0].byte2_at = 0x80077627u;
    frames[0].byte2 = 1;
    frames[0].pitch = -17.25f;
    frames[0].memory = 0x0123456789ABCDEFull;
    frames[2].pitch = 3.5f;
    frames[2].memory = 0xFFFFFFFFFFFFFFFFull;
    uint64_t first = 0;
    const std::string bytes = disruptor::capsule::encode(frames, 0x0000000867A1B2C3ull);
    expect(bytes.size() == 20 + 3 * 26 && bytes.compare(0, 8, "DCAPSUL2") == 0 && static_cast<uint8_t>(bytes[8]) == 3 &&
               static_cast<uint8_t>(bytes[12]) == 0xC3 && static_cast<uint8_t>(bytes[16]) == 0x08,
           "the file is a name, a count, the first VBlank's cycle and 26 bytes a frame");
    expect(static_cast<uint8_t>(bytes[20]) == 0xFE && static_cast<uint8_t>(bytes[21]) == 0xBF && static_cast<uint8_t>(bytes[24]) == 0x24 &&
               static_cast<uint8_t>(bytes[27]) == 0x80 && static_cast<uint8_t>(bytes[28]) == 0xC3 && static_cast<uint8_t>(bytes[33]) == 0xEF &&
               static_cast<uint8_t>(bytes[40]) == 0x01 && static_cast<uint8_t>(bytes[41]) == 0x27 &&
               static_cast<uint8_t>(bytes[45]) == 1,
           "every number is written low byte first, wherever the file was made");
    expect(disruptor::capsule::decode(bytes, back, first) && back.size() == 3 && same(back[0], frames[0]) && same(back[1], frames[1]) && same(back[2], frames[2]) &&
               first == 0x0000000867A1B2C3ull,
           "what was written is what is read");
    expect(same(back[1], Frame{}) && back[1].pad[0] == 0xFFFF, "a frame with nothing pressed has both pads released");

    std::string legacy = bytes.substr(0, 20);
    legacy[7] = '1';
    for (size_t i = 0; i < frames.size(); ++i) legacy.append(bytes, 20 + i * 26, 21);
    expect(disruptor::capsule::decode(legacy, back, first) && back.size() == frames.size() &&
               back[0].byte_at == frames[0].byte_at && back[0].byte == frames[0].byte &&
               back[0].byte2_at == 0u && back[0].pitch == frames[0].pitch &&
               back[0].memory == frames[0].memory && first == 0x0000000867A1B2C3ull,
           "older capsules still read with no second byte write");

    std::vector<Frame> none;
    expect(disruptor::capsule::decode(disruptor::capsule::encode({}, 7), none, first) && none.empty() && first == 7, "an empty recording is a file too");
    std::string wrong = bytes;
    wrong[0] = 'X';
    expect(!disruptor::capsule::decode(wrong, back, first) && back.empty() && first == 0, "a file of another kind is refused and leaves no frames");
    expect(!disruptor::capsule::decode(bytes.substr(0, bytes.size() - 1), back, first) && !disruptor::capsule::decode(bytes + '\0', back, first) &&
               !disruptor::capsule::decode(bytes.substr(0, 19), back, first) && !disruptor::capsule::decode(bytes.substr(0, 9), back, first) &&
               !disruptor::capsule::decode(bytes.substr(0, 5), back, first) && !disruptor::capsule::decode("", back, first),
           "a file cut short or with more than its count says is refused");
    const std::vector<char> few(bytes.begin(), bytes.begin() + 5), nine(bytes.begin(), bytes.begin() + 9);
    expect(!disruptor::capsule::decode({few.data(), few.size()}, back, first) && !disruptor::capsule::decode({nine.data(), nine.size()}, back, first),
           "and nothing is read past the end of a file shorter than its head");
    std::string more = bytes;
    more[8] = 4;
    expect(!disruptor::capsule::decode(more, back, first), "a count the file does not hold is refused");
}

void test_simultaneous_mouse_and_weapon_writes() {
    Frame frame;
    frame.note_byte(0x80077627u, 1u);
    frame.note_byte(0x80077624u, 90u);
    frame.note_byte(0x80077624u, 89u);
    frame.note_byte(0x80077627u, 2u);
    expect(frame.byte_at == 0x80077627u && frame.byte == 2u &&
               frame.byte2_at == 0x80077624u && frame.byte2 == 89u,
           "weapon selection and mouse yaw retain their latest value independently");
}

void test_the_digest_sees_every_byte() {
    std::vector<uint8_t> memory(4099, 0);
    const uint64_t empty = disruptor::capsule::digest(memory.data(), memory.size());
    expect(disruptor::capsule::digest(memory.data(), memory.size()) == empty, "the same memory has the same digest");
    for (const size_t at : {size_t{0}, size_t{7}, size_t{8}, size_t{2048}, size_t{4095}, size_t{4096}, size_t{4098}}) {
        memory[at] = 1;
        expect(disruptor::capsule::digest(memory.data(), memory.size()) != empty, "one changed byte changes the digest, in the last odd bytes too");
        memory[at] = 0;
    }
    memory[16] = 5;
    const uint64_t here = disruptor::capsule::digest(memory.data(), memory.size());
    memory[16] = 0;
    memory[24] = 5;
    expect(disruptor::capsule::digest(memory.data(), memory.size()) != here, "the same byte elsewhere is another digest");
    expect(disruptor::capsule::digest(memory.data(), 4098) != disruptor::capsule::digest(memory.data(), 4099), "the length counts");
}

void test_a_picture_is_a_bitmap() {
    const std::vector<uint8_t> rgb = {10, 20, 30, 40, 50, 60, 70, 80, 90, /* second row */ 1, 2, 3, 4, 5, 6, 7, 8, 9};
    const std::string file = disruptor::capsule::bitmap(rgb, 3, 2);
    const auto byte = [&](size_t at) { return static_cast<uint8_t>(file[at]); };
    expect(file.size() == 54 + 2 * 12 && file.compare(0, 2, "BM") == 0 && byte(2) == 78 && byte(10) == 54 && byte(14) == 40, "a header, then rows padded to four bytes");
    expect(byte(18) == 3 && byte(22) == 2 && byte(26) == 1 && byte(28) == 24 && byte(34) == 24, "three by two, one plane, 24 bits, 24 bytes of picture");
    expect(byte(54) == 3 && byte(55) == 2 && byte(56) == 1 && byte(60) == 9 && byte(63) == 0 && byte(64) == 0 && byte(65) == 0,
           "the last row comes first, blue before red, zeros to the row's end");
    expect(byte(66) == 30 && byte(67) == 20 && byte(68) == 10 && byte(74) == 70, "then the first row");
    expect(disruptor::capsule::bitmap({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, 4, 1).size() == 54 + 12, "a row of four pixels needs no padding");
}
}  // namespace

void test_an_f12_press_records_or_loads() {
    using disruptor::capsule::Press;
    for (int bits = 0; bits < 16; ++bits) {
        const bool repeat = (bits & 1) != 0, ctrl = (bits & 2) != 0, recording = (bits & 4) != 0, shift = (bits & 8) != 0;
        const Press wanted = repeat || (shift && !ctrl) ? Press::kNothing : ctrl || recording ? Press::kRecord : Press::kLoad;
        expect(disruptor::capsule::press(repeat, ctrl, shift, recording) == wanted,
               "a press with Ctrl records, one without loads unless a recording runs, which it ends, and a held key's repeats and Shift alone do nothing");
    }
    using disruptor::capsule::Stored;
    expect(disruptor::capsule::newest({}).empty(), "no capsule is the newest of none");
    expect(disruptor::capsule::newest({Stored{"300", 300, 0, 0, 0}, Stored{"200-2", 200, 9, 0, 0}, Stored{"200", 200, 5, 0, 0}}) == "200-2",
           "the newest capsule to load is not the first that was finished");
    expect(disruptor::capsule::newest({Stored{"300", 300, 0, 0, 0}}).empty(), "an unfinished capsule is offered to load");
}

void test_a_capsule_begins_only_when_it_is_whole() {
    namespace fs = std::filesystem;
    using disruptor::capsule::begins;
    const fs::path all = fs::temp_directory_path() / ("disruptor-capsule-begins-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code failed;
    fs::create_directories(all / "whole", failed);
    std::vector<Frame> frames(3);
    frames[0].pitch = -12.5f;
    frames[1].pitch = 40.0f;
    const std::string input = disruptor::capsule::encode(frames, 77);
    const auto put = [&](const char *name, const std::string &note, const std::string &bytes) {
        fs::create_directories(all / name, failed);
        if (!note.empty()) std::ofstream(all / name / "capsule.toml", std::ios::binary) << note;
        if (!bytes.empty()) std::ofstream(all / name / "input.bin", std::ios::binary) << bytes;
    };
    put("whole", "frames = 3\nended = \"the key\"\n", input);
    put("no_note", "", input);
    put("no_input", "frames = 3\n", "");
    put("unfinished", "ended = \"the key\"\n", input);
    put("other_length", "frames = 4\n", input);
    put("cut", "frames = 3\n", input.substr(0, input.size() - 1));
    put("not_input", "frames = 3\n", std::string(input.size(), 'x'));
    put("empty", "frames = 0\n", disruptor::capsule::encode({}, 77));
    const std::vector<Frame> many(50000);
    expect(disruptor::capsule::encode(many, 77).size() > disruptor::capsule::kInputBytes, "the long recording of this test fits the bound");
    put("too_long", "frames = 50000\n", disruptor::capsule::encode(many, 77));
    fs::create_directories(all / "folders" / "input.bin", failed);
    std::ofstream(all / "folders" / "capsule.toml", std::ios::binary) << "frames = 3\n";
    float pitch = 5.0f;
    expect(begins(all / "whole", pitch) && pitch == -12.5f, "a whole capsule does not begin with its first frame's look");
    for (const char *broken : {"no_note", "no_input", "unfinished", "empty", "other_length", "cut", "not_input", "too_long", "folders", "absent"}) {
        pitch = 5.0f;
        expect(!begins(all / broken, pitch) && pitch == 5.0f, "a capsule that is not whole begins, or the look is touched");
    }
    fs::remove_all(all, failed);
    expect(!fs::exists(all, failed), "the test left its folder behind");
}

void test_a_capsules_folder_is_told_by_its_name() {
    int64_t began = 0;
    expect(disruptor::capsule::stored_name("1791918902", began) && began == 1791918902, "a second is no capsule's name");
    expect(disruptor::capsule::stored_name("1791918902-2", began) && began == 1791918902 && disruptor::capsule::stored_name("7-99", began) && began == 7,
           "a second recording within one second is no capsule's name");
    for (const char *other : {"", "notes", "..", ".", "1791918902/..", "1791918902\\..", "../1791918902", "1791918902-", "-2", "1791918902-1",
                              "1791918902-0", "1791918902-100", "1791918902-2-3", "0179", "1234567890123456789", "17919 18902", "1791918902x"}) {
        began = -1;
        expect(!disruptor::capsule::stored_name(other, began) && began == -1, "a name that is no capsule's is taken for one, or the second is touched");
    }
}

void test_a_capsules_length_is_read_from_its_note() {
    using disruptor::capsule::stored_frames;
    expect(stored_frames("frames = 952\ncodegen_hash = \"0x2CECE446\"\nentry_pc = \"0x80048CE4\"\nended = \"the key\"\n") == 952, "the length is not read");
    expect(stored_frames("ended = \"the key\"\r\nframes = 407\r\n") == 407 && stored_frames("frames = 5") == 5, "the length is not read on a later line, before a carriage return or at the file's end");
    expect(stored_frames("") == 0 && stored_frames("ended = \"the key\"\n") == 0 && stored_frames("xframes = 9\n") == 0 && stored_frames("frames = \n") == 0 &&
               stored_frames("frames = 12x\n") == 0 && stored_frames("frames = -3\n") == 0,
           "a note without a whole length gives one");
    expect(stored_frames("frames = 4294967295\n") == 4294967295u && stored_frames("frames = 4294967296\n") == 0 && stored_frames("frames = 5000000000\n") == 0 && stored_frames("frames = 99999999999999999999999\n") == 0,
           "a length past 32 bits wraps");
}

void test_the_capsules_on_disk_are_listed_and_one_is_removed() {
    namespace fs = std::filesystem;
    const fs::path all = fs::temp_directory_path() / ("disruptor-capsule-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code failed;
    expect(disruptor::capsule::stored(all).empty(), "a folder that is not there lists capsules");
    for (const char *name : {"100", "200", "200-2", "200-10", "notes", "100x"}) fs::create_directories(all / name, failed);
    std::ofstream(all / "100" / "capsule.toml", std::ios::binary) << "frames = 10\nended = \"the key\"\n";
    std::ofstream(all / "200-2" / "capsule.toml", std::ios::binary) << "frames = 5\n";
    std::ofstream(all / "200-10" / "capsule.toml", std::ios::binary) << "ended = \"the key\"\n";
    std::ofstream(all / "notes" / "capsule.toml", std::ios::binary) << "frames = 77\n";
    std::ofstream(all / "300", std::ios::binary) << "a file, not a capsule";
    std::ofstream(all / "100" / "input.bin", std::ios::binary) << "x";
    const std::vector<disruptor::capsule::Stored> found = disruptor::capsule::stored(all);
    expect(found.size() == 4 && found[0].name == "200-10" && found[1].name == "200-2" && found[2].name == "200" && found[3].name == "100",
           "the capsules are not listed the newest first, or something else is listed");
    expect(found.size() == 4 && found[0].frames == 0 && found[1].frames == 5 && found[2].frames == 0 && found[3].frames == 10 && found[3].began == 100 &&
               found[0].began == 200,
           "a capsule's length or second is not its own");

    for (const char *other : {"notes", "100x", "300", "..", "100/../notes", "400", ""})
        expect(!disruptor::capsule::discard(all, other), "something that is no capsule's folder is removed");
    expect(fs::exists(all / "notes" / "capsule.toml", failed) && fs::exists(all / "100x", failed) && fs::exists(all / "300", failed) &&
               fs::exists(all / "100" / "input.bin", failed),
           "a refused removal removed something");
    expect(disruptor::capsule::discard(all, "100") && !fs::exists(all / "100", failed) && fs::exists(all / "200", failed) && fs::exists(all / "200-2", failed),
           "a capsule is not removed with what it holds, or another went with it");
    expect(disruptor::capsule::stored(all).size() == 3, "a removed capsule is still listed");
    fs::remove_all(all, failed);
    expect(!fs::exists(all, failed), "the test left its folder behind");
}

void test_a_recording_keeps_pictures_for_the_list() {
    using namespace disruptor::capsule;
    expect(!preview_due(0) && !preview_due(59) && preview_due(60) && !preview_due(61) && !preview_due(89) && preview_due(90) && preview_due(60 + 30 * 500),
           "a picture is not due every half second from the first second on");
    expect(preview_slot(0) == 0 && preview_slot(79) == 79 && preview_slot(80) == 79 && preview_slot(5000) == 79, "the eightieth picture is not the last slot, taken again");
    expect(preview_name(0) == "preview_000.ppm" && preview_name(7) == "preview_007.ppm" && preview_name(79) == "preview_079.ppm", "a picture's file is not named by its number");

    const uint8_t rows[2 * 3 * 3] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 255};
    const std::string file = preview(rows, 3, 2);
    uint32_t width = 0, height = 0;
    std::string_view rgb;
    expect(file.substr(0, 11) == "P6\n3 2\n255\n" && file.size() == 11 + 18, "a picture's file is not a binary PPM of its rows");
    expect(preview_decode(file, width, height, rgb) && width == 3 && height == 2 && rgb.size() == 18 && std::memcmp(rgb.data(), rows, 18) == 0,
           "a picture does not come back from its file");
    for (const std::string &broken : {std::string(), file.substr(0, file.size() - 1), file + "x", "P5" + file.substr(2), "P6\n3 2\n254\n" + file.substr(11),
                                      "P6\n0 2\n255\n" + std::string(), "P6\n3 0\n255\n" + std::string(), "P6\n3\n2\n255\n" + file.substr(11),
                                      "P6\n-3 2\n255\n" + file.substr(11), "P6\n5000 1\n255\n" + std::string(15000, 'x'), "P6\n3 2 255\n" + file.substr(11)}) {
        width = height = 77;
        expect(!preview_decode(broken, width, height, rgb) && width == 77 && height == 77, "a file that is no whole picture is taken for one, or its size is touched");
    }

    namespace fs = std::filesystem;
    const fs::path all = fs::temp_directory_path() / ("disruptor-capsule-pictures-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code failed;
    fs::create_directories(all / "500", failed);
    fs::create_directories(all / "600", failed);
    for (const uint32_t number : {0u, 1u, 2u, 4u}) std::ofstream(all / "500" / preview_name(number), std::ios::binary) << file;
    const std::vector<Stored> found = stored(all);
    expect(found.size() == 2 && found[0].name == "600" && found[0].pictures == 0 && found[1].name == "500" && found[1].pictures == 3,
           "a capsule's pictures are not counted from 0 up to the first gap");
    const fs::path last = all / "500" / preview_name(2);
    fs::last_write_time(last, fs::last_write_time(last, failed) + std::chrono::seconds(10), failed);
    const std::vector<Stored> again = stored(all);
    expect(found.size() == 2 && again.size() == 2 && found[1].revised != 0 && again[1].revised != found[1].revised && again[0].revised == 0,
           "a last picture written again does not show in the capsule's time, or a capsule without pictures has one");

    expect(preview_read(last) == file && preview_read(all / "500").empty() && preview_read(all / "500" / "none.ppm").empty(),
           "a picture's file is not read whole, or a folder or nothing is read as one");
    const fs::path large = all / "500" / "large.ppm";
    std::ofstream(large, std::ios::binary) << "P6\n";
    fs::resize_file(large, kPreviewBytes + 1, failed);
    expect(!failed && preview_read(large).empty(), "a file past the bound is read");
    fs::resize_file(large, kPreviewBytes, failed);
    expect(!failed && preview_read(large).size() == kPreviewBytes, "a file at the bound is not read");

    expect(stored_seconds(0) == 0 && stored_seconds(29) == 0 && stored_seconds(30) == 1 && stored_seconds(952) == 16 && stored_seconds(4294967295u) == 71582788u,
           "a length is not its seconds rounded, or the largest wraps");
    fs::remove_all(all, failed);
    expect(!fs::exists(all, failed), "the test left its folder behind");
}

void test_a_folder_is_a_url_for_the_host() {
    namespace fs = std::filesystem;
#ifdef _WIN32
    expect(disruptor::capsule::folder_url(fs::path(u8"C:\\Games C\\Disruptor\\capsules")) == "file:///C:/Games%20C/Disruptor/capsules",
           "a Windows folder with a space is not a file URL");
    expect(disruptor::capsule::folder_url(fs::path(u8"D:\\\u0438\u0433\u0440\u044b\\a#b")) == "file:///D:/%D0%B8%D0%B3%D1%80%D1%8B/a%23b",
           "a folder with letters past ASCII or a mark of a URL is not escaped byte by byte");
#else
    expect(disruptor::capsule::folder_url(fs::path(u8"/home/player/Games C/capsules")) == "file:///home/player/Games%20C/capsules",
           "a folder with a space is not a file URL");
    expect(disruptor::capsule::folder_url(fs::path(u8"/\u0438\u0433\u0440\u044b/a#b")) == "file:///%D0%B8%D0%B3%D1%80%D1%8B/a%23b",
           "a folder with letters past ASCII or a mark of a URL is not escaped byte by byte");
#endif
}

int main() {
    test_an_f12_press_records_or_loads();
    test_a_capsule_begins_only_when_it_is_whole();
    test_a_capsules_folder_is_told_by_its_name();
    test_a_capsules_length_is_read_from_its_note();
    test_the_capsules_on_disk_are_listed_and_one_is_removed();
    test_a_recording_keeps_pictures_for_the_list();
    test_a_folder_is_a_url_for_the_host();
    test_frames_survive_the_file();
    test_simultaneous_mouse_and_weapon_writes();
    test_the_digest_sees_every_byte();
    test_a_picture_is_a_bitmap();
    if (g_failures) return 1;
    std::cout << "capsule file, digest and picture: PASS\n";
    return 0;
}
