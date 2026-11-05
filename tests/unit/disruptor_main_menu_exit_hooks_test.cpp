#include "game/language/language.h"
#include "game/input/menu_pointer.h"
#include "game/session/main_menu_exit.h"

#include "cpu_state.h"
#include "func_override.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "psx_sdl.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>

extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
}

namespace {

using namespace disruptor::main_menu_exit;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr uint32_t kCross = 0x0040u;
constexpr uint32_t kGuest = 0x9F000000u;
constexpr uint32_t kOtherString = 0x80139FB9u;

struct Hook {
    uint32_t address;
    PSXModFunctionEntryCallback callback;
};
Hook g_hooks[4];
int g_hook_count = 0;
PSXModActivationCallback g_activation = nullptr;
uint32_t g_asked = 0u;
uint8_t g_guest[64];
uint32_t g_allocated = 0u;
int g_allocations = 0;
uint32_t g_cursor = 0u;
uint32_t g_screen = 0u;
int g_finished = 0;
uint32_t g_layout = 0u;
uint8_t g_changed = 0u;
int g_applied = 0;
uint32_t g_applied_with = 0xFFu;
int g_netplay = 0;
int g_pushed = 0;
uint32_t g_pushed_type = 0u;
const char *g_language = "";

PSXModFunctionEntryCallback hook_at(uint32_t address) {
    for (int index = 0; index < g_hook_count; ++index)
        if (g_hooks[index].address == address) return g_hooks[index].callback;
    return nullptr;
}

std::string guest_string(uint32_t address) {
    std::string text;
    for (uint32_t at = address - kGuest; at < sizeof(g_guest) && g_guest[at] != 0u; ++at)
        text += static_cast<char>(g_guest[at]);
    return text;
}

uint32_t chosen_with(uint32_t cursor, uint32_t pressed) {
    CPUState cpu{};
    cpu.gpr[kButtons] = pressed;
    g_cursor = cursor;
    hook_at(kHandler)(&cpu, kHandler);
    return cpu.gpr[kButtons];
}

}  // namespace

int psx_mod_register_function_entry_plugin(const char *, uint32_t address, PSXModFunctionEntryCallback callback) {
    if (g_hook_count == 4) return 0;
    g_hooks[g_hook_count++] = {address, callback};
    return 1;
}

int psx_mod_register_activation_plugin(const char *, PSXModActivationCallback callback) {
    g_activation = callback;
    return 1;
}

uint32_t psx_mod_alloc_guest_memory(uint32_t size, uint32_t) {
    ++g_allocations;
    g_asked = size;
    if (size > sizeof(g_guest)) return 0u;
    std::memset(g_guest, 0xAA, sizeof(g_guest));
    g_allocated = kGuest;
    return kGuest;
}

void psx_mod_write_byte(uint32_t address, uint8_t value) {
    if (address >= kGuest && address - kGuest < sizeof(g_guest)) g_guest[address - kGuest] = value;
    if (address == kLayoutChanged) g_changed = value;
}

uint32_t psx_mod_read_word(uint32_t address) {
    if (address == kCursor) return g_cursor;
    if (address == kLayout) return g_layout;
    return address == 0x800715B0u ? g_screen : 0u;
}

void psx_mod_write_word(uint32_t address, uint32_t value) {
    if (address == kCursor) g_cursor = value;
    if (address == kLayout) g_layout = value;
}

uint8_t psx_mod_read_byte(uint32_t address) {
    if (address == kLayoutChanged) return g_changed;
    if (address >= disruptor::menu_pointer::kGlyphWidths && address < disruptor::menu_pointer::kGlyphWidths + 64u) return 10u;
    if (address >= kGuest && address - kGuest < sizeof(g_guest)) return g_guest[address - kGuest];
    return 0u;
}

bool g_pointer = false;
double g_pointer_x = 0.0, g_pointer_y = 0.0;
int g_notes = 0;
extern "C" {
int psx_host_ui_game_input_captured(void) { return 0; }
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
void disruptor_capsule_note_byte(uint32_t address, uint8_t) {
    if (address == kCursor) ++g_notes;
}
}

void func_override_guest_call(CPUState *cpu, uint32_t target, uint32_t site_ra) {
    if (target != kApplyLayout || site_ra != kApplyReturn) return;
    ++g_applied;
    g_applied_with = cpu->gpr[4];
    cpu->gpr[4] = 0xDEADu;
    cpu->gpr[16] = 0xBEEFu;
}

int psx_mod_finish_function(CPUState *) {
    ++g_finished;
    return 1;
}

int psx_netplay_active(void) { return g_netplay; }

const char *disruptor_language_disc_name(void) { return g_language; }

bool SDL_PushEvent(SDL_Event *event) {
    ++g_pushed;
    g_pushed_type = event->type;
    return true;
}

namespace {

void test_registration() {
    expect(g_hook_count == 2 && hook_at(kItemDraw) && hook_at(kHandler) && g_activation, "both hooks and the activation are registered at load");
}

void test_draw() {
    g_language = "French";
    CPUState cpu{};
    cpu.gpr[kReturn] = kItemDrawn;
    cpu.gpr[kItem] = kExit;
    cpu.gpr[kString] = kOtherString;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(cpu.gpr[kString] == kOtherString && g_allocations == 0, "no word before the activation");
    g_activation();
    g_activation();
    expect(g_allocations == 1 && g_asked == kLongestWord + 1u && kLongestWord == 7u,
           "the activation takes the longest word's memory once, before the guest runs");
    cpu.gpr[kReturn] = kItemDrawn;
    cpu.gpr[kItem] = 7u;
    cpu.gpr[kString] = kOtherString;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(cpu.gpr[kString] == kOtherString && g_allocations == 1, "CREDITS keeps the game's string");
    cpu.gpr[kItem] = kExit;
    cpu.gpr[kReturn] = kItemDrawn + 4u;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(cpu.gpr[kString] == kOtherString, "a draw from elsewhere keeps the game's string");
    cpu.gpr[kReturn] = kItemDrawn;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(cpu.gpr[kString] == g_allocated && guest_string(g_allocated) == "QUITTER",
           "the ninth item draws the disc's word, ended though the memory was not zero");
    cpu.gpr[kString] = kOtherString;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(cpu.gpr[kString] == g_allocated && g_allocations == 1, "the word is allocated once");
    g_language = "";
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(guest_string(g_allocated) == "EXIT", "each draw writes the word of the disc laid over now, ended");
}

void test_hidden() {
    CPUState cpu{};
    for (const uint32_t item : {kFirstHidden, kLastHidden}) {
        g_finished = 0;
        cpu.gpr[kReturn] = kItemDrawn;
        cpu.gpr[kItem] = item;
        hook_at(kItemDraw)(&cpu, kItemDraw);
        expect(g_finished == 1, "a hidden item's draw is finished before the game draws it");
    }
    g_finished = 0;
    cpu.gpr[kReturn] = kItemDrawn;
    cpu.gpr[kItem] = 6u;
    cpu.gpr[kY] = 64u + 6u * kRowStep;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(g_finished == 0 && cpu.gpr[kY] == 64u + 4u * kRowStep, "GENERAL OPTIONS is drawn two rows up");
    cpu.gpr[kReturn] = kItemDrawn + 4u;
    cpu.gpr[kItem] = kFirstHidden;
    cpu.gpr[kY] = 100u;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(g_finished == 0 && cpu.gpr[kY] == 100u, "a draw from elsewhere is the game's");
    expect(chosen_with(4u, 0u) == 0u && g_cursor == 6u, "a cursor on a hidden item moves off it before the draw");
    expect(chosen_with(3u, kDown | kCross) == kCross && g_cursor == 6u, "Down and Cross on PASSWORD choose GENERAL OPTIONS");
    expect(chosen_with(3u, kDown) == kDown && g_cursor == 3u, "a move alone is the game's");
}

void test_layout() {
    g_applied = 0;
    expect(chosen_with(3u, kDown) == kDown && g_applied == 0, "the default layout with no change mark is left alone");
    for (const auto [layout, changed] : {std::pair{2u, uint8_t{0}}, std::pair{0u, uint8_t{1}}, std::pair{3u, uint8_t{1}}}) {
        g_layout = layout;
        g_changed = changed;
        g_applied = 0;
        CPUState cpu{};
        cpu.gpr[kButtons] = kDown;
        cpu.gpr[16] = 7u;
        g_cursor = 3u;
        hook_at(kHandler)(&cpu, kHandler);
        expect(g_layout == 0u && g_changed == 0u, "a state's layout and change mark are put back at the menu");
        expect(g_applied == 1 && g_applied_with == 0u, "the game's own routine applies the default layout once");
        expect(cpu.gpr[kButtons] == kDown && cpu.gpr[16] == 7u, "the handler gets its own registers back");
    }
}

/* Each row's draw with a string of four letters in the guest's memory, as the menu's loop makes them. */
void draw_rows() {
    for (uint32_t item = 0; item < kItems; ++item) {
        std::memcpy(g_guest + 32, "ABCD", 5);
        CPUState cpu{};
        cpu.gpr[kReturn] = kItemDrawn;
        cpu.gpr[kItem] = item;
        cpu.gpr[kString] = kGuest + 32u;
        cpu.gpr[kY] = 64u + item * kRowStep;
        hook_at(kItemDraw)(&cpu, kItemDraw);
    }
}

void test_pointer(bool replaying) {
    draw_rows();
    g_layout = 0u;
    g_changed = 0u;
    g_pointer = true;
    g_notes = 0;
    const auto at_row = [](uint32_t item) {
        g_pointer_x = 640.0;
        g_pointer_y = (64.0 + (item - (item > kLastHidden ? 2u : 0u)) * kRowStep + 7.0) * 4.0;
    };
    at_row(1u);
    chosen_with(0u, 0u);
    expect(g_cursor == 0u && g_notes == 0, "a pointer seen first, not moved, leaves the cursor");
    chosen_with(0u, 0u);
    expect(g_cursor == 0u, "a pointer resting over an item leaves the cursor the keys put");
    g_pointer_y = (64.0 - 10.0) * 4.0;
    chosen_with(3u, 0u);
    expect(g_cursor == (replaying ? 3u : 0u), "ten pixels above NEW GAME's letters the pointer is on it");
    g_notes = 0;
    g_cursor = 0u;
    at_row(6u);
    chosen_with(0u, 0u);
    expect(g_cursor == (replaying ? 0u : 6u) && g_notes == (replaying ? 0 : 1),
           replaying ? "a replay leaves the cursor to its recording" : "moved onto GENERAL OPTIONS, two rows up, the cursor goes there, noted");
    g_pointer_y = 30.0 * 4.0;
    chosen_with(0u, 0u);
    expect(g_cursor == 0u && disruptor_main_menu_click() == -1, "off the items the cursor stays and a press asks for none");
    chosen_with(0u, kCross);
    expect(g_cursor == 0u, "so that press chooses nothing of the pointer's");
    g_pointer_y = (82.0 + 7.0) * 4.0;
    chosen_with(1u, 0u);
    for (int step = 0; step < 96; ++step) {
        g_pointer_y += 0.5;
        chosen_with(g_cursor, 0u);
    }
    expect(g_cursor == (replaying ? 1u : 2u), "half a pixel at a time the pointer still moves the cursor on to the next row");
    at_row(3u);
    chosen_with(0u, 0u);
    g_cursor = 0u;
    const int asked = disruptor_main_menu_click();
    expect(asked == (replaying ? -1 : 3), "a press asks for the item under the pointer now");
    g_pointer_y = 30.0 * 4.0;
    chosen_with(0u, kCross);
    expect(g_cursor == (replaying ? 0u : 3u), "the press's item is chosen though the pointer left it and the button went up");
    chosen_with(0u, 0u);
    expect(g_cursor == 0u, "and only once");
    at_row(3u);
    chosen_with(0u, 0u);
    g_cursor = 0u;
    disruptor_main_menu_click();
    g_ls_mode = 1;
    chosen_with(0u, kCross);
    expect(g_cursor == 0u, "a press waiting for the menu writes nothing once lockstep runs");
    g_ls_mode = 0;
    chosen_with(0u, 0u);
    expect(g_cursor == 0u, "and stays dropped after it");
    g_screen = 5u;
    at_row(3u);
    expect(disruptor_main_menu_pointing() == 0 && disruptor_main_menu_click() == -1,
           "once the game opens another screen the pointer is the main menu's no more");
    g_screen = 0u;
    g_pointer = false;
}

void test_menu(bool replaying) {
    const int quits = replaying ? 0 : 1;
    g_pushed = 0;
    expect(chosen_with(7u, kCross) == kCross && g_pushed == 0, "Cross on CREDITS is the game's");
    expect(chosen_with(kExit, kUp) == kUp && g_pushed == 0, "a move on EXIT is the game's");
    expect(chosen_with(kExit, kCross | kDown) == (kCross | kDown) && g_pushed == 0, "Down and Cross on EXIT choose NEW GAME");
    expect(chosen_with(kExit, kCross | kUp) == (kCross | kUp) && g_pushed == 0, "Up and Cross on EXIT choose CREDITS");
    expect(chosen_with(kExit, kCross) == 0u, "Cross on EXIT is taken from the game");
    expect(g_pushed == quits && (!quits || g_pushed_type == SDL_QUIT),
           replaying ? "a replay does not quit" : "Cross on EXIT closes the window as its close button does");
    expect(chosen_with(7u, kCross | kDown) == kDown && g_pushed == 2 * quits, "Down and Cross on CREDITS choose EXIT");
    g_pushed = 0;
    g_netplay = 1;
    expect(chosen_with(kExit, kCross) == 0u && g_pushed == 0, "netplay takes the choice and does not quit");
    g_netplay = 0;
    g_ls_mode = 1;
    expect(chosen_with(kExit, kCross) == 0u && g_pushed == 0, "the lockstep run takes the choice and does not quit");
    g_ls_mode = 0;
    g_ls_replay_active = 1;
    expect(chosen_with(kExit, kCross) == 0u && g_pushed == 0, "the lockstep replay takes the choice and does not quit");
    g_ls_replay_active = 0;
}

}  // namespace

int main(int argc, char **argv) {
    const bool replaying = argc == 2 && std::strcmp(argv[1], "--replay") == 0;
    expect((std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr) == replaying, "ctest sets the replay variable for --replay only");
    test_registration();
    test_draw();
    test_hidden();
    test_layout();
    test_pointer(replaying);
    test_menu(replaying);
    if (g_failures != 0) return 1;
    std::cout << "disruptor main menu exit hooks" << (replaying ? " in a replay" : "") << ": PASS\n";
    return 0;
}
