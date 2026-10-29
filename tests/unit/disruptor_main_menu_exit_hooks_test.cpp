#include "game/language/language.h"
#include "game/session/main_menu_exit.h"

#include "cpu_state.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "psx_sdl.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

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
uint8_t g_guest[64];
uint32_t g_allocated = 0u;
int g_allocations = 0;
uint32_t g_cursor = 0u;
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

uint32_t psx_mod_alloc_guest_memory(uint32_t size, uint32_t) {
    ++g_allocations;
    if (size > sizeof(g_guest)) return 0u;
    std::memset(g_guest, 0xAA, sizeof(g_guest));
    g_allocated = kGuest;
    return kGuest;
}

void psx_mod_write_byte(uint32_t address, uint8_t value) {
    if (address >= kGuest && address - kGuest < sizeof(g_guest)) g_guest[address - kGuest] = value;
}

uint32_t psx_mod_read_word(uint32_t address) { return address == kCursor ? g_cursor : 0u; }

int psx_netplay_active(void) { return g_netplay; }

const char *disruptor_language_disc_name(void) { return g_language; }

bool SDL_PushEvent(SDL_Event *event) {
    ++g_pushed;
    g_pushed_type = event->type;
    return true;
}

namespace {

void test_registration() {
    expect(g_hook_count == 2 && hook_at(kItemDraw) && hook_at(kHandler), "both hooks are registered at load");
}

void test_draw() {
    g_language = "French";
    CPUState cpu{};
    cpu.gpr[kReturn] = kItemDrawn;
    cpu.gpr[kItem] = 7u;
    cpu.gpr[kString] = kOtherString;
    hook_at(kItemDraw)(&cpu, kItemDraw);
    expect(cpu.gpr[kString] == kOtherString && g_allocations == 0, "CREDITS keeps the game's string");
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
    test_menu(replaying);
    if (g_failures != 0) return 1;
    std::cout << "disruptor main menu exit hooks" << (replaying ? " in a replay" : "") << ": PASS\n";
    return 0;
}
