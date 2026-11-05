#include "game/input/menu_pointer.h"
#include "game/input/menu_rows.h"
#include "game/session/menu_mouse.h"

#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <thread>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {

namespace rows = disruptor::menu_rows;
namespace pointer = disruptor::menu_pointer;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr uint32_t kItemDraw = 0x8001A46Cu;
constexpr uint32_t kTick = 0x80039450u;
constexpr uint32_t kStrings = 0x80120000u;
constexpr uint32_t kText = 0x80100000u, kOffsets = 0x80110000u;

std::map<uint32_t, PSXModFunctionEntryCallback> g_hooks;
std::map<uint32_t, uint32_t> g_words;
std::map<uint32_t, uint8_t> g_bytes;
std::map<uint32_t, int> g_notes;
bool g_pointer = false;
double g_pointer_x = 0.0, g_pointer_y = 0.0;
int g_main_pointing = 0;
int g_main_item = -1;

/* A string of the guest's memory, a glyph 10 wide. */
uint32_t put(uint32_t slot, const char *text) {
    const uint32_t address = kStrings + slot * 64u;
    for (size_t index = 0; index <= std::strlen(text); ++index) g_bytes[address + static_cast<uint32_t>(index)] = static_cast<uint8_t>(text[index]);
    return address;
}

void draw(uint32_t returns, int32_t x, int32_t y, uint32_t string, uint32_t counter = 0u) {
    CPUState cpu{};
    cpu.gpr[31] = returns;
    cpu.gpr[4] = string;
    cpu.gpr[5] = static_cast<uint32_t>(x);
    cpu.gpr[6] = static_cast<uint32_t>(y);
    cpu.gpr[16] = counter;
    g_hooks[kItemDraw](&cpu, kItemDraw);
}

/* A pause line as the pause menu's function calls 0x800429DC: (list, string, x, y). */
void pause_line(uint32_t returns, int32_t x, int32_t y, uint32_t string) {
    CPUState cpu{};
    cpu.gpr[31] = returns;
    cpu.gpr[5] = string;
    cpu.gpr[6] = static_cast<uint32_t>(x);
    cpu.gpr[7] = static_cast<uint32_t>(y);
    g_hooks[rows::kPauseDraw](&cpu, rows::kPauseDraw);
}

void tick() {
    CPUState cpu{};
    g_hooks[kTick](&cpu, kTick);
}

/* GENERAL OPTIONS as the game draws it: a title right of 208, its value from 220, RETURN TO MAIN MENU at 204. */
void options_pass() {
    draw(0x8001F248u, -208, 80, put(0, "SOUND"));
    draw(0x8001F268u, 220, 80, put(1, "eee"));
    draw(0x8001F2E4u, -208, 110, put(2, "MUSIC"));
    draw(0x8001F308u, 220, 110, put(3, "ee"));
    draw(0x8001F328u, -208, 140, put(4, "SWAY"));
    draw(0x8001F368u, 220, 140, put(5, "LIGHT"));
    draw(0x8001F394u, 0, 204, put(6, "RETURN"));
}

/* The pointer at a point of the front end's frame, the window four times as big. */
void point_at(double x, double y) {
    g_pointer = true;
    g_pointer_x = x * 4.0;
    g_pointer_y = y * 4.0;
}

uint32_t cursor() { return g_words[rows::kCursor]; }

}  // namespace

int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    g_hooks[address] = callback;
    return 1;
}

uint32_t psx_mod_read_word(uint32_t address) { return g_words[address]; }
void psx_mod_write_word(uint32_t address, uint32_t value) { g_words[address] = value; }
uint8_t psx_mod_read_byte(uint32_t address) {
    if (address >= pointer::kGlyphWidths && address < pointer::kGlyphWidths + 64u) return 10u;
    if (address >= pointer::kSmallGlyphWidths && address < pointer::kSmallGlyphWidths + 128u) return 8u;
    const auto found = g_bytes.find(address);
    return found == g_bytes.end() ? uint8_t{0} : found->second;
}

int psx_netplay_active(void) { return 0; }

extern "C" {
int disruptor_mouse_menu_pointer(double *x, double *y, int *width, int *height) {
    if (!g_pointer) return 0;
    *x = g_pointer_x;
    *y = g_pointer_y;
    *width = 1280;
    *height = 960;
    return 1;
}
void psx_host_video_get_display_aspect(int *numerator, int *denominator) {
    *numerator = 4;
    *denominator = 3;
}
void disruptor_capsule_note_byte(uint32_t address, uint8_t) { ++g_notes[address]; }
int disruptor_main_menu_pointing(void) { return g_main_pointing; }
int disruptor_main_menu_click(void) { return g_main_item; }
int disruptor_language_words(const char *, char *, uint32_t) { return 0; }
}

namespace {

void test_registration() {
    expect(g_hooks.size() == 3 && g_hooks.count(kItemDraw) && g_hooks.count(rows::kPauseDraw) && g_hooks.count(kTick),
           "hooks on the item draw, the pause line draw and the tick");
    expect(disruptor_menu_pointing() == 0, "no screen has drawn: nothing points");
}

void test_hover(bool replaying) {
    g_words[rows::kScreen] = rows::kOptions;
    g_words[rows::kCursor] = 3u;
    point_at(240.0, 86.0);
    options_pass();
    options_pass();
    expect(disruptor_menu_pointing() == (replaying ? 0 : 1), replaying ? "a replay points nowhere" : "GENERAL OPTIONS points");
    options_pass();
    expect(cursor() == 3u && g_notes.empty(), "a pointer seen, not moved, leaves the cursor the keys put");
    point_at(240.0, 116.0);
    options_pass();
    expect(cursor() == (replaying ? 3u : 1u) && g_notes[rows::kCursor] == (replaying ? 0 : 1),
           replaying ? "a replay leaves the cursor to its recording" : "moved onto MUSIC VOLUME, the cursor goes there at its draw, noted");
    g_words[rows::kCursor] = 3u;
    options_pass();
    expect(cursor() == 3u, "a pointer resting there leaves the keys' move");
    point_at(240.0, 170.0);
    options_pass();
    expect(cursor() == 3u, "between rows the cursor stays");
}

void test_presses(bool replaying) {
    g_words[rows::kCursor] = 3u;
    point_at(240.0, 116.0);
    options_pass();
    const uint16_t step = disruptor_menu_buttons(1, 0);
    expect(step == (replaying ? 0u : rows::kRight), "a left press on a volume's bar steps it up");
    expect(disruptor_menu_buttons(1, 0) == step, "and holds Right while the button is down");
    point_at(240.0, 146.0);
    options_pass();
    options_pass();
    expect(cursor() == (replaying ? 3u : 1u), "the press puts the cursor on its row, the pointer moved while held moves it no more");
    expect(disruptor_menu_buttons(0, 0) == 0u, "the release lets it go");
    options_pass();
    expect(cursor() == (replaying ? 3u : 2u), "after it the move made meanwhile moves the cursor");
    expect(disruptor_menu_buttons(0, 1) == (replaying ? 0u : rows::kLeft), "a right press on a value steps it down");
    disruptor_menu_buttons(0, 0);
    point_at(180.0, 146.0);
    expect(disruptor_menu_buttons(1, 0) == 0u, "a press on a title holds nothing");
    disruptor_menu_buttons(0, 0);
    point_at(160.0, 210.0);
    options_pass();
    g_words[rows::kCursor] = 0u;
    options_pass();
    expect(cursor() == 0u, "the keys moved the cursor off the row the pointer rests on");
    expect(disruptor_menu_buttons(1, 0) == (replaying ? 0u : rows::kCross), "a press on RETURN TO MAIN MENU holds Cross");
    disruptor_menu_buttons(0, 0);
    options_pass();
    expect(cursor() == (replaying ? 0u : 3u), "and puts the cursor there at its next draw, before the game reads Cross");
    point_at(160.0, 30.0);
    expect(disruptor_menu_buttons(1, 0) == 0u, "a press off the rows holds nothing");
    disruptor_menu_buttons(0, 0);
}

void test_lost_press(bool replaying) {
    point_at(160.0, 210.0);
    options_pass();
    g_words[rows::kCursor] = 0u;
    disruptor_menu_buttons(1, 0);
    disruptor_menu_buttons(0, 0);
    for (int pass = 0; pass < 2; ++pass) {
        draw(0x8001F248u, -208, 80, put(0, "SOUND"));
        draw(0x8001F268u, 220, 80, put(1, "eee"));
    }
    options_pass();
    expect(cursor() == 0u, replaying ? "a replay keeps its cursor" : "a press whose row a whole pass did not draw is dropped");
}

void test_screens(bool replaying) {
    g_words[rows::kScreen] = rows::kDeath;
    g_words[rows::kMenuText] = kText;
    g_words[rows::kMenuOffsets] = kOffsets;
    g_words[kOffsets + 180u] = 0x40u;
    g_words[kOffsets + 184u] = 0x80u;
    g_words[rows::kCursor] = 0u;
    const auto death_pass = [] {
        draw(0x8001AD58u, 28, 144, put(0, "CONTINUE"));
        draw(0x8001ADE0u, 0, 164, put(1, "RESTART"));
        draw(0x8001AE08u, 0, 184, kText + 0x80u);
        draw(0x8001AE34u, 0, 204, put(2, "RETURN"));
    };
    for (uint32_t at = 0; at < 8; ++at) g_bytes[kText + 0x80u + at] = static_cast<uint8_t>("MOVIE\0\0\0"[at]);
    point_at(160.0, 150.0);
    death_pass();
    death_pass();
    point_at(160.0, 190.0);
    death_pass();
    expect(cursor() == (replaying ? 0u : 2u), "on YOU DIED the movie's call draws REVIEW MOVIE, the third row");
    g_words[rows::kScreen] = 0u;
    expect(disruptor_menu_pointing() == 0 && disruptor_menu_buttons(1, 0) == 0u, "another screen's word: the rows point no more");
    disruptor_menu_buttons(0, 0);

    g_words[rows::kScreen] = rows::kNewGame;
    g_words[rows::kCursor] = 6u;
    g_words[rows::kSlotSide] = 0u;
    const auto name_pass = [] {
        draw(0x8001CA50u, 0, 204, put(0, "RETURN"));
        draw(0x8001D12Cu, 0, 156, put(1, "ACCEPT"));
    };
    point_at(160.0, 160.0);
    name_pass();
    name_pass();
    point_at(160.0, 208.0);
    name_pass();
    expect(cursor() == (replaying ? 6u : 7u) && g_words[rows::kSlotSide] == 0u,
           "beside the name RETURN TO MAIN MENU is the cursor's seventh place");
    g_words[rows::kCursor] = 2u;
    const auto slot_pass = [] {
        draw(0x8001CA50u, 0, 204, put(0, "RETURN"));
        draw(0x8001CD64u, 16, 124, put(1, "NAME"), 1u);
    };
    point_at(30.0, 128.0);
    slot_pass();
    slot_pass();
    point_at(160.0, 208.0);
    slot_pass();
    expect(cursor() == 2u && g_words[rows::kSlotSide] == (replaying ? 0u : 1u), "beside the slots it is the slot side's");
    point_at(30.0, 128.0);
    slot_pass();
    expect(cursor() == (replaying ? 2u : 1u) && g_words[rows::kSlotSide] == 0u, "a slot takes the cursor and the slot side back");
    expect(g_notes[rows::kSlotSide] == (replaying ? 0 : 2), "both writes noted for a capsule");
}

/* A press waiting for its row's draw while lockstep starts, and a press on a row the game left since its pass. */
void test_closed_gates(bool replaying) {
    g_words[rows::kScreen] = rows::kOptions;
    point_at(240.0, 116.0);
    options_pass();
    options_pass();
    g_words[rows::kCursor] = 3u;
    disruptor_menu_buttons(1, 0);
    disruptor_menu_buttons(0, 0);
    g_ls_mode = 1;
    options_pass();
    g_ls_mode = 0;
    options_pass();
    expect(cursor() == 3u, "a press made before lockstep writes nothing once it runs, nor after");

    g_words[rows::kScreen] = rows::kLoadGame;
    g_words[rows::kLoadMessage] = 0u;
    g_words[rows::kCursor] = 0u;
    g_bytes[rows::kSlots + 1u] = 1u;
    const auto load_pass = [] {
        draw(0x8001D6F8u, 0, 204, put(0, "RETURN"));
        draw(0x8001D7C8u, 16, 124, put(1, "NAME"), 1u);
    };
    point_at(30.0, 128.0);
    load_pass();
    load_pass();
    g_words[rows::kLoadMessage] = 3u;
    expect(disruptor_menu_buttons(1, 0) == 0u, "a saved game's name the game has since covered with a message holds nothing");
    disruptor_menu_buttons(0, 0);
    g_words[rows::kLoadMessage] = 0u;
    expect(disruptor_menu_buttons(1, 0) == (replaying ? 0u : rows::kCross), "the name still drawn holds Cross");
    disruptor_menu_buttons(0, 0);
}

/* The pause menu in play: its lines in the small font, 8 a glyph here, the tick reading its buttons before they are drawn. */
void test_pause(bool replaying) {
    g_words[rows::kScreen] = 0u;
    g_bytes[rows::kPauseState] = rows::kPaused;
    g_words[rows::kPauseAsks] = 0u;
    g_words[rows::kPauseCursor] = 0u;
    g_notes.clear();
    const auto lines = [] {
        pause_line(0x80042DE0u, 0, 106, put(0, "RESUME"));
        pause_line(0x80042E1Cu, 0, 120, put(1, "ABORT"));
        pause_line(0x80042EB0u, 0, 134, put(2, "SOUND"));
        pause_line(0x80042F48u, 0, 148, put(3, "MUSIC"));
    };
    point_at(160.0, 124.0);
    lines();
    lines();
    tick();
    expect(disruptor_menu_pointing() == (replaying ? 0 : 1) && g_words[rows::kPauseCursor] == 0u,
           replaying ? "a replay points nowhere in the pause menu" : "the pause menu points, a pointer seen first moves nothing");
    point_at(160.0, 138.0);
    tick();
    expect(g_words[rows::kPauseCursor] == (replaying ? 0u : 2u) && g_notes[rows::kPauseCursor] == (replaying ? 0 : 1),
           "moved onto SOUND VOLUME the cursor goes there at the tick, before the game reads its buttons, noted");
    tick();
    lines();
    expect(g_words[rows::kPauseCursor] == (replaying ? 0u : 2u), "a second tick and the draw leave it");
    expect(disruptor_menu_buttons(1, 0) == (replaying ? 0u : rows::kRight) && disruptor_menu_buttons(0, 0) == 0u &&
               disruptor_menu_buttons(0, 1) == (replaying ? 0u : rows::kLeft),
           "a press on a volume steps it, the right button back");
    disruptor_menu_buttons(0, 0);
    g_words[rows::kPauseCursor] = 3u;
    point_at(160.0, 110.0);
    tick();
    g_words[rows::kPauseCursor] = 3u;
    tick();
    expect(disruptor_menu_buttons(1, 0) == (replaying ? 0u : rows::kCross), "a press on RESUME MISSION holds Cross");
    disruptor_menu_buttons(0, 0);
    tick();
    expect(g_words[rows::kPauseCursor] == (replaying ? 3u : 0u), "and the next tick puts the cursor on it first");

    g_words[rows::kPauseAsks] = 1u;
    g_words[rows::kPauseCursor] = 1u;
    const auto question = [&lines] {
        pause_line(0x80042C44u, 141, 106, put(4, "ABORT"));
        pause_line(0x80042C70u, 136, 126, put(5, "YES"));
        pause_line(0x80042CA0u, 169, 126, put(6, "NO"));
        lines();
    };
    question();
    question();
    point_at(160.0, 110.0);
    expect(disruptor_menu_buttons(1, 0) == 0u, "while ABORT asks, RESUME MISSION under it is no row");
    disruptor_menu_buttons(0, 0);
    point_at(146.0, 130.0);
    tick();
    expect(g_words[rows::kPauseCursor] == (replaying ? 1u : 0u), "moved onto YES the cursor goes there");
    point_at(178.0, 130.0);
    expect(disruptor_menu_buttons(1, 0) == (replaying ? 0u : rows::kCross), "a press on NO holds Cross");
    disruptor_menu_buttons(0, 0);
    tick();
    expect(g_words[rows::kPauseCursor] == 1u, "and puts the cursor on NO, the nearer of the two answers");

    g_bytes[rows::kPauseState] = 0u;
    expect(disruptor_menu_pointing() == 0 && disruptor_menu_buttons(1, 0) == 0u, "out of the pause the pointer is no menu's");
    disruptor_menu_buttons(0, 0);
    g_words[rows::kPauseCursor] = 2u;
    tick();
    expect(g_words[rows::kPauseCursor] == 2u, "and the tick writes nothing in play");
}

/* A row not drawn again for a while, as while a screen saves, is gone. */
void test_stale_rows(bool replaying) {
    g_words[rows::kScreen] = rows::kOptions;
    point_at(160.0, 210.0);
    options_pass();
    options_pass();
    expect(disruptor_menu_buttons(1, 0) == (replaying ? 0u : rows::kCross), "RETURN TO MAIN MENU drawn now holds Cross");
    disruptor_menu_buttons(0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    expect(disruptor_menu_buttons(1, 0) == 0u, "drawn a moment ago and no more, it holds nothing");
    disruptor_menu_buttons(0, 0);
}

void test_main_menu() {
    g_main_pointing = 1;
    g_main_item = 2;
    expect(disruptor_menu_pointing() == 1, "the main menu points");
    expect(disruptor_menu_buttons(1, 0) == rows::kCross, "a press on its item holds Cross");
    disruptor_menu_buttons(0, 0);
    expect(disruptor_menu_buttons(0, 1) == 0u, "a right press there holds nothing");
    disruptor_menu_buttons(0, 0);
    g_main_item = -1;
    expect(disruptor_menu_buttons(1, 0) == 0u, "nor a press beside its items");
    disruptor_menu_buttons(0, 0);
    g_main_pointing = 0;
}

}  // namespace

int main(int argc, char **argv) {
    const bool replaying = argc == 2 && std::strcmp(argv[1], "--replay") == 0;
    expect((std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr) == replaying, "ctest sets the replay variable for --replay only");
    test_registration();
    test_hover(replaying);
    test_presses(replaying);
    test_lost_press(replaying);
    test_screens(replaying);
    test_closed_gates(replaying);
    test_pause(replaying);
    test_stale_rows(replaying);
    test_main_menu();
    if (g_failures != 0) return 1;
    std::cout << "disruptor menu mouse hooks" << (replaying ? " in a replay" : "") << ": PASS\n";
    return 0;
}
