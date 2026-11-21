#include "cpu_state.h"
#include "mod_plugins.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
PSXModFunctionEntryCallback g_renderer_callback = nullptr;
std::uint32_t g_registered_address = 0;
std::array<PSXModFunctionEntryCallback, 8> g_combat_callbacks{};
bool g_game_started = false;
bool g_netplay = false;
int g_gte_writes = 0;
int g_projection_writes = 0;
double g_projection_center = 120.0;
std::vector<std::uint32_t> g_guest_writes;
int g_failures = 0;

std::size_t physical(std::uint32_t address) {
    return static_cast<std::size_t>(address & 0x001FFFFFu);
}

void seed_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[physical(address)] = value;
}

void seed_half(std::uint32_t address, std::uint16_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
}

void seed_word(std::uint32_t address, std::uint32_t value) {
    const std::size_t p = physical(address);
    g_ram[p + 0] = static_cast<std::uint8_t>(value >> 0);
    g_ram[p + 1] = static_cast<std::uint8_t>(value >> 8);
    g_ram[p + 2] = static_cast<std::uint8_t>(value >> 16);
    g_ram[p + 3] = static_cast<std::uint8_t>(value >> 24);
}

std::uint16_t load_half(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint16_t>(g_ram[p + 0]) |
           (static_cast<std::uint16_t>(g_ram[p + 1]) << 8);
}

std::uint32_t load_word(std::uint32_t address) {
    const std::size_t p = physical(address);
    return static_cast<std::uint32_t>(g_ram[p + 0]) |
           (static_cast<std::uint32_t>(g_ram[p + 1]) << 8) |
           (static_cast<std::uint32_t>(g_ram[p + 2]) << 16) |
           (static_cast<std::uint32_t>(g_ram[p + 3]) << 24);
}

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

extern "C" {

int g_ls_replay_active = 0;
int g_ls_mode = 0;

int psx_mod_register_function_entry_plugin(
        const char *id, std::uint32_t address,
        PSXModFunctionEntryCallback callback) {
    if (!id || std::string(id) != "disruptor.vertical_camera.renderer" ||
        !address || !callback || g_renderer_callback) {
        return 0;
    }
    g_registered_address = address;
    g_renderer_callback = callback;
    return 1;
}

int psx_mod_game_started(void) {
    return g_game_started ? 1 : 0;
}

int psx_mod_register_instruction_plugin(const char *id, std::uint32_t address, std::uint32_t opcode,
                                        PSXModFunctionEntryCallback callback) {
    constexpr std::array<std::uint32_t, 8> sites{0x8003CE3Cu, 0x8002EB58u, 0x80022934u, 0x8002DFE0u,
                                                 0x8002E128u, 0x8002E31Cu, 0x8002E48Cu, 0x80022918u};
    constexpr std::array<std::uint32_t, 8> opcodes{0x00071880u, 0x00431023u, 0x0044102Au, 0x00021040u,
                                                   0x00021040u, 0x00021040u, 0x00021040u, 0x0082102Au};
    for (size_t i = 0; i < sites.size(); ++i) {
        if (address == sites[i] && opcode == opcodes[i] && std::string(id) == "disruptor.vertical_camera.renderer") {
            g_combat_callbacks[i] = callback;
            return 1;
        }
    }
    return 0;
}

int psx_netplay_active(void) {
    return g_netplay ? 1 : 0;
}

std::uint8_t psx_mod_read_byte(std::uint32_t address) {
    return g_ram[physical(address)];
}

void psx_mod_write_byte(std::uint32_t address, std::uint8_t value) {
    g_guest_writes.push_back(address);
    seed_byte(address, value);
}

std::uint16_t psx_mod_read_half(std::uint32_t address) {
    return load_half(address);
}

void psx_mod_write_half(std::uint32_t address, std::uint16_t value) {
    g_guest_writes.push_back(address);
    seed_half(address, value);
}

std::uint32_t psx_mod_read_word(std::uint32_t address) {
    return load_word(address);
}

void psx_mod_write_word(std::uint32_t address, std::uint32_t value) {
    g_guest_writes.push_back(address);
    seed_word(address, value);
}

void gte_write_ctrl(CPUState *cpu, std::uint8_t reg, std::uint32_t value) {
    ++g_gte_writes;
    if (cpu && reg < 32u) cpu->gte_ctrl[reg] = value;
}

void gpu_geometry_camera_projection_center_y_set(double center_y) {
    ++g_projection_writes;
    g_projection_center = center_y;
}

double gpu_geometry_camera_projection_center_y_get(void) { return g_projection_center; }

int g_late_tilts = 0;
double g_late_pitch = 0.0;
bool g_late_sets = false;
void disruptor_vertical_camera_set_requested_pitch(double pitch_units);
void disruptor_mouse_late_tilt(void) {
    ++g_late_tilts;
    if (g_late_sets) disruptor_vertical_camera_set_requested_pitch(g_late_pitch);
}

}  // extern "C"

// Include the implementation to exercise the version-pinned internal guards.
#include "../../src/game/input/vertical_camera.cpp"

namespace {

constexpr std::uint32_t kTestPlayer = 0x80010000u;
constexpr std::uint32_t kWeaponLoad = 0x80071324u, kWantedWeapon = 0x80077627u, kHeldWeapon = 0x80071484u;
constexpr std::uint32_t kTestProjectile = 0x80012000u;
constexpr std::uint32_t kMidMissionContinue = 0x800714E0u;

void reset_state() {
    g_ram.fill(0);
    g_game_started = false;
    g_netplay = false;
    g_ls_replay_active = 0;
    g_ls_mode = 0;
    g_gte_writes = 0;
    g_projection_writes = 0;
    g_projection_center = 120.0;
    g_guest_writes.clear();
    g_requested_pitch.store(0.0);
    g_effective_pitch.store(0.0);
    g_effective_slope.store(0.0);
    g_effective_center_y.store(kNeutralCenterY);
    g_effective_center16.store(kNeutralCenter16);
    g_effective_active.store(0);
    g_free_aim.store(0);
}

void seed_live_game() {
    g_game_started = true;
    seed_word(kMaximumHealth, 100u);
    seed_word(kCurrentHealth, 80u);
    seed_byte(kSceneMode, 0u);
    seed_word(kPlayerPointer, kTestPlayer);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 0u);
    seed_word(kAimTargetPointer, 0u);
}

void activate(CPUState *cpu, double pitch) {
    disruptor_vertical_camera_set_requested_pitch(pitch);
    g_renderer_callback(cpu, kRendererEntry);
}

void test_registration_and_projection() {
    expect(g_renderer_callback != nullptr, "renderer callback registered");
    expect(g_registered_address == kRendererEntry,
           "renderer callback pinned to main body entry");

    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, 10.0);
    const int expected_center = center_for_pitch(10.0);
    expect(disruptor_vertical_camera_active() == 1,
           "live non-zero pitch activates");
    expect(disruptor_vertical_camera_effective_center_y() == expected_center,
           "projection center derives from pitch");
    const std::int32_t expected_center16 = center16_for_pitch(10.0);
    expect(std::abs(disruptor_vertical_camera_effective_slope() - std::tan(10.0 * 2.0 * kPi / 256.0)) < 1e-6,
           "aim slope is the tilt's own tangent, not a whole row's");
    expect(cpu.gte_ctrl[25] == static_cast<std::uint32_t>(expected_center16) &&
               expected_center16 != expected_center << 16 && (expected_center16 + 0x8000) >> 16 == expected_center,
           "renderer entry writes GTE OFY with the horizon's fraction, the game's literal its nearest row");
    expect(g_projection_center == expected_center16 / 65536.0,
           "renderer entry publishes the fractional presentation center");
    expect(disruptor_vertical_camera_projection_row16() == expected_center16 &&
               disruptor_vertical_camera_projection_row() == expected_center,
           "sprite rows take the fraction, packet checks the whole row");

    cpu.gpr[2] = 120u;  // vanilla LI result
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == static_cast<std::uint32_t>(expected_center),
           "verified CPU projection literal follows active center");

    cpu.gpr[3] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003D398u, 0x34030078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[3] == static_cast<std::uint32_t>(expected_center),
           "LI destination is decoded rather than assumed");

    cpu.gpr[2] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020079u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 120u, "opcode mismatch is an exact no-op");

    g_netplay = true;
    cpu.gpr[2] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 120u,
           "mid-frame netplay transition blocks CPU projection mutation");
}

void test_restore_and_zero_identity() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, -12.0);
    const double requested = disruptor_vertical_camera_requested_pitch();
    disruptor_vertical_camera_instruction_hook(
        &cpu, kRendererRestore, kRendererRestoreOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(!disruptor_vertical_camera_active() &&
               disruptor_vertical_camera_effective_center_y() == 120,
           "renderer exit restores neutral effective projection");
    expect(cpu.gte_ctrl[25] == (120u << 16) && g_projection_center == center16_for_pitch(-12.0) / 65536.0,
           "renderer exit gives the game its GTE back and leaves the GPU the frame's horizon for its list");
    expect(disruptor_vertical_camera_requested_pitch() == requested,
           "temporary renderer restore preserves requested pitch");

    disruptor_vertical_camera_recenter();
    g_renderer_callback(&cpu, kRendererEntry);
    expect(g_projection_center == 120.0, "the next frame at zero pitch gives the GPU the level horizon");
    const int writes_before = g_gte_writes + g_projection_writes;
    g_renderer_callback(&cpu, kRendererEntry);
    expect(g_gte_writes + g_projection_writes == writes_before,
           "already-neutral zero pitch performs no projection writes");

    cpu.gpr[2] = 120u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003AD54u, 0x34020078u,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 120u, "zero pitch preserves vanilla LI exactly");
}

void test_gameplay_guards() {
    reset_state();
    CPUState cpu{};
    disruptor_vertical_camera_set_requested_pitch(10.0);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "renderer fails closed before live gameplay");

    seed_live_game();
    expect(disruptor_vertical_camera_input_allowed() == 1,
           "verified live gameplay accepts vertical input");
    seed_byte(kMidMissionContinue, 1u);
    expect(disruptor_vertical_camera_input_allowed() == 1,
           "after Continue from mid mission the mouse still tilts");
    g_renderer_callback(&cpu, kRendererEntry);
    expect(disruptor_vertical_camera_active(),
           "after Continue from mid mission the pitch still applies");

    seed_byte(kSceneMode, 1u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "special renderer mode suspends pitch");

    seed_byte(kSceneMode, 0u);
    seed_byte(kTestPlayer + kPlayerInactiveOffset, 1u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "inactive player object suspends pitch");

    seed_byte(kWeaponLoad, 1u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(disruptor_vertical_camera_active(), "a weapon's load keeps the pitch while the player is marked");
    seed_word(kPlayerPointer, 0u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(disruptor_vertical_camera_active(), "a weapon's load keeps the pitch without the player pointer");
    seed_word(kPlayerPointer, kTestPlayer);
    seed_byte(kWeaponLoad, 2u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(), "another load keeps the retail horizon");
    seed_byte(kWeaponLoad, 0u);
    seed_byte(kWantedWeapon, 3u);
    seed_byte(kHeldWeapon, 1u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(disruptor_vertical_camera_active(), "empty hands with another weapon wanted are a switch before its load");
    seed_byte(kWeaponLoad, 2u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(), "not while another load runs");
    seed_byte(kWeaponLoad, 0u);
    seed_byte(kHeldWeapon, 3u);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(), "empty hands with the wanted weapon held are not");

    seed_byte(kTestPlayer + kPlayerInactiveOffset, 0u);
    g_netplay = true;
    const double before = disruptor_vertical_camera_requested_pitch();
    disruptor_vertical_camera_set_requested_pitch(20.0);
    expect(disruptor_vertical_camera_requested_pitch() == before,
           "netplay rejects non-zero input mutation");
    g_renderer_callback(&cpu, kRendererEntry);
    expect(!disruptor_vertical_camera_active(),
           "netplay never activates vertical camera");
}

void test_cull_widening() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, 22.0);

    cpu.gpr[16] = 100u;  // depth/native vertical bound
    cpu.gpr[5] = 130u;   // +Y: vanilla 100 < 130 rejects
    cpu.gpr[2] = 1u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B900u, 0x0205102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 0u,
           "positive vertical plane is conservatively widened");

    cpu.gpr[5] = static_cast<std::uint32_t>(-130);
    cpu.gpr[2] = 1u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B90Cu, 0x0202102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 0u,
           "negative vertical plane is conservatively widened");

    cpu.gpr[16] = static_cast<std::uint32_t>(-999);  // unrelated in this path
    cpu.gpr[20] = 100u;
    cpu.gpr[5] = 190u;  // vanilla (100+64) < 190 rejects
    cpu.gpr[2] = 1u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003CDB8u, 0x0065102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(cpu.gpr[2] == 0u,
           "padded primitive plane uses its verified $s4 depth");
}

void seed_projectile(std::int16_t x, std::int16_t z, std::int16_t y) {
    seed_half(kTestProjectile + kVelocityXOffset,
              static_cast<std::uint16_t>(x));
    seed_half(kTestProjectile + kVelocityZOffset,
              static_cast<std::uint16_t>(z));
    seed_half(kTestProjectile + kVelocityYOffset,
              static_cast<std::uint16_t>(y));
}

std::int16_t projectile_y() {
    return static_cast<std::int16_t>(
        load_half(kTestProjectile + kVelocityYOffset));
}

void test_projectile_alignment() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    cpu.gpr[16] = kTestProjectile;
    disruptor_vertical_camera_set_requested_pitch(10.0);
    seed_projectile(300, 400, 10);
    const double slope = slope_of(center16_for_pitch(10.0));
    const std::int16_t expected = static_cast<std::int16_t>(
        10 + std::lround(500.0 * slope));
    disruptor_vertical_camera_instruction_hook(
        &cpu, kNormalProjectileSite, kNormalProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == expected,
           "normal projectile follows the exact visible horizon slope");

    seed_projectile(300, 400, 10);
    seed_word(kAimTargetPointer, 0x80013000u);
    g_guest_writes.clear();
    disruptor_vertical_camera_instruction_hook(
        &cpu, kPsionicProjectileSite, kPsionicProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 10 && g_guest_writes.empty(),
           "native target-assisted aim remains authoritative");

    seed_word(kAimTargetPointer, 0u);
    disruptor_vertical_camera_set_requested_pitch(22.0);
    seed_projectile(32767, 32767, 30000);
    disruptor_vertical_camera_instruction_hook(
        &cpu, kPsionicProjectileSite, kPsionicProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == std::numeric_limits<std::int16_t>::max(),
           "psionic vertical velocity saturates instead of wrapping");
}

void test_live_frames_count_drawn_play() {
    reset_state();
    CPUState cpu{};
    const std::uint32_t before = disruptor_vertical_camera_live_frames();
    g_renderer_callback(&cpu, kRendererEntry);
    expect(disruptor_vertical_camera_live_frames() == before, "a frame outside live play is not counted");
    seed_live_game();
    g_renderer_callback(&cpu, kRendererEntry);
    expect(disruptor_vertical_camera_live_frames() == before + 1, "a frame the renderer draws in live play is");
}

void test_the_renderer_reads_the_mouse_again() {
    reset_state();
    CPUState cpu{};
    const int before = g_late_tilts;
    g_renderer_callback(&cpu, kRendererEntry);
    expect(g_late_tilts == before, "outside live play the renderer's entry asks the mouse nothing");
    seed_live_game();
    disruptor_vertical_camera_set_requested_pitch(4.0);
    g_late_pitch = 9.0;
    g_late_sets = true;
    g_renderer_callback(&cpu, kRendererEntry);
    g_late_sets = false;
    expect(g_late_tilts == before + 1 && disruptor_vertical_camera_effective_pitch() == 9.0 &&
               cpu.gte_ctrl[25] == static_cast<std::uint32_t>(center16_for_pitch(9.0)),
           "in play it reads the mouse once more and draws the frame with that look, not the VBlank's");
}

void test_free_aim_takes_the_look() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    cpu.gpr[16] = kTestProjectile;
    disruptor_vertical_camera_set_requested_pitch(10.0);
    seed_word(kAimTargetPointer, 0x80013000u);
    seed_projectile(300, 400, 90);
    disruptor_vertical_camera_instruction_hook(&cpu, kNormalProjectileSite, kNormalProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 90, "with Auto-aim a shot keeps the height the game aimed it at");
    disruptor_vertical_camera_set_free_aim(1);
    disruptor_vertical_camera_instruction_hook(&cpu, kNormalProjectileSite, kNormalProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    const std::int16_t looked = static_cast<std::int16_t>(std::lround(500.0 * slope_of(center16_for_pitch(10.0))));
    expect(projectile_y() == looked, "free aim drops the game's height and takes the look's slope");
    disruptor_vertical_camera_set_requested_pitch(0.0);
    seed_projectile(300, 400, 90);
    disruptor_vertical_camera_instruction_hook(&cpu, kPsionicProjectileSite, kPsionicProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 0, "looking straight, a free shot flies level past the target the game picked");
    seed_byte(kGameMode, kDemoMode);
    seed_projectile(300, 400, 90);
    disruptor_vertical_camera_instruction_hook(&cpu, kNormalProjectileSite, kNormalProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 90, "the menu's demo keeps the game's aim");
    seed_byte(kGameMode, 1u);
    seed_byte(kSceneMode, 4u);
    disruptor_vertical_camera_instruction_hook(&cpu, kNormalProjectileSite, kNormalProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 90, "and so does any scene but play");
    seed_byte(kSceneMode, 0u);
    seed_byte(kMenuState, kMainMenu);
    disruptor_vertical_camera_instruction_hook(&cpu, kNormalProjectileSite, kNormalProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 90, "and the main menu, which runs in scene mode 0");
    seed_byte(kMenuState, 0u);
    g_netplay = true;
    seed_projectile(300, 400, 90);
    disruptor_vertical_camera_instruction_hook(&cpu, kNormalProjectileSite, kNormalProjectileOpcode,
                                               DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    expect(projectile_y() == 90, "a network game keeps the game's aim");
    g_netplay = false;
    disruptor_vertical_camera_set_free_aim(0);
}

void test_target_and_drain() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    for (auto callback : g_combat_callbacks) {
        expect(callback != nullptr, "combat hook registered at its retail instruction");
        if (!callback) return;
    }
    cpu.gpr[21] = 0x80013000u;
    seed_byte(cpu.gpr[21] + 0x36u, 3);
    seed_word(0x80076E3Cu + 12u, 0x80014000u);
    seed_word(0x80014028u, 80u);
    cpu.gpr[20] = 400;
    cpu.gpr[22] = 0x80014000u;
    cpu.gpr[7] = 39;
    g_combat_callbacks[0](&cpu, 0x8003CE3Cu);
    expect(cpu.gpr[7] == 39, "level sight keeps retail target ranking");
    activate(&cpu, 10.0);
    cpu.gpr[5] = 100;
    cpu.gpr[7] = 100;
    g_combat_callbacks[0](&cpu, 0x8003CE3Cu);
    expect(cpu.gpr[7] == 0, "tilted sight crosses this enemy");
    cpu.gpr[5] = 0;
    g_combat_callbacks[0](&cpu, 0x8003CE3Cu);
    expect(cpu.gpr[7] > 50, "enemy at the original horizon ranks behind it");
    cpu.gpr[2] = 30;
    cpu.gpr[3] = 40;
    g_combat_callbacks[1](&cpu, 0x8002EB58u);
    expect(cpu.gpr[2] - cpu.gpr[3] == 0, "distant Drain keeps homing until expiry");
    cpu.gpr[3] = 10;
    g_combat_callbacks[1](&cpu, 0x8002EB58u);
    expect(cpu.gpr[2] - cpu.gpr[3] == 20, "ordinary Drain keeps its native homing window");
    cpu.gpr[16] = kTestProjectile;
    seed_word(kAimTargetPointer, cpu.gpr[21]);
    seed_half(cpu.gpr[21] + 0x16u, 200u);
    seed_half(cpu.gpr[21] + 0x1Au, 100u);
    seed_word(0x80014024u, 20u);
    seed_word(0x80014028u, 20u);
    seed_projectile(8192, 0, 0);
    for (unsigned distance = 1; distance < 4096; ++distance)
        seed_half(0x80057E98u + 2u * distance, static_cast<uint16_t>(std::min(65535u, 65536u / distance)));
    seed_half(0x80057E58u + 2u * 132u, 655u);
    seed_half(0x80057E58u + 2u * 232u, 327u);
    cpu.gpr[19] = 0x80015000u;
    seed_byte(cpu.gpr[19] + 0x21u, 200u);
    seed_byte(cpu.gpr[19] + 0x22u, 32u);
    activate(&cpu, 0.0);
    for (size_t index = 3; index < 7; ++index) {
        constexpr uint32_t sites[]{0x8002DFE0u, 0x8002E128u, 0x8002E31Cu, 0x8002E48Cu};
        const bool fixed = index < 5;
        seed_byte(cpu.gpr[19] + 0x22u, fixed ? 40u : 24u);
        const uint32_t corrected = fixed ? 232u : 182u;
        seed_half(0x80057E58u + 2u * corrected, fixed ? 327u : 436u);
        seed_half(0x80057E58u + 2u * 600u, 115u);
        cpu.gpr[2] = 600u;
        g_combat_callbacks[index](&cpu, sites[index - 3] & 0x1FFFFFFFu);
        expect(cpu.gpr[2] == corrected, "level-view lock corrects the range for this weapon's speed");
        g_combat_callbacks[index](&cpu, sites[index - 3]);
        expect(cpu.gpr[2] == corrected, "an on-target trajectory retains its range");
    }
    seed_byte(cpu.gpr[19] + 0x22u, 32u);
    seed_half(cpu.gpr[21] + 0x16u, 197u);
    seed_word(0x80014024u, 10u);
    seed_word(0x80014028u, 20u);
    seed_word(0x8001407Cu, static_cast<uint32_t>(-9));
    seed_half(0x80057E58u + 2u * 192u, 409u);
    seed_half(0x80057E58u + 2u * 229u, 332u);
    seed_word(0x80014028u, 120u);
    cpu.gpr[2] = 192u;
    g_combat_callbacks[3](&cpu, 0x8002DFE0u);
    expect(cpu.gpr[2] == 192u, "prefer the original index when both shots hit");
    seed_word(0x80014028u, 20u);
    cpu.gpr[2] = 192u;
    g_combat_callbacks[3](&cpu, 0x8002DFE0u);
    expect(cpu.gpr[2] == 192u, "keep the retail hit at X=192 even when closest approach predicts a miss");
    for (uint32_t missed_index : {600u, 229u}) {
        cpu.gpr[2] = missed_index;
        g_combat_callbacks[3](&cpu, 0x8002DFE0u);
        const auto reciprocal = load_half(0x80057E58u + 2u * cpu.gpr[2]);
        const int impact_y = 12 * ((91 * 32 * reciprocal >> 8) >> 6) >> 3;
        expect(impact_y > 90 && impact_y < 110, "find a reachable substep for a stale or nominal index that misses");
    }
    seed_half(cpu.gpr[21] + 0x16u, 208u);
    seed_word(0x80014024u, 1u);
    seed_word(0x8001407Cu, 0u);
    seed_projectile(8192, 63, 0);
    seed_half(0x80057E58u + 2u * 240u, 315u);
    cpu.gpr[2] = 132u;
    g_combat_callbacks[3](&cpu, 0x8002DFE0u);
    expect(cpu.gpr[2] == 240u, "substep rounding removes lateral drift");
    seed_half(cpu.gpr[21] + 0x16u, 200u);
    seed_word(0x80014024u, 60u);
    seed_word(0x80014028u, 10u);
    seed_byte(cpu.gpr[19] + 0x21u, 15u);
    cpu.gpr[2] = 288u;
    seed_half(0x80057E58u + 2u * 288u, 256u);
    g_combat_callbacks[3](&cpu, 0x8002DFE0u);
    expect(cpu.gpr[2] == 232u, "a retail hit after expiry does not veto an earlier hit");
    seed_word(0x80014024u, 20u);
    cpu.gpr[16] = cpu.gpr[21];
    cpu.gpr[17] = kTestProjectile;
    cpu.gpr[18] = 0x80015000u;
    cpu.gpr[6] = 0x80014000u;
    seed_byte(kTestProjectile + 0x1Au, 14u);
    seed_byte(cpu.gpr[18] + 0x20u, 11u);
    seed_word(kTestProjectile + 4u, 200u * 8u);
    seed_word(kTestProjectile + 12u, 141u * 8u);
    seed_projectile(0, 0, 4096);
    cpu.gpr[3] = 40;
    cpu.gpr[4] = 141;
    cpu.gpr[5] = 100;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] > 60u && cpu.gpr[4] < 140u, "uphill Drain crosses the open top of the enemy cylinder");
    seed_word(kTestProjectile + 12u, 59u * 8u);
    seed_projectile(0, 0, -4096);
    cpu.gpr[4] = 59u;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] > 60u && cpu.gpr[4] < 140u, "downhill Drain crosses the bottom");
    disruptor_vertical_camera_set_free_aim(1);
    cpu.gpr[2] = 200;
    cpu.gpr[3] = 210;
    g_combat_callbacks[1](&cpu, 0x8002EB58u);
    expect(cpu.gpr[2] - cpu.gpr[3] == 0, "homing Drain retains its distant window with free aim");
    cpu.gpr[3] = 40;
    cpu.gpr[4] = 59u;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] > 60u && cpu.gpr[4] < 140u, "Drain collision also works with free aim");
    seed_word(kTestProjectile + 4u, 181u * 8u);
    seed_word(kTestProjectile + 12u, 141u * 8u);
    seed_projectile(8192, 0, 4096);
    cpu.gpr[4] = 141u;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] == 141u, "the endpoint is inside horizontally but the crossing is outside");
    cpu.gpr[3] = 1;
    seed_word(0x80014024u, 5u);
    seed_half(cpu.gpr[16] + 0x16u, 4u);
    seed_word(kTestProjectile + 4u, 0u);
    seed_word(kTestProjectile + 8u, 23u);
    seed_word(kTestProjectile + 12u, 815u);
    seed_projectile(-49 * 64, -7 * 64, 49 * 64);
    cpu.gpr[4] = 815u >> 3;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] == 100u, "the grazing segment reaches height 100");
    seed_half(cpu.gpr[16] + 0x16u, 0u);
    seed_word(kTestProjectile + 4u, 34u);
    seed_word(kTestProjectile + 12u, 818u);
    seed_projectile(3 * 64, -3 * 64, 22 * 64);
    cpu.gpr[4] = 818u >> 3;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] == 100u, "test the in-band segment before it leaves the horizontal footprint");
    seed_word(0x80014024u, 20u);
    seed_half(cpu.gpr[16] + 0x16u, 200u);
    seed_word(kTestProjectile + 4u, 221u * 8u);
    seed_word(kTestProjectile + 8u, 0u);
    seed_word(kTestProjectile + 12u, 104u * 8u);
    seed_projectile(4 * 8 * 64, 0, 8 * 8 * 64);
    cpu.gpr[2] = 20;
    cpu.gpr[3] = 4;
    cpu.gpr[4] = 21;
    g_combat_callbacks[7](&cpu, 0xA0022918u);
    expect(cpu.gpr[4] < cpu.gpr[2], "midstep Drain hit reaches the native height test after exiting horizontally");
    cpu.gpr[3] >>= 1;
    cpu.gpr[4] = 104;
    g_combat_callbacks[2](&cpu, 0x80022934u);
    expect(cpu.gpr[4] > 98u && cpu.gpr[4] < 102u, "the same crossing passes both native height comparisons");
    seed_word(0x80014024u, 1u);
    seed_word(kTestProjectile + 4u, 1596u);
    seed_word(kTestProjectile + 12u, 804u);
    seed_projectile(-8 * 64, 0, 8 * 64);
    cpu.gpr[2] = 1;
    cpu.gpr[3] = 2;
    cpu.gpr[4] = 1;
    g_combat_callbacks[7](&cpu, 0x80022918u);
    expect(cpu.gpr[4] == 0u, "only the simultaneous crossing has both X=200 and Y=100");
}

void test_lockstep_replay_is_side_effect_free() {
    reset_state();
    seed_live_game();
    CPUState cpu{};
    activate(&cpu, 10.0);
    cpu.gpr[16] = kTestProjectile;
    seed_projectile(300, 400, 10);
    g_guest_writes.clear();
    const double requested = disruptor_vertical_camera_requested_pitch();
    const int center = disruptor_vertical_camera_effective_center_y();
    const int gte_writes = g_gte_writes;
    const int projection_writes = g_projection_writes;

    g_ls_replay_active = 1;
    disruptor_vertical_camera_set_requested_pitch(20.0);
    disruptor_vertical_camera_recenter();
    g_renderer_callback(&cpu, kRendererEntry);
    cpu.gpr[2] = 77u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B900u, 0x0205102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    disruptor_vertical_camera_instruction_hook(
        &cpu, kNormalProjectileSite, kNormalProjectileOpcode,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);

    expect(disruptor_vertical_camera_requested_pitch() == requested &&
               disruptor_vertical_camera_effective_center_y() == center,
           "lockstep replay cannot mutate camera globals");
    expect(cpu.gpr[2] == 77u && projectile_y() == 10,
           "lockstep replay preserves guest registers and RAM");
    expect(g_guest_writes.empty() && g_gte_writes == gte_writes &&
               g_projection_writes == projection_writes,
           "lockstep replay emits no host or guest writes");

    g_ls_replay_active = 0;
    g_ls_mode = 1;
    cpu.gpr[2] = 55u;
    disruptor_vertical_camera_instruction_hook(
        &cpu, 0x8003B900u, 0x0205102Au,
        DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION);
    g_renderer_callback(&cpu, kRendererEntry);
    expect(cpu.gpr[2] == 55u &&
               disruptor_vertical_camera_effective_center_y() == center &&
               g_guest_writes.empty() && g_gte_writes == gte_writes &&
               g_projection_writes == projection_writes,
           "lockstep record is neutral as well as replay");
    g_ls_mode = 0;
}

void test_requested_pitch_validation() {
    reset_state();
    disruptor_vertical_camera_set_requested_pitch(100.0);
    expect(disruptor_vertical_camera_requested_pitch() == 22.0,
           "requested pitch clamps to verified positive limit");
    disruptor_vertical_camera_set_requested_pitch(-100.0);
    expect(disruptor_vertical_camera_requested_pitch() == -22.0,
           "requested pitch clamps to verified negative limit");
    disruptor_vertical_camera_set_requested_pitch(
        std::numeric_limits<double>::quiet_NaN());
    expect(disruptor_vertical_camera_requested_pitch() == -22.0,
           "non-finite pitch is rejected rather than recentering");
}

}  // namespace

int main() {
    test_registration_and_projection();
    test_restore_and_zero_identity();
    test_gameplay_guards();
    test_cull_widening();
    test_projectile_alignment();
    test_target_and_drain();
    test_live_frames_count_drawn_play();
    test_the_renderer_reads_the_mouse_again();
    test_free_aim_takes_the_look();
    test_lockstep_replay_is_side_effect_free();
    test_requested_pitch_validation();

    if (g_failures) {
        std::cerr << g_failures << " vertical-camera test(s) failed\n";
        return 1;
    }
    std::cout << "Disruptor vertical camera contracts: PASS\n";
    return 0;
}
