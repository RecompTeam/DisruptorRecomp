#pragma once

#include "game/session/state_picture.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/* What the save state screen reads from a slot's file: the game's mission and difficulty and the picture its display
 * held, and the picture of the screen kept beside the state when it was saved. */
namespace disruptor::state_files {

inline constexpr uint32_t kMagic = 0x50535842u; /* "PSXB", boot_state.h */
inline constexpr size_t kHeaderBytes = 36u, kSectionHead = 16u;
inline constexpr uint32_t kPacked = 1u;
inline constexpr uint32_t kRamSection = 0x02u, kGpuSection = 0x07u, kVramSection = 0x08u;
inline constexpr size_t kRamBytes = 2u << 20, kRamMost = 8u << 20, kVramBytes = 1u << 20;
inline constexpr uint32_t kSectionsMost = 64u;
inline constexpr uint32_t kMission = 0x80071354u, kDifficulty = 0x80071708u;
/* gpu_snap_emit's words: the display's widths, depth, whether it is off, and its corner in video memory. */
inline constexpr size_t kHres1 = 18u, kHres2 = 19u, kDepth = 22u, kDisabled = 24u, kDisplayX = 28u, kDisplayY = 29u;
inline constexpr uint32_t kVramWide = 1024u, kVramHigh = 512u, kDisplayHigh = 240u;
inline constexpr uint32_t kPictureMost = 4096u;

struct Contents {
    uint32_t mission = 0;
    uint8_t difficulty = 0;
    uint32_t ram_crc = 0;
    Picture display;
};

struct Shown {
    uint32_t ram_crc = 0;
    Place place = Place::unknown;
    Picture picture;
};

struct Section {
    uint32_t tag = 0, flags = 0;
    std::string_view payload;
};

inline uint32_t le32(std::string_view bytes, size_t at) {
    return static_cast<uint32_t>(static_cast<uint8_t>(bytes[at])) | static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + 1])) << 8 |
           static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + 2])) << 16 |
           static_cast<uint32_t>(static_cast<uint8_t>(bytes[at + 3])) << 24;
}

inline void put_le32(std::string &bytes, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>(value >> shift & 0xFFu));
}

/* The sections of a state's file in order, none when it is no state or a length runs past its end. */
inline std::optional<std::vector<Section>> sections(std::string_view file) {
    if (file.size() < kHeaderBytes || le32(file, 0) != kMagic) return std::nullopt;
    const uint32_t count = le32(file, 28);
    if (count > kSectionsMost) return std::nullopt;
    std::vector<Section> out;
    size_t at = kHeaderBytes;
    for (uint32_t index = 0; index < count; ++index) {
        if (file.size() - at < kSectionHead) return std::nullopt;
        const uint64_t length = le32(file, at + 8) | static_cast<uint64_t>(le32(file, at + 12)) << 32;
        if (length > file.size() - at - kSectionHead) return std::nullopt;
        out.push_back({le32(file, at), le32(file, at + 4), file.substr(at + kSectionHead, static_cast<size_t>(length))});
        at += kSectionHead + static_cast<size_t>(length);
    }
    return out;
}

/* A section's bytes as the machine held them, inflated when it was packed: `bytes` long, or at most `most` when 0. */
inline std::optional<std::vector<uint8_t>> unpacked(const Section &section, size_t bytes, size_t most = 0u) {
    const bool packed_bytes = (section.flags & kPacked) != 0u;
    if (packed_bytes && section.payload.size() < 4u) return std::nullopt;
    const size_t whole = bytes != 0u ? bytes : packed_bytes ? le32(section.payload, 0) : section.payload.size();
    if (whole > (bytes != 0u ? bytes : most)) return std::nullopt;
    std::vector<uint8_t> out(whole);
    if (!packed_bytes) {
        if (section.payload.size() != whole) return std::nullopt;
        std::transform(section.payload.begin(), section.payload.end(), out.begin(), [](char each) { return static_cast<uint8_t>(each); });
        return out;
    }
    if (le32(section.payload, 0) != whole) return std::nullopt;
    uLongf length = static_cast<uLongf>(whole);
    const std::string_view packed = section.payload.substr(4);
    if (uncompress(out.data(), &length, reinterpret_cast<const Bytef *>(packed.data()), static_cast<uLong>(packed.size())) != Z_OK ||
        length != whole)
        return std::nullopt;
    return out;
}

/* The picture the display showed: 15-bit pixels at its corner, the first 240 rows. None for 24-bit movies or a display off. */
inline Picture display_picture(const std::vector<uint8_t> &vram, std::string_view gpu) {
    if (vram.size() != kVramBytes || gpu.size() < (kDisplayY + 1u) * 4u) return {};
    const auto word = [gpu](size_t index) { return le32(gpu, index * 4u); };
    if (word(kDepth) != 0u || word(kDisabled) != 0u) return {};
    constexpr std::array<uint32_t, 4> widths = {256u, 320u, 512u, 640u};
    const uint32_t wide = word(kHres2) != 0u ? 368u : widths[word(kHres1) & 3u];
    const uint32_t left = word(kDisplayX), top = word(kDisplayY);
    if (left >= kVramWide || top >= kVramHigh || left + wide > kVramWide || top + kDisplayHigh > kVramHigh) return {};
    Picture picture{wide, kDisplayHigh, std::vector<uint8_t>(static_cast<size_t>(wide) * kDisplayHigh * 3u)};
    for (uint32_t row = 0; row < kDisplayHigh; ++row)
        for (uint32_t column = 0; column < wide; ++column) {
            const size_t at = (static_cast<size_t>(top + row) * kVramWide + left + column) * 2u;
            const uint32_t pixel = vram[at] | static_cast<uint32_t>(vram[at + 1]) << 8;
            uint8_t *out = &picture.rgb[(static_cast<size_t>(row) * wide + column) * 3u];
            out[0] = widened(pixel & 31u);
            out[1] = widened(pixel >> 5 & 31u);
            out[2] = widened(pixel >> 10 & 31u);
        }
    return picture;
}

inline std::optional<Contents> read_contents(std::string_view file) {
    const std::optional<std::vector<Section>> all = sections(file);
    if (!all) return std::nullopt;
    const Section *ram = nullptr, *gpu = nullptr, *vram = nullptr;
    for (const Section &section : *all) {
        if (section.tag == kRamSection) ram = &section;
        if (section.tag == kGpuSection) gpu = &section;
        if (section.tag == kVramSection) vram = &section;
    }
    if (!ram) return std::nullopt;
    const std::optional<std::vector<uint8_t>> memory = unpacked(*ram, 0u, kRamMost);
    if (!memory || memory->size() < kRamBytes) return std::nullopt;
    Contents contents;
    const size_t mission = kMission & 0x1FFFFFu;
    contents.mission = (*memory)[mission] | static_cast<uint32_t>((*memory)[mission + 1]) << 8 |
                       static_cast<uint32_t>((*memory)[mission + 2]) << 16 | static_cast<uint32_t>((*memory)[mission + 3]) << 24;
    contents.difficulty = (*memory)[kDifficulty & 0x1FFFFFu];
    contents.ram_crc = static_cast<uint32_t>(crc32(0L, memory->data(), static_cast<uInt>(memory->size())));
    if (gpu && vram) {
        const std::optional<std::vector<uint8_t>> video = unpacked(*vram, kVramBytes);
        const std::optional<std::vector<uint8_t>> registers = unpacked(*gpu, 0u, 4096u);
        if (video && registers)
            contents.display = display_picture(*video, std::string_view(reinterpret_cast<const char *>(registers->data()), registers->size()));
    }
    return contents;
}

inline constexpr std::string_view kShownMagic = "DSS1";

/* The picture of the screen beside a state: the state's RAM checksum ties it to the state it was saved with. */
inline std::string encode(const Shown &shown) {
    std::string bytes(kShownMagic);
    put_le32(bytes, shown.ram_crc);
    put_le32(bytes, static_cast<uint32_t>(shown.place));
    put_le32(bytes, shown.picture.width);
    put_le32(bytes, shown.picture.height);
    uLongf room = compressBound(static_cast<uLong>(shown.picture.rgb.size()));
    std::string packed(static_cast<size_t>(room), '\0');
    if (compress2(reinterpret_cast<Bytef *>(packed.data()), &room, shown.picture.rgb.data(), static_cast<uLong>(shown.picture.rgb.size()),
                  Z_BEST_SPEED) != Z_OK)
        return {};
    packed.resize(static_cast<size_t>(room));
    return bytes + packed;
}

inline std::optional<Shown> decode(std::string_view bytes) {
    if (bytes.size() < 20u || bytes.substr(0, 4) != kShownMagic) return std::nullopt;
    Shown shown;
    shown.ram_crc = le32(bytes, 4);
    const uint32_t place = le32(bytes, 8);
    if (place > static_cast<uint32_t>(Place::menus)) return std::nullopt;
    shown.place = static_cast<Place>(place);
    const uint32_t width = le32(bytes, 12), height = le32(bytes, 16);
    if (width == 0u || height == 0u || width > kPictureMost || height > kPictureMost) return std::nullopt;
    shown.picture = {width, height, std::vector<uint8_t>(static_cast<size_t>(width) * height * 3u)};
    uLongf length = static_cast<uLongf>(shown.picture.rgb.size());
    const std::string_view packed = bytes.substr(20);
    if (uncompress(shown.picture.rgb.data(), &length, reinterpret_cast<const Bytef *>(packed.data()), static_cast<uLong>(packed.size())) != Z_OK ||
        length != shown.picture.rgb.size())
        return std::nullopt;
    return shown;
}

/* The game's part of a picture of the window, without the bars beside or above it at the display's aspect. */
inline Picture without_bars(const uint8_t *rgb, uint32_t width, uint32_t height, int numerator, int denominator) {
    if (!rgb || width == 0u || height == 0u || numerator <= 0 || denominator <= 0) return {};
    uint32_t wide = width, high = height;
    if (static_cast<uint64_t>(width) * static_cast<uint64_t>(denominator) > static_cast<uint64_t>(height) * static_cast<uint64_t>(numerator))
        wide = static_cast<uint32_t>(static_cast<uint64_t>(height) * static_cast<uint64_t>(numerator) / static_cast<uint64_t>(denominator));
    else
        high = static_cast<uint32_t>(static_cast<uint64_t>(width) * static_cast<uint64_t>(denominator) / static_cast<uint64_t>(numerator));
    if (wide == 0u || high == 0u) return {};
    const uint32_t left = (width - wide) / 2u, top = (height - high) / 2u;
    Picture picture{wide, high, std::vector<uint8_t>(static_cast<size_t>(wide) * high * 3u)};
    for (uint32_t row = 0; row < high; ++row) {
        const uint8_t *from = rgb + (static_cast<size_t>(top + row) * width + left) * 3u;
        std::copy(from, from + static_cast<size_t>(wide) * 3u, picture.rgb.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(row) * wide * 3u));
    }
    return picture;
}

/* The file beside a slot's state with another ending, ".pst" put aside. */
inline std::string beside(std::string_view state, std::string_view ending) {
    constexpr std::string_view kState = ".pst";
    if (state.size() < kState.size() || state.substr(state.size() - kState.size()) != kState) return {};
    return std::string(state.substr(0, state.size() - kState.size())) + std::string(ending);
}

}  // namespace disruptor::state_files
