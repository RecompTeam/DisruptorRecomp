/* PSX_DISRUPTOR_CAPSULE_REPLAY=<folder> replays a capsule and ends the run: 0 in step with the recording to the end, 3 not, 2 unplayable. */
/* PSX_DISRUPTOR_CAPSULE_FRAMES=<folder> writes each replayed frame as a picture, which is whole only without a window. */
/* PSX_DISRUPTOR_CAPSULE_RECORD=<frames> is the key for a script: two seconds in, it records that many frames and ends the run. */
/* Measured: a loaded state takes its interrupts a few instructions apart, so one frame in four differs in a few bytes, nine in a row at most. */
#include "game/session/capsule.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#ifndef DISRUPTOR_CAPSULE_NO_HOST
#include "gpu.h"
#include "interrupts.h"
#include "overlay_api.h"
#include "psx_cycles.h"
#include "savestate.h"

extern "C" uint8_t *memory_get_ram_ptr(void);
extern "C" void psx_write_byte(uint32_t address, uint8_t value);
extern "C" uint16_t sio_get_pad_buttons_slot(int slot);
extern "C" void sio_set_pad_state_slot(int slot, uint16_t buttons);
extern "C" double disruptor_mouse_vertical_pitch(void);
extern "C" void disruptor_mouse_set_vertical_pitch(double pitch);
extern "C" const char *psx_host_user_settings_path(void);
extern "C" int psx_netplay_active(void);
extern "C" int gl_renderer_snap_shown(int width);
extern "C" void gl_renderer_snap_cancel(void);
extern "C" int gl_renderer_snap_taken(unsigned char *rgb, int room, int *width, int *height);
#endif

namespace disruptor::capsule {
namespace {

constexpr char kMagic[8] = {'D', 'C', 'A', 'P', 'S', 'U', 'L', '1'};
constexpr size_t kFrameBytes = 21, kHeadBytes = sizeof(kMagic) + 4 + 8;

void put(std::string &out, uint64_t value, int bytes) {
    for (int at = 0; at < bytes; ++at) out.push_back(static_cast<char>(value >> (8 * at)));
}

uint64_t take(std::string_view bytes, size_t at, int count) {
    uint64_t value = 0;
    for (int index = 0; index < count; ++index) value |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[at + static_cast<size_t>(index)])) << (8 * index);
    return value;
}

}  /* namespace */

std::string encode(const std::vector<Frame> &frames, uint64_t first_cycle) {
    std::string out(kMagic, sizeof(kMagic));
    put(out, frames.size(), 4);
    put(out, first_cycle, 8);
    for (const Frame &frame : frames) {
        uint32_t pitch = 0;
        std::memcpy(&pitch, &frame.pitch, sizeof(pitch));
        put(out, frame.pad[0], 2);
        put(out, frame.pad[1], 2);
        put(out, frame.byte_at, 4);
        put(out, frame.byte, 1);
        put(out, pitch, 4);
        put(out, frame.memory, 8);
    }
    return out;
}

bool decode(std::string_view bytes, std::vector<Frame> &frames, uint64_t &first_cycle) {
    frames.clear();
    first_cycle = 0;
    if (bytes.size() < kHeadBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) return false;
    const uint64_t count = take(bytes, sizeof(kMagic), 4);
    if (bytes.size() != kHeadBytes + count * kFrameBytes) return false;
    first_cycle = take(bytes, sizeof(kMagic) + 4, 8);
    frames.resize(static_cast<size_t>(count));
    for (size_t index = 0; index < frames.size(); ++index) {
        const size_t at = kHeadBytes + index * kFrameBytes;
        Frame &frame = frames[index];
        const auto pitch = static_cast<uint32_t>(take(bytes, at + 9, 4));
        frame.pad[0] = static_cast<uint16_t>(take(bytes, at, 2));
        frame.pad[1] = static_cast<uint16_t>(take(bytes, at + 2, 2));
        frame.byte_at = static_cast<uint32_t>(take(bytes, at + 4, 4));
        frame.byte = static_cast<uint8_t>(take(bytes, at + 8, 1));
        std::memcpy(&frame.pitch, &pitch, sizeof(pitch));
        frame.memory = take(bytes, at + 13, 8);
    }
    return true;
}

uint64_t digest(const uint8_t *bytes, size_t size) {
    uint64_t hash = 0xCBF29CE484222325ull;
    size_t at = 0;
    for (; at + 8 <= size; at += 8) {
        uint64_t word = 0;
        std::memcpy(&word, bytes + at, 8);
        hash = (hash ^ word) * 0x100000001B3ull;
        hash ^= hash >> 29;
    }
    for (; at < size; ++at) hash = (hash ^ bytes[at]) * 0x100000001B3ull;
    return hash;
}

std::string bitmap(const std::vector<uint8_t> &rgb, uint32_t width, uint32_t height) {
    const uint32_t row = (width * 3 + 3) & ~3u, pixels = row * height;
    std::string out = "BM";
    put(out, 54 + pixels, 4);
    put(out, 0, 4);
    put(out, 54, 4);
    put(out, 40, 4);
    put(out, width, 4);
    put(out, height, 4);
    put(out, 1, 2);
    put(out, 24, 2);
    put(out, 0, 4);
    put(out, pixels, 4);
    put(out, 2835, 4);
    put(out, 2835, 4);
    put(out, 0, 8);
    for (uint32_t y = height; y-- > 0;) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t *pixel = rgb.data() + (static_cast<size_t>(y) * width + x) * 3;
            out.push_back(static_cast<char>(pixel[2]));
            out.push_back(static_cast<char>(pixel[1]));
            out.push_back(static_cast<char>(pixel[0]));
        }
        out.append(row - width * 3, '\0');
    }
    return out;
}

Press press(bool repeat, bool ctrl, bool shift, bool recording) {
    if (repeat || (shift && !ctrl)) return Press::kNothing;
    return ctrl || recording ? Press::kRecord : Press::kLoad;
}

std::string newest(const std::vector<Stored> &kept) {
    for (const Stored &one : kept)
        if (one.frames != 0) return one.name;
    return {};
}

bool stored_name(std::string_view name, int64_t &began) {
    const size_t dash = name.find('-');
    const std::string_view second = name.substr(0, dash), copy = dash == std::string_view::npos ? std::string_view() : name.substr(dash + 1);
    const auto digits = [](std::string_view text) { return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }); };
    if (!digits(second) || second.size() > 18 || second[0] == '0') return false;
    if (dash != std::string_view::npos && (!digits(copy) || copy.size() > 2 || copy[0] == '0' || copy == "1")) return false;
    began = 0;
    for (const char c : second) began = began * 10 + (c - '0');
    return true;
}

uint32_t stored_frames(std::string_view about) {
    constexpr std::string_view key = "frames = ";
    for (size_t line = 0; line < about.size(); line = about.find('\n', line) == std::string_view::npos ? about.size() : about.find('\n', line) + 1) {
        if (about.substr(line, key.size()) != key) continue;
        uint64_t frames = 0;
        size_t at = line + key.size();
        for (; at < about.size() && about[at] >= '0' && about[at] <= '9' && frames <= UINT32_MAX; ++at) frames = frames * 10 + static_cast<uint64_t>(about[at] - '0');
        const bool whole = at == about.size() || about[at] == '\n' || about[at] == '\r';
        return whole && frames <= UINT32_MAX ? static_cast<uint32_t>(frames) : 0u;
    }
    return 0u;
}

uint32_t stored_seconds(uint32_t frames) { return frames / 60u + (frames % 60u >= 30u ? 1u : 0u); }

std::vector<Stored> stored(const std::filesystem::path &all) {
    std::vector<Stored> found;
    std::error_code failed;
    for (std::filesystem::directory_iterator each(all, failed), end; !failed && each != end; each.increment(failed)) {
        Stored one;
        const std::u8string name = each->path().filename().u8string();
        one.name.assign(name.begin(), name.end());
        std::error_code unread;
        if (each->is_symlink(unread) || unread || !each->is_directory(unread) || unread || !stored_name(one.name, one.began)) continue;
        const std::filesystem::path note = each->path() / "capsule.toml";
        std::ifstream about;
        if (std::filesystem::is_regular_file(note, unread)) about.open(note, std::ios::binary);
        char text[512] = {};
        about.read(text, sizeof(text) - 1);
        one.frames = stored_frames(std::string_view(text, static_cast<size_t>(about.gcount())));
        while (one.pictures < kPreviewMost && std::filesystem::exists(each->path() / preview_name(one.pictures), unread) && !unread) ++one.pictures;
        if (one.pictures != 0) {
            const auto written = std::filesystem::last_write_time(each->path() / preview_name(one.pictures - 1), unread);
            one.revised = unread ? 0 : static_cast<int64_t>(written.time_since_epoch().count());
        }
        found.push_back(std::move(one));
    }
    std::sort(found.begin(), found.end(), [](const Stored &one, const Stored &other) {
        if (one.began != other.began) return one.began > other.began;
        return one.name.size() != other.name.size() ? one.name.size() > other.name.size() : one.name > other.name;
    });
    return found;
}

bool discard(const std::filesystem::path &all, std::string_view name) {
    int64_t began = 0;
    std::error_code failed;
    if (!stored_name(name, began)) return false;
    const std::filesystem::path folder = all / std::filesystem::path(std::u8string(name.begin(), name.end()));
    if (std::filesystem::is_symlink(folder, failed) || failed || !std::filesystem::is_directory(folder, failed) || failed) return false;
    std::filesystem::remove_all(folder, failed);
    return !failed;
}

bool preview_due(size_t frame) { return frame >= kPreviewFirst && (frame - kPreviewFirst) % kPreviewEvery == 0; }

uint32_t preview_slot(uint32_t taken) { return std::min(taken, kPreviewMost - 1u); }

std::string preview_name(uint32_t number) {
    char name[32];
    std::snprintf(name, sizeof(name), "preview_%03u.ppm", static_cast<unsigned>(number));
    return name;
}

std::string preview(const uint8_t *rgb, uint32_t width, uint32_t height) {
    char head[40];
    std::snprintf(head, sizeof(head), "P6\n%u %u\n255\n", static_cast<unsigned>(width), static_cast<unsigned>(height));
    std::string file = head;
    file.append(reinterpret_cast<const char *>(rgb), static_cast<size_t>(width) * height * 3);
    return file;
}

bool preview_decode(std::string_view file, uint32_t &width, uint32_t &height, std::string_view &rgb) {
    uint64_t size[2] = {0, 0};
    size_t at = 3;
    if (file.substr(0, 3) != "P6\n") return false;
    for (int which = 0; which < 2; ++which) {
        const size_t first = at;
        for (; at < file.size() && file[at] >= '0' && file[at] <= '9' && size[which] <= 4096; ++at) size[which] = size[which] * 10 + static_cast<uint64_t>(file[at] - '0');
        if (at == first || at >= file.size() || file[at] != (which == 0 ? ' ' : '\n') || size[which] == 0 || size[which] > 4096) return false;
        ++at;
    }
    if (file.substr(at, 4) != "255\n" || file.size() - at - 4 != size[0] * size[1] * 3) return false;
    width = static_cast<uint32_t>(size[0]);
    height = static_cast<uint32_t>(size[1]);
    rgb = file.substr(at + 4);
    return true;
}

static std::string whole_file(const std::filesystem::path &file, uintmax_t most) {
    std::error_code failed;
    if (!std::filesystem::is_regular_file(file, failed) || failed) return {};
    const uintmax_t size = std::filesystem::file_size(file, failed);
    if (failed || size == 0 || size > most) return {};
    std::string bytes(static_cast<size_t>(size), '\0');
    std::ifstream in(file, std::ios::binary);
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<uintmax_t>(in.gcount()) == size ? bytes : std::string();
}

std::string preview_read(const std::filesystem::path &file) { return whole_file(file, kPreviewBytes); }

bool begins(const std::filesystem::path &folder, float &pitch) {
    std::vector<Frame> frames;
    uint64_t first_cycle = 0;
    const uint32_t noted = stored_frames(whole_file(folder / "capsule.toml", 4096));
    if (noted == 0 || !decode(whole_file(folder / "input.bin", kInputBytes), frames, first_cycle) || frames.size() != noted) return false;
    pitch = frames[0].pitch;
    return true;
}

std::string folder_url(const std::filesystem::path &folder) {
    static constexpr char hex[] = "0123456789ABCDEF";
    const std::u8string text = folder.generic_u8string();
    std::string url = !text.empty() && text[0] == u8'/' ? "file://" : "file:///";
    for (const char8_t unit : text) {
        const unsigned char c = static_cast<unsigned char>(unit);
        const bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':';
        if (plain) url += static_cast<char>(c);
        else url.append({'%', hex[c >> 4], hex[c & 15]});
    }
    return url;
}

}  /* namespace disruptor::capsule */

#ifndef DISRUPTOR_CAPSULE_NO_HOST
namespace {

namespace fs = std::filesystem;
using disruptor::capsule::Frame;

constexpr size_t kRamBytes = 0x200000;
constexpr uint32_t kVBlankCycles = 564480u;  /* VBLANK_CYCLES of the framework's interrupts.c */
constexpr size_t kSettle = 120, kInStep = 60, kLongest = 60 * 60 * 10;  /* ten minutes at 60 VBlanks a second, then the recording ends itself */

fs::path from_utf8(const char *text) { return fs::path(std::u8string(text, text + std::strlen(text))); }

std::string utf8(const fs::path &path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

enum class Recording { kOff, kSaving, kOn };
enum class Replay { kOff, kAsked, kLoading, kOn };

struct Capsule {
    Recording recording = Recording::kOff;
    Replay replay = Replay::kOff;
    fs::path made, played, pictures;
    std::string states_before;
    bool states_turned = false;  /* the frontend's slot folder is a capsule's now */
    uint32_t bios = 0, entry = 0;
    std::vector<Frame> recorded, replayed;
    std::vector<uint64_t> seen;
    Frame open;  /* the VBlank being recorded: its memory is known, its input is not yet */
    size_t at = 0, same = 0, last_same = 0, unwritten = 0;
    uint64_t first_cycle = 0, played_first_cycle = 0;
    uint32_t noted_at = 0;
    uint8_t noted = 0;
    size_t scripted = 0, waited = 0;
    const char *notice = nullptr;
    std::chrono::steady_clock::time_point notice_until{};
    bool loaded = false;
    bool end_asked = false;  /* the key came again while the recording's state was still being saved */
    fs::path loading;        /* the capsule whose state was asked for, empty when none is */
    bool load_done = false;
    float load_pitch = 0.0f; /* the look up or down its recording began with, which no state holds */
    uint32_t previews = 0;   /* pictures kept for the list so far */
    bool preview_asked = false;
    std::vector<unsigned char> preview = std::vector<unsigned char>(disruptor::capsule::kPreviewWidth * disruptor::capsule::kPreviewWidth * 3u);

    Capsule() {
        if (const char *record = std::getenv("PSX_DISRUPTOR_CAPSULE_RECORD"); record && record[0])
            scripted = static_cast<size_t>(std::strtoull(record, nullptr, 10));
        const char *replay_of = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY");
        if (!replay_of || !replay_of[0]) return;
        played = from_utf8(replay_of);
        std::ifstream input(played / "input.bin", std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(input)), {});
        if (!disruptor::capsule::decode(bytes, replayed, played_first_cycle) || replayed.empty()) {
            std::fprintf(stderr, "disruptor: capsule: %s holds no readable input.bin\n", replay_of);
            std::exit(2);
        }
        if (const char *out = std::getenv("PSX_DISRUPTOR_CAPSULE_FRAMES"); out && out[0]) pictures = from_utf8(out);
        replay = Replay::kAsked;
    }
};

Capsule g_capsule;

/* A state goes through the framework's slot 0 with the slot folder turned to the capsule for that one save or load. */
bool turn_states_to(const fs::path &folder) {
    Capsule &capsule = g_capsule;
    capsule.states_before = savestate_dir();
    savestate_get_integrity(&capsule.bios, &capsule.entry);
    if (capsule.states_before.empty() || capsule.entry == 0u) return false;
    savestate_configure(utf8(folder).c_str(), capsule.bios, capsule.entry);
    capsule.states_turned = true;
    return true;
}

void turn_states_back() {
    savestate_configure(g_capsule.states_before.c_str(), g_capsule.bios, g_capsule.entry);
    g_capsule.states_turned = false;
}

bool write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}

uint64_t memory_now() { return disruptor::capsule::digest(memory_get_ram_ptr(), kRamBytes); }

/* The open frame gets what the last VBlank left in the pads and wrote into the game. */
void close_frame() {
    Capsule &capsule = g_capsule;
    capsule.open.pad[0] = sio_get_pad_buttons_slot(0);
    capsule.open.pad[1] = sio_get_pad_buttons_slot(1);
    capsule.open.byte_at = capsule.noted_at;
    capsule.open.byte = capsule.noted;
    capsule.open.pitch = static_cast<float>(disruptor_mouse_vertical_pitch());
    capsule.recorded.push_back(capsule.open);
    capsule.noted_at = 0;
}

void open_frame() {
    g_capsule.open = Frame{};
    g_capsule.open.memory = memory_now();
    g_capsule.noted_at = 0;
}

void say(const char *word) {
    g_capsule.notice = word;
    g_capsule.notice_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

void refuse_recording(const char *why) {
    std::fprintf(stderr, "disruptor: capsule: no recording, %s\n", why);
    g_capsule.recording = Recording::kOff;
    say("REC refused");
    if (g_capsule.scripted == 0) return;
    std::fflush(nullptr);
    std::_Exit(2);  /* a script waits for the run to end */
}

/* The renderer hands a picture over a VBlank or two after it is asked: it is kept under the next number, the last number again once all are used. */
void keep_preview() {
    Capsule &capsule = g_capsule;
    int width = 0, height = 0;
    if (!capsule.preview_asked || !gl_renderer_snap_taken(capsule.preview.data(), static_cast<int>(capsule.preview.size()), &width, &height)) return;
    capsule.preview_asked = false;
    if (write(capsule.made / disruptor::capsule::preview_name(disruptor::capsule::preview_slot(capsule.previews)),
              disruptor::capsule::preview(capsule.preview.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height))))
        ++capsule.previews;
}

/* Writes what was recorded. Its last frame is there for its memory alone: a replay ends on it. */
void store_recording(const char *why) {
    Capsule &capsule = g_capsule;
    keep_preview();
    gl_renderer_snap_cancel();
    capsule.preview_asked = false;
    char about[256];
    std::snprintf(about, sizeof(about), "frames = %zu\ncodegen_hash = \"0x%08X\"\nentry_pc = \"0x%08X\"\nended = \"%s\"\n",
                  capsule.recorded.size(), static_cast<unsigned>(PSX_OVERLAY_CODEGEN_HASH), static_cast<unsigned>(capsule.entry), why);
    std::error_code failed;
    const char *settings = psx_host_user_settings_path();
    if (settings && fs::exists(from_utf8(settings), failed))
        fs::copy_file(from_utf8(settings), capsule.made / "settings.toml", fs::copy_options::overwrite_existing, failed);
    const bool input = write(capsule.made / "input.bin", disruptor::capsule::encode(capsule.recorded, capsule.first_cycle));
    const bool stored = write(capsule.made / "capsule.toml", about) && input && !failed;
    if (stored)
        std::fprintf(stderr, "disruptor: capsule: %zu frames in %s (%s)\n", capsule.recorded.size(), utf8(capsule.made).c_str(), why);
    else
        std::fprintf(stderr, "disruptor: capsule: the capsule in %s could NOT be written whole (%s)\n", utf8(capsule.made).c_str(), why);
    capsule.recorded.clear();
    capsule.recording = Recording::kOff;
    say(stored ? "REC saved" : "REC NOT saved");
    if (capsule.scripted == 0) return;
    std::fflush(nullptr);
    std::_Exit(stored ? 0 : 2);
}

/* Called before the pads are sampled, so the game's memory is that of this VBlank and the pads are still the last one's. */
void finish_recording(const char *why) {
    Capsule &capsule = g_capsule;
    close_frame();
    Frame last;
    last.pitch = capsule.open.pitch;
    last.memory = memory_now();
    capsule.recorded.push_back(last);
    store_recording(why);
}

void write_picture(size_t number) {
    GpuDisplayInfo display{};
    gpu_get_display_info(&display);
    if (display.width == 0 || display.height == 0) return;
    std::vector<uint8_t> rgb(static_cast<size_t>(display.width) * display.height * 3);
    for (uint32_t y = 0; y < display.height; ++y)
        for (uint32_t x = 0; x < display.width; ++x) {
            uint8_t *pixel = rgb.data() + (static_cast<size_t>(y) * display.width + x) * 3;
            gpu_display_pixel_rgb(&display, x, y, pixel, pixel + 1, pixel + 2);
        }
    char name[32];
    std::snprintf(name, sizeof(name), "%05zu.bmp", number);
    if (!write(g_capsule.pictures / name, disruptor::capsule::bitmap(rgb, display.width, display.height))) ++g_capsule.unwritten;
}

[[noreturn]] void finish_replay() {
    Capsule &capsule = g_capsule;
    const bool in_step = capsule.same != 0 && capsule.last_same + kInStep >= capsule.replayed.size();
    std::fprintf(stderr, "disruptor: capsule: replayed %zu frames and %s: the game's memory is the recording's at %zu of them, last at frame %zu\n",
                 capsule.replayed.size(), in_step ? "stayed in step with the recording" : "LEFT the recording", capsule.same, capsule.last_same);
    if (!capsule.pictures.empty()) {
        std::string lines;
        char line[64];
        for (size_t index = 0; index < capsule.seen.size(); ++index) {
            std::snprintf(line, sizeof(line), "%zu %016llx %016llx\n", index, static_cast<unsigned long long>(capsule.replayed[index].memory),
                          static_cast<unsigned long long>(capsule.seen[index]));
            lines += line;
        }
        if (!write(capsule.pictures / "memory.txt", lines)) ++capsule.unwritten;
        if (capsule.unwritten != 0)
            std::fprintf(stderr, "disruptor: capsule: %zu of the files asked for in %s could NOT be written\n", capsule.unwritten, utf8(capsule.pictures).c_str());
    }
    std::fflush(nullptr);
    std::_Exit(!in_step ? 3 : capsule.unwritten != 0 ? 2 : 0);  /* the run ends inside a VBlank on the game's thread: nothing is left to save */
}

void replay_frame() {
    Capsule &capsule = g_capsule;
    const Frame &frame = capsule.replayed[capsule.at];
    capsule.seen.push_back(memory_now());
    if (capsule.seen.back() == frame.memory) {
        ++capsule.same;
        capsule.last_same = capsule.at;
    }
    if (!capsule.pictures.empty()) write_picture(capsule.at);
    if (++capsule.at == capsule.replayed.size()) finish_replay();
    sio_set_pad_state_slot(0, frame.pad[0]);
    sio_set_pad_state_slot(1, frame.pad[1]);
    if (frame.byte_at != 0u) {
        psx_write_byte(frame.byte_at, frame.byte);
        disruptor_capsule_note_byte(frame.byte_at, frame.byte);  /* a recording made inside a replay holds the byte too */
    }
    disruptor_mouse_set_vertical_pitch(static_cast<double>(frame.pitch));
}

}  /* namespace */

std::filesystem::path disruptor::capsule::home() {
    std::error_code failed;
    const char *settings = psx_host_user_settings_path();
    return fs::absolute((settings ? from_utf8(settings).parent_path() : fs::path()) / "capsules", failed);
}

std::string disruptor::capsule::recording() {
    return g_capsule.recording == Recording::kOff ? std::string() : utf8(g_capsule.made.filename());
}

bool disruptor::capsule::load(std::string_view name) {
    Capsule &capsule = g_capsule;
    int64_t began = 0;
    float pitch = 0.0f;
    std::error_code failed;
    const fs::path folder = home() / fs::path(std::u8string(name.begin(), name.end()));
    const bool free_now = capsule.recording == Recording::kOff && capsule.replay == Replay::kOff && capsule.loading.empty() && !savestate_pending() &&
                          !psx_netplay_active();
    if (!free_now || !stored_name(name, began) || fs::is_symlink(folder, failed) || failed || !fs::is_directory(folder, failed) || failed ||
        !begins(folder, pitch) || !turn_states_to(folder)) {
        say("capsule NOT loaded");
        return false;
    }
    if (!savestate_slot_exists(0) || !savestate_request_load(0)) {
        turn_states_back();
        say("capsule NOT loaded");
        return false;
    }
    capsule.loading = folder;
    capsule.load_pitch = pitch;
    capsule.load_done = false;
    return true;
}

extern "C" const char *disruptor_capsule_notice(void) {
    return std::chrono::steady_clock::now() < g_capsule.notice_until ? g_capsule.notice : nullptr;
}

extern "C" void disruptor_capsule_note_byte(uint32_t address, uint8_t value) {
    g_capsule.noted_at = address;
    g_capsule.noted = value;
}

extern "C" void disruptor_capsule_state_loaded(void) {
    if (!g_capsule.loading.empty()) g_capsule.load_done = true;
    if (g_capsule.recording == Recording::kOn) {  /* a capsule holds one state: the recording ends at the last VBlank before this one */
        if (g_capsule.recorded.empty()) refuse_recording("a state was loaded before its first frame");
        else store_recording("a state was loaded");
    }
    if (g_capsule.replay != Replay::kLoading) return;
    g_capsule.loaded = true;
    /* A state does not hold how far the game is into its VBlank period: it would run on from wherever this session was. The capsule holds it. */
    const uint64_t now = psx_get_cycle_count(), first = g_capsule.played_first_cycle;
    const uint32_t left = interrupts_cycles_to_vblank();
    if (left == 0u || first <= now || first - now > kVBlankCycles) {
        std::fprintf(stderr, "disruptor: capsule: the first VBlank is not within a period of the state, so the replay keeps this session's\n");
        return;
    }
    interrupts_advance_cycles(left - static_cast<uint32_t>(first - now));
}

extern "C" void disruptor_capsule_toggle(void) {
    Capsule &capsule = g_capsule;
    if (capsule.recording == Recording::kOn) {
        finish_recording("the key");
        return;
    }
    if (capsule.recording == Recording::kSaving) capsule.end_asked = true;
    if (capsule.recording != Recording::kOff || capsule.replay == Replay::kAsked || capsule.replay == Replay::kLoading || !capsule.loading.empty() ||
        savestate_pending())
        return;
    if (psx_netplay_active()) return refuse_recording("a network game is not recorded");  /* the frame loop does not call the capsule there */
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::error_code failed;
    const fs::path all = disruptor::capsule::home();
    fs::create_directories(all, failed);
    bool made = false;  /* a folder of its own: two recordings may start within one second */
    for (int copy = 1; copy < 100 && !failed && !made; ++copy) {
        capsule.made = all / (std::to_string(now) + (copy > 1 ? "-" + std::to_string(copy) : ""));
        made = fs::create_directory(capsule.made, failed);
    }
    if (failed || !made || !turn_states_to(capsule.made)) return refuse_recording("the folder or the save states are not there");
    if (!savestate_request_save(0)) {
        turn_states_back();
        return refuse_recording("a save state is refused now");
    }
    capsule.recorded.clear();
    capsule.end_asked = false;
    capsule.recording = Recording::kSaving;
}

extern "C" int disruptor_capsule_holds_states(void) { return g_capsule.states_turned ? 1 : 0; }

extern "C" void disruptor_capsule_key(int repeat, int ctrl, int shift) {
    switch (disruptor::capsule::press(repeat != 0, ctrl != 0, shift != 0, g_capsule.recording != Recording::kOff)) {
    case disruptor::capsule::Press::kNothing:
        break;
    case disruptor::capsule::Press::kRecord:
        disruptor_capsule_toggle();
        break;
    case disruptor::capsule::Press::kLoad:
        if (const std::string name = disruptor::capsule::newest(disruptor::capsule::stored(disruptor::capsule::home())); name.empty()) say("no capsule");
        else disruptor::capsule::load(name);
        break;
    }
}

extern "C" void disruptor_capsule_before_input(void) {
    Capsule &capsule = g_capsule;
    switch (capsule.recording) {
    case Recording::kOff:
        if (capsule.scripted != 0 && capsule.replay != Replay::kAsked && capsule.replay != Replay::kLoading && ++capsule.waited == kSettle)
            disruptor_capsule_toggle();
        break;
    case Recording::kSaving:
        if (savestate_pending()) break;
        if (const bool saved = savestate_slot_exists(0) != 0; turn_states_back(), !saved) {
            refuse_recording("the state was not written");
            break;
        }
        std::fprintf(stderr, "disruptor: capsule: recording into %s\n", utf8(capsule.made).c_str());
        gl_renderer_snap_cancel();
        capsule.previews = 0;
        capsule.preview_asked = false;
        capsule.recording = Recording::kOn;
        say("REC");
        capsule.first_cycle = psx_get_cycle_count();
        open_frame();
        break;
    case Recording::kOn:
        if (capsule.end_asked) {
            finish_recording("the key");
            break;
        }
        if (capsule.recorded.size() + 2 >= (capsule.scripted != 0 ? capsule.scripted : kLongest)) {
            finish_recording(capsule.scripted != 0 ? "the asked length" : "the longest recording");
            break;
        }
        keep_preview();
        if (disruptor::capsule::preview_due(capsule.recorded.size()) && gl_renderer_snap_shown(static_cast<int>(disruptor::capsule::kPreviewWidth)))
            capsule.preview_asked = true;
        close_frame();
        open_frame();
        break;
    }
}

/* The state asked for was loaded or refused: the slot folder goes back, and the look up or down is the recording's first. */
static void finish_load() {
    Capsule &capsule = g_capsule;
    turn_states_back();
    if (capsule.load_done) disruptor_mouse_set_vertical_pitch(capsule.load_pitch);
    std::fprintf(stderr, "disruptor: capsule: the state of %s %s\n", utf8(capsule.loading).c_str(),
                 capsule.load_done ? "is loaded" : "was refused: it is of another build or disc");
    say(capsule.load_done ? "capsule loaded" : "capsule NOT loaded");
    capsule.loading.clear();
}

extern "C" void disruptor_capsule_after_input(void) {
    Capsule &capsule = g_capsule;
    if (!capsule.loading.empty() && !savestate_pending()) finish_load();
    if (capsule.replay == Replay::kAsked) {
        if (!turn_states_to(capsule.played) || !savestate_request_load(0)) {
            std::fprintf(stderr, "disruptor: capsule: the state of %s cannot be loaded\n", utf8(capsule.played).c_str());
            std::fflush(nullptr);
            std::_Exit(2);
        }
        capsule.replay = Replay::kLoading;
        return;
    }
    if (capsule.replay == Replay::kLoading) {
        if (savestate_pending()) return;
        turn_states_back();
        if (!capsule.loaded) {
            std::fprintf(stderr, "disruptor: capsule: the state of %s was refused: it is of another build or disc\n", utf8(capsule.played).c_str());
            std::fflush(nullptr);
            std::_Exit(2);
        }
        capsule.replay = Replay::kOn;
    }
    if (capsule.replay == Replay::kOn) replay_frame();
}
#endif
