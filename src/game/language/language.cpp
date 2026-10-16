/*
 * Another language's words for Disruptor (SLUS-00224).
 *
 * Every string of the executable seen on screen is drawn by one of two
 * routines that take the string in $a0 and return the width drawn:
 * func_80044A10 (pause menu, messages) and func_80044BDC (menus). The third
 * text routine, func_80044AE8, returns how many characters it read, and the
 * text seen through it comes from the disc, so it is left alone. A language pack names strings of the US executable by
 * address and carries the words to draw in their place. At a routine's entry
 * the string in $a0 is looked up by its text and $a0 is pointed at the other
 * words, written to a scratch string in mod memory that the routine reads
 * before it returns. A string that sprintf made is matched against the format
 * it came from, and the parts it took are looked up in turn. The game's own
 * memory is never written, so its buffers keep the sizes the US text needs.
 *
 * Menus draw through func_8001A46C(string, x, y, colour), the pause menu's
 * lines through func_800429DC(list, string, x, y). Where the other
 * executable calls one with another x, for words of another length, the pack
 * names the call by its return address and the x register takes that x, or
 * moves by a step where the x is a sum the two executables end with another
 * constant.
 *
 * The other disc's large menu font has signs the US one lacks. The disc code
 * puts their glyphs where the US routine can reach them, and the pack says
 * which characters to give that routine for the letters the other disc
 * writes. That swap is made for every string func_80044BDC draws.
 *
 * The pack comes from the language disc (language_disc.cpp) and
 * holds what language_pack.h says, by the rules written there.
 */

#include "game/language/language.h"

#include "cpu_state.h"
#include "game/language/language_pack.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::array<uint32_t, 2> kTextRoutines = {0x80044A10u, 0x80044BDCu};
constexpr uint32_t kLargeFont = kTextRoutines[1];
constexpr uint32_t kMenuDraw = 0x8001A46Cu;
constexpr uint32_t kStripDraw = 0x800429DCu;
constexpr int kStringRegister = 4;  /* $a0 */
constexpr int kXRegister = 5;  /* $a1 */
constexpr int kStripXRegister = 6;  /* $a2 */
constexpr uint32_t kCallToReturn = 8;  /* a jal and its delay slot */
constexpr int kReturnRegister = 31;  /* $ra */
constexpr int kDeepestPart = 2;

namespace rules = disruptor::pack_rules;
using rules::Format;
using rules::kAnyX;
using rules::kLastPart;
using rules::kLongestString;

enum class State { kUnread, kOff, kOn };

std::unordered_map<std::string, std::string> g_words;
std::vector<Format> g_formats;
std::unordered_map<uint32_t, std::pair<int32_t, int32_t>> g_places;
std::array<std::string, 256> g_signs;  /* by the letter written: what the large font's routine is given, empty for itself */
uint32_t g_scratch = 0;
State g_state = State::kUnread;

bool guest_string(uint32_t address, std::string &text) {
    text.clear();
    if (!rules::readable(address)) return false;
    for (size_t index = 0; index <= kLongestString; ++index) {
        const uint8_t byte = psx_mod_read_byte(address + static_cast<uint32_t>(index));
        if (byte == 0u) return true;
        text.push_back(static_cast<char>(byte));
    }
    return false;
}

size_t number_end(std::string_view text, size_t at) {
    const size_t digits = at < text.size() && text[at] == '-' ? at + 1 : at;
    size_t end = digits;
    while (end < text.size() && text[end] >= '0' && text[end] <= '9') ++end;
    return end > digits ? end : std::string_view::npos;
}

bool take_rest(const Format &format, std::string_view text, size_t index, size_t at, std::vector<std::string_view> &parts, bool &many) {
    if (index == format.kinds.size()) return at == text.size();
    const std::string &next = format.between[index + 1];
    const auto ends_at = [&](size_t end) {
        if (end == std::string_view::npos || text.substr(end, next.size()) != next) return false;
        parts.push_back(text.substr(at, end - at));
        if (take_rest(format, text, index + 1, end + next.size(), parts, many)) return true;
        parts.pop_back();
        return false;
    };
    if (format.kinds[index] == 'd') return ends_at(number_end(text, at));
    if (format.kinds[index] == 'c') return ends_at(at < text.size() ? at + 1 : std::string_view::npos);
    /* A text part that may end in two places: the string could have been made in two ways, and `many` says so. */
    const size_t before = parts.size();
    std::vector<std::string_view> taken;
    for (size_t end = at; end <= text.size(); ++end) {
        if (!ends_at(end)) continue;
        many = !taken.empty();
        taken = parts;
        parts.resize(before);
        if (many) return false;
    }
    if (taken.empty()) return false;
    parts = std::move(taken);
    return true;
}

bool take_parts(const Format &format, std::string_view text, std::vector<std::string_view> &parts, bool &many) {
    parts.clear();
    many = false;
    const std::string &head = format.between.front();
    return text.substr(0, head.size()) == head && take_rest(format, text, 0, head.size(), parts, many);
}

bool other_words(std::string_view text, int depth, std::string &words) {
    if (const auto whole = g_words.find(std::string(text)); whole != g_words.end()) {
        words = whole->second;
        return true;
    }
    /* Every format is asked before one is used: a string one of them could have made in two ways is left as it is. */
    std::vector<std::string_view> parts;
    const Format *fits = nullptr;
    bool many = false;
    for (const Format &format : g_formats) {
        if (take_parts(format, text, parts, many)) fits = fits ? fits : &format;
        else if (many) return false;
    }
    if (!fits || !take_parts(*fits, text, parts, many)) return false;
    words.clear();
    for (const char byte : fits->words) {
        if (byte < 1 || byte > kLastPart) {
            words.push_back(byte);
            continue;
        }
        const std::string_view part = parts[static_cast<size_t>(byte - 1)];
        std::string inner;
        const bool text_part = fits->kinds[static_cast<size_t>(byte - 1)] == 's';
        if (text_part && depth < kDeepestPart && other_words(part, depth + 1, inner)) words += inner;
        else words.append(part);
    }
    return true;
}

void write_guest(uint32_t address, std::string_view text) {
    for (size_t index = 0; index < text.size(); ++index)
        psx_mod_write_byte(address + static_cast<uint32_t>(index), static_cast<uint8_t>(text[index]));
    psx_mod_write_byte(address + static_cast<uint32_t>(text.size()), 0u);
}

uint32_t stand_in(uint32_t address, bool large_font) {
    std::string text;
    if (!guest_string(address, text)) return 0u;
    std::string words;
    bool changed = other_words(text, 0, words);
    if (!changed) words = text;
    if (large_font) {
        std::string given;
        for (const char letter : words) {
            const std::string &sign = g_signs[static_cast<uint8_t>(letter)];
            changed = changed || !sign.empty();
            given += sign.empty() ? std::string(1, letter) : sign;
        }
        words = std::move(given);
    }
    if (!changed || words.empty() || words.size() > kLongestString) return 0u;
    write_guest(g_scratch, words);
    return g_scratch;
}

uint32_t word_at(const uint8_t *data) {
    return static_cast<uint32_t>(data[0]) | static_cast<uint32_t>(data[1]) << 8 | static_cast<uint32_t>(data[2]) << 16 |
           static_cast<uint32_t>(data[3]) << 24;
}

/* Hands out a pack's bytes in order and none past its end. */
class PackReader {
public:
    PackReader(const uint8_t *data, uint32_t size) : data_(data), left_(size) {}
    const uint8_t *take(uint32_t count) {
        if (count > left_) return nullptr;
        const uint8_t *taken = data_;
        data_ += count;
        left_ -= count;
        return taken;
    }
    uint32_t left() const { return left_; }

private:
    const uint8_t *data_;
    uint32_t left_;
};

int take_pack(const uint8_t *data, uint32_t size) {
    if (!data) return 0;
    PackReader pack(data, size);
    const uint8_t *head = pack.take(8u);
    if (!head || word_at(head) != rules::kMagic) return 0;
    const uint32_t count = word_at(head + 4);
    if (count == 0u || count > rules::kMostStrings) return 0;
    std::unordered_map<std::string, std::string> whole;
    std::vector<Format> formats;
    for (uint32_t index = 0; index < count; ++index) {
        const uint8_t *entry = pack.take(10u);
        if (!entry) return 0;
        const uint32_t address = word_at(entry);
        const uint32_t hash = word_at(entry + 4);
        const uint32_t length = static_cast<uint32_t>(entry[8]) | static_cast<uint32_t>(entry[9]) << 8;
        const uint8_t *letters = pack.take(length);
        if (!letters) return 0;
        const std::string words(reinterpret_cast<const char *>(letters), length);
        std::string text;
        Format format;
        if (!guest_string(address, text) || rules::hash_of(text) != hash || !rules::entry_fits(text, words, format)) return 0;
        if (format.kinds.empty()) {
            whole.emplace(text, words);
            continue;
        }
        format.words = words;
        formats.push_back(std::move(format));
    }
    const uint8_t *tail = pack.take(4u);
    if (!tail) return 0;
    const uint32_t places = word_at(tail);
    if (places > rules::kMostPlaces) return 0;
    std::unordered_map<uint32_t, std::pair<int32_t, int32_t>> placed;
    for (uint32_t index = 0; index < places; ++index) {
        const uint8_t *place = pack.take(8u);
        if (!place) return 0;
        const uint32_t both = word_at(place + 4);
        placed[word_at(place)] = {static_cast<int16_t>(both & 0xFFFFu), static_cast<int16_t>(both >> 16)};
    }
    const uint8_t *last = pack.take(4u);
    if (!last) return 0;
    const uint32_t signs = word_at(last);
    if (signs > rules::kMostSigns || pack.left() != signs * 4u) return 0;
    std::array<std::string, 256> given;
    for (uint32_t index = 0; index < signs; ++index) {
        const uint8_t *sign = pack.take(4u);
        const std::string drawn(reinterpret_cast<const char *>(sign + 2), sign[1] == 2u ? 2u : 1u);
        const bool sized = sign[1] == 2u || (sign[1] == 1u && sign[3] == 0u);
        if (!sized || !rules::sign_fits(sign[0], drawn) || !given[sign[0]].empty()) return 0;
        given[sign[0]] = drawn;
    }
    g_scratch = psx_mod_alloc_guest_memory(static_cast<uint32_t>(kLongestString) + 1u, 4u);
    if (g_scratch == 0u) return 0;
    g_words = std::move(whole);
    g_formats = std::move(formats);
    g_places = std::move(placed);
    g_signs = std::move(given);
    return static_cast<int>(g_words.size() + g_formats.size());
}

bool may_run() {
    return psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0;
}

void read_pack() {
    g_state = State::kOff;
    const uint8_t *data = nullptr;
    uint32_t size = 0;
    if (disruptor_language_disc_pack(&data, &size) && disruptor_language_load_pack(data, size) == 0)
        std::fprintf(stderr, "disruptor: the strings of the language disc were refused\n");
}

void text_entry(CPUState *cpu, uint32_t routine) {
    if (!cpu || !may_run()) return;
    if (g_state == State::kUnread) read_pack();
    if (g_state != State::kOn) return;
    if (const uint32_t other = stand_in(cpu->gpr[kStringRegister], routine == kLargeFont)) cpu->gpr[kStringRegister] = other;
}

/* Whether the call that returns to `returns_to` is a jal of `routine`: a place moves the x of the routine it was found for. */
bool called_from(uint32_t returns_to, uint32_t routine) {
    const uint32_t at = returns_to - kCallToReturn;
    if (!rules::readable(at)) return false;
    uint32_t word = 0;
    for (uint32_t byte = 0; byte < 4; ++byte) word |= static_cast<uint32_t>(psx_mod_read_byte(at + byte)) << (8 * byte);
    return word == (0x0C000000u | (routine >> 2 & 0x03FFFFFFu));
}

void place_entry(CPUState *cpu, uint32_t routine) {
    if (!cpu || !may_run()) return;
    if (g_state == State::kUnread) read_pack();
    if (g_state != State::kOn) return;
    const auto place = g_places.find(cpu->gpr[kReturnRegister]);
    if (place == g_places.end() || !called_from(place->first, routine)) return;
    const int x = routine == kStripDraw ? kStripXRegister : kXRegister;
    const auto [home_x, other_x] = place->second;
    if (home_x == kAnyX) cpu->gpr[x] += static_cast<uint32_t>(other_x);
    else if (static_cast<int32_t>(cpu->gpr[x]) == home_x) cpu->gpr[x] = static_cast<uint32_t>(other_x);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_language) {
    for (const uint32_t routine : kTextRoutines) {
        if (!psx_mod_register_function_entry_plugin("disruptor.language.text", routine, text_entry))
            std::fprintf(stderr, "disruptor: failed to register the text hook at 0x%08X\n", routine);
    }
    for (const uint32_t routine : {kMenuDraw, kStripDraw}) {
        if (!psx_mod_register_function_entry_plugin("disruptor.language.layout", routine, place_entry))
            std::fprintf(stderr, "disruptor: failed to register the layout hook at 0x%08X\n", routine);
    }
}

}  // namespace

extern "C" int disruptor_language_load_pack(const uint8_t *data, uint32_t size) {
    if (g_state == State::kOn || !may_run()) return 0;
    const int taken = take_pack(data, size);
    g_state = taken > 0 ? State::kOn : State::kOff;
    return taken;
}

extern "C" int disruptor_language_active(void) { return g_state == State::kOn ? 1 : 0; }
