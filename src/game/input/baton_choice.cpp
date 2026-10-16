#include "game/input/baton_choice.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"

// With the baton in hand the game calls 0x80032514 with 0xFF, which swaps in the best gun with ammunition.
namespace {

constexpr uint32_t kAutoPick = 0x80032514u;
constexpr uint32_t kCurrentWeapon = 0x80077627u;
constexpr uint32_t kAnyWeapon = 0xFFu;
constexpr uint32_t kBaton = 0u;
constexpr int kArgument = 4;  // $a0

bool g_baton_chosen = false;

void auto_pick_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kAutoPick || !g_baton_chosen || g_ls_mode != 0) return;
    if (psx_mod_read_byte(kCurrentWeapon) != kBaton) {
        g_baton_chosen = false;
        return;
    }
    if (cpu->gpr[kArgument] == kAnyWeapon) cpu->gpr[kArgument] = kBaton;
}

}  // namespace

extern "C" void disruptor_weapon_chosen(uint8_t weapon) { g_baton_chosen = weapon == kBaton; }

extern "C" void disruptor_weapon_choice_state_loaded(void) { g_baton_chosen = false; }

extern "C" bool disruptor_baton_held_by_choice(void) { return g_baton_chosen && psx_mod_read_byte(kCurrentWeapon) == kBaton; }

PSX_MOD_CONSTRUCTOR(register_disruptor_baton_choice) {
    psx_mod_register_function_entry_plugin("disruptor.baton_choice", kAutoPick, auto_pick_entry);
}
