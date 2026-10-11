#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {
std::unordered_map<std::uint32_t, std::uint8_t> g_ram;
std::vector<std::uint32_t> g_read;
std::vector<std::pair<std::uint32_t, PSXModFunctionEntryCallback>> g_hooks;
std::uint32_t g_allocated = 0;
std::uint32_t g_scratch_first = 0;
std::uint32_t g_scratch_end = 0;
int g_allocations = 0;
int g_writes = 0;
int g_stray_writes = 0;
int g_started = 0;
int g_netplay = 0;
std::vector<std::uint8_t> g_disc_pack;
constexpr std::uint32_t kModMemory = 0x9F000000u;
}  // namespace

int psx_mod_game_started(void) { return g_started; }
int psx_netplay_active(void) { return g_netplay; }
uint8_t psx_mod_read_byte(uint32_t address) {
    g_read.push_back(address);
    const auto found = g_ram.find(address);
    return found == g_ram.end() ? 0u : found->second;
}
void psx_mod_write_byte(uint32_t address, uint8_t value) {
    ++g_writes;
    if (address < g_scratch_first || address >= g_scratch_end) ++g_stray_writes;
    g_ram[address] = value;
}
uint32_t psx_mod_alloc_guest_memory(uint32_t size, uint32_t) {
    ++g_allocations;
    g_scratch_first = kModMemory + g_allocated;
    g_allocated += size;
    g_scratch_end = kModMemory + g_allocated;
    return g_scratch_first;
}
int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    g_hooks.emplace_back(address, callback);
    return 1;
}

extern "C" int disruptor_language_disc_pack(const uint8_t **data, uint32_t *size) {
    if (g_disc_pack.empty()) return 0;
    *data = g_disc_pack.data();
    *size = static_cast<uint32_t>(g_disc_pack.size());
    return 1;
}

#include "../../src/disruptor_language.cpp"
#include "../../src/disruptor_language_disc.cpp"

namespace {

constexpr std::uint32_t kResume = 0x80010398u;
constexpr std::uint32_t kPickedUp = 0x800102D0u;
constexpr std::uint32_t kOverwrite = 0x80010098u;
constexpr std::uint32_t kSeconds = 0x80010324u;
constexpr std::uint32_t kRifle = 0x8005728Cu;
constexpr std::uint32_t kAmmo = 0x8007123Cu;
constexpr std::uint32_t kRange = 0x80012600u;
constexpr std::uint32_t kChoice = 0x80012700u;
constexpr std::uint32_t kLevel = 0x80012800u;
constexpr std::uint32_t kFirst = 0x80012A00u;
constexpr std::uint32_t kBuffer = 0x80077694u;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void put(std::uint32_t address, const std::string &text) {
    for (std::size_t index = 0; index <= text.size(); ++index)
        g_ram[address + static_cast<std::uint32_t>(index)] = index < text.size() ? static_cast<std::uint8_t>(text[index]) : 0u;
}

std::string at(std::uint32_t address) {
    std::string text;
    for (; g_ram.count(address) && g_ram[address] != 0u; ++address) text.push_back(static_cast<char>(g_ram[address]));
    return text;
}

using Entries = std::vector<std::pair<std::uint32_t, std::string>>;

struct Place {
    std::uint32_t returns_to;
    int from;
    int to;
};

const std::vector<Place> kPlaces = {{0x8001CE14u, 28, 14}, {0x8001CE5Cu, 252, 270}, {0x8001D01Cu, 48, 0}, {0x8001E000u, -40, -60}, {0x8001D0C0u, -32768, -20}};

std::uint32_t fnv(const std::string &text) {
    std::uint32_t hash = 0x811C9DC5u;
    for (const char byte : text) {
        hash ^= static_cast<std::uint8_t>(byte);
        hash *= 0x01000193u;
    }
    return hash;
}

/* A pack for the strings now in RAM, or for the string `expected` where the pack was made for another one. */
std::vector<std::uint8_t> pack(const Entries &entries, std::uint32_t magic = 0x34504C44u, const std::string &expected = "",
                               const std::vector<Place> &places = kPlaces, const std::vector<std::array<std::uint8_t, 4>> &signs = {}) {
    std::vector<std::uint8_t> bytes;
    const auto word = [&bytes](std::uint32_t value, int count) {
        for (int index = 0; index < count; ++index) bytes.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    };
    word(magic, 4);
    word(static_cast<std::uint32_t>(entries.size()), 4);
    for (const auto &[address, words] : entries) {
        word(address, 4);
        word(fnv(expected.empty() ? at(address) : expected), 4);
        word(static_cast<std::uint32_t>(words.size()), 2);
        bytes.insert(bytes.end(), words.begin(), words.end());
    }
    word(static_cast<std::uint32_t>(places.size()), 4);
    for (const Place &place : places) {
        word(place.returns_to, 4);
        word(static_cast<std::uint16_t>(place.from), 2);
        word(static_cast<std::uint16_t>(place.to), 2);
    }
    word(static_cast<std::uint32_t>(signs.size()), 4);
    for (const auto &sign : signs) bytes.insert(bytes.end(), sign.begin(), sign.end());
    return bytes;
}

int load(const std::vector<std::uint8_t> &bytes) {
    return disruptor_language_load_pack(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
}

void forget() {
    g_words.clear();
    g_formats.clear();
    g_places.clear();
    g_signs.fill(std::string());
    g_scratch = 0;
    g_state = State::kUnread;
}

/* What a text routine is handed for a string at this address: the address it reads from and the words there. */
std::pair<std::uint32_t, std::string> drawn(std::uint32_t address, std::size_t routine = 0) {
    CPUState cpu{};
    for (std::uint32_t index = 0; index < 32; ++index) cpu.gpr[index] = 0x1000u + index;
    cpu.gpr[4] = address;
    const CPUState before = cpu;
    g_hooks[routine].second(&cpu, g_hooks[routine].first);
    const std::uint32_t handed = cpu.gpr[4];
    cpu.gpr[4] = address;
    expect(std::memcmp(&cpu, &before, sizeof cpu) == 0, "nothing of the CPU state but $a0 may change");
    return {handed, at(handed)};
}

/* The x the menu's string routine is left with when it is called from this place with this x. */
int placed(std::uint32_t returns_to, int x) {
    CPUState cpu{};
    for (std::uint32_t index = 0; index < 32; ++index) cpu.gpr[index] = 0x1000u + index;
    cpu.gpr[4] = kResume;
    cpu.gpr[5] = static_cast<std::uint32_t>(x);
    cpu.gpr[31] = returns_to;
    const CPUState before = cpu;
    g_hooks[2].second(&cpu, g_hooks[2].first);
    const int left = static_cast<int>(cpu.gpr[5]);
    cpu.gpr[5] = static_cast<std::uint32_t>(x);
    expect(std::memcmp(&cpu, &before, sizeof cpu) == 0, "nothing of the CPU state but $a1 may change");
    return left;
}

std::string drawn_from(const std::string &text) {
    put(kBuffer, text);
    return drawn(kBuffer).second;
}

const Entries kGood = {
    {kResume, "REPRENDRE"},
    {kPickedUp, "\x01 RECUPERE"},
    {kOverwrite, "REMPLACER PAR MISSION \x02 f  NON"},
    {kSeconds, "\x01 SECONDES"},
    {kRifle, "Fusil Laser"},
    {kAmmo, "\x01 MUNITIONS"},
    {kRange, "\x02/\x01"},
    {kChoice, "\x02 (\x01"},
    {kLevel, "NIVEAU 1\x01"},
    {kFirst, "\x01 UN"},
};

/* Fourteen instructions that end by giving $a1 a constant, then a call of `routine`. */
void add_menu_call(disruptor::Bytes &exe, int x, std::uint32_t routine) {
    for (int before = 14; before >= -1; --before) {
        const std::uint32_t word = before == 2 ? 0x34050000u | static_cast<std::uint32_t>(x) : before == 0 ? 0x0C000000u | (routine >> 2 & 0x03FFFFFFu) : before == -1 ? 0u : 0x01095021u;
        exe.resize(exe.size() + 4);
        disruptor::put_le32(exe.data() + exe.size() - 4, word);
    }
}

void test_a_pack_the_disc_code_writes_is_taken() {
    forget();
    g_disc_pack.clear();
    g_started = 1;
    disruptor::Bytes home(0x2000, 0), other(0x2000, 0), written;
    disruptor::Language language{};
    language.menu_draw = 0x8001A4E0u;
    language.more_signs = 4;
    language.signs = {{'k', "j"}, {'n', "SS"}};
    const auto both = [&](std::uint32_t address, const std::string &text, const std::string &words) {
        language.strings.push_back({address, address});
        std::copy(text.begin(), text.end(), home.begin() + static_cast<std::ptrdiff_t>(address - 0x80010000u + 0x800u));
        std::copy(words.begin(), words.end(), other.begin() + static_cast<std::ptrdiff_t>(address - 0x80010000u + 0x800u));
        put(address, text);
    };
    both(0x80011100u, "ABORT MISSION", "ABANDONNER LA MISSION");
    both(0x80011140u, "%s TAKEN", "%s PRIS");
    both(0x80011180u, "AREA %d OF %d", "ZONE %d SUR %d");
    both(0x800111C0u, "QUIT", "QUITTER LkECRAN");
    both(0x80011200u, "%s-%d", "%s|%d");
    both(0x80011240u, "GREAT", "GROnARTIG");
    add_menu_call(home, 28, 0x8001A46Cu);
    add_menu_call(other, 14, 0x8001A4E0u);
    std::string why;
    expect(disruptor::build_pack(home, other, language, written, why) && load(written) == 6 && disruptor_language_active() == 1,
           "the module must take the pack the disc code writes, whole");
    expect(drawn(0x80011100u).second == "ABANDONNER LA MISSION" && drawn_from("ABORT MISSION TAKEN") == "ABANDONNER LA MISSION PRIS" &&
               drawn_from("AREA 3 OF 12") == "ZONE 3 SUR 12" && drawn(0x800111C0u).second == "QUITTER LkECRAN",
           "and draw its words, a format's parts in their places");
    expect(drawn(0x800111C0u, 1).second == "QUITTER LjECRAN" && drawn(0x80011240u).second == "GROnARTIG" && drawn(0x80011240u, 1).second == "GROSSARTIG",
           "and give the large font's routine the characters the disc code names for its signs");
    expect(placed(0x80010000u + 0x1800u + 56 + 8, 28) == 14, "and move the call it names");
    expect(drawn_from("A-1") == "A|1" && drawn_from("A--1") == "A--1" && drawn_from("A-B-2") == "A-B|2",
           "a string that could have been made from a format in two ways must be drawn as it is");
}

void test_a_string_made_in_two_ways_is_left_alone() {
    put(0x80012C00u, "%s-%d");
    put(0x80012D00u, "%s2");
    const std::pair<std::uint32_t, std::string> dashed{0x80012C00u, "\x01|\x02"}, numbered{0x80012D00u, "\x01 DEUX"};
    for (const bool dashed_first : {true, false}) {
        forget();
        g_started = 1;
        expect(load(pack(dashed_first ? Entries{dashed, numbered} : Entries{numbered, dashed})) == 2, "two formats must load");
        expect(drawn_from("A--2") == "A--2", "a string one format could have made in two ways must be drawn as it is, whichever format is asked first");
        expect(drawn_from("A-12") == (dashed_first ? "A|12" : "A|1 DEUX") && drawn_from("A-7") == "A|7" && drawn_from("B2") == "B DEUX",
               "a string two formats each fit in one way takes the one earlier in the pack, its text part looked up in turn");
    }
}

void test_the_large_font_takes_its_signs() {
    const auto taken = [](const std::vector<std::array<std::uint8_t, 4>> &signs) {
        forget();
        g_started = 1;
        return load(pack(kGood, 0x34504C44u, "", kPlaces, signs)) > 0;
    };
    expect(taken({{'a', 1, 'j', 0}}) && taken({{'z', 2, '0', '9'}}) && taken({{'k', 2, 'A', 'Z'}}) && taken({{'k', 2, 'a', 0x90}}) &&
               taken({{'k', 1, 'j', 0}, {'l', 1, 'j', 0}, {'m', 1, 'j', 0}, {'n', 1, 'j', 0}, {'o', 1, 'j', 0}, {'p', 1, 'j', 0}, {'q', 1, 'j', 0}, {'r', 1, 'j', 0}}),
           "any small letter may be a sign, drawn with digits, capitals and the characters the large font has glyphs for, and a pack may hold eight");
    forget();
    g_started = 1;
    put(0x80012E00u, "MENk OHNE PACK");
    put(0x80012E40u, "NICHTS ZU TUN");
    put(0x80012E80u, std::string(49, 'n'));
    put(0x80012EC0u, "n");
    expect(load(pack({{kResume, "GROnE lBUNG mL kRGER"}, {kPickedUp, "\x01 kNDERN"}}, 0x34504C44u, "", kPlaces,
                     {{'k', 1, 'j', 0}, {'l', 1, 0x8F, 0}, {'m', 1, 0x90, 0}, {'n', 2, 'S', 'S'}})) == 2,
           "a pack with signs must be taken");
    expect(drawn(kResume).second == "GROnE lBUNG mL kRGER" && drawn(kResume, 1).second == "GROSSE \x8F" "BUNG \x90" "L jRGER",
           "the large font's routine is given the signs' characters, the other routine the letters as written");
    expect(drawn(0x80012E00u, 1).second == "MENj OHNE PACK" && drawn(0x80012E00u).first == 0x80012E00u,
           "a string the pack does not name takes the signs too, in the large font only");
    expect(drawn(0x80012E40u, 1).first == 0x80012E40u, "a string with no sign in it stays where it is");
    expect(drawn(0x80012EC0u, 1).second == "SS" && drawn(0x80012E80u, 1).first == 0x80012E80u,
           "a sign may be two characters, and a string they would make longer than the module writes stays as it is");
    put(kBuffer, "PICKED UP SHELLS kn");
    expect(drawn(kBuffer, 1).second == "SHELLS jSS jNDERN", "a format's text part takes the signs with the words around it");
    g_state = State::kUnread;
    expect(load(pack({{kResume, "REPRENDRE"}})) == 1 && drawn(0x80012E00u, 1).first == 0x80012E00u, "a pack without signs leaves none of another's");
}

void test_three_routines_are_hooked() {
    expect(g_hooks.size() == 3 && g_hooks[0].first == 0x80044A10u && g_hooks[1].first == 0x80044BDCu && g_hooks[2].first == 0x8001A46Cu,
           "the two routines that return a width and the menu's string routine must be hooked, and no other");
    expect(g_hooks[0].second == g_hooks[1].second && g_hooks[2].second != g_hooks[0].second, "one callback serves the two text routines, another the menu's");
}

void test_nothing_changes_without_a_pack() {
    forget();
    g_disc_pack.clear();
    g_started = 0;
    expect(load(pack(kGood)) == 0, "a pack must wait for the game to run");
    expect(drawn(kResume).first == kResume, "before the game runs a string stays");
    g_started = 1;
    expect(drawn(kResume).first == kResume && disruptor_language_active() == 0, "with no pack named a string stays");
    expect(g_allocations == 0, "no mod memory may be taken without a pack");
}

void test_bad_packs_are_refused() {
    forget();
    g_started = 1;
    std::vector<std::uint8_t> cut = pack(kGood);
    cut.resize(cut.size() - 3);
    put(0x80012100u, "HELLO");
    put(0x80012200u, "%s VS %s");
    put(0x80012300u, "%s%d");
    put(0x80012400u, "100%");
    expect(load(pack(kGood, 0x33504C44u)) == 0, "another magic must be refused");
    const std::vector<std::uint8_t> no_places = pack(kGood, 0x34504C44u, "", {});
    std::vector<std::uint8_t> one_missing = no_places, many_places = no_places, wrapped_places = no_places;
    expect(load(pack(kGood, 0x34504C44u, "", std::vector<Place>(513, {0x8001CE14u, 28, 14}))) == 0, "a pack with more places than one may hold must be refused");
    one_missing[one_missing.size() - 8] = 1;
    many_places[many_places.size() - 7] = 0x10;
    wrapped_places[wrapped_places.size() - 5] = 0x20;
    expect(disruptor_language_load_pack(no_places.data(), static_cast<std::uint32_t>(no_places.size() - 8)) == 0, "a pack that ends before its places must be refused");
    expect(load(one_missing) == 0 && load(many_places) == 0 && load(wrapped_places) == 0 && placed(0x8001CE14u, 28) == 28,
           "a pack with fewer places than it counts, or more than it may hold, must be refused");
    std::vector<std::uint8_t> long_signs = pack(kGood), many_signs = no_places, wrapped_signs = no_places;
    long_signs.push_back(0);
    many_signs[many_signs.size() - 3] = 0x10;
    wrapped_signs[wrapped_signs.size() - 1] = 0x40;
    expect(disruptor_language_load_pack(no_places.data(), static_cast<std::uint32_t>(no_places.size() - 4)) == 0, "a pack that ends before its signs must be refused");
    expect(load(long_signs) == 0 && load(many_signs) == 0 && load(wrapped_signs) == 0,
           "a pack whose signs do not end with the pack, or are more than it may hold, must be refused");
    const auto with_signs = [](const std::vector<std::array<std::uint8_t, 4>> &signs) { return load(pack(kGood, 0x34504C44u, "", kPlaces, signs)); };
    expect(with_signs({{'A', 1, 'j', 0}}) == 0 && with_signs({{'{', 1, 'j', 0}}) == 0 && with_signs({{'`', 1, 'j', 0}}) == 0,
           "a sign written with anything but a small letter must refuse the pack");
    expect(with_signs({{'k', 0, 'j', 0}}) == 0 && with_signs({{'k', 3, 'j', 'j'}}) == 0 && with_signs({{'k', 1, 'j', 'j'}}) == 0,
           "a sign drawn with no character, with three, or with one and a byte after it must refuse the pack");
    expect(with_signs({{'k', 1, ' ', 0}}) == 0 && with_signs({{'k', 2, 'S', 5}}) == 0 && with_signs({{'k', 2, 0, 'S'}}) == 0,
           "a sign drawn with a space, a part's number or a zero must refuse the pack");
    for (const std::uint8_t drawn : std::array<std::uint8_t, 7>{0x2F, 0x3A, 0x40, 0x5B, 0x60, 0x91, 0xFF})
        expect(with_signs({{'k', 1, drawn, 0}}) == 0 && with_signs({{'k', 2, 'S', drawn}}) == 0, "a sign drawn with a character the large font has no glyph for must refuse the pack");
    expect(with_signs({{'k', 1, 'j', 0}, {'k', 1, 'i', 0}}) == 0, "a letter given two signs must refuse the pack");
    expect(with_signs({{'k', 1, 'j', 0}, {'l', 1, 'j', 0}, {'m', 1, 'j', 0}, {'n', 1, 'j', 0}, {'o', 1, 'j', 0}, {'p', 1, 'j', 0}, {'q', 1, 'j', 0}, {'r', 1, 'j', 0}, {'s', 1, 'j', 0}}) == 0,
           "more signs than a pack may hold must refuse it");
    expect(load(pack({{0x80012100u, "REPRENDRE"}}, 0x34504C44u, "RESUME MISSION")) == 0,
           "another string at the address must refuse the pack");
    expect(load(pack({{0x80012200u, "\x01 CONTRE \x02"}})) == 0, "a format with two text parts must refuse the pack");
    expect(load(pack({{0x80012300u, "\x01\x02"}})) == 0, "a format with nothing between two parts must refuse the pack");
    put(0x80012500u, "%x HEX");
    put(0x80012900u, "%d1%d");
    expect(load(pack({{0x80012900u, "\x01/\x02"}})) == 0, "a format with a digit right after a number must refuse the pack");
    for (int *mode : {&g_netplay, &g_ls_mode, &g_ls_replay_active}) {
        *mode = 1;
        expect(load(pack(kGood)) == 0 && g_allocations == 0 && disruptor_language_active() == 0,
               "the loader must not act under netplay, lockstep or a replay");
        *mode = 0;
    }
    expect(load(pack({{0x80012400u, "100"}})) == 0, "a format that ends in a percent sign must refuse the pack");
    expect(load(pack({{0x80012500u, "HEX"}})) == 0, "a format with a part the module cannot take must refuse the pack");
    const std::vector<std::uint8_t> one = pack({{kResume, "REPRENDRE"}});
    expect(disruptor_language_load_pack(one.data(), 15u) == 0, "a pack cut inside a string's header must be refused");
    expect(disruptor_language_load_pack(one.data(), 22u) == 0, "a pack cut inside a string's words must be refused");
    expect(disruptor_language_load_pack(one.data(), 7u) == 0 && disruptor_language_load_pack(nullptr, 64u) == 0,
           "a pack shorter than its header, or none, must be refused");
    const std::uint8_t four[4] = {1, 2, 3, 4};
    PackReader reader(four, 4u);
    expect(reader.take(5u) == nullptr && reader.left() == 4u && reader.take(3u) == four && reader.left() == 1u && reader.take(2u) == nullptr &&
               reader.take(1u) == four + 3 && reader.take(1u) == nullptr && reader.take(0u) == four + 4,
           "the pack reader hands out bytes in order and none past the end");
    expect(load(cut) == 0, "a cut pack must be refused");
    expect(load(pack({})) == 0, "an empty pack must be refused");
    put(0x80012B00u, std::string(97, 'A'));
    expect(load(pack({{0x80012B00u, "B"}}, 0x34504C44u, std::string(96, 'A'))) == 0, "a string longer than the module reads must refuse the pack, not be taken cut");
    expect(load(pack(Entries(513, {kResume, "REPRENDRE"}))) == 0, "a pack with more strings than one may hold must be refused");
    expect(load(pack({{kResume, std::string("RE\0PRENDRE", 10)}})) == 0, "words with a zero byte in them must refuse the pack");
    expect(load(pack({{0x80012100u, ""}})) == 0, "a string with no words must refuse the pack");
    expect(load(pack({{0x80012000u, "RIEN"}})) == 0, "an address with no string must refuse the pack");
    expect(load(pack({{kPickedUp, "\x02 RECUPERE"}})) == 0, "a part the format lacks must refuse the pack");
    expect(load(pack({{kResume, "\x01"}})) == 0, "a part in words for a whole string must refuse the pack");
    expect(load(pack({{kResume, std::string(97, 'A')}})) == 0, "words longer than a string may be must refuse the pack");
    expect(load(pack({{0x1F801040u, "RIEN"}})) == 0, "an address outside RAM must refuse the pack");
    expect(g_allocations == 0 && g_writes == 0 && disruptor_language_active() == 0 && drawn(kResume).first == kResume,
           "a refused pack must leave nothing behind");
}

void test_the_language_disc_brings_the_pack() {
    forget();
    g_started = 1;
    const std::vector<std::uint8_t> bytes = pack(kGood);
    g_disc_pack = bytes;
    g_started = 0;
    expect(drawn(kResume).first == kResume && g_state == State::kUnread, "a pack must not be read before the game runs");
    g_started = 1;
    g_netplay = 1;
    expect(drawn(kResume).first == kResume && disruptor_language_active() == 0, "netplay must keep the game's own words");
    g_netplay = 0;
    const auto [address, words] = drawn(kResume);
    expect(address >= kModMemory && words == "REPRENDRE" && disruptor_language_active() == 1,
           "the first string drawn must load the language disc's pack");
    expect(g_allocations == 1 && g_allocated == 97u && load(bytes) == 0 && g_allocations == 1, "a pack is taken once, with one scratch string");
    g_disc_pack.clear();
}

void test_menu_strings_take_the_other_places() {
    expect(placed(0x8001CE14u, 28) == 14 && placed(0x8001CE5Cu, 252) == 270 && placed(0x8001D01Cu, 48) == 0 && placed(0x8001E000u, -40) == -60,
           "a call the pack names takes the other x, a negative one too");
    expect(placed(0x8001D0C0u, 62) == 42 && placed(0x8001D0C0u, 162) == 142 && placed(0x8001D0C0u, -5) == -25 && placed(0x8001D0C0u, -32768) == -32788 &&
               placed(0x8001D0C0u, INT32_MIN + 5) == INT32_MAX - 14,
           "a call whose x is a sum moves by the step, whatever x it has");
    expect(placed(0x8001CE14u, 29) == 29 && placed(0x8001CE14u, 14) == 14 && placed(0x8001CE18u, 28) == 28 && placed(0, 0) == 0 && placed(0x8001CE14u, -32768) == -32768,
           "another x at that call, or another call, is left alone");
    for (int *mode : {&g_netplay, &g_ls_mode, &g_ls_replay_active}) {
        *mode = 1;
        expect(placed(0x8001CE14u, 28) == 28, "netplay, lockstep and a replay keep the game's own places");
        *mode = 0;
    }
    g_started = 0;
    expect(placed(0x8001CE14u, 28) == 28, "nothing is placed before the game runs");
    g_started = 1;
}

void test_strings_are_found_by_their_text() {
    for (std::size_t routine = 0; routine < 2; ++routine)
        expect(drawn(kResume, routine).second == "REPRENDRE", "both routines must get the other words");
    g_ls_mode = 1;
    expect(drawn(kResume).first == kResume, "a lockstep run must keep the game's own words");
    g_ls_mode = 0;
    g_ls_replay_active = 1;
    expect(drawn(kResume).first == kResume, "a replay must keep the game's own words");
    g_ls_replay_active = 0;
    put(kBuffer, "Phase Rifle");
    expect(drawn(kBuffer).second == "Fusil Laser" && drawn(kRifle).second == "Fusil Laser", "a copy of a string is the same string");
    expect(at(kResume) == "RESUME MISSION" && at(kBuffer) == "Phase Rifle", "the game's memory must stay as it was");
    expect(drawn_from("HELLO") == "HELLO" && drawn(kBuffer).first == kBuffer, "a string the pack does not know stays");
    expect(drawn_from("RESUME MISSION ") == "RESUME MISSION ", "a longer string is another string");
    expect(drawn_from(std::string(97, 'A')) == std::string(97, 'A'), "a string too long to look up stays");
}

void test_formats_take_their_parts() {
    expect(drawn_from("PICKED UP Phase Rifle") == "Fusil Laser RECUPERE", "a part must be translated and moved");
    expect(drawn_from("PICKED UP KEYCARD") == "KEYCARD RECUPERE", "an unknown part is kept");
    expect(drawn_from("PICKED UP Phase Rifle AMMO") == "Fusil Laser MUNITIONS RECUPERE", "a part made by a format is taken apart too");
    expect(drawn_from("OVERWRITE M3 WITH M12 f  NO") == "REMPLACER PAR MISSION 12 f  NON", "the words choose which parts they show");
    expect(drawn_from("OVERWRITE Mx WITH M12 f  NO") == "OVERWRITE Mx WITH M12 f  NO", "a number part must be a number");
    expect(drawn_from("7 SECONDS") == "7 SECONDES" && drawn_from("-3 SECONDS") == "-3 SECONDES", "a number may be negative");
    expect(drawn_from(" SECONDS") == " SECONDS" && drawn_from("ab SECONDS") == "ab SECONDS", "a number part needs digits");
    expect(drawn_from("12-345") == "345/12" && drawn_from("1--2") == "-2/1", "two numbers with a sign between them come apart as they went in");
    expect(drawn_from("-1-2") == "2/-1" && drawn_from("-1--2") == "-2/-1", "a number keeps its sign");
    expect(drawn_from("1-") == "1-" && drawn_from("-") == "-" && drawn_from("1-2-3") == "1-2-3", "a number part is one whole number");
    expect(drawn_from("7 SECONDS!") == "7 SECONDS!" && drawn_from("OVERWRITE M3 WITS M12 f  NO") == "OVERWRITE M3 WITS M12 f  NO",
           "the fixed text after a number must be there, and nothing after the last");
    expect(drawn_from("A B1") == "A B UN" && drawn_from("A1B1") == "A1B UN" && drawn_from("1") == " UN",
           "a text part runs to the last place that lets the rest fit");
    expect(drawn_from("a) HELLO") == "HELLO (a" && drawn_from(")) HELLO") == "HELLO ()", "a single character is one part");
    expect(drawn_from("ab) HELLO") == "ab) HELLO", "a character part is one character");
    expect(drawn_from("LEVEL 12") == "NIVEAU 12" && drawn_from("LEVEL 1X") == "LEVEL 1X", "a digit before a number is fixed text");
    expect(drawn_from("ZONE1") == "ZONE UN", "a digit may follow a text part");
    expect(drawn_from("PICKED UP") == "PICKED UP" && drawn_from("XICKED UP Phase Rifle") == "XICKED UP Phase Rifle",
           "a format needs all of its fixed text in front");
    expect(drawn_from("Phase Rifle AMMX") == "Phase Rifle AMMX", "a format needs its fixed text at the end");
    expect(drawn_from("PICKED UP " + std::string(86, 'A')) == std::string(86, 'A') + " RECUPERE", "a string of 96 characters is still looked up");
    expect(drawn_from("PICKED UP PICKED UP PICKED UP Phase Rifle") == "Phase Rifle RECUPERE RECUPERE RECUPERE",
           "parts are taken apart two deep and no deeper");
    const std::string longest = std::string(91, 'A') + " AMMO";
    expect(drawn_from(longest) == longest && drawn(kBuffer).first == kBuffer, "words too long to hold stay as they were");
    expect(drawn_from(std::string(86, 'A') + " AMMO") == std::string(86, 'A') + " MUNITIONS", "96 characters of words still fit");
}

void test_only_ram_is_read() {
    for (const std::uint32_t address : {0x00000000u, 0x1F801040u, 0x7FFFFFFFu, 0x801FFFF0u, 0x9F000000u, 0xBFC00000u}) {
        g_read.clear();
        expect(drawn(address).first == address && g_read.empty(), "a pointer outside RAM must not be read");
    }
}

}  // namespace

int main() {
    put(kResume, "RESUME MISSION");
    put(kPickedUp, "PICKED UP %s");
    put(kOverwrite, "OVERWRITE M%d WITH M%d f  NO");
    put(kSeconds, "%d SECONDS");
    put(kRifle, "Phase Rifle");
    put(kAmmo, "%s AMMO");
    put(kRange, "%d-%d");
    put(kChoice, "%c) %s");
    put(kLevel, "LEVEL 1%d");
    put(kFirst, "%s1");
    test_three_routines_are_hooked();
    test_nothing_changes_without_a_pack();
    test_bad_packs_are_refused();
    test_the_language_disc_brings_the_pack();
    test_menu_strings_take_the_other_places();
    test_strings_are_found_by_their_text();
    test_formats_take_their_parts();
    test_a_pack_the_disc_code_writes_is_taken();
    test_a_string_made_in_two_ways_is_left_alone();
    test_the_large_font_takes_its_signs();
    test_only_ram_is_read();
    expect(g_writes > 0 && g_stray_writes == 0, "every write must land in the scratch string the module was given");
    if (g_failures != 0) return 1;
    std::cout << "disruptor language: PASS\n";
    return 0;
}
