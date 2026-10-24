#include "game/frame/stock_frames.h"

#include "cpu_state.h"
#include "game/frame/frame_rate.h"
#include "game/guest.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" int psx_netplay_active(void);

namespace {

using namespace disruptor::stock_frames;
using disruptor::Guest;

constexpr uint32_t kEffects = 0x8003FE6Cu; /* the screen effects, drawn once a frame from the world's draw (0x800410F0) */
constexpr uint32_t kMessageDraw = 0x800427D0u; /* the HUD's message, once a frame from the loop (0x80043C9C) */
constexpr uint32_t kTick = 0x80039450u;
constexpr uint32_t kSounds = 0x80013544u;
constexpr uint32_t kSoundsSite = 0x80043AD8u; /* the frame loop's call; the level's end and the briefing call it too */
constexpr uint32_t kVSync = 0x8004B3D4u;
constexpr uint32_t kFrameWait = 0x80043E84u; /* where the frame loop's wait after its last draw returns to */

Frames g_frames;
bool g_off = false;

bool wanted() { return !g_off && disruptor_frame_rate_gameplay_unlocked() && !psx_netplay_active(); }

void effects(CPUState *cpu, uint32_t) {
    if (!cpu || !wanted()) return;
    Guest guest;
    effects_begin(guest, g_frames, cpu->gpr[31]);
}

void sounds(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[31] != kSoundsSite || !wanted()) return;
    Guest guest;
    sounds_begin(guest, g_frames, cpu->gpr[31]);
}

void message(CPUState *cpu, uint32_t) {
    Guest guest;
    if (!wanted()) {
        give_back(guest, g_frames.shown);
        forget(g_frames.shown);
        return;
    }
    if (cpu) message_begins(guest, g_frames, cpu->gpr[31]);
}

void frame_waits(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[31] != kFrameWait) return;
    Guest guest;
    frame_over(guest, g_frames, true);
}

/* The ticks come after the frame's wait: whatever a hook left is long taken by then. */
void tick_begins(CPUState *, uint32_t) {
    Guest guest;
    frame_over(guest, g_frames, false);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_stock_frames) {
    const char *asked = std::getenv("PSX_DISRUPTOR_STOCK_FRAMES");
    g_off = asked && asked[0] == '0';
    if (!psx_mod_register_function_entry_plugin("disruptor.stock_frames.effects", kEffects, effects) ||
        !psx_mod_register_function_entry_plugin("disruptor.stock_frames.message", kMessageDraw, message) ||
        !psx_mod_register_function_entry_plugin("disruptor.stock_frames.sounds", kSounds, sounds) ||
        !psx_mod_register_function_entry_plugin("disruptor.stock_frames.tick", kTick, tick_begins) ||
        !psx_mod_register_function_entry_plugin("disruptor.stock_frames.vsync", kVSync, frame_waits)) {
        g_off = true;
        std::fprintf(stderr, "disruptor: failed to register the stock frame hooks\n");
    }
}

} // namespace

extern "C" void disruptor_stock_frames_state_loaded(void) {
    const Guest guest;
    loaded(guest, g_frames);
}

extern "C" void disruptor_stock_frames_state_saving(void) {
    Guest guest;
    state_saving(guest, g_frames);
}

/* A state is held back from a hook's change until the game has taken it: no bound, the frame's draw takes it within. */
extern "C" int disruptor_stock_frames_save_deferred(void) {
    const Guest guest;
    return saving_deferred(guest, g_frames) ? 1 : 0;
}
