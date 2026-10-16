#include "cpu_state.h"
#include "mod_plugins.h"

#include <cstdint>
#include <iostream>

namespace {

constexpr std::uint32_t kTestAutoPick = 0x80032514u, kTestWeapon = 0x80077627u;
std::uint8_t g_current = 0;
std::uint32_t g_entry_address = 0;
PSXModFunctionEntryCallback g_entry = nullptr;
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
std::uint8_t psx_mod_read_byte(std::uint32_t address) { return address == kTestWeapon ? g_current : 0u; }
int psx_mod_register_function_entry_plugin(const char *, std::uint32_t address, PSXModFunctionEntryCallback callback) {
    g_entry_address = address;
    g_entry = callback;
    return 1;
}
}

#include "../../src/game/input/baton_choice.cpp"

namespace {

std::uint32_t auto_pick(std::uint32_t argument) {
    CPUState cpu{};
    cpu.gpr[4] = argument;
    g_entry(&cpu, kTestAutoPick);
    return cpu.gpr[4];
}

}  // namespace

int main() {
    expect(g_entry_address == kTestAutoPick && g_entry, "the hook sits on the game's automatic weapon pick");
    g_current = 0;
    expect(auto_pick(0xFFu) == 0xFFu, "a baton the game fell back to is swapped for a gun as before");
    disruptor_weapon_chosen(0);
    expect(auto_pick(0xFFu) == 0u, "a baton the player chose stays in hand");
    expect(auto_pick(0xFFu) == 0u, "on every later call too");
    expect(auto_pick(3u) == 3u, "a weapon the game hands over itself, such as one picked up, still goes through");
    g_ls_mode = 1;
    expect(auto_pick(0xFFu) == 0xFFu, "the lockstep comparator sees the game's own pick");
    g_ls_mode = 0;
    g_current = 2;
    expect(auto_pick(0xFFu) == 0xFFu, "once another weapon is in hand the choice is spent");
    g_current = 0;
    expect(auto_pick(0xFFu) == 0xFFu, "and a later fall back to the baton is the game's again");
    disruptor_weapon_chosen(0);
    disruptor_weapon_chosen(4);
    expect(auto_pick(0xFFu) == 0xFFu, "choosing a gun forgets the baton");
    disruptor_weapon_chosen(0);
    disruptor_weapon_choice_state_loaded();
    expect(auto_pick(0xFFu) == 0xFFu, "a loaded state forgets the choice");
    expect(!disruptor_baton_held_by_choice(), "nothing chosen, no baton held by choice");
    disruptor_weapon_chosen(0);
    expect(disruptor_baton_held_by_choice(), "a chosen baton in hand is held by choice, so a capsule begun now records it");
    g_current = 5;
    expect(!disruptor_baton_held_by_choice(), "a chosen baton no longer in hand is not");
    g_current = 0;
    if (g_failures) return 1;
    std::cout << "Disruptor baton choice tests passed\n";
    return 0;
}
