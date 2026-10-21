#include "game/input/weapon_list.h"

#include "cpu_state.h"
#include "game/render/hud_widgets.h"
#include "game/session/capsule.h"
#include "lockstep.h"
#include "mod_plugins.h"

#include <cstdint>

// A list's mode also stops the player: only the HUD's read of it is told the list, and the list's up and down see no press.
namespace {

constexpr uint32_t kModeSite = 0x8003D840u;
constexpr uint32_t kModeWord = 0x93840310u; /* lbu $a0, 0x310($gp) */
constexpr int kModeRegister = 4;            /* $a0 */
constexpr uint32_t kPsionicModeSite = 0x8003E0F8u;
constexpr uint32_t kPsionicModeWord = 0x93830310u; /* lbu $v1, 0x310($gp): the psionics list reads the mode again */
constexpr int kPsionicModeRegister = 3;            /* $v1 */
/* The open list idles the psionic hand (gp+0x1D4) at 0x8003E118 and flags it at 0x8003E8C4. Forced, the HUD builder's
 * last pass gives its state back where every path ends: lw $ra at 0x8003E8C8. */
constexpr uint32_t kHudEndSite = 0x8003E8C8u, kHudEndWord = 0x8FBF00DCu;
constexpr uint32_t kPsionicHand = 0x80071320u, kHandState = 0x62u;
constexpr uint32_t kPlay = 0u, kWeaponList = 2u, kPsionicList = 3u;

struct MoveSite {
    uint32_t address, word;
    int reg;
    uint32_t list;
};

constexpr MoveSite kMoves[] = {
    {0x8003E014u, 0x8C637670u, 3, kWeaponList},  /* lw $v1, buttons: down takes the next weapon */
    {0x8003E098u, 0x8C427670u, 2, kWeaponList},  /* lw $v0, buttons: up takes the one before */
    {0x8003E7B8u, 0x8C637670u, 3, kPsionicList}, /* the next psionic power */
    {0x8003E84Cu, 0x8C427670u, 2, kPsionicList}, /* the one before */
};

constexpr uint32_t kGameClock = 0x800716F8u; /* gp+0x5AC */
constexpr uint32_t kShownFor = 60u;          /* a second of the game's clock */

uint32_t g_asked = 0u;  // the list a key asked for, 0 for none
uint32_t g_shown = 0u;
uint32_t g_shown_at = 0u;
uint32_t g_forced = 0u;
uint32_t g_hand = 0u;  // the psionic hand whose state a forced list keeps
uint8_t g_hand_state = 0u;

/* The second starts at the HUD's read, where a capsule's replay finds it too. A clock that starts over ends it. */
uint32_t shown() {
    const uint32_t clock = psx_mod_read_word(kGameClock);
    if (g_asked) {
        g_shown = g_asked;
        g_asked = 0u;
        g_shown_at = clock;
    }
    if (g_shown && clock - g_shown_at <= kShownFor) return g_shown;
    g_shown = 0u;
    return 0u;
}

void show(uint32_t list, uint8_t noted) {
    g_asked = list;
    disruptor_capsule_note_list(noted);
}

}  // namespace

extern "C" void disruptor_weapon_list_show(void) { show(kWeaponList, 1u); }

extern "C" void disruptor_psionic_list_show(void) { show(kPsionicList, 2u); }

extern "C" void disruptor_weapon_list_state_loaded(void) {
    g_asked = g_shown = g_forced = g_hand = 0u;
}

extern "C" void disruptor_weapon_list_instruction_hook(CPUState *cpu, uint32_t address, uint32_t instruction,
                                                       int phase) {
    if (!cpu || phase != 1 || g_ls_mode != 0 || g_ls_replay_active != 0) return;
    if (address == kModeSite && instruction == kModeWord) {
        const uint32_t list = shown();
        g_forced = list && cpu->gpr[kModeRegister] == kPlay ? list : 0u;
        if (g_forced) cpu->gpr[kModeRegister] = g_forced;
        disruptor_hud_widgets_mode(cpu->gpr[kModeRegister]);
        return;
    }
    if (g_forced == kPsionicList && address == kPsionicModeSite && instruction == kPsionicModeWord) {
        cpu->gpr[kPsionicModeRegister] = kPsionicList;
        g_hand = psx_mod_read_word(kPsionicHand);
        if (g_hand) g_hand_state = psx_mod_read_byte(g_hand + kHandState);
        return;
    }
    if (address == kHudEndSite && instruction == kHudEndWord) {
        if (g_hand) psx_mod_write_byte(g_hand + kHandState, g_hand_state);
        g_hand = 0u;
        return;
    }
    for (const MoveSite &move : kMoves)
        if (g_forced == move.list && address == move.address && instruction == move.word) cpu->gpr[move.reg] = 0u;
}
