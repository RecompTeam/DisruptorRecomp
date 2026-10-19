/*
 * Lets a mission's briefing play as long as the language disc's own player lets it.
 *
 * The US code keys the briefing on as voice 21 (0x80043604), zeroes a count (0x8004360C) that the loading screen's
 * VSync callback raises once a VBlank (0x80012318), and starts the mission when the count reaches 540 (0x800437C4),
 * writing the mission's music over the voice's memory: 9 s, what every English briefing is cut to on the disc. The
 * French, German and Japanese players wait each mission's entry of their own table, as long as their recordings. Right
 * before the callback raises the count it is set so that it reaches 540 on the VBlank the table names: at once when
 * that comes sooner, and for a longer wait it is sent below zero at 539 and walks back up. The wait lives in the count
 * alone, so a save state carries it, and a build without the module waits out what is left and 540 more. Under netplay
 * or lockstep the US wait stands.
 */

#include "game/language/language.h"
#include "game/session/diagnostics.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "spu.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kMoveImage = 0x8004D410u;
constexpr uint32_t kBeforeCount = 0x800122E8u;  /* the callback's last MoveImage returns here, the count rises after it */
constexpr uint32_t kCount = 0x80071700u;        /* gp+0x5B4 */
constexpr uint32_t kMission = 0x80071518u;      /* gp+0x3CC: the mission whose briefing and music are loaded */
constexpr int32_t kUsWait = 540;
constexpr int kBriefingVoice = 21;
constexpr int kReturn = 31;

bool g_hooked = false;

/* The count to leave before the callback raises it, so that raise `wait` (a wait of 0: the first) is the first to make it 540. */
int32_t held_count(int32_t count, int32_t wait) {
    if (count >= kUsWait) return count;
    if (count < 0) return count == -1 ? kUsWait - 1 : count;
    if (count + 1 >= wait) return kUsWait - 1;
    return count == kUsWait - 1 ? kUsWait - 1 - wait : count;
}

bool active() {
    return g_hooked && psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0;
}

void move_entry(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[kReturn] != kBeforeCount || !active()) return;
    const uint32_t mission = psx_mod_read_byte(kMission);
    const int wait = disruptor_language_disc_briefing_wait(mission);
    if (wait < 0) return;
    const auto count = static_cast<int32_t>(psx_mod_read_word(kCount));
    if (count == 0 && disruptor_diagnostics_detailed()) {
        SpuVoiceState voice{};
        spu_get_voice_state(kBriefingVoice, &voice);
        if (voice.active)
            disruptor_diagnostics_note("mission %u briefing begins, the language disc's player starts the mission %d VBlanks later, the US one %d",
                                       mission, wait, kUsWait);
    }
    const int32_t left = held_count(count, wait);
    if (left != count) psx_mod_write_word(kCount, static_cast<uint32_t>(left));
}

PSX_MOD_CONSTRUCTOR(register_disruptor_language_briefing) {
    g_hooked = psx_mod_register_function_entry_plugin("disruptor.language.briefing", kMoveImage, move_entry) != 0;
    if (!g_hooked) std::fprintf(stderr, "disruptor: failed to register the hook for the briefings' wait\n");
}

}  // namespace
