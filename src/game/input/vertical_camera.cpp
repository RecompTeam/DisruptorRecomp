/*
 * Game-aware vertical camera and weapon aim for Disruptor (SLUS-00224).
 *
 * The retail renderer is yaw-only, but its projection consistently treats
 * screen Y=120 as the horizon and uses GTE H=160.  Moving that horizon by
 * H*tan(pitch) therefore gives the game one coherent, bounded vertical-look
 * model without altering its world transform.  Exact resident instruction
 * hooks carry that same center through CPU-side projection, conservatively
 * widen the original vertical cull planes, and add the matching slope to
 * freshly-created projectiles.
 *
 * Every site is pinned by both PC and opcode.  At zero pitch, during lockstep
 * replay, in netplay, or outside verified live gameplay, the original guest
 * camera projection stays neutral. During live play, locked shots can use a
 * corrected range even at zero pitch. Drain collision and homing apply in both aim modes.
 */

#include "game/input/vertical_camera.h"

#include "game/input/mouse_aim.h"

#include "cpu_state.h"
#include "game/session/weapon_load.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

extern "C" void gpu_geometry_camera_projection_center_y_set(double center_y);
extern "C" double gpu_geometry_camera_projection_center_y_get(void);

namespace {

constexpr uint32_t kRendererEntry = 0x80040E68u;
constexpr uint32_t kRendererRestore = 0x8004279Cu;
constexpr uint32_t kRendererRestoreOpcode = 0x8FBF016Cu;

constexpr uint32_t kCurrentHealth = 0x80077660u;
constexpr uint32_t kMaximumHealth = 0x80077664u;
constexpr uint32_t kSceneMode = 0x8007145Cu;
constexpr uint32_t kGameMode = 0x800716F0u;  /* gp+0x5A4: 3 while the menu's demo plays */
constexpr uint8_t kDemoMode = 3u;
constexpr uint32_t kMenuState = 0x800715FCu;
constexpr uint8_t kMainMenu = 0x18u;  /* the main menu runs in scene mode 0 too */
constexpr uint32_t kPlayerPointer = 0x80071714u;
constexpr uint32_t kAimTargetPointer = 0x80071560u;
constexpr uint32_t kPlayerInactiveOffset = 0x7Fu;

constexpr int kNeutralCenterY = 120;
constexpr int32_t kNeutralCenter16 = kNeutralCenterY << 16;
constexpr double kProjectionH = 160.0;
constexpr double kFullTurnUnits = 256.0;
constexpr double kMaximumPitchUnits = 22.0;
constexpr double kPi = 3.14159265358979323846264338327950288;

struct CenterSite {
    uint32_t pc;
    uint32_t opcode;
};

constexpr std::array<CenterSite, 12> kCenterSites = {{
    {0x8003AD54u, 0x34020078u},
    {0x8003ADE4u, 0x34020078u},
    {0x8003B140u, 0x34020078u},
    {0x8003B1ECu, 0x34020078u},
    {0x8003B764u, 0x34020078u},
    {0x8003B990u, 0x34020078u},
    {0x8003BDA4u, 0x34020078u},
    {0x8003C4B4u, 0x34020078u},
    {0x8003C99Cu, 0x34020078u},
    {0x8003D07Cu, 0x34020078u},
    {0x8003D398u, 0x34030078u},
    {0x8003F6B0u, 0x34020078u},
}};

struct CullSite {
    uint32_t pc;
    uint32_t opcode;
    uint8_t vertical_register;
    uint8_t depth_register;
    bool negative_side;
};

constexpr std::array<CullSite, 6> kCullSites = {{
    {0x8003B900u, 0x0205102Au, 5u, 16u, false},
    {0x8003B90Cu, 0x0202102Au, 5u, 16u, true},
    {0x8003BD14u, 0x0206102Au, 6u, 16u, false},
    {0x8003BD20u, 0x0202102Au, 6u, 16u, true},
    /* This primitive path compares (depth+64), retained in $v1, with Y.
     * Use the unpadded depth in $s4 when calculating only the pitch margin. */
    {0x8003CDB8u, 0x0065102Au, 5u, 20u, false},
    {0x8003CDC4u, 0x0062102Au, 5u, 20u, true},
}};

constexpr uint32_t kNormalProjectileSite = 0x8002E4B8u;
constexpr uint32_t kNormalProjectileOpcode = 0x92640022u;
constexpr uint32_t kPsionicProjectileSite = 0x8002EAF0u;
constexpr uint32_t kPsionicProjectileOpcode = 0x92430020u;
constexpr uint32_t kVelocityXOffset = 0x10u;
constexpr uint32_t kVelocityZOffset = 0x12u;
constexpr uint32_t kVelocityYOffset = 0x14u;

std::atomic<double> g_requested_pitch{0.0};
std::atomic<double> g_effective_pitch{0.0};
std::atomic<double> g_effective_slope{0.0};
std::atomic<int> g_effective_center_y{kNeutralCenterY};
std::atomic<int32_t> g_effective_center16{kNeutralCenter16};
std::atomic<int> g_effective_active{0};
std::atomic<int> g_free_aim{0};
std::atomic<uint32_t> g_live_frames{0};

bool comparator_active() {
    /* The optional differential checker runs a compiled RECORD pass followed
     * by an interpreter REPLAY.  Camera hooks are deliberately outside the
     * architectural model being compared, so they must be neutral in both
     * halves; suppressing replay alone would manufacture a false divergence. */
    return g_ls_mode != 0 || g_ls_replay_active != 0;
}

bool valid_main_ram_pointer(uint32_t pointer, uint32_t trailing_bytes) {
    /* The verified player/entity pointers are KSEG0 aliases of 2 MiB main
     * RAM.  Requiring that exact region avoids host reads through MMIO,
     * scratchpad, BIOS, or a corrupt pointer when the game is transitioning. */
    constexpr uint32_t kRamFirst = 0x80000000u;
    constexpr uint32_t kRamLast = 0x801FFFFFu;
    return pointer >= kRamFirst && pointer <= kRamLast &&
           trailing_bytes <= kRamLast - pointer;
}

bool plausible_live_gameplay() {
    if (comparator_active() || psx_netplay_active() ||
        !psx_mod_game_started()) {
        return false;
    }

    const uint32_t maximum = psx_mod_read_word(kMaximumHealth);
    const uint32_t current = psx_mod_read_word(kCurrentHealth);
    if (maximum == 0u || maximum > 10000u ||
        current == 0u || current > maximum) {
        return false;
    }

    /* Scene mode 1 is the renderer's special non-gameplay presentation path.
     * Inactive player objects retain the retail horizon so cutscenes, death
     * sequences, and level transitions cannot be steered by a stale host pitch.
     * 0x800714E0 is no camera: Continue from mid mission sets it to the mission's end (0x8001AF70). */
    if (psx_mod_read_byte(kSceneMode) == 1u) return false;

    const uint32_t player = psx_mod_read_word(kPlayerPointer);
    const bool present = valid_main_ram_pointer(player, kPlayerInactiveOffset);
    const bool hands_empty = present && psx_mod_read_byte(player + kPlayerInactiveOffset) != 0u;
    return (present && !hands_empty) || disruptor::weapon_switching(hands_empty) || disruptor::unarmed();
}

double clamp_pitch(double pitch) {
    if (!std::isfinite(pitch)) return 0.0;
    return std::clamp(pitch, -kMaximumPitchUnits, kMaximumPitchUnits);
}

/* The horizon in 16.16, as GTE OFY takes it: the world moves by fractions of a row as the mouse tilts. */
int32_t center16_for_pitch(double pitch) {
    const double radians = pitch * (2.0 * kPi / kFullTurnUnits);
    return static_cast<int32_t>(std::llround(
        (static_cast<double>(kNeutralCenterY) + kProjectionH * std::tan(radians)) * 65536.0));
}

/* The whole row the game's own projection literals take. */
int center_for_pitch(double pitch) {
    return static_cast<int>((static_cast<int64_t>(center16_for_pitch(pitch)) + 0x8000) >> 16);
}

double slope_of(int32_t center16) {
    return static_cast<double>(center16 - kNeutralCenter16) / (kProjectionH * 65536.0);
}

void give_gpu_center(double center) {
    if (gpu_geometry_camera_projection_center_y_get() != center)
        gpu_geometry_camera_projection_center_y_set(center);
}

void publish_neutral(CPUState *cpu, bool host) {
    if (comparator_active()) return;

    const int was_active = g_effective_active.load(std::memory_order_relaxed);
    const int32_t old_center16 = g_effective_center16.load(std::memory_order_relaxed);
    g_effective_pitch.store(0.0, std::memory_order_relaxed);
    g_effective_slope.store(0.0, std::memory_order_relaxed);
    g_effective_center_y.store(kNeutralCenterY, std::memory_order_relaxed);
    g_effective_center16.store(kNeutralCenter16, std::memory_order_relaxed);
    g_effective_active.store(0, std::memory_order_release);

    /* The GPU takes a frame's list after the renderer's end: a probe counted no world triangle between entry and end. */
    if (host) give_gpu_center(static_cast<double>(kNeutralCenterY));
    if (!was_active && old_center16 == kNeutralCenter16) return;
    if (cpu) gte_write_ctrl(cpu, 25u, static_cast<uint32_t>(kNeutralCenter16));
}

void publish_pitch(CPUState *cpu, double pitch) {
    if (!cpu || comparator_active()) return;

    pitch = clamp_pitch(pitch);
    const int32_t center16 = center16_for_pitch(pitch);
    if (center16 == kNeutralCenter16) {
        publish_neutral(cpu, true);
        return;
    }
    const int center = center_for_pitch(pitch);
    const double slope = slope_of(center16);

    g_effective_pitch.store(pitch, std::memory_order_relaxed);
    g_effective_slope.store(slope, std::memory_order_relaxed);
    g_effective_center_y.store(center, std::memory_order_relaxed);
    g_effective_center16.store(center16, std::memory_order_relaxed);
    g_effective_active.store(1, std::memory_order_release);

    gte_write_ctrl(cpu, 25u, static_cast<uint32_t>(center16));
    give_gpu_center(static_cast<double>(center16) / 65536.0);
}

void renderer_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kRendererEntry || comparator_active()) return;
    if (!plausible_live_gameplay()) {
        publish_neutral(cpu, true);
        return;
    }
    g_live_frames.fetch_add(1, std::memory_order_relaxed);
    disruptor_mouse_late_tilt();
    publish_pitch(cpu,
                  g_requested_pitch.load(std::memory_order_acquire));
}

const CenterSite *find_center_site(uint32_t pc, uint32_t opcode) {
    for (const CenterSite& site : kCenterSites) {
        if (site.pc == pc && site.opcode == opcode) return &site;
    }
    return nullptr;
}

const CullSite *find_cull_site(uint32_t pc, uint32_t opcode) {
    for (const CullSite& site : kCullSites) {
        if (site.pc == pc && site.opcode == opcode) return &site;
    }
    return nullptr;
}

void apply_center_literal(CPUState *cpu, uint32_t opcode) {
    if (!g_effective_active.load(std::memory_order_acquire) ||
        !plausible_live_gameplay()) {
        return;
    }
    const uint32_t destination = (opcode >> 16u) & 31u;
    if (destination == 0u) return;
    cpu->gpr[destination] = static_cast<uint32_t>(
        g_effective_center_y.load(std::memory_order_relaxed));
}

int64_t signed_register(const CPUState *cpu, uint8_t index) {
    return static_cast<int64_t>(static_cast<int32_t>(cpu->gpr[index]));
}

void widen_vertical_cull(CPUState *cpu, const CullSite& site) {
    if (!g_effective_active.load(std::memory_order_acquire) ||
        !plausible_live_gameplay()) {
        return;
    }
    const double slope =
        std::abs(g_effective_slope.load(std::memory_order_relaxed));
    if (slope == 0.0) return;

    const int64_t depth = signed_register(cpu, site.depth_register);
    const int64_t native_bound = depth +
        (site.depth_register == 20u ? 64 : 0);
    const int64_t margin = static_cast<int64_t>(std::llround(
        std::abs(static_cast<double>(depth)) * slope));
    const int64_t widened_bound = native_bound + margin;
    const int64_t vertical = signed_register(cpu, site.vertical_register);
    const int64_t compared_vertical = site.negative_side ? -vertical : vertical;

    /* Preserve the SLT result contract in $v0: one still means reject. */
    cpu->gpr[2] = widened_bound < compared_vertical ? 1u : 0u;
}

int16_t load_signed_half(uint32_t address) {
    return static_cast<int16_t>(psx_mod_read_half(address));
}

int16_t saturated_half(int64_t value) {
    value = std::clamp<int64_t>(
        value, std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max());
    return static_cast<int16_t>(value);
}

void adjust_projectile(CPUState *cpu) {
    const double projectile_slope = slope_of(center16_for_pitch(g_requested_pitch.load(std::memory_order_acquire)));
    const bool free_aim = g_free_aim.load(std::memory_order_relaxed) != 0;
    if ((projectile_slope == 0.0 && !free_aim) || !plausible_live_gameplay()) return;
    /* The shots of the player in control only: not the demo's, nor any outside play. */
    if (psx_mod_read_byte(kSceneMode) != 0u || psx_mod_read_byte(kGameMode) == kDemoMode ||
        psx_mod_read_byte(kMenuState) == kMainMenu) {
        return;
    }
    /* With a target the game raised the shot to the target's height (0x8002E2CC, 0x8002EA74): free aim takes the look. */
    const bool aimed = psx_mod_read_word(kAimTargetPointer) != 0u;
    if (aimed && !free_aim) return;

    const uint32_t entity = cpu->gpr[16];  /* $s0 at both verified seams. */
    if (!valid_main_ram_pointer(entity, kVelocityYOffset + 1u)) return;

    const int16_t velocity_x = load_signed_half(entity + kVelocityXOffset);
    const int16_t velocity_z = load_signed_half(entity + kVelocityZOffset);
    const int16_t stored_y = load_signed_half(entity + kVelocityYOffset);
    const int16_t velocity_y = aimed ? 0 : stored_y;
    const double horizontal_speed = std::hypot(
        static_cast<double>(velocity_x), static_cast<double>(velocity_z));
    const int64_t pitch_velocity = static_cast<int64_t>(std::llround(
        horizontal_speed * projectile_slope));
    const int16_t adjusted = saturated_half(
        static_cast<int64_t>(velocity_y) + pitch_velocity);
    if (adjusted == stored_y) return;
    psx_mod_write_half(entity + kVelocityYOffset,
                       static_cast<uint16_t>(adjusted));
}

bool player_combat_active() {
    return plausible_live_gameplay() && psx_mod_read_byte(kSceneMode) == 0u &&
           psx_mod_read_byte(kGameMode) != kDemoMode && psx_mod_read_byte(kMenuState) != kMainMenu;
}

void rank_aim_target(CPUState *cpu, uint32_t) {
    const double slope = g_effective_slope.load(std::memory_order_relaxed);
    if (slope == 0.0 || g_free_aim.load(std::memory_order_relaxed) || !player_combat_active()) return;
    const uint32_t body = cpu->gpr[22];
    if (!valid_main_ram_pointer(body, 0x2Bu)) return;
    const int32_t height = static_cast<int32_t>(psx_mod_read_word(body + 0x28u));
    if (height < 2) return;
    const int64_t offset = signed_register(cpu, 5) - std::llround(signed_register(cpu, 20) * slope);
    cpu->gpr[7] = static_cast<uint32_t>(std::max<int64_t>(0, std::abs(offset) - (height / 2 - 1)));
}

double horizontal_distance(double x, double z) {
    x = std::abs(x);
    z = std::abs(z);
    return std::max(x, z) + std::min(x, z) * 0.375;
}

double shot_position(uint32_t projectile, uint32_t field) {
    return static_cast<int32_t>(psx_mod_read_word(projectile + field)) / 8.0;
}

void correct_aim_range(CPUState *cpu, uint32_t pc) {
    if (g_free_aim.load(std::memory_order_relaxed) || !player_combat_active()) return;
    const uint32_t projectile = cpu->gpr[16], target = psx_mod_read_word(kAimTargetPointer);
    if (!valid_main_ram_pointer(projectile, 0x15u) || !valid_main_ram_pointer(target, 0x36u)) return;
    const uint32_t body = psx_mod_read_word(0x80076E3Cu + 4u * psx_mod_read_byte(target + 0x36u));
    if (!valid_main_ram_pointer(body, 0x7Fu)) return;
    const int half_height = static_cast<int32_t>(psx_mod_read_word(body + 0x28u)) / 2;
    const int64_t aim_offset = static_cast<int32_t>(psx_mod_read_word(body + 0x7Cu));
    if (half_height <= 0) return;
    const int vx = load_signed_half(projectile + kVelocityXOffset);
    const int vz = load_signed_half(projectile + kVelocityZOffset);
    const double step_x = (vx >> 6) / 8.0, step_z = (vz >> 6) / 8.0;
    const double step_squared = step_x * step_x + step_z * step_z;
    if (!step_squared) return;
    const double dx = psx_mod_read_half(target + 0x16u) - shot_position(projectile, 4u);
    const double dz = psx_mod_read_half(target + 0x18u) - shot_position(projectile, 8u);
    const double substeps = (dx * step_x + dz * step_z) / step_squared;
    const uint32_t physical_pc = pc & 0x1FFFFFFFu;
    const bool fixed_speed = physical_pc == 0x2DFE0u || physical_pc == 0x2E128u;
    const uint32_t weapon = cpu->gpr[19];
    if (!valid_main_ram_pointer(weapon, 0x22u) || psx_mod_read_byte(weapon + 0x20u) > 8u) return;
    const int weapon_speed = psx_mod_read_byte(weapon + 0x22u);
    const int speed = fixed_speed ? 32 : weapon_speed;
    const double distance = substeps * speed / 2.0;
    if (substeps < 0.0 || distance > 4095) return;
    constexpr uint32_t end_of_table = 4128u;
    const uint32_t old_depth = cpu->gpr[2];
    const uint32_t new_depth = static_cast<uint32_t>(std::lround(std::max(1.0, distance))) + 32u;
    if (old_depth >= end_of_table) return;
    int64_t x = static_cast<int32_t>(psx_mod_read_word(projectile + 4u));
    int64_t z = static_cast<int32_t>(psx_mod_read_word(projectile + 8u));
    const int64_t y = static_cast<int32_t>(psx_mod_read_word(projectile + 12u));
    const int target_x = psx_mod_read_half(target + 0x16u), target_z = psx_mod_read_half(target + 0x18u);
    const int target_y = psx_mod_read_half(target + 0x1Au);
    const int radius = static_cast<int32_t>(psx_mod_read_word(body + 0x24u));
    const int64_t center_rise = target_y - (y >> 3);
    const int64_t retail_rise = center_rise + aim_offset;
    const int64_t rise = std::abs(aim_offset) < half_height ? retail_rise : center_rise;
    const auto raw_velocity = [speed](int64_t height, uint32_t depth) {
        return (height * speed * psx_mod_read_half(0x80057E58u + 2u * depth)) >> 8;
    };
    const int64_t new_velocity = raw_velocity(rise, new_depth);
    const bool new_depth_valid = new_velocity >= INT16_MIN && new_velocity <= INT16_MAX;
    const auto vertical_step = [weapon_speed](int64_t raw) {
        const int velocity = static_cast<int16_t>(raw);
        return std::clamp(velocity, -160 * weapon_speed, 160 * weapon_speed) >> 6;
    };
    const int old_step = vertical_step(raw_velocity(retail_rise, old_depth)), new_step = vertical_step(new_velocity);
    if (rise == 0 && old_step == 0) return;
    const int timer = psx_mod_read_byte(weapon + 0x21u);
    const int lifetime = timer ? timer : 256;
    uint32_t selected = end_of_table;
    for (int step = 1; step < lifetime; ++step) {
        x += vx >> 6;
        z += vz >> 6;
        if (horizontal_distance((x >> 3) - target_x, (z >> 3) - target_z) >= radius) continue;
        if (std::abs(((y + step * old_step) >> 3) - target_y) < half_height) return;
        if (new_depth_valid && std::abs(((y + step * new_step) >> 3) - target_y) < half_height) {
            selected = new_depth;
        } else if (selected == end_of_table) {
            // Find an unwrapped retail slope that reaches the open height band on this substep.
            uint32_t first = 33u, last = end_of_table;
            while (first < last) {
                const uint32_t middle = first + (last - first) / 2u;
                const auto velocity = raw_velocity(rise, middle);
                const auto error = ((y + step * vertical_step(velocity)) >> 3) - target_y;
                if (velocity < INT16_MIN || velocity > INT16_MAX || (rise < 0 ? -error : error) >= half_height)
                    first = middle + 1u;
                else
                    last = middle;
            }
            if (first < end_of_table &&
                std::abs(((y + step * vertical_step(raw_velocity(rise, first))) >> 3) - target_y) < half_height)
                selected = first;
        }
    }
    if (selected == end_of_table) return;
    cpu->gpr[2] = selected;
    if (rise != retail_rise) {
        // The variable-speed paths have already multiplied the rise into LO.
        if (fixed_speed) cpu->gpr[4] += static_cast<uint32_t>(rise - retail_rise);
        else cpu->lo = static_cast<uint32_t>(rise * speed);
    }
}

void limit_drain_flight_time(CPUState *cpu, uint32_t) {
    if (player_combat_active()) cpu->gpr[3] = std::min(cpu->gpr[3], cpu->gpr[2]);
}

void check_drain_crossing(CPUState *cpu, uint32_t pc) {
    const bool horizontal = (pc & 0x1FFFFFFFu) == 0x22918u;
    if (horizontal) {
        const auto gap = signed_register(cpu, 4) - signed_register(cpu, 2);
        // A signed halfword velocity moves at most 64 world units per axis, or 88 in the collision metric.
        if (gap < 0 || gap > 88) return;
    } else if (std::abs(signed_register(cpu, 4) - signed_register(cpu, 5)) < signed_register(cpu, 3)) {
        return;
    }
    const uint32_t projectile = cpu->gpr[17], weapon = cpu->gpr[18], target = cpu->gpr[16], body = cpu->gpr[6];
    if (!valid_main_ram_pointer(projectile, 0x1Du) || !valid_main_ram_pointer(weapon, 0x20u) ||
        !valid_main_ram_pointer(target, 0x1Bu) || !valid_main_ram_pointer(body, 0x27u) ||
        psx_mod_read_byte(projectile + 0x1Au) != 14u || psx_mod_read_byte(weapon + 0x20u) != 11u ||
        !player_combat_active()) return;
    const std::array<int, 3> step{load_signed_half(projectile + kVelocityXOffset) >> 6,
                                  load_signed_half(projectile + kVelocityZOffset) >> 6,
                                  load_signed_half(projectile + kVelocityYOffset) >> 6};
    const int64_t center = psx_mod_read_half(target + 0x1Au);
    const int64_t half = horizontal ? signed_register(cpu, 3) >> 1 : signed_register(cpu, 3);
    if (half < 1) return;
    const int64_t end = static_cast<int32_t>(psx_mod_read_word(projectile + 12u)) >> 3;
    const std::array<int64_t, 3> start{
        static_cast<int64_t>(static_cast<int32_t>(psx_mod_read_word(projectile + 4u))) - step[0],
        static_cast<int64_t>(static_cast<int32_t>(psx_mod_read_word(projectile + 8u))) - step[1],
        static_cast<int64_t>(static_cast<int32_t>(psx_mod_read_word(projectile + 12u))) - step[2]};
    if (std::abs(end - center) >= half && std::abs((start[2] >> 3) - center) >= half &&
        ((start[2] >> 3) < center) == (end < center)) return;
    const int target_x = psx_mod_read_half(target + 0x16u), target_z = psx_mod_read_half(target + 0x18u);
    const int radius = static_cast<int32_t>(psx_mod_read_word(body + 0x24u));
    const auto intersects = [&](int numerator, int denominator, bool after) {
        const auto cell = [&](size_t axis) {
            const int64_t fixed = start[axis] * denominator + static_cast<int64_t>(step[axis]) * numerator -
                                  (after && step[axis] < 0);
            const int scale = 8 * denominator;
            return (fixed - (fixed < 0 ? scale - 1 : 0)) / scale;
        };
        const int64_t height = cell(2);
        if (std::abs(height - center) >= half) return false;
        const int64_t distance = static_cast<int64_t>(horizontal_distance(cell(0) - target_x, cell(1) - target_z));
        if (distance >= radius) return false;
        cpu->gpr[4] = static_cast<uint32_t>(horizontal ? distance : height);
        return true;
    };
    if (intersects(0, 1, false)) return;
    // At simultaneous grid crossings the exact cell can differ from both adjacent cells.
    for (size_t axis = 0; axis < step.size(); ++axis) {
        const int distance = std::abs(step[axis]);
        const int phase = static_cast<int>(static_cast<uint64_t>(start[axis]) & 7u);
        if (!distance) continue;
        for (int crossed = step[axis] > 0 ? 8 - phase : phase; crossed <= distance; crossed += 8) {
            if (intersects(crossed, distance, false) || (crossed < distance && intersects(crossed, distance, true)))
                return;
        }
    }
}

PSX_MOD_CONSTRUCTOR(register_disruptor_vertical_camera) {
    constexpr char plugin[] = "disruptor.vertical_camera.renderer";
    bool registered = psx_mod_register_instruction_plugin(plugin, 0x8003CE3Cu, 0x00071880u, rank_aim_target) != 0;
    for (uint32_t pc : {0x8002DFE0u, 0x8002E128u, 0x8002E31Cu, 0x8002E48Cu})
        registered &= psx_mod_register_instruction_plugin(plugin, pc, 0x00021040u, correct_aim_range) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x8002EB58u, 0x00431023u, limit_drain_flight_time) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80022918u, 0x0082102Au, check_drain_crossing) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80022934u, 0x0044102Au, check_drain_crossing) != 0;
    if (!registered) {
        std::fprintf(stderr, "disruptor: failed to register combat hooks\n");
    }
    if (!psx_mod_register_function_entry_plugin(
            "disruptor.vertical_camera.renderer", kRendererEntry,
            renderer_entry)) {
        std::fprintf(stderr,
                     "disruptor: failed to register vertical-camera renderer hook\n");
    }
}

}  // namespace

extern "C" void disruptor_vertical_camera_set_requested_pitch(
        double pitch_units) {
    if (comparator_active() || !std::isfinite(pitch_units)) return;
    if (psx_netplay_active() && pitch_units != 0.0) return;
    g_requested_pitch.store(clamp_pitch(pitch_units),
                            std::memory_order_release);
}

extern "C" void disruptor_vertical_camera_set_free_aim(int free_aim) {
    g_free_aim.store(free_aim != 0 ? 1 : 0, std::memory_order_relaxed);
}

extern "C" void disruptor_vertical_camera_recenter(void) {
    if (comparator_active()) return;
    g_requested_pitch.store(0.0, std::memory_order_release);
}

extern "C" double disruptor_vertical_camera_requested_pitch(void) {
    return g_requested_pitch.load(std::memory_order_acquire);
}

extern "C" double disruptor_vertical_camera_effective_pitch(void) {
    return g_effective_pitch.load(std::memory_order_acquire);
}

extern "C" int disruptor_vertical_camera_effective_center_y(void) {
    return g_effective_center_y.load(std::memory_order_acquire);
}

extern "C" int disruptor_vertical_camera_projection_row(void) {
    if (comparator_active() || !g_effective_active.load(std::memory_order_acquire) || !plausible_live_gameplay())
        return kNeutralCenterY;
    return g_effective_center_y.load(std::memory_order_relaxed);
}

extern "C" int32_t disruptor_vertical_camera_projection_row16(void) {
    if (comparator_active() || !g_effective_active.load(std::memory_order_acquire) || !plausible_live_gameplay())
        return kNeutralCenter16;
    return g_effective_center16.load(std::memory_order_relaxed);
}

extern "C" double disruptor_vertical_camera_effective_slope(void) {
    return g_effective_slope.load(std::memory_order_acquire);
}

extern "C" int disruptor_vertical_camera_active(void) {
    return g_effective_active.load(std::memory_order_acquire);
}

extern "C" uint32_t disruptor_vertical_camera_live_frames(void) {
    return g_live_frames.load(std::memory_order_relaxed);
}

extern "C" int disruptor_vertical_camera_input_allowed(void) {
    return plausible_live_gameplay() ? 1 : 0;
}

extern "C" void disruptor_vertical_camera_instruction_hook(
        CPUState *cpu, uint32_t pc, uint32_t instruction, int phase) {
    if (!cpu || comparator_active()) return;

    if (phase == DISRUPTOR_VERTICAL_CAMERA_BEFORE_INSTRUCTION) return;
    if (phase != DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION) return;

    if (pc == kRendererRestore && instruction == kRendererRestoreOpcode) {
        publish_neutral(cpu, false);
        return;
    }

    if (find_center_site(pc, instruction)) {
        apply_center_literal(cpu, instruction);
        return;
    }
    if (const CullSite *site = find_cull_site(pc, instruction)) {
        widen_vertical_cull(cpu, *site);
        return;
    }
    if ((pc == kNormalProjectileSite &&
         instruction == kNormalProjectileOpcode) ||
        (pc == kPsionicProjectileSite &&
         instruction == kPsionicProjectileOpcode)) {
        adjust_projectile(cpu);
    }
}
