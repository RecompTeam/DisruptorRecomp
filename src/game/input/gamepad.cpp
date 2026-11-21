#include "game/input/gamepad.h"

#include "cpu_state.h"
#include "game/input/sticks.h"
#include "game/settings/game_user_settings.h"
#include "host_ui.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_sdl.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" SDL_Window *sdl_window;
extern "C" SDL_JoystickID psx_input_pad_instance(int slot);

namespace disruptor::gamepad {
namespace {

constexpr uint32_t kPlayerYaw = 0x80077624u;
constexpr uint32_t kYawSine = 0x80057798u, kYawCosine = 0x80057998u;
constexpr uint32_t kWalkSpeed = 0x800715D4u;
constexpr uint32_t kSliding = 0x80077610u;
constexpr uint32_t kScene = 0x8007145Cu, kMode = 0x800716F0u, kMenu = 0x800715FCu;
constexpr int kDigitalMove = 20, kSavedPositionX = 6, kSavedPositionZ = 5;
constexpr int kVelocityX = 22, kVelocityZ = 23, kPositionX = 23, kPositionZ = 22;
constexpr auto kNoPad = static_cast<SDL_JoystickID>(-1);

struct Pulse {
    int strength = 0;
    int frames = 0;
    SDL_JoystickID pad = kNoPad;
};

Pulse vibration;

bool comparator_active() { return g_ls_mode != 0 || g_ls_replay_active != 0; }

void drive_movement(CPUState *cpu, uint32_t) {
    const sticks::Movement move = sticks::movement();
    if (!move || cpu->gpr[kDigitalMove] != 0u || comparator_active()) return;

    const uint32_t direction = 2u * psx_mod_read_byte(kPlayerYaw);
    const int sine = static_cast<int16_t>(psx_mod_read_half(kYawSine + direction));
    const int cosine = static_cast<int16_t>(psx_mod_read_half(kYawCosine + direction));
    const int64_t speed = static_cast<int32_t>(psx_mod_read_word(kWalkSpeed));
    const int64_t forward = move.forward, right = move.strafe;
    cpu->gpr[kVelocityX] = static_cast<uint32_t>(speed * (forward * cosine + right * sine) / sticks::kFullMovement);
    cpu->gpr[kVelocityZ] = static_cast<uint32_t>(speed * (forward * sine - right * cosine) / sticks::kFullMovement);
}

void keep_movement_fraction(CPUState *cpu, uint32_t) {
    if (!sticks::movement() || cpu->gpr[kDigitalMove] != 0u || comparator_active()) return;
    const uint32_t airborne = cpu->gpr[29] + 0x30u;
    if (psx_mod_read_byte(airborne) != 0u || psx_mod_read_word(kSliding) != 0u) return;
    cpu->gpr[kSavedPositionX] = cpu->gpr[kPositionX];
    cpu->gpr[kSavedPositionZ] = cpu->gpr[kPositionZ];
}

bool can_vibrate() {
    static const bool replay = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr;
    return sticks::tuning().vibration && !replay && !comparator_active() &&
           sdl_window && (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_INPUT_FOCUS) &&
           !psx_host_ui_game_input_captured() && psx_mod_read_byte(kScene) == 0u &&
           psx_mod_read_byte(kMode) != 3u && psx_mod_read_byte(kMenu) != 0x18u;
}

void add_vibration(int strength, int frames) {
    const auto pad = psx_input_pad_instance(0);
    if (pad != kNoPad && can_vibrate() &&
        (vibration.pad != pad || vibration.frames == 0 || strength >= vibration.strength)) vibration = {strength, frames, pad};
}

void fired(CPUState *, uint32_t) { add_vibration(85, 4); }

void took_damage(CPUState *cpu, uint32_t) {
    const int damage = static_cast<int32_t>(cpu->gpr[16]);
    if (damage > 0) add_vibration(100 + std::min(damage, 25) * 5, 8);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_gamepad) {
    constexpr char plugin[] = "disruptor.gamepad";
    bool registered = psx_mod_register_instruction_plugin(plugin, 0x80038704u, 0x12800010u, drive_movement) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80038950u, 0x00C0B821u, keep_movement_fraction) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80032980u, 0xACC20000u, fired) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80032BA0u, 0x0C004A83u, fired) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80032BC0u, 0x0C004A83u, fired) != 0;
    registered &= psx_mod_register_instruction_plugin(plugin, 0x80020E54u, 0xAC227660u, took_damage) != 0;
    if (!registered) std::fprintf(stderr, "disruptor: failed to register gamepad hooks\n");
}

}

void apply_settings(const PSXRecompV4::GameUserSettings &settings) {
    sticks::tune({static_cast<sticks::Layout>(settings.stick_layout), settings.look_speed, settings.look_deadzone,
                  settings.invert_look, settings.analog_movement, settings.vibration});
}

void stop_vibration() {
    if (vibration.strength == 0) return;
    vibration = {};
    psx_mod_set_host_rumble(0, 0, 0);
}

void update_vibration() {
    if (vibration.strength == 0) return;
    if (vibration.frames == 0 || vibration.pad != psx_input_pad_instance(0) || !can_vibrate()) {
        stop_vibration();
        return;
    }
    psx_mod_set_host_rumble(0, 0, static_cast<uint32_t>(vibration.strength));
    --vibration.frames;
}

}
