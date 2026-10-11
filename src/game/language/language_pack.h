#ifndef DISRUPTOR_LANGUAGE_PACK_H
#define DISRUPTOR_LANGUAGE_PACK_H

/*
 * What a language pack may hold.
 *
 * The disc code writes a pack and the language module takes one. Both go by
 * the rules here, so a pack that was written is one that is taken: "DLP4", a
 * count, then per string its US address, the FNV-1a hash of the US string, a
 * length and the words. For a format the bytes 1..9 in the words stand for
 * its parts. A format may take one %s at most and no digit may follow a %d:
 * a string made from either could be split in two ways. A string that still
 * can under one of the pack's formats, because its text part ends like what
 * follows it, is drawn as it is. Where two formats each fit a string in one
 * way, the one earlier in the pack is used.
 * Then a count of places and per place the return address of a call, the x
 * the US code gives it and the x to give it, each in 16 bits. An x of -32768
 * stands for any x, and the other number is then the step to move it by.
 * Last a count of signs and four bytes per sign: a small letter the other disc
 * writes for a sign of its large menu font, how many characters the US code is
 * given in its place, one or two, and those characters, each one the US font
 * has a glyph for. They are swapped in for every string the large font's
 * routine draws, and for no other routine.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace disruptor::pack_rules {

constexpr uint32_t kMagic = 0x34504C44u;  /* "DLP4" */
constexpr uint32_t kMostStrings = 512u;
constexpr uint32_t kMostPlaces = 512u;
constexpr uint32_t kMostSigns = 8u;
constexpr uint8_t kLastDrawn = 0x90;  /* 'a' + 47: the large font's routine has a glyph and a width for no character above it */
constexpr size_t kLongestString = 96;
constexpr char kLastPart = 9;
constexpr int32_t kAnyX = -32768;
constexpr uint32_t kRamFirst = 0x80000000u;
constexpr uint32_t kRamEnd = 0x80200000u;

struct Format {
    std::vector<std::string> between;  /* one more than there are parts */
    std::string kinds;
    std::string words;
};

/* Whether a string of the longest length can be read at the address from main RAM. */
constexpr bool readable(uint32_t address) {
    return address >= kRamFirst && address < kRamEnd - static_cast<uint32_t>(kLongestString);
}

inline uint32_t hash_of(std::string_view text) {
    uint32_t hash = 2166136261u;
    for (const char byte : text) hash = (hash ^ static_cast<uint8_t>(byte)) * 16777619u;
    return hash;
}

inline bool parse_format(std::string_view text, Format &format) {
    format.between.assign(1, std::string());
    format.kinds.clear();
    for (size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '%') {
            format.between.back().push_back(text[index]);
            continue;
        }
        const char kind = index + 1 < text.size() ? text[index + 1] : '\0';
        if (kind != 's' && kind != 'd' && kind != 'c') return false;
        if (!format.kinds.empty() && format.between.back().empty()) return false;
        format.kinds.push_back(kind);
        format.between.emplace_back();
        ++index;
    }
    for (size_t part = 0; part < format.kinds.size(); ++part) {
        const std::string &next = format.between[part + 1];
        if (format.kinds[part] == 'd' && !next.empty() && next.front() >= '0' && next.front() <= '9') return false;
    }
    return format.kinds.size() <= static_cast<size_t>(kLastPart) && std::count(format.kinds.begin(), format.kinds.end(), 's') <= 1;
}

inline bool has_part(std::string_view words, size_t parts) {
    for (const char byte : words)
        if (byte >= 1 && byte <= kLastPart && static_cast<size_t>(byte) > parts) return true;
    return false;
}

/* Whether a pack may give `words` for the US string `text`. `format` comes back with parts where the text is one. */
inline bool entry_fits(std::string_view text, std::string_view words, Format &format) {
    format = Format{};
    if (text.empty() || text.size() > kLongestString || words.empty() || words.size() > kLongestString) return false;
    if (words.find('\0') != std::string_view::npos) return false;
    if (text.find('%') == std::string_view::npos) return !has_part(words, 0);
    return parse_format(text, format) && !has_part(words, format.kinds.size());
}

/* Whether a pack may have `drawn` given to the large font's routine wherever the other disc writes `written`. */
inline bool sign_fits(uint8_t written, std::string_view drawn) {
    const auto drawable = [](char letter) {
        const uint8_t byte = static_cast<uint8_t>(letter);
        return (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= kLastDrawn);
    };
    return written >= 'a' && written <= 'z' && !drawn.empty() && drawn.size() <= 2 && std::all_of(drawn.begin(), drawn.end(), drawable);
}

}  // namespace disruptor::pack_rules

#endif
