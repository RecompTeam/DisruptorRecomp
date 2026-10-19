/*
 * Gameplay frame-rate floor for Disruptor (SLUS-00224).
 *
 * The retail gameplay loop already steps its simulation once per elapsed
 * VBlank and only refuses to present more often than every second VBlank.
 * game.toml removes that hard-coded floor with a guarded instruction patch;
 * this hook re-imposes it at the loop's VSync call, so the stock 30 FPS
 * cadence stays the default and the unlock is a live host switch.
 *
 * A retail frame costs 0.9 to 1.7 VBlank periods of guest CPU time, so the
 * unlock also divides instruction cost inside the frame loop. The PsyQ VSync
 * wait counts loop iterations for its timeout and always runs at stock cost.
 */

#include "game/frame/frame_rate.h"

#include "cpu_state.h"
#include "interrupts.h"
#include "mod_plugins.h"
#include "psx_cyc.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr uint32_t kVSyncFunction = 0x8004B3D4u;
constexpr uint32_t kGameplayGateReturn = 0x80043E84u;
constexpr uint32_t kFrameLoopBegin = 0x800438FCu;
constexpr uint32_t kFrameLoopEnd = 0x800442F4u;
constexpr uint32_t kVBlankCounter = 0x8005B920u;
constexpr uint32_t kFixedStepOverride = 0x80071438u;  /* gp+0x2EC */
constexpr uint32_t kLoopMode = 0x800716F0u;           /* gp+0x5A4 */
constexpr uint32_t kPlayerState = 0x8007145Cu;        /* gp+0x310 */
constexpr uint8_t kAttractLoopMode = 3u;
constexpr uint8_t kSingleStepPlayerState = 5u;
constexpr uint32_t kVBlankCycles = 564480u;  /* VBLANK_CYCLES in interrupts.c */
constexpr uint32_t kRetailFloorVBlanks = 2u;
constexpr uint32_t kWindowVBlanks = 60u;
constexpr uint32_t kDefaultCpuShift = 2u;
constexpr uint32_t kMaximumCpuShift = 3u;
constexpr int kLastFrameVBlankRegister = 22;  /* $s6 in func_80043404 */

struct WindowAccumulator {
    uint32_t last_frame_vblank = UINT32_MAX;
    uint32_t start_vblank = 0;
    uint32_t frames = 0;
    uint32_t late_frames = 0;
    uint64_t work_cycles = 0;
    uint32_t peak_work_cycles = 0;
};

std::atomic<bool> g_unlocked{false};
std::atomic<uint32_t> g_cpu_shift{kDefaultCpuShift};
WindowAccumulator g_window;
std::atomic<uint32_t> g_last_frames{0};
std::atomic<uint32_t> g_last_vblanks{0};
std::atomic<uint32_t> g_last_late_frames{0};
std::atomic<uint32_t> g_last_average_permille{0};
std::atomic<uint32_t> g_last_peak_permille{0};

uint32_t permille_of_vblank(uint64_t cycles) {
    return static_cast<uint32_t>(cycles * 1000u / kVBlankCycles);
}

void publish_window(uint32_t vblanks) {
    const uint32_t average = g_window.frames
        ? permille_of_vblank(g_window.work_cycles / g_window.frames) : 0u;
    const uint32_t peak = permille_of_vblank(g_window.peak_work_cycles);
    g_last_frames.store(g_window.frames, std::memory_order_relaxed);
    g_last_vblanks.store(vblanks, std::memory_order_relaxed);
    g_last_late_frames.store(g_window.late_frames, std::memory_order_relaxed);
    g_last_average_permille.store(average, std::memory_order_relaxed);
    g_last_peak_permille.store(peak, std::memory_order_relaxed);
}

void record_frame(const CPUState *cpu) {
    const uint32_t now = psx_mod_read_word(kVBlankCounter);
    const uint32_t last_frame = cpu->gpr[kLastFrameVBlankRegister];
    const uint32_t crossed = now - last_frame;
    const bool stale_window = g_window.frames != 0u &&
        now - g_window.start_vblank > 2u * kWindowVBlanks;
    if (crossed > 600u || stale_window) {
        g_window = WindowAccumulator{};
        return;
    }
    /* A sliced entry block re-enters the hook for the same guest call. */
    if (last_frame == g_window.last_frame_vblank) return;
    g_window.last_frame_vblank = last_frame;

    const uint32_t remaining =
        std::min(interrupts_cycles_to_vblank(), kVBlankCycles);
    const uint32_t work_cycles = (crossed + 1u) * kVBlankCycles - remaining;

    if (g_window.frames == 0u) g_window.start_vblank = now - crossed;
    ++g_window.frames;
    if (crossed != 0u) ++g_window.late_frames;
    g_window.work_cycles += work_cycles;
    g_window.peak_work_cycles =
        std::max(g_window.peak_work_cycles, work_cycles);

    const uint32_t elapsed = now + 1u - g_window.start_vblank;
    if (elapsed < kWindowVBlanks) return;
    publish_window(elapsed);
    g_window = WindowAccumulator{};
    g_window.last_frame_vblank = last_frame;
}

/* func_80043404 steps min(elapsed, 6) ticks only in this state; demo playback,
 * the attract mode and player state 5 run a fixed count per frame instead. */
bool simulation_follows_elapsed_vblanks() {
    return psx_mod_read_word(kFixedStepOverride) == 0u &&
           psx_mod_read_byte(kLoopMode) != kAttractLoopMode &&
           psx_mod_read_byte(kPlayerState) != kSingleStepPlayerState;
}

/* The carry counts in units of the divisor that left it, so another divisor starts from none. */
void set_cpu_shift(uint32_t shift) {
    if (shift == g_psx_cyc_overclock_shift) return;
    g_psx_cyc_overclock_shift = shift;
    g_psx_cyc_overclock_carry = 0u;
}

void vsync_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kVSyncFunction) return;
    const uint32_t return_address = cpu->gpr[31];
    /* Library code polls VSync(-1) mid-frame, so only a real wait ends the
     * fast section and only the frame loop's own poll starts it. */
    const bool waits = static_cast<int32_t>(cpu->gpr[4]) >= 0;
    if (waits) set_cpu_shift(0u);
    const bool frame_gate =
        return_address == kGameplayGateReturn && cpu->gpr[4] == 0u;
    const bool frame_loop_poll = !waits && return_address >= kFrameLoopBegin &&
                                 return_address < kFrameLoopEnd;
    if (!frame_gate && !frame_loop_poll) return;

    const bool unlocked = g_unlocked.load(std::memory_order_relaxed) &&
                          simulation_follows_elapsed_vblanks();
    if (frame_loop_poll) {
        if (unlocked) set_cpu_shift(g_cpu_shift.load(std::memory_order_relaxed));
        return;
    }
    record_frame(cpu);
    /* VSync(2) waits to the second VBlank after the previous VSync, which is
     * what the retail "elapsed < 2" branch did with a second VSync(0). */
    if (!unlocked) cpu->gpr[4] = kRetailFloorVBlanks;
}

bool env_enabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

uint32_t shift_for_multiplier(int multiplier) {
    uint32_t shift = 0u;
    while (shift < kMaximumCpuShift && (1 << (shift + 1u)) <= multiplier)
        ++shift;
    return shift;
}

PSX_MOD_CONSTRUCTOR(register_disruptor_frame_rate) {
    g_unlocked.store(env_enabled("PSX_DISRUPTOR_FRAME_UNLOCK"),
                     std::memory_order_relaxed);
    if (const char *value = std::getenv("PSX_DISRUPTOR_FRAME_UNLOCK_CPU")) {
        g_cpu_shift.store(shift_for_multiplier(std::atoi(value)),
                          std::memory_order_relaxed);
    }
    if (!psx_mod_register_function_entry_plugin(
            "disruptor.frame_rate.vsync", kVSyncFunction, vsync_entry)) {
        std::fprintf(stderr,
                     "disruptor: failed to register frame-rate VSync hook\n");
    }
}

}  // namespace

extern "C" int disruptor_frame_rate_unlocked(void) {
    return g_unlocked.load(std::memory_order_relaxed) ? 1 : 0;
}

extern "C" void disruptor_frame_rate_set_unlocked(int enabled) {
    g_unlocked.store(enabled != 0, std::memory_order_relaxed);
}

extern "C" int disruptor_frame_rate_cpu_multiplier(void) {
    return 1 << g_cpu_shift.load(std::memory_order_relaxed);
}

extern "C" void disruptor_frame_rate_set_cpu_multiplier(int multiplier) {
    g_cpu_shift.store(shift_for_multiplier(multiplier),
                      std::memory_order_relaxed);
}

extern "C" int disruptor_frame_rate_last_window(
        DisruptorFrameRateWindow *out) {
    if (!out) return 0;
    out->frames = g_last_frames.load(std::memory_order_relaxed);
    out->vblanks = g_last_vblanks.load(std::memory_order_relaxed);
    out->late_frames = g_last_late_frames.load(std::memory_order_relaxed);
    out->average_work_permille =
        g_last_average_permille.load(std::memory_order_relaxed);
    out->peak_work_permille =
        g_last_peak_permille.load(std::memory_order_relaxed);
    return out->frames != 0u ? 1 : 0;
}
