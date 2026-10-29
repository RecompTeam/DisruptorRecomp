/*
 * Starts any mission from the main menu with the writes of an accepted password (0x8001EB58..0x8001ED40), every
 * carried flag clear, on the difficulty asked. They land at the menu loop's own VSync, after its entry zeroed the start flag (0x80020078) and
 * before the start flag is read (0x800207A4).
 */

#include "game/session/mission_select.h"

#include "game/achievements/game_client.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr uint32_t kVSync = 0x8004B3D4u;
constexpr uint32_t kMenuLoopWait = 0x800205A4u;  /* return of the main menu loop's VSync(0) */
constexpr int kReturn = 31;
constexpr int kPatience = 600;  /* VSync calls a request waits for the menu loop */
constexpr uint32_t kMenuState = 0x800715FCu;
constexpr uint8_t kMainMenu = 0x18u;
constexpr uint32_t kLevels = 0x80056D24u;
constexpr uint32_t kMission = 0x80071354u;  /* gp+0x208 */
constexpr uint32_t kLevel = 0x80071518u;    /* gp+0x3CC */
constexpr std::array<uint32_t, 5> kFlags = {0x800713D8u, 0x800713D9u, 0x80071524u, 0x80071525u, 0x80071690u};
constexpr uint32_t kDifficulty = 0x80071708u;  /* gp+0x5BC: the menu shows string 70 (REGULAR) for 2, 71 (HARD) else */
constexpr uint32_t kMode = 0x800716F0u;      /* gp+0x5A4: 1 the campaign, 3 the demo */
constexpr uint32_t kStoryMovies = 0x800715F0u;
constexpr uint32_t kScreen = 0x8007132Cu;
constexpr uint32_t kSaved = 0x80071740u;
constexpr uint32_t kStart = 0x800716E0u;     /* gp+0x594 */

std::atomic<int> g_asked{0};  /* the mission in the low byte, the difficulty above it */
bool g_from_launch = false;
std::atomic<int> g_waited{0};
bool g_hooked = false;

int request(int mission, int difficulty) {
    const bool known = difficulty == DISRUPTOR_DIFFICULTY_REGULAR || difficulty == DISRUPTOR_DIFFICULTY_HARD;
    return mission >= DISRUPTOR_FIRST_MISSION && mission <= DISRUPTOR_LAST_MISSION && known ? mission | difficulty << 8 : 0;
}

bool at_main_menu() {
    return g_hooked && psx_mod_game_started() && !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0 &&
           psx_mod_read_byte(kMenuState) == kMainMenu;
}

void vsync_entry(CPUState *cpu, uint32_t) {
    const int asked = g_asked.load(std::memory_order_relaxed);
    if (asked == 0) return;
    if (!cpu || cpu->gpr[kReturn] != kMenuLoopWait || !at_main_menu()) {
        if (!g_from_launch && g_waited.fetch_add(1, std::memory_order_relaxed) >= kPatience)
            g_asked.store(0, std::memory_order_relaxed);
        return;
    }
    g_asked.store(0, std::memory_order_relaxed);
    g_from_launch = false;
    if (!disruptor_achievements_allows(DISRUPTOR_ACHIEVEMENTS_START_MISSION)) return;
    const uint8_t mission = static_cast<uint8_t>(asked & 0xFF);
    psx_mod_write_byte(kMission, mission);
    psx_mod_write_byte(kLevel, psx_mod_read_byte(kLevels + mission));
    for (const uint32_t flag : kFlags) psx_mod_write_byte(flag, 0u);
    psx_mod_write_byte(kDifficulty, static_cast<uint8_t>(asked >> 8));
    psx_mod_write_byte(kMode, 1u);
    psx_mod_write_word(kStoryMovies, 1u);
    psx_mod_write_word(kScreen, 3u);
    psx_mod_write_word(kSaved, 0u);
    psx_mod_write_word(kStart, 1u);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_mission_select) {
    g_hooked = psx_mod_register_function_entry_plugin("disruptor.mission_select", kVSync, vsync_entry) != 0;
    if (!g_hooked) std::fprintf(stderr, "disruptor: failed to register the mission select hook\n");
    if (const char *asked = std::getenv("PSX_DISRUPTOR_START_MISSION"); asked && asked[0]) {
        const char *hard = std::getenv("PSX_DISRUPTOR_START_HARD");
        const int difficulty = hard && hard[0] && hard[0] != '0' ? DISRUPTOR_DIFFICULTY_HARD : DISRUPTOR_DIFFICULTY_REGULAR;
        if (const int start = request(std::atoi(asked), difficulty)) {
            g_asked.store(start, std::memory_order_relaxed);
            g_from_launch = true;
        }
    }
}

}  // namespace

extern "C" int disruptor_mission_select_ready(void) {
    return at_main_menu() ? 1 : 0;
}

extern "C" int disruptor_mission_select_start(int mission, int difficulty) {
    const int start = request(mission, difficulty);
    if (start == 0 || !at_main_menu()) return 0;
    if (!disruptor_achievements_allows(DISRUPTOR_ACHIEVEMENTS_START_MISSION)) return 0;
    g_waited.store(0, std::memory_order_relaxed);
    g_asked.store(start, std::memory_order_relaxed);
    return 1;
}
