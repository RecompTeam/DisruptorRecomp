#include "cpu_state.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <string>

namespace {

constexpr std::uint32_t kTestDraw = 0x800427D0u, kTestSent = 0x8004D684u, kTestTick = 0x80039450u;
constexpr std::uint32_t kTestSentByDraw = 0x800429BCu;
constexpr std::uint32_t kSprite = 0x800A0000u;

std::map<std::uint32_t, std::uint8_t> g_ram;
std::map<std::uint32_t, PSXModFunctionEntryCallback> g_entries;
int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
std::uint8_t psx_mod_read_byte(std::uint32_t address) { return g_ram[address]; }
std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return g_ram[address] | g_ram[address + 1u] << 8 | g_ram[address + 2u] << 16 |
           static_cast<std::uint32_t>(g_ram[address + 3u]) << 24;
}
void psx_mod_write_byte(std::uint32_t address, std::uint8_t value) { g_ram[address] = value; }
void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    for (std::uint32_t at = 0; at < 4u; ++at) g_ram[address + at] = static_cast<std::uint8_t>(value >> (8u * at));
}
int psx_mod_register_function_entry_plugin(const char *, std::uint32_t address, PSXModFunctionEntryCallback callback) {
    g_entries[address] = callback;
    return 1;
}
const char *g_other_words = nullptr;
int disruptor_language_words(const char *, char *words, std::uint32_t size) {
    if (!g_other_words || std::strlen(g_other_words) >= size) return 0;
    std::strcpy(words, g_other_words);
    return 1;
}
}

#include "../../src/game/input/list_message.cpp"

namespace {

using disruptor::stock_frames::kMessage;
using disruptor::stock_frames::kMessageBytes;
using disruptor::stock_frames::kMessageTime;

struct Memory {
    int writes = 0;
    uint32_t byte(uint32_t address) const { return psx_mod_read_byte(address); }
    uint32_t word(uint32_t address) const { return psx_mod_read_word(address); }
    void write_byte(uint32_t address, uint8_t value) {
        psx_mod_write_byte(address, value);
        ++writes;
    }
    void write(uint32_t address, uint32_t value) {
        psx_mod_write_word(address, value);
        ++writes;
    }
};

void text(const char *value) {
    for (uint32_t at = 0; at < kMessageBytes; ++at)
        g_ram[kMessage + at] = at < std::strlen(value) ? static_cast<uint8_t>(value[at]) : 0u;
}

std::string text() {
    std::string value;
    for (uint32_t at = 0; at < kMessageBytes && g_ram[kMessage + at] != 0u; ++at) value += static_cast<char>(g_ram[kMessage + at]);
    return value;
}

/* What the countdown's tick leaves (0x80039C44..0x80039C70). */
void countdown(uint32_t ticks, const char *seconds) {
    psx_mod_write_word(kCountdown, ticks);
    text(seconds);
    psx_mod_write_word(kMessageTime, 666u);
    psx_mod_write_word(kMessageX, 0u);
    psx_mod_write_word(kMessageY, 50u);
}

/* What an open weapon list leaves in a drawn frame: its mode, the chosen weapon's name, the list's x and its own y. A
 * list a key shows leaves the mode as it was. */
void list(const char *name, bool forced = false) {
    if (!forced) g_ram[kMode] = 2u;
    text(name);
    psx_mod_write_word(kMessageTime, 1u);
    psx_mod_write_word(kMessageX, 32u);
    psx_mod_write_word(kMessageY, 117u);
}

bool shows_countdown(const char *seconds) {
    return text() == seconds && psx_mod_read_word(kMessageTime) == 666u && psx_mod_read_word(kMessageX) == 0u &&
           psx_mod_read_word(kMessageY) == 50u;
}

/* A pass of the HUD builder in play: no list is open. */
void played() {
    g_ram[kMode] = 0u;
    disruptor_list_message_builder_begins();
    disruptor_list_message_builder_ends(0u);
}

void reactor() {
    disruptor_list_message_state_loaded();
    g_ram.clear();
    g_ram[kLevel] = static_cast<uint8_t>(kReactor);
    psx_mod_write_word(kNextSprite, kSprite);
    played();
}

/* A pass of the HUD builder with the list open and no draw: its panel waits for the next frame's table. */
void opened(bool forced = false) {
    disruptor_list_message_builder_begins();
    list("AB", forced);
    disruptor_list_message_builder_ends(forced ? 2u : 0u);
    CPUState cpu{};
    g_entries[kTestTick](&cpu, kTestTick);
}

/* The message draw as the game makes it from the memory it is given: time one less, the play colour with the reactor's
 * flash, black in a list's mode. Nothing at all without time left. */
void game_draws() {
    const uint32_t time = psx_mod_read_word(kMessageTime);
    if (time == 0u) return;
    psx_mod_write_word(kMessageTime, time - 1u);
    const bool red = g_ram[kLevel] == kReactor && time - 1u == 665u && static_cast<int32_t>(psx_mod_read_word(kCountdown)) % 30 >= 25;
    const uint8_t colour[3] = {static_cast<uint8_t>(red ? 255u : 128u), static_cast<uint8_t>(red ? 0u : 128u),
                               static_cast<uint8_t>(red ? 0u : 128u)};
    for (uint32_t at = 0; at < 3u; ++at) g_ram[kSprite + 4u + at] = g_ram[kMode] == 2u || g_ram[kMode] == 3u ? 0u : colour[at];
}

/* The draw sends its own list with the sprite built, and the GPU takes it then. */
void sent_by_draw() {
    CPUState cpu{};
    cpu.gpr[31] = kTestSentByDraw;
    g_entries[kTestSent](&cpu, kTestSent);
}

/* A drawn frame through the module's hooks: the HUD builder with a list writing its name, then the message's draw. */
void frame(const char *name) {
    disruptor_list_message_builder_begins();
    if (name) list(name);
    disruptor_list_message_builder_ends(0u);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    sent_by_draw();
}

uint32_t colour() { return g_ram[kSprite + 4u] | g_ram[kSprite + 5u] << 8 | g_ram[kSprite + 6u] << 16; }

void keeps_the_countdown() {
    reactor();
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    list("SHOTGUN");
    disruptor_list_message_builder_ends(0u);
    expect(shows_countdown("4 SECONDS"), "an open list leaves the countdown's text, time and place as the tick wrote them");

    reactor();
    countdown(299u, "4 SECONDS");
    text("HEALTH BOOST");
    frame("SHOTGUN");
    expect(text() == "HEALTH BOOST", "and so whatever the tick left after the countdown's write");
}

void colours_the_line_as_in_play() {
    reactor();
    countdown(300u, "4 SECONDS");
    frame("SHOTGUN");
    expect(colour() == 0x808080u, "the line keeps the play colour in a list's mode");
    countdown(295u, "4 SECONDS");
    frame("SHOTGUN");
    expect(colour() == 0x0000FFu, "and the reactor's red flash");
    g_ram[kMode] = 3u;
    countdown(296u, "4 SECONDS");
    frame(nullptr);
    expect(colour() == 0x0000FFu, "in the psionics list's mode too");
    countdown(290u, "4 SECONDS");
    g_ram[kMode] = 0u;
    frame(nullptr);
    expect(colour() == 0x808080u, "a list closed is the game's own draw");
}

void leaves_the_game_alone() {
    g_ram.clear();
    g_ram[kLevel] = 3u;
    psx_mod_write_word(kNextSprite, kSprite);
    countdown(300u, "4 SECONDS");
    frame("SHOTGUN");
    expect(text() == "SHOTGUN" && psx_mod_read_word(kMessageX) == 32u && colour() == 0u,
           "on another level the list writes its name, drawn black, as in the game");
    for (const uint32_t over : {0u, 0xFFFFFFFFu}) {
        reactor();
        countdown(over, "0 SECONDS");
        frame("SHOTGUN");
        expect(text() == "SHOTGUN" && colour() == 0u, "and at the reactor once the countdown is over");
    }

    reactor();
    countdown(300u, "4 SECONDS");
    psx_mod_write_word(kMessageTime, 0u);
    disruptor_list_message_builder_begins();
    list("SHOTGUN");
    disruptor_list_message_builder_ends(0u);
    psx_mod_write_word(kMessageTime, 0u);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    g_ram[kSprite + 4u] = g_ram[kSprite + 5u] = g_ram[kSprite + 6u] = 0u;
    sent_by_draw();
    expect(colour() == 0u, "a draw with no time left built no sprite, and none is touched");
}

void other_sends_left_alone() {
    reactor();
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    list("SHOTGUN");
    disruptor_list_message_builder_ends(0u);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    cpu.gpr[31] = 0x80043E08u;
    g_entries[kTestSent](&cpu, kTestSent);
    expect(colour() == 0u, "a list sent from elsewhere is not the message's");
    sent_by_draw();
    expect(colour() == 0x808080u, "the message's own send is");
}

void comparator_runs_alone() {
    for (int *lane : {&g_ls_mode, &g_ls_replay_active}) {
        reactor();
        countdown(300u, "4 SECONDS");
        *lane = 1;
        frame("SHOTGUN");
        *lane = 0;
        expect(text() == "SHOTGUN" && colour() == 0u, "the comparator's runs are the game's own, nothing is kept across them");
    }
}

void saves_wait_for_the_line() {
    reactor();
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    expect(disruptor_list_message_save_deferred() == 1, "no state is written inside the builder");
    list("SHOTGUN");
    disruptor_list_message_builder_ends(0u);
    expect(disruptor_list_message_save_deferred() == 1, "nor before the message is drawn");
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    expect(disruptor_list_message_save_deferred() == 1, "nor before its colour is the play colour");
    sent_by_draw();
    expect(disruptor_list_message_save_deferred() == 0, "after the draw it may be");

    disruptor_list_message_builder_begins();
    disruptor_list_message_builder_ends(0u);
    g_entries[kTestTick](&cpu, kTestTick);
    expect(disruptor_list_message_save_deferred() == 0, "a tick ends what a frame without its draw had left");
    disruptor_list_message_builder_begins();
    disruptor_list_message_state_loaded();
    expect(disruptor_list_message_save_deferred() == 0, "and so does a loaded state");
    list("SHOTGUN");
    disruptor_list_message_builder_ends(0u);
    expect(text() == "SHOTGUN", "which leaves the list's name as it found it");
}

void writes_only_when_kept() {
    reactor();
    countdown(300u, "4 SECONDS");
    Memory memory;
    Kept kept;
    builder_ends(memory, kept, 0u);
    draw_ends(memory, kept);
    expect(memory.writes == 0, "an end without a beginning writes nothing");
    builder_begins(memory, kept);
    builder_ends(memory, kept, 0u);
    memory.writes = 0;
    builder_ends(memory, kept, 0u);
    expect(memory.writes == 0, "and nor does a second end");
}

constexpr uint32_t kTestLoad = 0x8004D348u, kTestLoadedByDraw = 0x80042858u;
constexpr uint32_t kPictureAt = 0x800C0000u, kFontAt = 0x800B0000u, kLineTag = 0x04FFFFFFu, kLineUv = 0x7FE0B500u;

/* The font of the test: A, B and C 6, 7 and 5 wide, each glyph's bytes its index, row and column. */
void font() {
    psx_mod_write_word(kFont, kFontAt);
    psx_mod_write_word(kPicture, kPictureAt);
    for (const uint8_t letter : {'A', 'B', 'C'}) {
        const uint32_t index = glyph(letter);
        g_ram[kGlyphWidths + index] = static_cast<uint8_t>(letter == 'A' ? 6u : letter == 'B' ? 7u : 5u);
        for (uint32_t at = 0; at < kGlyphBytes; ++at) g_ram[kFontAt + index * kGlyphBytes + at] = static_cast<uint8_t>(index + at);
    }
}

/* A drawn frame with the line's picture loaded after the printer made the countdown `printed` wide. */
void printed_frame(const char *name, uint32_t printed) {
    disruptor_list_message_builder_begins();
    if (name) list(name);
    disruptor_list_message_builder_ends(0u);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    psx_mod_write_word(kSprite, kLineTag);
    g_ram[kSprite + 7u] = 0x64u;
    psx_mod_write_word(kSprite + 12u, kLineUv);
    psx_mod_write_word(kSprite + 16u, 11u << 16 | printed);
    cpu.gpr[31] = kTestLoadedByDraw;
    cpu.gpr[16] = printed;
    g_entries[kTestLoad](&cpu, kTestLoad);
    sent_by_draw();
}

bool glyph_at(uint8_t letter, uint32_t column) {
    const uint32_t index = glyph(letter);
    for (uint32_t row = 0; row < kGlyphRows; ++row)
        for (uint32_t across = 0; across < kGlyphWide; ++across)
            if (g_ram[kPictureAt + row * kPictureWide + column + across] != static_cast<uint8_t>(index + row * kGlyphWide + across))
                return false;
    return true;
}

uint32_t name_sprite(uint32_t word) { return psx_mod_read_word(kPictureAt + kNameSprite + 4u * word); }

void names_follow_the_countdown() {
    reactor();
    font();
    countdown(300u, "4 SECONDS");
    opened();
    printed_frame("AB", 40u);
    expect(glyph_at('B', 54u) && glyph_at('A', 48u) == false && g_ram[kPictureAt + 48u] == glyph('A'),
           "the name is printed after the countdown and a gap, each letter over the one before as the printer does");
    expect(psx_mod_read_word(kSprite) == (0x04000000u | ((kPictureAt + kNameSprite) & 0x00FFFFFFu)) && name_sprite(0) == kLineTag,
           "the name's sprite is sent after the line's");
    expect(name_sprite(1) == 0x64000000u && name_sprite(2) == (117u << 16 | 32u) && name_sprite(3) == (kLineUv | 48u) &&
               name_sprite(4) == (11u << 16 | 13u),
           "in the list's black and place, with the name's part of the picture");
    expect(colour() == 0x808080u && psx_mod_read_word(kSprite + 16u) == (11u << 16 | 40u), "the line keeps its colour and width");
    expect(text() == "4 SECONDS", "and its text");

    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    printed_frame("A BC", 40u);
    expect(g_ram[kPictureAt + 58u] == glyph('B') && glyph_at('C', 65u) && name_sprite(4) == (11u << 16 | 22u),
           "a space moves the name on by 4");

    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    g_other_words = "CA";
    printed_frame("AB", 40u);
    g_other_words = nullptr;
    expect(glyph_at('A', 53u) && name_sprite(4) == (11u << 16 | 11u), "the name is printed in the player's language");

    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    list("AB");
    psx_mod_write_word(kMessageX, 0u);
    disruptor_list_message_builder_ends(0u);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    psx_mod_write_word(kSprite, kLineTag);
    cpu.gpr[31] = kTestLoadedByDraw;
    cpu.gpr[16] = 40u;
    g_entries[kTestLoad](&cpu, kTestLoad);
    sent_by_draw();
    expect(name_sprite(2) == (117u << 16 | (kCentre - 6u)), "a name at x 0 is centred as the draw centres a line");
}

void names_left_out() {
    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    printed_frame("ABC", kPictureWide - kNameGap - 18u);
    expect(psx_mod_read_word(kSprite) == kLineTag && colour() == 0x808080u, "a name that does not fit whole is left out");

    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    printed_frame("4 SECONDS", 40u);
    expect(psx_mod_read_word(kSprite) == kLineTag, "nor is the countdown printed twice when no list wrote its name");

    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    list("AB");
    disruptor_list_message_builder_ends(0u);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    psx_mod_write_word(kSprite, kLineTag);
    cpu.gpr[31] = 0x80042A64u;
    cpu.gpr[16] = 40u;
    g_entries[kTestLoad](&cpu, kTestLoad);
    sent_by_draw();
    expect(psx_mod_read_word(kSprite) == kLineTag && g_ram[kPictureAt + 48u] == 0u, "a picture another draw loads is not touched");

    disruptor_list_message_state_loaded();
    g_ram.clear();
    g_ram[kLevel] = 3u;
    psx_mod_write_word(kNextSprite, kSprite);
    font();
    opened();
    countdown(300u, "4 SECONDS");
    printed_frame("AB", 40u);
    expect(psx_mod_read_word(kSprite) == kLineTag && g_ram[kPictureAt + 48u] == 0u && text() == "AB",
           "on another level the game prints the name itself");

    reactor();
    font();
    opened();
    countdown(300u, "4 SECONDS");
    printed_frame("AB", 40u);
    g_ram[kPictureAt + 48u] = 0u;
    psx_mod_write_word(kSprite, kLineTag);
    CPUState late{};
    late.gpr[31] = kTestLoadedByDraw;
    late.gpr[16] = 40u;
    g_entries[kTestLoad](&late, kTestLoad);
    sent_by_draw();
    expect(psx_mod_read_word(kSprite) == kLineTag && g_ram[kPictureAt + 48u] == 0u, "a name is printed once a frame");
}

/* A key shows the list through the builder's register: the mode stays play's and the draw gives the line its colour. */
void names_of_a_list_a_key_shows() {
    reactor();
    font();
    opened(true);
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    list("AB", true);
    disruptor_list_message_builder_ends(2u);
    expect(shows_countdown("4 SECONDS"), "the countdown keeps its line while a key shows the list");
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    psx_mod_write_word(kSprite, kLineTag);
    g_ram[kSprite + 7u] = 0x64u;
    psx_mod_write_word(kSprite + 12u, kLineUv);
    psx_mod_write_word(kSprite + 16u, 11u << 16 | 40u);
    cpu.gpr[31] = kTestLoadedByDraw;
    cpu.gpr[16] = 40u;
    g_entries[kTestLoad](&cpu, kTestLoad);
    sent_by_draw();
    expect(glyph_at('B', 54u) && psx_mod_read_word(kSprite) == (0x04000000u | ((kPictureAt + kNameSprite) & 0x00FFFFFFu)),
           "its name is printed after the countdown and sent after the line");
    expect(name_sprite(1) == 0x64808080u && name_sprite(2) == (117u << 16 | 32u) && name_sprite(4) == (11u << 16 | 13u),
           "in the grey the game draws it in play, at the list's place");
    expect(colour() == 0x808080u, "and the line keeps the draw's own colour");

    reactor();
    font();
    opened(true);
    countdown(300u, "4 SECONDS");
    disruptor_list_message_builder_begins();
    list("AB", true);
    disruptor_list_message_builder_ends(0u);
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    psx_mod_write_word(kSprite, kLineTag);
    g_entries[kTestLoad](&cpu, kTestLoad);
    sent_by_draw();
    expect(psx_mod_read_word(kSprite) == kLineTag && g_ram[kPictureAt + 48u] == 0u,
           "a line written with no list shown is no list's name");
}

/* A pass that opens a list: the draw's sprite in the opening frame, then the next frame's with the panel drawn. */
uint32_t opening_tag(uint32_t level, uint32_t mode, uint32_t forced) {
    g_ram[kLevel] = static_cast<uint8_t>(level);
    disruptor_list_message_builder_begins();
    list("AB", forced != 0u);
    if (forced == 0u) g_ram[kMode] = static_cast<uint8_t>(mode);
    disruptor_list_message_builder_ends(forced);
    CPUState cpu{};
    g_entries[kTestDraw](&cpu, kTestDraw);
    game_draws();
    psx_mod_write_word(kSprite, kLineTag);
    sent_by_draw();
    g_entries[kTestTick](&cpu, kTestTick);
    return psx_mod_read_word(kSprite);
}

void names_wait_for_the_panel() {
    disruptor_list_message_state_loaded();
    g_ram.clear();
    psx_mod_write_word(kNextSprite, kSprite);
    played();
    expect(opening_tag(3u, 2u, 0u) == (kLineTag & 0x00FFFFFFu), "the name of a list that opens is not drawn before its panel");
    expect(opening_tag(3u, 2u, 0u) == kLineTag, "it is drawn with the panel in the next frame");
    expect(opening_tag(3u, 3u, 0u) == (kLineTag & 0x00FFFFFFu), "another list opens again");
    g_ram[kMode] = 0u;
    disruptor_list_message_builder_begins();
    disruptor_list_message_builder_ends(0u);
    expect(opening_tag(3u, 0u, 2u) == (kLineTag & 0x00FFFFFFu), "and so does a list a key shows after play");
    expect(opening_tag(3u, 0u, 2u) == kLineTag, "drawn the next frame");

    reactor();
    font();
    countdown(300u, "4 SECONDS");
    printed_frame("AB", 40u);
    expect(psx_mod_read_word(kSprite) == kLineTag && g_ram[kPictureAt + 48u] == 0u && colour() == 0x808080u,
           "at the reactor the countdown keeps its line in play colour, the name waits for the panel");
    expect(text() == "4 SECONDS", "and the countdown's text is kept");
    countdown(299u, "4 SECONDS");
    printed_frame("AB", 40u);
    expect(psx_mod_read_word(kSprite) != kLineTag && glyph_at('B', 54u), "then the name follows the countdown");

    disruptor_list_message_state_loaded();
    g_ram.clear();
    psx_mod_write_word(kNextSprite, kSprite);
    played();
    g_ls_mode = 1;
    expect(opening_tag(3u, 2u, 0u) == kLineTag, "the comparator's runs are the game's own");
    g_ls_mode = 0;

    disruptor_list_message_state_loaded();
    g_ram.clear();
    psx_mod_write_word(kNextSprite, kSprite);
    expect(opening_tag(3u, 2u, 0u) == kLineTag, "after a load the table loaded may hold the panel: the name is drawn");
    played();
    expect(opening_tag(3u, 3u, 0u) == (kLineTag & 0x00FFFFFFu), "a list opened after it waits again");

    played();
    g_ram[kMode] = 2u;
    disruptor_list_message_builder_begins();
    disruptor_list_message_builder_ends(0u);
    expect(disruptor_list_message_save_deferred() == 1, "no state is written between an opening pass and its draw");
    CPUState cpu{};
    g_entries[kTestTick](&cpu, kTestTick);
    expect(disruptor_list_message_save_deferred() == 0, "a tick ends it");
    g_ram[kMode] = 0u;
    played();
    disruptor_list_message_builder_begins();
    disruptor_list_message_builder_ends(3u);
    expect(disruptor_list_message_save_deferred() == 1, "nor for a list a key shows");
    g_entries[kTestTick](&cpu, kTestTick);
}

void hooks_registered() {
    expect(g_entries.count(kTestDraw) && g_entries.count(kTestSent) && g_entries.count(kTestTick) && g_entries.count(kTestLoad) &&
               !g_off,
           "the message draw, the load of its picture, the send of its list and the tick are hooked");
}

}  // namespace

int main() {
    hooks_registered();
    keeps_the_countdown();
    colours_the_line_as_in_play();
    leaves_the_game_alone();
    other_sends_left_alone();
    comparator_runs_alone();
    saves_wait_for_the_line();
    writes_only_when_kept();
    names_follow_the_countdown();
    names_left_out();
    names_of_a_list_a_key_shows();
    names_wait_for_the_panel();
    if (g_failures != 0) return 1;
    std::cout << "disruptor list message: PASS\n";
    return 0;
}
