#include "game/render/mover_steps.h"

#include "cpu_state.h"
#include "game/frame/frame_rate.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" int psx_netplay_active(void);

namespace {

using namespace disruptor::movers;

constexpr uint32_t kDrawWorld = 0x80040E68u;
constexpr uint32_t kDrawSector = 0x80046134u;
constexpr uint32_t kAfterSector = 0x8003B78Cu;
constexpr uint32_t kWorldLoop = 0x800411A4u; /* where the world's loop of sectors is returned to: its next call is kAfterSector */
constexpr uint32_t kTick = 0x80039450u;
constexpr uint32_t kVSync = 0x8004B3D4u; /* the frame's draw ends in it: no half step is in memory while the game waits */
constexpr uint32_t kFrameWait = 0x80043E84u; /* where the frame loop's wait after its last draw returns to */
constexpr uint32_t kTicks = 0x800716F8u;       /* gp+1452: odd after a tick means the next tick steps the movers */
constexpr uint32_t kMoverList = 0x80071424u;   /* gp+728 */
constexpr uint32_t kMoverCount = 0x80071498u;  /* gp+844 */
constexpr uint32_t kLiftCount = 0x80071408u;   /* gp+700 */
constexpr uint32_t kLiftRecords = 0x8007173Cu; /* gp+1520 */
constexpr uint32_t kCameraHeight = 0x800775D0u;
constexpr uint32_t kPlayerHeight = 0x800775C4u;
constexpr uint32_t kPlayerStands = 0x80077608u; /* how far above the floor that height is */
constexpr uint32_t kPlayerSector = 0x80077630u;

struct Guest {
    uint32_t word(uint32_t address) const { return psx_mod_read_word(address); }
    uint32_t byte(uint32_t address) const { return psx_mod_read_byte(address); }
    void write(uint32_t address, uint32_t value) { psx_mod_write_word(address, value); }
};

Shifts g_shifts;
Lifts g_lifts;
bool g_off = false;

bool wanted() { return !g_off && disruptor_frame_rate_unlocked() && !psx_netplay_active(); }

void draw_world(CPUState *, uint32_t) {
    Guest guest;
    catch_up(guest, g_lifts);
    if (!wanted()) return;
    const uint32_t sector = guest.word(kSectors + 4u * guest.byte(kPlayerSector));
    const uint32_t riding = sector != 0u ? guest.byte(sector + kSectorLift) : kNoSector;
    lag(guest, g_lifts, guest.word(kLiftRecords), kCameraHeight, guest.word(kPlayerHeight) - guest.word(kPlayerStands), riding);
}

void draw_sector(CPUState *cpu, uint32_t) {
    Guest guest;
    take_back(guest, g_shifts);
    if (!cpu || cpu->gpr[31] != kWorldLoop || !wanted() || (guest.word(kTicks) & 1u) == 0u) return;
    lead(guest, g_shifts, cpu->gpr[4], guest.word(kMoverList), guest.word(kMoverCount), guest.byte(kPlayerSector));
}

void sector_drawn(CPUState *, uint32_t) {
    Guest guest;
    take_back(guest, g_shifts);
}

void frame_waits(CPUState *cpu, uint32_t) {
    Guest guest;
    take_back(guest, g_shifts);
    /* VSync is asked inside the draw too: by the library, and by the frame loop while it waits at 0x80043BA0. */
    if (cpu && cpu->gpr[31] == kFrameWait) catch_up(guest, g_lifts);
}

void tick_begins(CPUState *, uint32_t) {
    Guest guest;
    take_back(guest, g_shifts);
    catch_up(guest, g_lifts);
    note(guest, g_lifts, guest.word(kLiftRecords), guest.word(kLiftCount));
}

PSX_MOD_CONSTRUCTOR(register_disruptor_mover_motion) {
    const char *asked = std::getenv("PSX_DISRUPTOR_MOVER_STEPS");
    g_off = asked && asked[0] == '0';
    if (!psx_mod_register_function_entry_plugin("disruptor.mover_motion.drawn", kAfterSector, sector_drawn) ||
        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.tick", kTick, tick_begins) ||
        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.vsync", kVSync, frame_waits) ||
        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.draw", kDrawSector, draw_sector) ||
        !psx_mod_register_function_entry_plugin("disruptor.mover_motion.world", kDrawWorld, draw_world)) {
        g_off = true;
        std::fprintf(stderr, "disruptor: failed to register the mover hooks\n");
    }
}

} // namespace

extern "C" void disruptor_mover_motion_state_loaded(void) {
    forget(g_shifts);
    forget(g_lifts);
}

extern "C" void disruptor_mover_motion_state_saving(void) {
    Guest guest;
    take_back(guest, g_shifts);
    catch_up(guest, g_lifts);
}
