/*
 * Shows a level's hint that is twice as tall as a US one.
 *
 * A hint is a picture 320 wide. The menu's code reads it from the disc and
 * stages it in VRAM, and the loading screen moves it from there onto both
 * frame buffers. The US and PAL hints have 32 rows. The Japanese ones have 64,
 * and that game reads twice the bytes, stages them 32 rows higher and puts
 * them on the screen 16 rows higher. With such a disc laid over the US one,
 * the US code's read, its LoadImage and its two MoveImage calls for a hint are
 * given those numbers at their entries, each call told apart by where it
 * returns to.
 *
 * One decision serves a whole hint. It is made at the read: the picture is
 * staged tall only when it was read tall, and moved tall only when it was
 * staged tall. So nothing that changes between the three steps can make them
 * disagree. The decision is not part of a save state: one made between the
 * steps and loaded in another run shows the lower half of the picture.
 *
 * The rectangle comes from the module's own mod memory: game memory is never
 * written. Nothing is done under netplay, lockstep or a replay, where the US
 * code then shows the upper half.
 */

#include "game/language/language.h"

#include "cpu_state.h"
#include "game/language/language_disc.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace {

struct Move {
    uint32_t returns_to;
    uint32_t row;  /* the frame buffer row the US code moves the hint to */
};

constexpr uint32_t kRead = 0x80011888u, kLoadImage = 0x8004D348u, kMoveImage = 0x8004D410u;
constexpr uint32_t kReadReturn = 0x80020928u, kStageReturn = 0x80020954u;
constexpr std::array<Move, 2> kMoves = {{{0x80043508u, 0xA0u}, {0x80043518u, 0x190u}}};
constexpr std::array<uint16_t, 4> kStage = {0x2C0, 0x1E0, 0x140, 0x20};  /* x, y, width and rows of the US hint in VRAM */
constexpr std::array<uint16_t, 4> kTallStage = {0x2C0, 0x1C0, 0x140, 0x40};  /* the Japanese code's own, read from SLPS-00804 at 0x8001D8EC */
constexpr uint32_t kRowsHigher = 16;  /* its loading screen has the picture at row 0x90 where the US one has it at 0xA0 */
constexpr uint32_t kRamFirst = 0x80000000u, kRamEnd = 0x80200000u;
constexpr int kRectRegister = 4;
constexpr int kSizeRegister = 6;
constexpr int kRowRegister = 6;
constexpr int kReturnRegister = 31;

enum class Hint { kUntouched, kReadTall, kStagedTall };

bool g_hooked = false;
uint32_t g_tall_stage = 0;
Hint g_hint = Hint::kUntouched;

bool tall_hints() {
    return g_hooked && psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0 &&
           disruptor_language_disc_tall_hints() != 0;
}

/* Whether a call was handed the rectangle the US code stages a hint in. */
bool us_stage(uint32_t rect) {
    if (rect < kRamFirst || rect > kRamEnd - 2u * kStage.size() || rect % 2u != 0u) return false;
    for (size_t field = 0; field < kStage.size(); ++field)
        if (psx_mod_read_half(rect + 2u * static_cast<uint32_t>(field)) != kStage[field]) return false;
    return true;
}

uint32_t tall_stage() {
    if (g_tall_stage != 0u) return g_tall_stage;
    g_tall_stage = psx_mod_alloc_guest_memory(2u * static_cast<uint32_t>(kTallStage.size()), 4u);
    for (size_t field = 0; g_tall_stage != 0u && field < kTallStage.size(); ++field)
        psx_mod_write_half(g_tall_stage + 2u * static_cast<uint32_t>(field), kTallStage[field]);
    return g_tall_stage;
}

void read_entry(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[kReturnRegister] != kReadReturn) return;
    g_hint = Hint::kUntouched;
    if (cpu->gpr[kSizeRegister] != disruptor::kHintBytes || !tall_hints() || tall_stage() == 0u) return;
    cpu->gpr[kSizeRegister] = disruptor::kTallHintBytes;
    g_hint = Hint::kReadTall;
}

void stage_entry(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[kReturnRegister] != kStageReturn) return;
    const bool tall = g_hint == Hint::kReadTall && us_stage(cpu->gpr[kRectRegister]);
    g_hint = tall ? Hint::kStagedTall : Hint::kUntouched;
    if (tall) cpu->gpr[kRectRegister] = g_tall_stage;
}

void move_entry(CPUState *cpu, uint32_t) {
    if (!cpu || g_hint != Hint::kStagedTall) return;
    const auto move = std::find_if(kMoves.begin(), kMoves.end(), [&](const Move &one) { return one.returns_to == cpu->gpr[kReturnRegister]; });
    if (move == kMoves.end() || cpu->gpr[kRowRegister] != move->row || !us_stage(cpu->gpr[kRectRegister])) return;
    cpu->gpr[kRectRegister] = g_tall_stage;
    cpu->gpr[kRowRegister] -= kRowsHigher;
}

/* All three or none: a hint read tall and moved by the US numbers would show its lower half. */
bool hook_all() {
    return psx_mod_register_function_entry_plugin("disruptor.language.hint.read", kRead, read_entry) &&
           psx_mod_register_function_entry_plugin("disruptor.language.hint.stage", kLoadImage, stage_entry) &&
           psx_mod_register_function_entry_plugin("disruptor.language.hint.move", kMoveImage, move_entry);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_language_hints) {
    g_hooked = hook_all();
    if (!g_hooked) std::fprintf(stderr, "disruptor: failed to register the hooks for tall hints\n");
}

}  // namespace
