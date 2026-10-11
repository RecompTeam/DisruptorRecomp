/*
 * Intro skip for Disruptor (SLUS-00224).
 *
 * The boot sequence is three logo screens and the title movie, and each has
 * a skip of its own that the game holds back for a while. func_80020990
 * shows a logo for 110 frames and lets any button cut to its fade-out from
 * frame 25, once the next image is loaded. func_80014520 plays movie $s4
 * and ignores the skip buttons for 120 frames when the number is below 3;
 * the boot sequence plays number 0. With the switch on a logo cuts to its
 * fade-out at frame 25 by itself, and the title movie gets no floor and
 * sees START held, so it leaves by the game's own exit on its second
 * frame. Every other movie is left alone. The skip key does the same by
 * hand for the logo or any movie on screen. Only registers are changed,
 * and for a movie skipped by the key one word: its floor.
 *
 * A cut logo still takes its 25 frames and its fade, and the disc is read at
 * its own pace before the first one. So with the switch on the frontend also
 * runs the boot unpaced and unseen until the main menu, which then comes 5 s
 * after launch instead of 14.
 */

#include "game/session/intro_skip.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>

namespace {

constexpr uint32_t kLogoSite = 0x80020A80u;
constexpr uint32_t kLogoWord = 0x2A02005Eu;  /* slti $v0, $s0, 94 */
constexpr uint32_t kFloorSite = 0x800147B0u;
constexpr uint32_t kFloorWord = 0x2A820003u;  /* slti $v0, $s4, 3 */
constexpr uint32_t kButtonsSite = 0x80048678u;
constexpr uint32_t kButtonsWord = 0x30420940u;  /* andi $v0, $v0, START | SELECT | CROSS */
constexpr int32_t kLogoEarliestCut = 25;
constexpr int32_t kLogoFadeOut = 94;
constexpr int32_t kTitleMovie = 0;
constexpr uint32_t kStart = 0x0800u;
constexpr int kLogoFrameRegister = 16;  /* $s0 */
constexpr int kMovieRegister = 20;  /* $s4 */
constexpr int kResultRegister = 2;  /* $v0 */
constexpr uint32_t kMovieFloor = 0x800713C8u;  /* gp+0x27C, read back at 0x80048688 */
constexpr int64_t kRequestLifeMs = 500;
constexpr int64_t kNoRequest = INT64_MIN;
constexpr uint32_t kMenuState = 0x800715FCu;
constexpr uint8_t kMainMenu = 0x18u;
constexpr uint32_t kRendererEntry = 0x80040E68u;
constexpr int kBootBudget = 1500;  /* Fast-forwarded VBlanks. The boot takes 750 to 780 with the logos cut. */

bool env_enabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && value[0] != '0';
}

extern "C" void psx_host_set_fast_forward_query(int (*query)(void));  /* host_ui.h, which needs SDL */

using Clock = std::chrono::steady_clock;

std::atomic<bool> g_enabled{env_enabled("PSX_DISRUPTOR_SKIP_INTRO")};
std::atomic<int64_t> g_requested_at{kNoRequest};
Clock::time_point (*g_now)() = &Clock::now;
bool g_title_movie = false;
bool g_movie_skip = false;
bool g_logo_cut_pending = false;
int32_t g_logo_frame = 0;
int64_t g_logo_seen_at = kNoRequest;
bool g_boot_over = false;
int g_boot_frames = 0;

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        g_now().time_since_epoch()).count();
}

/* A press is taken for half a second: by the movie on screen, or by the logo on screen or next up. */
bool requested() {
    const int64_t at = g_requested_at.load(std::memory_order_relaxed);
    return at != kNoRequest && now_ms() - at <= kRequestLifeMs;
}

}  // namespace

extern "C" void disruptor_intro_skip_instruction_hook(
        CPUState *cpu, uint32_t address, uint32_t instruction, int phase) {
    if (!cpu || phase != 1 || g_ls_mode != 0 || g_ls_replay_active != 0) return;
    const bool enabled = g_enabled.load(std::memory_order_relaxed);
    if (address == kLogoSite && instruction == kLogoWord) {
        const int32_t frame = static_cast<int32_t>(cpu->gpr[kLogoFrameRegister]);
        if (frame < g_logo_frame) {
            g_logo_cut_pending = false;
            if (g_requested_at.load(std::memory_order_relaxed) <= g_logo_seen_at)
                g_requested_at.store(kNoRequest, std::memory_order_relaxed);
        }
        g_logo_frame = frame;
        g_logo_seen_at = now_ms();
        if (requested()) g_logo_cut_pending = true;
        if ((enabled || g_logo_cut_pending) && frame >= kLogoEarliestCut && frame < kLogoFadeOut) {
            cpu->gpr[kLogoFrameRegister] = static_cast<uint32_t>(kLogoFadeOut);
            cpu->gpr[kResultRegister] = 0u;
            g_logo_cut_pending = false;
            g_requested_at.store(kNoRequest, std::memory_order_relaxed);
        }
        return;
    }
    if (address == kFloorSite && instruction == kFloorWord) {
        g_title_movie = enabled &&
            static_cast<int32_t>(cpu->gpr[kMovieRegister]) == kTitleMovie;
        g_movie_skip = false;
        g_requested_at.store(kNoRequest, std::memory_order_relaxed);
        if (g_title_movie) cpu->gpr[kResultRegister] = 0u;
        return;
    }
    if (address == kButtonsSite && instruction == kButtonsWord) {
        if (!g_movie_skip && requested()) {
            g_movie_skip = true;
            g_requested_at.store(kNoRequest, std::memory_order_relaxed);
            psx_mod_write_word(kMovieFloor, 0u);
        }
        if (g_title_movie || g_movie_skip) cpu->gpr[kResultRegister] |= kStart;
    }
}

extern "C" void disruptor_intro_skip_request(void) {
    g_requested_at.store(now_ms(), std::memory_order_relaxed);
}

extern "C" int disruptor_intro_skip_fast_forward(void) {
    if (g_boot_over || !psx_mod_game_started()) return 0;
    const bool enabled = g_enabled.load(std::memory_order_relaxed);
    if (g_ls_mode != 0 || g_ls_replay_active != 0 || psx_mod_read_byte(kMenuState) == kMainMenu ||
        (enabled && ++g_boot_frames > kBootBudget)) {
        g_boot_over = true;
        return 0;
    }
    return enabled ? 1 : 0;
}

namespace {

/* A state loaded during the boot lands in play, where the menu byte never comes. */
void renderer_entry(CPUState *, uint32_t address) {
    if (address == kRendererEntry) g_boot_over = true;
}

PSX_MOD_CONSTRUCTOR(register_disruptor_intro_skip) {
    psx_mod_register_function_entry_plugin("disruptor.intro_skip.renderer", kRendererEntry, renderer_entry);
    psx_host_set_fast_forward_query(disruptor_intro_skip_fast_forward);
}

}  // namespace

extern "C" int disruptor_intro_skip_enabled(void) {
    return g_enabled.load(std::memory_order_relaxed) ? 1 : 0;
}

extern "C" void disruptor_intro_skip_set_enabled(int enabled) {
    g_enabled.store(enabled != 0, std::memory_order_relaxed);
}
