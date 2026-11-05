/* PSX_DISRUPTOR_CAPSULE_REPLAY=<folder> replays a capsule and ends the run: 0 in step with the recording to the end, 3 not, 2 unplayable. */
/* PSX_DISRUPTOR_CAPSULE_FRAMES=<folder> writes each replayed frame as a picture, which is whole only without a window. */
/* PSX_DISRUPTOR_CAPSULE_RECORD=<frames> is the key for a script: two seconds in, it records that many frames and ends the run. */
/* PSX_DISRUPTOR_CAPSULE_FROM_BOOT=1 records from the first VBlank with no state, and replays such a capsule from power-on. */
/* Measured: a loaded state takes its interrupts a few instructions apart, so one frame in four differs in a few bytes, nine in a row at most. */
#include "game/session/capsule.h"
#include "game/input/baton_choice.h"
#include "game/input/weapon_list.h"

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
#include <utility>
#include <vector>

#ifndef DISRUPTOR_CAPSULE_NO_HOST
#include "cdrom.h"
#include "gpu.h"
#include "gpu_gl_renderer.h"
#include "gpu_presentation.h"
#include "interrupts.h"
#include "mod_plugins.h"
#include "overlay_api.h"
#include "psx_cycles.h"
#include "savestate.h"

extern "C" uint8_t *memory_get_ram_ptr(void);
extern "C" void psx_host_write_byte(uint32_t address, uint8_t value);
extern "C" uint16_t sio_get_pad_buttons_slot(int slot);
extern "C" void psx_game_pad_word(int slot, uint16_t word);
extern "C" void psx_host_video_get_applied_display_aspect(int *numerator, int *denominator);
extern "C" double disruptor_mouse_vertical_pitch(void);
extern "C" void disruptor_mouse_set_vertical_pitch(double pitch);
extern "C" const char *psx_host_user_settings_path(void);
extern "C" int psx_netplay_active(void);
extern "C" int gl_renderer_snap_shown(int width);
extern "C" void gl_renderer_snap_cancel(void);
extern "C" int gl_renderer_snap_taken(unsigned char *rgb, int room, int *width, int *height);
extern "C" int gl_renderer_snap_taken_sequence(void);
extern "C" void gl_renderer_shown_log_begin(uint64_t first, uint64_t period);
extern "C" void gl_renderer_shown_log_end(void);
extern "C" int gl_renderer_shown_log_take(PsxShownPicture *out, int room, unsigned *lost);
extern "C" uint64_t gl_renderer_shown_captures(void);
extern "C" int gl_renderer_shown_script(const PsxShownPicture *pictures, int count, uint64_t first, uint64_t period);
extern "C" void gl_renderer_shown_script_counts(unsigned *shown, unsigned *missed, unsigned *late, unsigned *timeouts, unsigned *left);
#endif

namespace disruptor::capsule {
namespace {

constexpr char kMagic[8] = {'D', 'C', 'A', 'P', 'S', 'U', 'L', '4'};
constexpr size_t kFrameBytes = 35, kHeadBytes = sizeof(kMagic) + 4 + 8;

/* A frame of version 1 has no second byte write, one of version 2 no weapon list, one of version 3 no turn remainder or presenter count. */
size_t frame_bytes_of(char version) {
    switch (version) {
    case '1': return 21;
    case '2': return 26;
    case '3': return 27;
    case '4': return kFrameBytes;
    default: return 0;
    }
}

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
        put(out, frame.byte2_at, 4);
        put(out, frame.byte2, 1);
        put(out, frame.list, 1);
        uint32_t residual = 0;
        std::memcpy(&residual, &frame.residual, sizeof(residual));
        put(out, residual, 4);
        put(out, frame.captures, 4);
    }
    return out;
}

bool decode(std::string_view bytes, std::vector<Frame> &frames, uint64_t &first_cycle) {
    frames.clear();
    first_cycle = 0;
    if (bytes.size() < kHeadBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic) - 1) != 0) return false;
    const size_t frame_bytes = frame_bytes_of(bytes[sizeof(kMagic) - 1]);
    if (frame_bytes == 0) return false;
    const uint64_t count = take(bytes, sizeof(kMagic), 4);
    if (bytes.size() != kHeadBytes + count * frame_bytes) return false;
    first_cycle = take(bytes, sizeof(kMagic) + 4, 8);
    frames.resize(static_cast<size_t>(count));
    for (size_t index = 0; index < frames.size(); ++index) {
        const size_t at = kHeadBytes + index * frame_bytes;
        Frame &frame = frames[index];
        const auto pitch = static_cast<uint32_t>(take(bytes, at + 9, 4));
        frame.pad[0] = static_cast<uint16_t>(take(bytes, at, 2));
        frame.pad[1] = static_cast<uint16_t>(take(bytes, at + 2, 2));
        frame.byte_at = static_cast<uint32_t>(take(bytes, at + 4, 4));
        frame.byte = static_cast<uint8_t>(take(bytes, at + 8, 1));
        std::memcpy(&frame.pitch, &pitch, sizeof(pitch));
        frame.memory = take(bytes, at + 13, 8);
        if (frame_bytes > 21) {
            frame.byte2_at = static_cast<uint32_t>(take(bytes, at + 21, 4));
            frame.byte2 = static_cast<uint8_t>(take(bytes, at + 25, 1));
        }
        if (frame_bytes > 26) frame.list = static_cast<uint8_t>(take(bytes, at + 26, 1));
        if (frame_bytes > 27) {
            const auto residual = static_cast<uint32_t>(take(bytes, at + 27, 4));
            std::memcpy(&frame.residual, &residual, sizeof(residual));
            frame.captures = static_cast<uint32_t>(take(bytes, at + 31, 4));
        }
    }
    return true;
}

std::string shown_list(const std::vector<Shown> &pictures) {
    std::string out = "sequence,frame,kind,alpha,width,height\n";
    char line[96];
    for (const Shown &picture : pictures) {
        std::snprintf(line, sizeof(line), "%u,%u,%u,%.9g,%u,%u\n", static_cast<unsigned>(picture.sequence), static_cast<unsigned>(picture.frame),
                      static_cast<unsigned>(picture.kind), static_cast<double>(picture.alpha), static_cast<unsigned>(picture.width),
                      static_cast<unsigned>(picture.height));
        out += line;
    }
    return out;
}

bool shown_decode(std::string_view text, std::vector<Shown> &pictures) {
    constexpr std::string_view head = "sequence,frame,kind,alpha,width,height\n";
    std::vector<Shown> read;
    pictures.clear();
    if (text.substr(0, head.size()) != head) return false;
    for (size_t at = head.size(); at < text.size();) {
        const size_t end = text.find('\n', at);
        if (end == std::string_view::npos) return false;
        const std::string line(text.substr(at, end - at));
        unsigned sequence = 0, frame = 0, kind = 0, width = 0, height = 0;
        double alpha = 0.0;
        int used = 0;
        if (std::sscanf(line.c_str(), "%u,%u,%u,%lf,%u,%u%n", &sequence, &frame, &kind, &alpha, &width, &height, &used) != 6 ||
            static_cast<size_t>(used) != line.size() || kind > 1u || !(alpha >= 0.0 && alpha <= 1.0) || width > 65535u || height > 65535u)
            return false;
        if (!read.empty() && (sequence <= read.back().sequence || frame < read.back().frame)) return false;
        read.push_back({sequence, frame, static_cast<float>(alpha), static_cast<uint16_t>(width), static_cast<uint16_t>(height),
                            static_cast<uint8_t>(kind)});
        at = end + 1;
    }
    pictures = std::move(read);
    return true;
}

std::string marks_list(const std::vector<Mark> &marks) {
    std::string out = "mark,frame,picture,width,height\n";
    char line[96];
    for (size_t number = 0; number < marks.size(); ++number) {
        std::snprintf(line, sizeof(line), "%zu,%u,%lld,%u,%u\n", number, static_cast<unsigned>(marks[number].frame),
                      static_cast<long long>(marks[number].picture), static_cast<unsigned>(marks[number].width), static_cast<unsigned>(marks[number].height));
        out += line;
    }
    return out;
}

std::string mark_name(uint32_t number) {
    char name[32];
    std::snprintf(name, sizeof(name), "mark_%03u.ppm", static_cast<unsigned>(number));
    return name;
}

Verdict judge(const std::vector<Frame> &recorded, const std::vector<uint64_t> &seen) {
    Verdict verdict;
    size_t apart = 0, last_compared = 0;
    for (size_t index = 0; index < seen.size() && index < recorded.size(); ++index) {
        if (recorded[index].memory == 0u) {
            ++verdict.scripted;
            continue;
        }
        ++verdict.compared;
        last_compared = index;
        if (seen[index] == recorded[index].memory) {
            ++verdict.same;
            verdict.last_same = index;
            apart = 0;
        } else {
            verdict.longest_apart = std::max(verdict.longest_apart, ++apart);
        }
    }
    verdict.in_step = verdict.same != 0 && verdict.last_same + kInStep > last_compared;
    return verdict;
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

bool stored_aspect(std::string_view about, uint32_t &numerator, uint32_t &denominator) {
    constexpr std::string_view key = "aspect = \"";
    for (size_t line = 0; line < about.size(); line = about.find('\n', line) == std::string_view::npos ? about.size() : about.find('\n', line) + 1) {
        if (about.substr(line, key.size()) != key) continue;
        size_t at = line + key.size();
        uint32_t parts[2] = {0u, 0u};
        for (int part = 0; part < 2; ++part) {
            const size_t first = at;
            for (; at < about.size() && about[at] >= '0' && about[at] <= '9'; ++at) {
                parts[part] = parts[part] * 10u + static_cast<uint32_t>(about[at] - '0');
                if (parts[part] >= 100000u) return false;
            }
            if (at == first || parts[part] == 0u || at == about.size() || about[at] != (part == 0 ? ':' : '"')) return false;
            ++at;
        }
        if (at != about.size() && about[at] != '\n' && about[at] != '\r') return false;
        numerator = parts[0];
        denominator = parts[1];
        return true;
    }
    return false;
}

std::vector<uint64_t> page_digests(const uint8_t *bytes, size_t size) {
    std::vector<uint64_t> pages;
    pages.reserve((size + kPageBytes - 1) / kPageBytes);
    for (size_t at = 0; at < size; at += kPageBytes) pages.push_back(digest(bytes + at, std::min(kPageBytes, size - at)));
    return pages;
}

std::string pages_encode(const std::vector<std::vector<uint64_t>> &frames) {
    const size_t pages = frames.empty() ? 0 : frames[0].size();
    std::string out = "DPAGES01";
    put(out, frames.size(), 4);
    put(out, pages, 4);
    for (const std::vector<uint64_t> &frame : frames)
        for (size_t page = 0; page < pages; ++page) put(out, page < frame.size() ? frame[page] : 0u, 8);
    return out;
}

bool pages_decode(std::string_view file, size_t pages_per_frame, std::vector<std::vector<uint64_t>> &frames) {
    frames.clear();
    if (file.size() < 16 || file.substr(0, 8) != "DPAGES01") return false;
    const uint64_t count = take(file, 8, 4), pages = take(file, 12, 4);
    if (count > kPagedFrames || pages != pages_per_frame || file.size() != 16 + count * pages * 8) return false;
    for (uint64_t frame = 0; frame < count; ++frame) {
        std::vector<uint64_t> digests(static_cast<size_t>(pages));
        for (uint64_t page = 0; page < pages; ++page) digests[page] = take(file, static_cast<size_t>(16 + (frame * pages + page) * 8), 8);
        frames.push_back(std::move(digests));
    }
    return true;
}

std::vector<size_t> pages_apart(const std::vector<uint64_t> &recorded, const std::vector<uint64_t> &seen) {
    std::vector<size_t> apart;
    for (size_t page = 0; page < std::max(recorded.size(), seen.size()); ++page)
        if (page >= recorded.size() || page >= seen.size() || recorded[page] != seen[page]) apart.push_back(page);
    return apart;
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
constexpr uint32_t kWeaponInHand = 0x80077627u;
using disruptor::capsule::Frame;
using disruptor::capsule::Mark;
using disruptor::capsule::Shown;

constexpr size_t kRamBytes = 0x200000;
constexpr uint32_t kVBlankCycles = 564480u;  /* PSX_VBLANK_CYCLES_NTSC of the framework: the game runs NTSC only */
constexpr size_t kSettle = 120, kLongest = 60 * 60 * 10;  /* ten minutes at 60 VBlanks a second, then the recording ends itself */
constexpr uint32_t kCameraZ = 0x800775C8u, kCameraX = 0x800775CCu, kCameraHeight = 0x800775D0u, kYaw = 0x80077624u;
constexpr int kMarkWidth = 3840;
constexpr size_t kMarkWait = 120;  /* a mark's picture comes a VBlank or two later: two seconds is a failure */

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
    std::vector<std::vector<uint64_t>> pages, played_pages;  /* the first frames' memory by page, recorded and to be replayed */
    uint32_t aspect[2] = {0u, 0u}, played_aspect[2] = {0u, 0u};  /* the display aspect at the recording's first VBlank */
    bool aspect_applied = false, pages_told = false;
    uint64_t run = 0xCBF29CE484222325ull;  /* each replayed frame's memory and guest cycle count, folded: builds that run alike print one value */
    Frame open;  /* the VBlank being recorded: its memory is known, its input is not yet */
    size_t at = 0, same = 0, last_same = 0, unwritten = 0;
    uint64_t first_cycle = 0, played_first_cycle = 0;
    Frame notes;
    size_t scripted = 0, waited = 0;
    bool from_boot = false;  /* no state: a recording begins at power-on, a replay at its first cycle */
    const char *notice = nullptr;
    std::chrono::steady_clock::time_point notice_until{};
    bool loaded = false;
    bool end_asked = false;  /* the key came again while the recording's state was still being saved */
    fs::path loading;        /* the capsule whose state was asked for, empty when none is */
    bool load_done = false;
    float load_pitch = 0.0f; /* the look up or down its recording began with, which no state holds */
    uint32_t previews = 0;   /* pictures kept for the list so far */
    bool preview_asked = false;
    std::vector<Shown> shown;        /* what reached the screen while recording, or what a replay is to present again */
    uint64_t shown_base = 0;         /* the presenter's count of game frames when the recording or replay began */
    unsigned shown_lost = 0;
    std::vector<Mark> marks;
    bool mark_asked = false;
    std::vector<unsigned char> mark_pixels;
    size_t mark_waited = 0;          /* VBlanks the mark's picture has been waited for */
    bool settings_kept = false;      /* the settings were copied as the recording began */
    bool presented_whole = true;     /* a replay as shown presented every picture it could reach */
    bool as_shown = false;           /* the replay presents the recorded pictures instead of its own */
    fs::path pose;                   /* where a replay writes the camera at each frame */
    std::string poses;
    std::vector<unsigned char> preview = std::vector<unsigned char>(disruptor::capsule::kPreviewWidth * disruptor::capsule::kPreviewWidth * 3u);

    Capsule() {
        if (const char *boot = std::getenv("PSX_DISRUPTOR_CAPSULE_FROM_BOOT"); boot && boot[0] == '1') from_boot = true;
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
        (void)disruptor::capsule::stored_aspect(disruptor::capsule::whole_file(played / "capsule.toml", 4096), played_aspect[0], played_aspect[1]);
        std::ifstream paged(played / disruptor::capsule::kPagesFile, std::ios::binary);
        (void)disruptor::capsule::pages_decode(std::string((std::istreambuf_iterator<char>(paged)), {}), kRamBytes / disruptor::capsule::kPageBytes,
                                               played_pages);
        if (const char *out = std::getenv("PSX_DISRUPTOR_CAPSULE_FRAMES"); out && out[0]) pictures = from_utf8(out);
        if (const char *out = std::getenv("PSX_DISRUPTOR_CAPSULE_POSE"); out && out[0]) pose = from_utf8(out);
        if (const char *asked = std::getenv("PSX_DISRUPTOR_CAPSULE_AS_SHOWN"); asked && asked[0] == '1') {
            std::ifstream list(played / disruptor::capsule::kShownFile, std::ios::binary);
            const std::string listed((std::istreambuf_iterator<char>(list)), {});
            as_shown = disruptor::capsule::shown_decode(listed, shown) && !shown.empty();
            if (!as_shown) std::fprintf(stderr, "disruptor: capsule: %s holds no readable %s, the replay presents its own pictures\n", replay_of,
                                        disruptor::capsule::kShownFile);
        }
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
    savestate_configure(utf8(folder).c_str(), capsule.bios, capsule.entry, nullptr, 0u);
    capsule.states_turned = true;
    return true;
}

void turn_states_back() {
    savestate_configure(g_capsule.states_before.c_str(), g_capsule.bios, g_capsule.entry, nullptr, 0u);
    g_capsule.states_turned = false;
}

bool write(const fs::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return !out.fail();
}

uint64_t memory_now() { return disruptor::capsule::digest(memory_get_ram_ptr(), kRamBytes); }

int32_t ram_word(uint32_t address) {
    int32_t word = 0;
    std::memcpy(&word, memory_get_ram_ptr() + (address & (kRamBytes - 1u)), sizeof(word));
    return word;
}

/* The pictures the presenter logged since the last call join the recording's list. */
void take_shown() {
    Capsule &capsule = g_capsule;
    PsxShownPicture taken[256];
    for (int count; (count = gl_renderer_shown_log_take(taken, 256, &capsule.shown_lost)) > 0;)
        for (int index = 0; index < count; ++index)
            capsule.shown.push_back({taken[index].sequence, taken[index].frame, taken[index].alpha, taken[index].width, taken[index].height,
                                     taken[index].kind});
}

/* The open frame gets what the last VBlank left in the pads and wrote into the game. */
void close_frame() {
    Capsule &capsule = g_capsule;
    capsule.open.pad[0] = sio_get_pad_buttons_slot(0);
    capsule.open.pad[1] = sio_get_pad_buttons_slot(1);
    capsule.open.byte_at = capsule.notes.byte_at;
    capsule.open.byte = capsule.notes.byte;
    capsule.open.byte2_at = capsule.notes.byte2_at;
    capsule.open.byte2 = capsule.notes.byte2;
    capsule.open.list = capsule.notes.list;
    capsule.open.pitch = static_cast<float>(disruptor_mouse_vertical_pitch());
    capsule.open.residual = capsule.notes.residual;
    capsule.open.captures = static_cast<uint32_t>(gl_renderer_shown_captures() - capsule.shown_base);
    take_shown();
    capsule.recorded.push_back(capsule.open);
    capsule.notes = Frame{};
}

void open_frame() {
    g_capsule.open = Frame{};
    g_capsule.open.memory = memory_now();
    if (g_capsule.recorded.size() < disruptor::capsule::kPagedFrames)
        g_capsule.pages.push_back(disruptor::capsule::page_digests(memory_get_ram_ptr(), kRamBytes));
    g_capsule.notes = Frame{};
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

/* The picture a mark asked for is kept with the number of the picture it was read from. */
void keep_mark() {
    Capsule &capsule = g_capsule;
    int width = 0, height = 0;
    if (!capsule.mark_asked) return;
    if (!gl_renderer_snap_taken(capsule.mark_pixels.data(), static_cast<int>(capsule.mark_pixels.size()), &width, &height)) {
        if (++capsule.mark_waited < kMarkWait) return;
        gl_renderer_snap_cancel();  /* never taken, or larger than the room kept for it: the mark keeps no picture */
        capsule.mark_asked = false;
        say("MARK NOT saved");
        return;
    }
    capsule.mark_asked = false;
    Mark &mark = capsule.marks.back();
    if (!write(capsule.made / disruptor::capsule::mark_name(static_cast<uint32_t>(capsule.marks.size() - 1)),
               disruptor::capsule::preview(capsule.mark_pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height)))) {
        say("MARK NOT saved");
        return;
    }
    mark.picture = gl_renderer_snap_taken_sequence();
    mark.width = static_cast<uint32_t>(width);
    mark.height = static_cast<uint32_t>(height);
    say("MARK");
}

/* The settings as the recording begins: a replay runs with these. */
bool keep_settings() {
    std::error_code failed;
    const char *settings = psx_host_user_settings_path();
    if (!settings || !fs::exists(from_utf8(settings), failed)) return false;
    fs::copy_file(from_utf8(settings), g_capsule.made / "settings.toml", fs::copy_options::overwrite_existing, failed);
    return !failed;
}

/* Writes what was recorded. Its last frame is there for its memory alone: a replay ends on it. */
void store_recording(const char *why) {
    Capsule &capsule = g_capsule;
    keep_mark();
    if (!capsule.mark_asked) keep_preview();
    gl_renderer_snap_cancel();
    capsule.preview_asked = capsule.mark_asked = false;
    gl_renderer_shown_log_end();
    take_shown();
    if (capsule.shown_lost != 0)
        std::fprintf(stderr, "disruptor: capsule: %u shown pictures were lost, a full log could not keep them\n", capsule.shown_lost);
    char about[512];
    std::snprintf(about, sizeof(about),
                  "frames = %zu\ncodegen_hash = \"0x%08X\"\nentry_pc = \"0x%08X\"\nended = \"%s\"\npresented = %zu\nlost = %u\nmarks = %zu\nfrom_boot = %s\n",
                  capsule.recorded.size(), static_cast<unsigned>(PSX_OVERLAY_CODEGEN_HASH), static_cast<unsigned>(capsule.entry), why,
                  capsule.shown.size(), capsule.shown_lost, capsule.marks.size(), capsule.from_boot ? "true" : "false");
    std::string note = about;
    if (capsule.aspect[0] != 0u && capsule.aspect[1] != 0u)
        note += "aspect = \"" + std::to_string(capsule.aspect[0]) + ":" + std::to_string(capsule.aspect[1]) + "\"\n";
    std::error_code failed;
    const bool settings = capsule.settings_kept;
    const bool input = write(capsule.made / "input.bin", disruptor::capsule::encode(capsule.recorded, capsule.first_cycle));
    const bool lists = (capsule.shown.empty() || write(capsule.made / disruptor::capsule::kShownFile, disruptor::capsule::shown_list(capsule.shown))) &&
                       (capsule.marks.empty() || write(capsule.made / disruptor::capsule::kMarksFile, disruptor::capsule::marks_list(capsule.marks))) &&
                       (capsule.pages.empty() || write(capsule.made / disruptor::capsule::kPagesFile, disruptor::capsule::pages_encode(capsule.pages)));
    const bool stored = write(capsule.made / "capsule.toml", note) && input && settings && lists;
    if (stored)
        std::fprintf(stderr, "disruptor: capsule: %zu frames in %s (%s)\n", capsule.recorded.size(), utf8(capsule.made).c_str(), why);
    else
        std::fprintf(stderr, "disruptor: capsule: the capsule in %s could NOT be written whole (%s)\n", utf8(capsule.made).c_str(), why);
    capsule.recorded.clear();
    capsule.pages.clear();
    capsule.shown.clear();
    capsule.marks.clear();
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
    /* PSX_DISRUPTOR_CAPSULE_FRAMES_HIRES=N: every Nth frame at the internal resolution, from the renderer. */
    static const long hires_every = [] {
        const char *every = std::getenv("PSX_DISRUPTOR_CAPSULE_FRAMES_HIRES");
        return every ? std::strtol(every, nullptr, 10) : 0L;
    }();
    if (hires_every > 0) {
        if (number % static_cast<size_t>(hires_every) != 0) return;
        static std::vector<uint32_t> argb(static_cast<size_t>(1024) * 512 * 64);
        int width = 0, height = 0;
        const int read = gl_renderer_read_display_hires(static_cast<int>(display.display_x), static_cast<int>(display.display_y),
                                                        static_cast<int>(display.width), static_cast<int>(display.height),
                                                        argb.data(), static_cast<int>(argb.size()), &width, &height);
        if (read <= 0 || width <= 0 || height <= 0) return;
        std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
        for (size_t at = 0; at < static_cast<size_t>(width) * height; ++at) {
            rgb[at * 3] = static_cast<uint8_t>(argb[at] >> 16);
            rgb[at * 3 + 1] = static_cast<uint8_t>(argb[at] >> 8);
            rgb[at * 3 + 2] = static_cast<uint8_t>(argb[at]);
        }
        char name[32];
        std::snprintf(name, sizeof(name), "%05zu.bmp", number);
        if (!write(g_capsule.pictures / name, disruptor::capsule::bitmap(rgb, static_cast<uint32_t>(width), static_cast<uint32_t>(height))))
            ++g_capsule.unwritten;
        return;
    }
    /* Under GL the picture is in the renderer: bring it down, but never over a 24-bit movie's packed bytes. */
    if (display.depth24) gl_renderer_flush_cpu_uploads();
    else gl_renderer_sync_cpu();
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
    if (capsule.as_shown) {
        unsigned shown = 0, missed = 0, late = 0, timeouts = 0, left = 0;
        gl_renderer_shown_script_counts(&shown, &missed, &late, &timeouts, &left);
        capsule.presented_whole = late == 0 && timeouts == 0;
        std::fprintf(stderr,
                     "disruptor: capsule: presented %u of the %zu recorded pictures, %u could not be (%u after the first), %u waits given up, "
                     "%u still to come: %s\n",
                     shown, capsule.shown.size(), missed, late, timeouts, left, capsule.presented_whole ? "as shown" : "NOT as shown");
    }
    const disruptor::capsule::Verdict verdict = disruptor::capsule::judge(capsule.replayed, capsule.seen);
    const bool in_step = verdict.in_step;
    std::fprintf(stderr,
                 "disruptor: capsule: replayed %zu frames and %s: the game's memory is the recording's at %zu of the %zu it holds, last at frame %zu, "
                 "%zu apart at most in a row, %zu frames of a script; run %016llx\n",
                 capsule.replayed.size(), in_step ? "stayed in step with the recording" : "LEFT the recording", verdict.same, verdict.compared,
                 verdict.last_same, verdict.longest_apart, verdict.scripted, static_cast<unsigned long long>(capsule.run));
    if (!capsule.pose.empty() && !write(capsule.pose, "frame,x,z,height,yaw,captures\n" + capsule.poses)) ++capsule.unwritten;
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
    std::_Exit(!in_step ? 3 : capsule.unwritten != 0 ? 2 : !capsule.presented_whole ? 4 : 0);  /* the run ends inside a VBlank on the game's thread: nothing is left to save */
}

void replay_frame() {
    Capsule &capsule = g_capsule;
    const Frame &frame = capsule.replayed[capsule.at];
    if (!capsule.pose.empty()) {
        char line[96];
        std::snprintf(line, sizeof(line), "%zu,%d,%d,%d,%u,%lld\n", capsule.at, ram_word(kCameraX), ram_word(kCameraZ), ram_word(kCameraHeight),
                      static_cast<unsigned>(memory_get_ram_ptr()[kYaw & (kRamBytes - 1u)]), static_cast<long long>(gl_renderer_shown_captures() - capsule.shown_base));
        capsule.poses += line;
    }
    if (capsule.at == 0 && capsule.played_aspect[0] != 0u) {
        int numerator = 0, denominator = 0;
        psx_host_video_get_applied_display_aspect(&numerator, &denominator);
        std::fprintf(stderr, "disruptor: capsule: the replay shows %d:%d\n", numerator, denominator);
    }
    capsule.seen.push_back(memory_now());
    capsule.run = (capsule.run ^ capsule.seen.back()) * 0x100000001B3ull;
    capsule.run = (capsule.run ^ psx_cycle_count) * 0x100000001B3ull;
    if (capsule.seen.back() == frame.memory) {
        ++capsule.same;
        capsule.last_same = capsule.at;
    } else if (frame.memory != 0u && !capsule.pages_told && capsule.at < capsule.played_pages.size()) {
        capsule.pages_told = true;
        const std::vector<size_t> apart =
            disruptor::capsule::pages_apart(capsule.played_pages[capsule.at], disruptor::capsule::page_digests(memory_get_ram_ptr(), kRamBytes));
        std::string named;
        for (size_t index = 0; index < apart.size() && index < 16; ++index) {
            char page[16];
            std::snprintf(page, sizeof(page), " %08zX", 0x80000000u + apart[index] * disruptor::capsule::kPageBytes);
            named += page;
        }
        std::fprintf(stderr, "disruptor: capsule: frame %zu is apart in %zu pages of 4 KiB:%s%s\n", capsule.at, apart.size(),
                     named.c_str(), apart.size() > 16 ? " ..." : "");
    }
    if (!capsule.pictures.empty()) write_picture(capsule.at);
    if (++capsule.at == capsule.replayed.size()) finish_replay();
    psx_game_pad_word(0, frame.pad[0]);
    psx_game_pad_word(1, frame.pad[1]);
    if (frame.byte_at != 0u) {
        psx_host_write_byte(frame.byte_at, frame.byte);
        disruptor_capsule_note_byte(frame.byte_at, frame.byte);  /* a recording made inside a replay holds the byte too */
        if (frame.byte_at == kWeaponInHand) disruptor_weapon_chosen(frame.byte);
    }
    if (frame.byte2_at != 0u) {
        psx_host_write_byte(frame.byte2_at, frame.byte2);
        disruptor_capsule_note_byte(frame.byte2_at, frame.byte2);
        if (frame.byte2_at == kWeaponInHand) disruptor_weapon_chosen(frame.byte2);
    }
    if (frame.list == 1) disruptor_weapon_list_show();
    else if (frame.list == 2) disruptor_psionic_list_show();
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
    g_capsule.notes.note_byte(address, value);
}

extern "C" void disruptor_capsule_note_list(uint8_t list) { g_capsule.notes.list = list; }

extern "C" void disruptor_capsule_note_residual(double yaw) { g_capsule.notes.residual = static_cast<float>(yaw); }

extern "C" int disruptor_capsule_replay_residual(double *yaw) {
    const Capsule &capsule = g_capsule;
    if (capsule.replay != Replay::kOn || capsule.at == 0 || !yaw) return 0;
    *yaw = static_cast<double>(capsule.replayed[capsule.at - 1].residual);
    return 1;
}

extern "C" int disruptor_capsule_mark(int repeat) {
    Capsule &capsule = g_capsule;
    if (capsule.recording != Recording::kOn) return 0;
    if (repeat || capsule.mark_asked) return 1;
    if (capsule.preview_asked) {
        gl_renderer_snap_cancel();
        capsule.preview_asked = false;
    }
    capsule.mark_pixels.resize(static_cast<size_t>(kMarkWidth) * 2160u * 3u);
    if (!gl_renderer_snap_shown(kMarkWidth)) return 1;
    capsule.marks.push_back({static_cast<uint32_t>(capsule.recorded.size()), -1, 0u, 0u});
    capsule.mark_asked = true;
    capsule.mark_waited = 0;
    return 1;
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
    if (failed || !made || (!capsule.from_boot && !turn_states_to(capsule.made)))
        return refuse_recording("the folder or the save states are not there");
    if (!capsule.from_boot) cdrom_savestate_boost_hold(1);  /* the replay loads without the drive's post-load speed-up */
    if (!capsule.from_boot && !savestate_request_save(0)) {
        cdrom_savestate_boost_hold(0);
        turn_states_back();
        return refuse_recording("a save state is refused now");
    }
    capsule.pages.clear();
    capsule.recorded.clear();
    capsule.end_asked = false;
    capsule.recording = Recording::kSaving;
}

extern "C" int disruptor_capsule_holds_states(void) { return g_capsule.states_turned ? 1 : 0; }

extern "C" int disruptor_capsule_recording(void) {
    return g_capsule.recording != Recording::kOff || g_capsule.replay != Replay::kOff ? 1 : 0;
}

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
        if (capsule.scripted != 0 && capsule.replay != Replay::kAsked && capsule.replay != Replay::kLoading && ++capsule.waited == (capsule.from_boot ? 1u : kSettle))
            disruptor_capsule_toggle();
        break;
    case Recording::kSaving:
        if (savestate_pending()) break;
        cdrom_savestate_boost_hold(0);
        if (!capsule.from_boot) {
            const bool saved = savestate_slot_exists(0) != 0;
            turn_states_back();
            if (!saved) {
                refuse_recording("the state was not written");
                break;
            }
        }
        std::fprintf(stderr, "disruptor: capsule: recording into %s\n", utf8(capsule.made).c_str());
        gl_renderer_snap_cancel();
        capsule.previews = 0;
        capsule.preview_asked = capsule.mark_asked = false;
        capsule.shown.clear();
        capsule.marks.clear();
        capsule.shown_lost = 0;
        capsule.settings_kept = keep_settings();
        {
            int numerator = 0, denominator = 0;
            psx_host_video_get_applied_display_aspect(&numerator, &denominator);
            capsule.aspect[0] = static_cast<uint32_t>(std::max(numerator, 0));
            capsule.aspect[1] = static_cast<uint32_t>(std::max(denominator, 0));
        }
        capsule.recording = Recording::kOn;
        say("REC");
        capsule.first_cycle = psx_get_cycle_count();
        gl_renderer_shown_log_begin(capsule.first_cycle, kVBlankCycles);
        capsule.shown_base = gl_renderer_shown_captures();
        open_frame();
        if (disruptor_baton_held_by_choice()) disruptor_capsule_note_byte(kWeaponInHand, 0u);  /* the state forgets the choice */
        disruptor_weapon_list_state_loaded();  /* and the list a key showed, which its replay cannot know */
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
        keep_mark();
        if (!capsule.mark_asked) keep_preview();
        if (!capsule.mark_asked && disruptor::capsule::preview_due(capsule.recorded.size()) &&
            gl_renderer_snap_shown(static_cast<int>(disruptor::capsule::kPreviewWidth)))
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

/* Adaptive view follows this session's window: a replay shows the recording's aspect from its first VBlank, before its state loads. */
static void apply_played_aspect() {
    Capsule &capsule = g_capsule;
    capsule.aspect_applied = true;
    const uint32_t numerator = capsule.played_aspect[0], denominator = capsule.played_aspect[1];
    if (numerator == 0u) return;
    if (!psx_mod_set_fixed_display_aspect(numerator, denominator)) {
        std::fprintf(stderr, "disruptor: capsule: the recorded aspect %u:%u is refused, the replay keeps this session's\n", numerator, denominator);
        return;
    }
    gl_renderer_set_display_aspect(static_cast<int>(numerator), static_cast<int>(denominator));
}

extern "C" void disruptor_capsule_after_input(void) {
    Capsule &capsule = g_capsule;
    if (capsule.replay != Replay::kOff && !capsule.aspect_applied) apply_played_aspect();
    if (!capsule.loading.empty() && !savestate_pending()) finish_load();
    if (capsule.replay == Replay::kAsked && capsule.from_boot) {
        if (psx_get_cycle_count() < capsule.played_first_cycle) return;
        capsule.replay = Replay::kOn;
        capsule.as_shown = false;
        capsule.shown_base = gl_renderer_shown_captures();
    }
    if (capsule.replay == Replay::kAsked) {
        cdrom_savestate_boost_hold(1);
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
        cdrom_savestate_boost_hold(0);
        turn_states_back();
        if (!capsule.loaded) {
            std::fprintf(stderr, "disruptor: capsule: the state of %s was refused: it is of another build or disc\n", utf8(capsule.played).c_str());
            std::fflush(nullptr);
            std::_Exit(2);
        }
        capsule.replay = Replay::kOn;
        capsule.shown_base = gl_renderer_shown_captures();
        if (capsule.as_shown) {
            std::vector<PsxShownPicture> script;
            for (const Shown &picture : capsule.shown) script.push_back({picture.sequence, picture.frame, picture.alpha, picture.width, picture.height, picture.kind});
            if (!gl_renderer_shown_script(script.data(), static_cast<int>(script.size()), capsule.played_first_cycle, kVBlankCycles)) {
                std::fprintf(stderr, "disruptor: capsule: the presenter takes no script, the replay presents its own pictures\n");
                capsule.as_shown = false;
            }
        }
    }
    if (capsule.replay == Replay::kOn) replay_frame();
}
#endif
