/*
 * Native mouse aiming and modern keyboard/mouse controls for Disruptor
 * (SLUS-00224).
 *
 * Dynamic correlation plus static instruction tracing identified 0x80077624
 * as the game's 8-bit wrapping player/camera yaw.  The original turn routine
 * adds a signed controller-derived step to this byte; renderer, collision and
 * camera code then consume it through the game's own sine/cosine tables.
 *
 * This opt-in mod applies relative host mouse X motion to the same yaw byte
 * after each guest input/update frame.  Disruptor has no retail pitch state,
 * so independently enabled mouse Y motion drives a bounded host-side camera
 * and weapon-aim extension.  A third opt-in path merges a conventional
 * WASD/mouse layout into the emulated port-1 digital pad after the runtime's
 * low-latency input sample.  The runtime keeps a dormant frame hook available
 * for live menu changes; normal play pays only one immediate return unless one
 * of the control features is enabled.
 */

#include "psx_sdl.h"
#include "game/input/mouse_aim.h"
#include "game/session/capsule.h"
#include "game/input/vertical_camera.h"
#include "game/settings/user_folder.h"
#include "game/input/baton_choice.h"
#include "game/input/selection_wheel.h"
#include "game/input/weapon_list.h"
#include "game/input/psionic_change.h"
#include "game/input/sticks.h"
#include "game/settings/second_keys.h"
#include "../../../psxrecomp/runtime/include/gpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <utility>

extern "C" void mod_register_frame_hook(void (*hook)(void));
extern "C" uint8_t psx_read_byte(uint32_t address);
extern "C" uint32_t psx_read_word(uint32_t address);
extern "C" void psx_write_byte(uint32_t address, uint8_t value);
extern "C" uint16_t sio_get_pad_buttons_slot(int slot);
extern "C" void sio_set_pad_state_slot(int slot, uint16_t buttons);
extern "C" int32_t psx_mod_widescreen_x_margin(void);
extern "C" int gpu_ws_present_native_43(void);
extern "C" int ws_native_wide_active(void);
extern "C" int ws_nw_extra(void);
extern "C" void gpu_geometry_camera_yaw_residual_set(double yaw_units);
extern "C" SDL_Window *sdl_window;
extern "C" int psx_host_ui_game_input_captured(void);
extern "C" int psx_netplay_active(void);
extern "C" void disruptor_name_entry_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count);
extern "C" int disruptor_key_bound(int scancode);
extern "C" int psx_input_pad_sticks(int slot, int16_t out[4]);
extern "C" void psx_input_set_game_sticks(int owned);
extern "C" int psx_input_deadzone(void);

namespace {

constexpr uint32_t kPlayerYawAddress = 0x80077624u;
constexpr uint32_t kCurrentWeaponAddress = 0x80077627u;
constexpr uint32_t kHeldWeaponAddress = 0x80071484u;  // gp + 0x338: the weapon a change has reached
constexpr uint32_t kWeaponOwnershipBase = 0x800770F4u;
constexpr uint32_t kSceneModeAddress = 0x8007145Cu;
constexpr uint32_t kGameModeAddress = 0x800716F0u;  // gp + 0x5A4: the menu's idle timer sets 3 for a demo (0x80020848)
constexpr uint8_t kDemoMode = 3u;
constexpr uint32_t kMenuStateAddress = 0x800715FCu;
constexpr uint8_t kMainMenu = 0x18u;  // the main menu runs in scene mode 0 too
/* Retail weapon IDs follow the HUD names: baton, 18mm Semi, 18mm Auto,
 * Phase Rifle, Phase Repeater, Lockon Cannon, AM Blaster, AM Cyclone,
 * Zodiac, Plasma Lance. The last slot is on 0, as in other PC shooters. */
constexpr int kWeapons = 10, kPsionics = 5, kPickRows = kWeapons + kPsionics;
/* A row's first key, every weapon then every power, then the second keys alike. The powers have none at first. */
constexpr std::array<int, 2 * kPickRows> kPickKeys = {
    SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3, SDL_SCANCODE_4,
    SDL_SCANCODE_5, SDL_SCANCODE_6, SDL_SCANCODE_7, SDL_SCANCODE_8,
    SDL_SCANCODE_9, SDL_SCANCODE_0,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
    SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_2, SDL_SCANCODE_KP_3, SDL_SCANCODE_KP_4,
    SDL_SCANCODE_KP_5, SDL_SCANCODE_KP_6, SDL_SCANCODE_KP_7, SDL_SCANCODE_KP_8,
    SDL_SCANCODE_KP_9, SDL_SCANCODE_KP_0,
    SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN,
};
constexpr uint32_t kPsionicHandAddress = 0x80071320u;   // gp + 0x1D4: zero while a power's pictures load
constexpr uint32_t kPsionicOwnershipBase = 0x80077B5Cu;
constexpr int kUseMargin = 2;     // frames Square stays down once the power is ready: one used Shock in the probe
constexpr int kUsePatience = 90;  // frames a key waits for its power: a change took 21, one behind another twice that
using disruptor::psionic_change::kCurrentPsionicAddress;
using disruptor::psionic_change::kHeldPsionicAddress;
using disruptor::psionic_change::kLoadKindAddress;
using disruptor::psionic_change::kNoPsionic;
using disruptor::psionic_change::kTeraBlast;
using disruptor::psionic_change::power_of;
constexpr double kDefaultHorizontalSensitivity = 0.080;
constexpr double kDefaultVerticalSensitivity = 0.080;
constexpr double kMinimumSensitivity = 0.005;
constexpr double kMaximumSensitivity = 2.000;
constexpr double kMaximumMotionPerFrame = 1024.0;
/* The native projectile vertical component is bounded to a slope of 160/256.
 * Twenty-two angular units (30.94 degrees) remain just inside that envelope
 * and keep the initial presentation horizon on-screen. */
constexpr double kMaximumPitchUnits = 22.0;

/* PS1 digital-pad bits are active-low: clearing a bit presses it. */
constexpr uint16_t kPadSelect   = 1u << 0;
constexpr uint16_t kPadStart    = 1u << 3;
constexpr uint16_t kPadUp       = 1u << 4;
constexpr uint16_t kPadRight    = 1u << 5;
constexpr uint16_t kPadDown     = 1u << 6;
constexpr uint16_t kPadLeft     = 1u << 7;
constexpr uint16_t kPadL2       = 1u << 8;
constexpr uint16_t kPadR2       = 1u << 9;
constexpr uint16_t kPadL1       = 1u << 10;
constexpr uint16_t kPadR1       = 1u << 11;
constexpr uint16_t kPadTriangle = 1u << 12;
constexpr uint16_t kPadCircle   = 1u << 13;
constexpr uint16_t kPadCross    = 1u << 14;
constexpr uint16_t kPadSquare   = 1u << 15;

using disruptor::settings::SecondCodes;

SecondCodes default_second_codes(bool modern) {
    return disruptor::settings::second_codes(disruptor::settings::default_second_keys(modern),
                                             [](std::string_view) { return 0; });
}

struct MouseAimState {
    bool enabled = false;
    bool mouse_aim_enabled = false;
    bool vertical_look_enabled = false;
    bool modern_controls_enabled = false;
    bool number_keys_enabled = false;
    bool high_precision_camera = false;
    bool configured = false;
    bool captured = false;
    bool given_up = false;      // the player let the mouse go with Escape or a middle click: only a click takes it back
    bool swallow_left = false;  // the click that took the mouse is no shot: the left button waits for its release
    bool left_was_down = false;
    bool middle_was_down = false;
    bool escape_was_down = false;
    std::array<int, kPickKeys.size()> pick_keys = kPickKeys;
    std::array<bool, kPickKeys.size()> pick_was_down{};
    std::array<bool, kPickKeys.size()> pick_held{};  // down where it may pick: not an action's key
    SecondCodes second_modern = default_second_codes(true);
    SecondCodes second_classic = default_second_codes(false);
    int pending_weapon = -1;   // picked while the game was still changing weapons
    int pending_psionic = -1;  // and a power picked while it changed powers
    bool psionic_keys_use = false;
    int using_power = -1;      // a power's key asked to use it
    int use_waited = 0;
    int use_ready = 0;         // frames that power has been ready
    disruptor::sticks::Tuning sticks;
    bool replaying = false;  // a capsule's replay turns and moves as its recording did: the sticks leave it alone
    bool invert_horizontal = false;
    bool invert_vertical = false;
    double horizontal_sensitivity = kDefaultHorizontalSensitivity;
    double vertical_sensitivity = kDefaultVerticalSensitivity;
    bool vertical_follows_horizontal = true;
    double fractional_yaw = 0.0;
    double shown_yaw_fraction = 0.0;  // what fractional_yaw was a frame ago
    double sweep_counts = 0.0;
    double vertical_pitch = 0.0;
    uint64_t frame = 0;
    uint64_t motion_samples = 0;
    double interval_mouse_x = 0.0;
    double interval_mouse_y = 0.0;
    int64_t interval_yaw_steps = 0;
    double interval_pitch_delta = 0.0;
    uint16_t interval_modern_press_mask = 0;
    std::FILE *log = nullptr;
};

MouseAimState g_mouse;

std::string trim_copy(const std::string& value) {
    size_t first = 0;
    while (first < value.size() &&
           (value[first] == ' ' || value[first] == '\t' ||
            value[first] == '\r' || value[first] == '\n')) {
        ++first;
    }
    size_t last = value.size();
    while (last > first &&
           (value[last - 1] == ' ' || value[last - 1] == '\t' ||
            value[last - 1] == '\r' || value[last - 1] == '\n')) {
        --last;
    }
    return value.substr(first, last - first);
}

bool parse_bool(const std::string& value, bool fallback) {
    std::string lowered = trim_copy(value);
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
                   });
    if (lowered == "1" || lowered == "true" || lowered == "yes" || lowered == "on")
        return true;
    if (lowered == "0" || lowered == "false" || lowered == "no" || lowered == "off")
        return false;
    return fallback;
}

double parse_sensitivity(const std::string& value, double fallback) {
    const std::string trimmed = trim_copy(value);
    if (trimmed.empty()) return fallback;
    char *tail = nullptr;
    const double parsed = std::strtod(trimmed.c_str(), &tail);
    if (tail == trimmed.c_str() || !std::isfinite(parsed)) return fallback;
    while (*tail == ' ' || *tail == '\t' || *tail == '\r' || *tail == '\n') ++tail;
    if (*tail != '\0') return fallback;
    return std::clamp(parsed, kMinimumSensitivity, kMaximumSensitivity);
}

bool env_enabled(const char *name) {
    const char *value = std::getenv(name);
    return value && value[0] != '\0' && !(
        std::strcmp(value, "0") == 0 || std::strcmp(value, "false") == 0 ||
        std::strcmp(value, "FALSE") == 0 || std::strcmp(value, "off") == 0 ||
        std::strcmp(value, "OFF") == 0);
}

void load_configuration() {
    if (g_mouse.configured) return;
    g_mouse.configured = true;

    std::ifstream input(disruptor::user_folder::own_or_shipped("mouse-aim.ini"));
    std::string line;
    while (std::getline(input, line)) {
        const size_t comment = line.find_first_of("#;");
        if (comment != std::string::npos) line.erase(comment);
        const size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim_copy(line.substr(0, equals));
        const std::string value = trim_copy(line.substr(equals + 1));
        if (key == "horizontal_sensitivity") {
            g_mouse.horizontal_sensitivity =
                parse_sensitivity(value, g_mouse.horizontal_sensitivity);
        } else if (key == "invert_horizontal") {
            g_mouse.invert_horizontal = parse_bool(value, g_mouse.invert_horizontal);
        } else if (key == "vertical_sensitivity") {
            g_mouse.vertical_sensitivity =
                parse_sensitivity(value, g_mouse.vertical_sensitivity);
        } else if (key == "invert_vertical") {
            g_mouse.invert_vertical = parse_bool(value, g_mouse.invert_vertical);
        }
    }

    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_SENSITIVITY")) {
        g_mouse.horizontal_sensitivity =
            parse_sensitivity(value, g_mouse.horizontal_sensitivity);
    }
    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_INVERT_X")) {
        g_mouse.invert_horizontal = parse_bool(value, g_mouse.invert_horizontal);
    }
    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y")) {
        g_mouse.vertical_sensitivity =
            parse_sensitivity(value, g_mouse.vertical_sensitivity);
    }
    if (const char *value = std::getenv("PSX_DISRUPTOR_MOUSE_INVERT_Y")) {
        g_mouse.invert_vertical = parse_bool(value, g_mouse.invert_vertical);
    }
}

void set_title(const char *message) {
    if (!sdl_window) return;
    const char *mode = g_mouse.modern_controls_enabled
        ? "Modern Controls" : "Mouse Aim";
    const std::string title = std::string("Disruptor Recompiled - ") +
                              mode + ": " + message;
    SDL_SetWindowTitle(sdl_window, title.c_str());
}

void open_log() {
    if (g_mouse.log) return;
    g_mouse.log = std::fopen(disruptor::user_folder::in_user_folder("disruptor-mouse-aim.log").string().c_str(), "wb");
    if (!g_mouse.log) return;
    std::fprintf(g_mouse.log,
                 "DISRUPTOR MODERN CONTROLS AND MOUSE CAMERA v3\n"
                 "yaw_address=0x%08X\n"
                 "mouse_aim=%s\n"
                 "vertical_look=%s\n"
                 "modern_controls=%s\n"
                 "high_precision_camera=%s\n"
                 "horizontal_sensitivity=%.6f\n"
                 "invert_horizontal=%s\n"
                 "vertical_sensitivity=%.6f\n"
                 "invert_vertical=%s\n"
                 "vertical_pitch_limit=%.6f\n"
                 "capture=middle-click release=middle-click-or-escape\n"
                 "keyboard=W/S:forward/back A/D:strafe Space:jump E:use "
                 "F:psionic Q:weapon 1-9/0:weapon-slot R:psionic-select Tab:map P:pause\n"
                 "mouse=left:fire right:psionic X1:weapon X2:psionic-select\n",
                 kPlayerYawAddress,
                 g_mouse.mouse_aim_enabled ? "true" : "false",
                 g_mouse.vertical_look_enabled ? "true" : "false",
                 g_mouse.modern_controls_enabled ? "true" : "false",
                 g_mouse.high_precision_camera ? "true" : "false",
                 g_mouse.horizontal_sensitivity,
                 g_mouse.invert_horizontal ? "true" : "false",
                 g_mouse.vertical_sensitivity,
                 g_mouse.invert_vertical ? "true" : "false",
                 kMaximumPitchUnits);
    std::fflush(g_mouse.log);
}

void log_event(const char *event) {
    if (!g_mouse.log) return;
    std::fprintf(g_mouse.log, "event=%s frame=%llu yaw=0x%02X\n", event,
                 static_cast<unsigned long long>(g_mouse.frame),
                 static_cast<unsigned>(psx_read_byte(kPlayerYawAddress)));
    std::fflush(g_mouse.log);
}

bool set_relative_mouse_mode(bool enabled) {
    if (!sdl_window) return false;
#if defined(PSX_SDL3)
    return SDL_SetWindowRelativeMouseMode(sdl_window, enabled);
#else
    return SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0;
#endif
}

uint32_t get_mouse_buttons() {
#if defined(PSX_SDL3)
    float x = 0.0f;
    float y = 0.0f;
    return static_cast<uint32_t>(SDL_GetMouseState(&x, &y));
#else
    int x = 0;
    int y = 0;
    return static_cast<uint32_t>(SDL_GetMouseState(&x, &y));
#endif
}

void discard_relative_motion() {
#if defined(PSX_SDL3)
    float x = 0.0f;
    float y = 0.0f;
    (void)SDL_GetRelativeMouseState(&x, &y);
#else
    int x = 0;
    int y = 0;
    (void)SDL_GetRelativeMouseState(&x, &y);
#endif
}

struct RelativeMotion {
    double x = 0.0;
    double y = 0.0;
};

RelativeMotion take_relative_motion() {
#if defined(PSX_SDL3)
    float x = 0.0f;
    float y = 0.0f;
    (void)SDL_GetRelativeMouseState(&x, &y);
    return {static_cast<double>(x), static_cast<double>(y)};
#else
    int x = 0;
    int y = 0;
    (void)SDL_GetRelativeMouseState(&x, &y);
    return {static_cast<double>(x), static_cast<double>(y)};
#endif
}

void set_capture(bool captured, const char *reason) {
    if (captured && !g_mouse.mouse_aim_enabled &&
        !g_mouse.vertical_look_enabled &&
        !g_mouse.modern_controls_enabled) {
        return;
    }
    if (captured == g_mouse.captured) return;
    if (!set_relative_mouse_mode(captured)) {
        set_title("ERROR enabling relative mode; see log");
        std::printf("disruptor: mouse %s failed (%s): %s\n", captured ? "capture" : "release", reason, SDL_GetError());
        if (g_mouse.log) {
            std::fprintf(g_mouse.log, "event=relative-mode-error requested=%s error=%s\n",
                         captured ? "capture" : "release", SDL_GetError());
            std::fflush(g_mouse.log);
        }
        return;
    }

    g_mouse.captured = captured;
    g_mouse.fractional_yaw = 0.0;
    g_mouse.shown_yaw_fraction = 0.0;
    gpu_geometry_camera_yaw_residual_set(0.0);
    discard_relative_motion();
    log_event(reason);
    std::printf("disruptor: mouse %s (%s)\n", captured ? "captured" : "released", reason);
    if (captured) {
        set_title(g_mouse.modern_controls_enabled
            ? "CAPTURED - middle-click or Esc releases"
            : "CAPTURED - move to aim; middle-click or Esc releases");
    } else {
        set_title("released - left-click in the window to capture");
    }
}

bool key_bound(int scancode) { return disruptor_key_bound(scancode) != 0; }

const SecondCodes &second_keys() {
    return g_mouse.modern_controls_enabled ? g_mouse.second_modern : g_mouse.second_classic;
}

// A demo plays its own recording: the mouse and the number keys leave it alone, its pad buttons end it as before.
bool demo_playing() { return psx_read_byte(kGameModeAddress) == kDemoMode; }

// Where select_weapon may write: play, not netplay, live gameplay.
bool weapon_pick_possible() {
    return !psx_netplay_active() && disruptor_vertical_camera_input_allowed() && psx_read_byte(kSceneModeAddress) == 0u &&
           !demo_playing();
}

// A key a weapon takes where it can be taken: elsewhere, in the pause and the game's lists, it stays what it was.
bool weapon_key(int scancode) {
    return g_mouse.number_keys_enabled && weapon_pick_possible() &&
           std::find(g_mouse.pick_keys.begin(), g_mouse.pick_keys.end(), scancode) != g_mouse.pick_keys.end();
}

bool key_down(const Uint8 *keys, int scancode) {
    return keys && scancode >= 0 && keys[scancode] != 0;
}

/* Track raw keys even when typing, unfocused, or in a menu. A held key must
 * be released before it can select after gameplay resumes. */
// The row a key picks this frame: a weapon below kWeapons, a power from there.
int sample_weapon_keys(const Uint8 *keys, const Uint8 *pad_keys) {
    int requested = -1;
    // Alt+Enter and Alt+Tab are the window's.
    const bool alt = key_down(keys, SDL_SCANCODE_LALT) || key_down(keys, SDL_SCANCODE_RALT);
    for (size_t i = 0; i < g_mouse.pick_keys.size(); ++i) {
        const int key = g_mouse.pick_keys[i];
        const bool down = key != SDL_SCANCODE_UNKNOWN && key_down(keys, key);
        const bool picks = down && key_down(pad_keys, key) && !key_bound(key);
        if (requested < 0 && picks && !alt && !g_mouse.pick_was_down[i])
            requested = static_cast<int>(i % kPickRows);
        g_mouse.pick_was_down[i] = down;
        g_mouse.pick_held[i] = picks;
    }
    return requested;
}

void forget_picks() { g_mouse.pending_weapon = g_mouse.pending_psionic = g_mouse.using_power = -1; }

bool power_key_down(int power) {
    return g_mouse.pick_held[kWeapons + power] || g_mouse.pick_held[kPickRows + kWeapons + power];
}

/* Psionic attack from the change's start: the game holds it through the load and uses the power on its last frame
 * (probe of 2026-10-21). Before the change it could use the old power. */
void press_psionic_use() {
    const int power = g_mouse.using_power;
    if (power < 0) return;
    const bool taken = power_of(psx_read_byte(kHeldPsionicAddress)) == power &&
                       power_of(psx_read_byte(kCurrentPsionicAddress)) == power;
    const bool ready = taken && psx_read_byte(kLoadKindAddress) != 2u && psx_read_word(kPsionicHandAddress) != 0u;
    g_mouse.use_ready = ready ? g_mouse.use_ready + 1 : 0;
    if ((g_mouse.use_ready > kUseMargin && !power_key_down(power)) || (!ready && ++g_mouse.use_waited > kUsePatience)) {
        g_mouse.using_power = -1;
        return;
    }
    if (taken) sio_set_pad_state_slot(0, static_cast<uint16_t>(sio_get_pad_buttons_slot(0) & ~kPadSquare));
}

/* As the psionics list's own move (0x8003E820), the ID alone: the power update sees it differ from the held one and
 * owns the change. Returns whether it wrote. */
bool select_psionic(int power) {
    if (power < 0 || power >= kPsionics || !weapon_pick_possible()) return false;
    const uint8_t current = psx_read_byte(kCurrentPsionicAddress);
    if ((current > kTeraBlast && current != kNoPsionic) ||
        psx_read_word(kPsionicOwnershipBase + static_cast<uint32_t>(power) * 4u) == 0u)
        return false;
    if (power_of(current) == static_cast<uint8_t>(power)) {
        g_mouse.pending_psionic = -1;
        return false;
    }
    disruptor_psionic_list_show();
    if (current != kNoPsionic && (power_of(current) != power_of(psx_read_byte(kHeldPsionicAddress)) ||
                                  psx_read_word(kPsionicHandAddress) == 0u)) {
        g_mouse.pending_psionic = power;
        return false;
    }
    g_mouse.pending_psionic = -1;
    psx_write_byte(kCurrentPsionicAddress, static_cast<uint8_t>(power));
    disruptor_capsule_note_byte(kCurrentPsionicAddress, static_cast<uint8_t>(power));
    return true;
}

// Returns whether it wrote.
bool select_weapon(int weapon) {
    if (weapon < 0 || weapon >= kWeapons || !weapon_pick_possible()) return false;

    /* The retail selector at 0x80039920 checks this same ownership word,
     * then stores only the selected ID. The normal weapon update notices
     * the change and owns its animation; ammunition and inventory stay put. */
    const uint8_t current = psx_read_byte(kCurrentWeaponAddress);
    if ((current >= 10u && current != 0xFFu) ||
        psx_read_word(kWeaponOwnershipBase + static_cast<uint32_t>(weapon) * 4u) != 1u)
        return false;
    if (current == static_cast<uint8_t>(weapon)) {
        g_mouse.pending_weapon = -1;
        if (weapon != 0) return false;
    } else {
        disruptor_weapon_list_show();
        // The game's own selector waits too: a pick mid-change turns the old weapon back up.
        if (current < 10u && current != psx_read_byte(kHeldWeaponAddress)) {
            g_mouse.pending_weapon = weapon;
            return false;
        }
    }
    g_mouse.pending_weapon = -1;
    psx_write_byte(kCurrentWeaponAddress, static_cast<uint8_t>(weapon));
    disruptor_capsule_note_byte(kCurrentWeaponAddress, static_cast<uint8_t>(weapon));
    disruptor_weapon_chosen(static_cast<uint8_t>(weapon));
    return true;
}

/* Merge host controls into the already sampled port-1 pad word.  This runs
 * after the runtime's final low-latency sample, so releases are naturally
 * refreshed on the next frame and no synthetic button can stick.  Keyboard
 * controls remain live while uncaptured; mouse actions require capture so a
 * desktop click cannot fire into gameplay accidentally. */
void apply_number_keys(const Uint8 *keys, bool focused) {
    std::array<Uint8, 512> pad_keys{};  /* SDL's scancode count, in 2 and 3 alike */
    if (keys && g_mouse.number_keys_enabled)
        disruptor_name_entry_keys_for_pad(keys, pad_keys.data(), static_cast<int>(pad_keys.size()));
    const int row = sample_weapon_keys(keys, g_mouse.number_keys_enabled && keys ? pad_keys.data() : nullptr);
    // A waiting pick lives only while one could be made: focus, the setting, play.
    const bool possible = g_mouse.number_keys_enabled && focused && weapon_pick_possible();
    if (!possible) forget_picks();
    if (!focused) return;
    // One pick a frame: a capsule's frame holds two bytes, and a weapon's pick takes both.
    const bool wrote = select_weapon(row >= 0 && row < kWeapons ? row : g_mouse.pending_weapon);
    if (!wrote) select_psionic(row >= kWeapons ? row - kWeapons : g_mouse.pending_psionic);
    else if (row >= kWeapons) g_mouse.pending_psionic = row - kWeapons;
    const int power = row - kWeapons;
    if (possible && power >= 0 && g_mouse.psionic_keys_use &&
        (power_of(psx_read_byte(kCurrentPsionicAddress)) == power || g_mouse.pending_psionic == power)) {
        g_mouse.using_power = power;
        g_mouse.use_waited = g_mouse.use_ready = 0;
    }
    press_psionic_use();
}

void apply_modern_controls(const Uint8 *keys, uint32_t mouse_buttons, uint16_t stick_press = 0) {
    std::array<Uint8, 512> pad_keys{};  /* SDL's scancode count, in 2 and 3 alike */
    if (keys) {
        disruptor_name_entry_keys_for_pad(keys, pad_keys.data(), static_cast<int>(pad_keys.size()));
        keys = pad_keys.data();
    }

    /* keybinds.ini holds the first keys, settings.toml the second, which press in either scheme. A mouse button presses
     * only while the game holds the mouse. */
    uint16_t press = disruptor::settings::second_press(second_keys(), keys, pad_keys.size(), g_mouse.captured ? mouse_buttons : 0u) |
                     stick_press;
    /* The arrows also walk forward and back where the player left them free. */
    if (g_mouse.modern_controls_enabled)
        for (const auto [arrow, button] : {std::pair{SDL_SCANCODE_UP, kPadUp}, std::pair{SDL_SCANCODE_DOWN, kPadDown}})
            if (key_down(keys, arrow) && !key_bound(arrow) && !weapon_key(arrow)) press |= button;

    press = disruptor_selection_wheel_filter_press(press);
    if (press != 0) {
        const uint16_t sampled = sio_get_pad_buttons_slot(0);
        sio_set_pad_state_slot(0, static_cast<uint16_t>(sampled & ~press));
        g_mouse.interval_modern_press_mask |= press;
    }
}

double vertical_speed() {
    return g_mouse.vertical_follows_horizontal ? g_mouse.horizontal_sensitivity : g_mouse.vertical_sensitivity;
}

// The mouse's motion and the stick's heading units of a VBlank: one write and one note for both.
void apply_turn(double raw_x, double stick) {
    if (raw_x == 0.0 && stick == 0.0) return;
    raw_x = std::clamp(raw_x, -kMaximumMotionPerFrame, kMaximumMotionPerFrame);
    const double direction = g_mouse.invert_horizontal ? 1.0 : -1.0;
    g_mouse.fractional_yaw +=
        raw_x * g_mouse.horizontal_sensitivity * direction + stick;

    /* Quantize to the nearest retail yaw byte and carry the signed error into
     * the next sample.  The previous toward-zero extraction could retain
     * almost one whole angular unit, making very small movements wait longer
     * and then arrive as a visible byte-sized jump.  Nearest-step error
     * diffusion bounds that discrepancy to half a unit without averaging,
     * predicting future input, or changing the cumulative mouse distance.
     * The carried error remains the exact sub-byte presentation correction. */
    const double whole = std::round(g_mouse.fractional_yaw);
    if (whole == 0.0) return;
    int yaw_steps = static_cast<int>(whole);
    yaw_steps = std::clamp(yaw_steps, -64, 64);
    g_mouse.fractional_yaw -= static_cast<double>(yaw_steps);
    if (std::abs(whole) > 64.0) g_mouse.fractional_yaw = 0.0;

    const uint8_t old_yaw = psx_read_byte(kPlayerYawAddress);
    const uint8_t new_yaw = static_cast<uint8_t>(
        static_cast<int>(old_yaw) + yaw_steps);
    psx_write_byte(kPlayerYawAddress, new_yaw);
    disruptor_capsule_note_byte(kPlayerYawAddress, new_yaw);
    ++g_mouse.motion_samples;
    g_mouse.interval_mouse_x += raw_x;
    g_mouse.interval_yaw_steps += yaw_steps;
}

void apply_mouse_x(double raw_x) { apply_turn(raw_x, 0.0); }

void reset_vertical_pitch(const char *event) {
    g_mouse.vertical_pitch = 0.0;
    disruptor_vertical_camera_recenter();
    if (event) log_event(event);
}

void apply_tilt(double raw_y, double stick) {
    if (raw_y == 0.0 && stick == 0.0) return;
    raw_y = std::clamp(raw_y, -kMaximumMotionPerFrame,
                       kMaximumMotionPerFrame);
    const double direction = g_mouse.invert_vertical ? 1.0 : -1.0;
    const double old_pitch = g_mouse.vertical_pitch;
    const double new_pitch = std::clamp(
        old_pitch + raw_y * vertical_speed() * direction + stick,
        -kMaximumPitchUnits, kMaximumPitchUnits);

    g_mouse.interval_mouse_y += raw_y;
    g_mouse.interval_pitch_delta += new_pitch - old_pitch;
    if (new_pitch == old_pitch) return;
    g_mouse.vertical_pitch = new_pitch;
    disruptor_vertical_camera_set_requested_pitch(new_pitch);
}

void apply_mouse_y(double raw_y) { apply_tilt(raw_y, 0.0); }

void log_u64_array(std::FILE *log, const char *key,
                   const uint64_t *values, size_t count) {
    std::fprintf(log, " %s=", key);
    for (size_t i = 0; i < count; ++i) {
        std::fprintf(log, "%s%llu", i == 0 ? "" : ",",
                     static_cast<unsigned long long>(values[i]));
    }
}

void log_geometry_diagnostics_scope(
        const char *scope, uint64_t frame,
        const GpuGeometryCorrectionStats& snapshot,
        const GpuGeometryCorrectionCounters& counters) {
    std::fprintf(
        g_mouse.log,
        "geometry_diag version=%u size=%u frame=%llu scope=%s "
        "presentation=%llu corrected_presentations=%llu "
        "latest_live_presentation=%llu latest_live_valid=%u "
        "triangle_candidates=%llu triangle_accepted=%llu "
        "polygon_candidates=%llu polygon_accepted=%llu "
        "vertex_candidates=%llu vertex_accepted=%llu "
        "partial_polygon_rejections=%llu partial_quad_rejections=%llu "
        "correction_vertices=%llu "
        "correction_abs_x16_sum=%llu correction_abs_y16_sum=%llu "
        "correction_magnitude16_sum=%llu correction_magnitude16_max=%llu "
        "correction_x16_squared_sum=%llu correction_y16_squared_sum=%llu "
        "correction_accumulators_saturated=%llu "
        "provenance_store_collisions=%llu provenance_store_evictions=%llu "
        "provenance_lookup_collisions=%llu "
        "provenance_store_uncommitted_rejections=%llu "
        "provenance_registered_store_attempts=%llu "
        "provenance_registered_store_accepts=%llu "
        "provenance_registered_store_packed_rejections=%llu "
        "provenance_copy_load_attempts=%llu "
        "provenance_copy_load_accepts=%llu "
        "provenance_copy_store_attempts=%llu "
        "provenance_copy_store_accepts=%llu "
        "provenance_copy_store_packed_rejections=%llu",
        snapshot.version, snapshot.size,
        static_cast<unsigned long long>(frame), scope,
        static_cast<unsigned long long>(snapshot.presentation_sequence),
        static_cast<unsigned long long>(snapshot.corrected_presentations),
        static_cast<unsigned long long>(snapshot.latest_live_presentation),
        snapshot.latest_live_valid,
        static_cast<unsigned long long>(counters.triangle_candidates),
        static_cast<unsigned long long>(counters.triangle_accepted),
        static_cast<unsigned long long>(counters.polygon_candidates),
        static_cast<unsigned long long>(counters.polygon_accepted),
        static_cast<unsigned long long>(counters.vertex_candidates),
        static_cast<unsigned long long>(counters.vertex_accepted),
        static_cast<unsigned long long>(counters.partial_polygon_rejections),
        static_cast<unsigned long long>(counters.partial_quad_rejections),
        static_cast<unsigned long long>(counters.correction_vertices),
        static_cast<unsigned long long>(counters.correction_abs_x16_sum),
        static_cast<unsigned long long>(counters.correction_abs_y16_sum),
        static_cast<unsigned long long>(counters.correction_magnitude16_sum),
        static_cast<unsigned long long>(counters.correction_magnitude16_max),
        static_cast<unsigned long long>(counters.correction_x16_squared_sum),
        static_cast<unsigned long long>(counters.correction_y16_squared_sum),
        static_cast<unsigned long long>(
            counters.correction_accumulators_saturated),
        static_cast<unsigned long long>(snapshot.provenance_store_collisions),
        static_cast<unsigned long long>(snapshot.provenance_store_evictions),
        static_cast<unsigned long long>(snapshot.provenance_lookup_collisions),
        static_cast<unsigned long long>(
            snapshot.provenance_store_uncommitted_rejections),
        static_cast<unsigned long long>(
            snapshot.provenance_registered_store_attempts),
        static_cast<unsigned long long>(
            snapshot.provenance_registered_store_accepts),
        static_cast<unsigned long long>(
            snapshot.provenance_registered_store_packed_rejections),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_load_attempts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_load_accepts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_store_attempts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_store_accepts),
        static_cast<unsigned long long>(
            snapshot.provenance_copy_store_packed_rejections));
    log_u64_array(g_mouse.log, "reject", counters.vertex_rejections,
                  GPU_GEOMETRY_REJECT_REASON_COUNT);
    log_u64_array(g_mouse.log, "primitive_candidates",
                  counters.primitive_candidates,
                  GPU_GEOMETRY_PRIMITIVE_CLASS_COUNT);
    log_u64_array(g_mouse.log, "primitive_accepted",
                  counters.primitive_accepted,
                  GPU_GEOMETRY_PRIMITIVE_CLASS_COUNT);
    log_u64_array(g_mouse.log, "opcode_candidates",
                  counters.opcode_candidates, GPU_GEOMETRY_OPCODE_COUNT);
    log_u64_array(g_mouse.log, "opcode_accepted",
                  counters.opcode_accepted, GPU_GEOMETRY_OPCODE_COUNT);
    log_u64_array(g_mouse.log, "depth_vertices", counters.depth_vertices,
                  GPU_GEOMETRY_DEPTH_BUCKET_COUNT);
    log_u64_array(g_mouse.log, "screen_vertices", counters.screen_vertices,
                  GPU_GEOMETRY_SCREEN_BUCKET_COUNT);
    log_u64_array(g_mouse.log, "correction_buckets",
                  counters.correction_magnitude_buckets,
                  GPU_GEOMETRY_CORRECTION_BUCKET_COUNT);
    std::fputc('\n', g_mouse.log);
}

void mouse_aim_frame() {
    if (disruptor_selection_wheel_frame()) {
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        sample_weapon_keys(keys, nullptr);
        forget_picks();
        g_mouse.escape_was_down = key_down(keys, SDL_SCANCODE_ESCAPE);
        const uint32_t buttons = get_mouse_buttons();
        g_mouse.middle_was_down = (buttons & SDL_BUTTON_MMASK) != 0;
        g_mouse.left_was_down = (buttons & SDL_BUTTON_LMASK) != 0;
        discard_relative_motion();
        gpu_geometry_camera_yaw_residual_set(0.0);
        return;
    }
    // A network game's pads are the runtime's alone: there the map's stick sources press, as in Classic.
    const bool netplay = psx_netplay_active() != 0;
    int16_t axes[4] = {};
    const bool pad = !netplay && !g_mouse.replaying && psx_input_pad_sticks(0, axes) != 0;
    if ((!g_mouse.enabled && !pad) || !sdl_window) return;
    ++g_mouse.frame;
#ifndef PSX_NO_DEBUG_TOOLS
    if (g_mouse.sweep_counts != 0.0)
        apply_mouse_x((g_mouse.frame / 40u) % 2u ? -g_mouse.sweep_counts : g_mouse.sweep_counts);
#endif
    load_configuration();
    open_log();

    const bool vertical_netplay_blocked = netplay;
    if (vertical_netplay_blocked && g_mouse.vertical_pitch != 0.0)
        reset_vertical_pitch("netplay-vertical-recenter");

    /* The host menu consumes polled state as well as SDL events.  Keep the
     * edge detectors current so a button held while closing cannot toggle
     * capture or inject a guest action on the following frame. */
    if (psx_host_ui_game_input_captured()) {
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        sample_weapon_keys(keys, nullptr);
        forget_picks();
        const uint32_t buttons = get_mouse_buttons();
        g_mouse.escape_was_down =
            keys && keys[SDL_SCANCODE_ESCAPE] != 0;
        g_mouse.middle_was_down = (buttons & SDL_BUTTON_MMASK) != 0;
        g_mouse.left_was_down = (buttons & SDL_BUTTON_LMASK) != 0;
        discard_relative_motion();
        gpu_geometry_camera_yaw_residual_set(0.0);
        return;
    }

    static bool announced = false;
    if (!announced) {
        announced = true;
        set_title("ready");
        log_event("ready");
    }

    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    const bool escape_down = keys && keys[SDL_SCANCODE_ESCAPE] != 0;
    const uint32_t buttons = get_mouse_buttons();
    const bool middle_down = (buttons & SDL_BUTTON_MMASK) != 0;
    const bool left_down = (buttons & SDL_BUTTON_LMASK) != 0;

    const bool focused =
        (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool usable = g_mouse.mouse_aim_enabled || g_mouse.vertical_look_enabled || g_mouse.modern_controls_enabled ||
                        disruptor::settings::any_mouse(second_keys());
    const bool was_captured = g_mouse.captured;
    /* The mouse is held whenever the window has focus, with no click asked for. The player's key and clicks come before the
     * focus branches: on the frame focus goes or comes back those would take them. */
    if (escape_down && !g_mouse.escape_was_down && (g_mouse.captured || !g_mouse.given_up)) {
        g_mouse.given_up = true;
        set_capture(false, "escape-release");
    } else if (middle_down && !g_mouse.middle_was_down) {
        g_mouse.given_up = !g_mouse.given_up;
        set_capture(!g_mouse.given_up && focused, g_mouse.given_up ? "middle-release" : "middle-capture");
    } else if (left_down && !g_mouse.left_was_down && g_mouse.given_up && focused) {
        g_mouse.given_up = false;
        set_capture(true, "click-capture");
    } else if (g_mouse.captured && !focused) {
        set_capture(false, "focus-lost-release");
    } else if (!g_mouse.captured && !g_mouse.given_up && focused && usable) {
        set_capture(true, "focus-capture");
        /* Relative mode refused: asking again every frame would fill the log. A click asks again. */
        if (!g_mouse.captured) g_mouse.given_up = true;
    }
    if (g_mouse.captured && !was_captured && left_down) g_mouse.swallow_left = true;
    if (!left_down) g_mouse.swallow_left = false;

    g_mouse.middle_was_down = middle_down;
    g_mouse.escape_was_down = escape_down;
    g_mouse.left_was_down = left_down;

    // Scene mode 0 is play: the pause (4), the game's own lists and a demo leave the camera to the game, the motion is dropped.
    const bool demo = demo_playing();
    const bool playing = psx_read_byte(kSceneModeAddress) == 0u && !demo;
    const bool live = disruptor_vertical_camera_input_allowed() != 0;
    const bool tilting = g_mouse.vertical_look_enabled && !vertical_netplay_blocked && live;
    const bool steering = playing && live && psx_read_byte(kMenuStateAddress) != kMainMenu;
    const disruptor::sticks::Reading stick =
        pad ? disruptor::sticks::read(g_mouse.sticks, {axes[0], axes[1], axes[2], axes[3]}, psx_input_deadzone(), steering, tilting)
            : disruptor::sticks::Reading{};
    apply_number_keys(keys, focused);
    apply_modern_controls(keys, g_mouse.swallow_left ? buttons & ~static_cast<uint32_t>(SDL_BUTTON_LMASK) : buttons, stick.press);
    if (demo) g_mouse.fractional_yaw = 0.0;
    double mouse_x = 0.0, mouse_y = 0.0;
    if (g_mouse.captured && playing && (g_mouse.mouse_aim_enabled || g_mouse.vertical_look_enabled)) {
        /* SDL drains both relative axes in one call.  Keep this a single
         * coherent sample so enabling one axis can never consume the other. */
        const RelativeMotion motion = take_relative_motion();
        if (g_mouse.mouse_aim_enabled) mouse_x = motion.x;
        if (tilting) mouse_y = motion.y;
    } else if (g_mouse.captured) {
        discard_relative_motion();
    }
    apply_turn(mouse_x, stick.turn);
    apply_tilt(mouse_y, stick.tilt);

    /* The retail yaw byte remains authoritative for gameplay.  The sub-byte
     * remainder is presentation metadata only, consumed by the independent
     * geometry-correction surface; releasing capture immediately restores the
     * exact retail camera. */
    // The game draws a written yaw a frame later: handed over at once, a steady turn stepped back 3.2 and forward 5.1 pixels of 960 at every whole step.
    const double shown = g_mouse.shown_yaw_fraction;
    g_mouse.shown_yaw_fraction = g_mouse.fractional_yaw;
    const bool stick_turns = pad && g_mouse.sticks.layout != disruptor::sticks::Layout::classic;
    gpu_geometry_camera_yaw_residual_set(
        g_mouse.high_precision_camera && ((g_mouse.captured && g_mouse.mouse_aim_enabled) || stick_turns)
            ? shown
            : 0.0);

    if (g_mouse.log && g_mouse.frame % 60u == 0u) {
        uint64_t geometry_world = 0;
        uint64_t geometry_precise = 0;
        const uint32_t texture_perspective = gpu_texture_correction_hits();
        GpuGeometryCorrectionStats geometry_diagnostics{};
        gpu_geometry_correction_stats(&geometry_world, &geometry_precise);
        gpu_geometry_correction_stats_detailed(
            &geometry_diagnostics,
            static_cast<uint32_t>(sizeof(geometry_diagnostics)));
        std::fprintf(g_mouse.log,
                     "sample frame=%llu captured=%s mouse_x=%.3f mouse_y=%.3f "
                     "yaw_steps=%lld pitch_delta=%.6f pitch=%.6f "
                     "yaw=0x%02X yaw_residual=%.6f motion_samples=%llu "
                      "modern_press_mask=0x%04X geometry_world=%llu "
                      "geometry_precise=%llu "
                      "texture_perspective=%u "
                      "ws_margin=%d ws_native_active=%d ws_pinned_43=%d "
                     "ws_extra=%d\n",
                     static_cast<unsigned long long>(g_mouse.frame),
                     g_mouse.captured ? "true" : "false",
                     g_mouse.interval_mouse_x,
                     g_mouse.interval_mouse_y,
                     static_cast<long long>(g_mouse.interval_yaw_steps),
                     g_mouse.interval_pitch_delta,
                     g_mouse.vertical_pitch,
                     static_cast<unsigned>(psx_read_byte(kPlayerYawAddress)),
                     g_mouse.fractional_yaw,
                     static_cast<unsigned long long>(g_mouse.motion_samples),
                     static_cast<unsigned>(g_mouse.interval_modern_press_mask),
                      static_cast<unsigned long long>(geometry_world),
                      static_cast<unsigned long long>(geometry_precise),
                      static_cast<unsigned>(texture_perspective),
                     static_cast<int>(psx_mod_widescreen_x_margin()),
                     ws_native_wide_active(), gpu_ws_present_native_43(),
                     ws_nw_extra());
        if (gpu_geometry_diagnostics_detail()) {
            log_geometry_diagnostics_scope(
                "cumulative", g_mouse.frame, geometry_diagnostics,
                geometry_diagnostics.cumulative);
            log_geometry_diagnostics_scope(
                "latest_live", g_mouse.frame, geometry_diagnostics,
                geometry_diagnostics.latest_live);
        }
        std::fflush(g_mouse.log);
        g_mouse.interval_mouse_x = 0.0;
        g_mouse.interval_mouse_y = 0.0;
        g_mouse.interval_yaw_steps = 0;
        g_mouse.interval_pitch_delta = 0.0;
        g_mouse.interval_modern_press_mask = 0;
    }
}

struct MouseAimRegistration {
    MouseAimRegistration() {
        // A replay's turns are the recording's: its run takes the mouse only when the viewer clicks for it.
        g_mouse.given_up = g_mouse.replaying = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr;
        psx_input_set_game_sticks(1);
        g_mouse.mouse_aim_enabled =
            env_enabled("PSX_DISRUPTOR_MOUSE_AIM");
        g_mouse.vertical_look_enabled =
            env_enabled("PSX_DISRUPTOR_VERTICAL_LOOK");
        g_mouse.modern_controls_enabled =
            env_enabled("PSX_DISRUPTOR_MODERN_CONTROLS");
        g_mouse.high_precision_camera =
            env_enabled("PSX_DISRUPTOR_HIGH_PRECISION_CAMERA");
#ifndef PSX_NO_DEBUG_TOOLS
        if (const char *sweep = std::getenv("PSX_DISRUPTOR_MOUSE_AIM_SWEEP"))
            g_mouse.sweep_counts = std::atof(sweep);
#endif
        g_mouse.enabled = g_mouse.mouse_aim_enabled ||
                          g_mouse.vertical_look_enabled ||
                          g_mouse.modern_controls_enabled ||
                          g_mouse.number_keys_enabled ||
                          g_mouse.high_precision_camera;
        /* Keep one dormant hook installed even when every launch-time option
         * is off.  The in-game settings menu can then enable controls without
         * restarting; mouse_aim_frame() immediately returns while dormant. */
        mod_register_frame_hook(mouse_aim_frame);
    }
};

MouseAimRegistration g_registration;

}  // namespace

namespace {

void reset_presentation_residual() {
    g_mouse.fractional_yaw = 0.0;
    g_mouse.shown_yaw_fraction = 0.0;
    gpu_geometry_camera_yaw_residual_set(0.0);
}

void refresh_live_control_state(const char *event) {
    g_mouse.enabled = g_mouse.mouse_aim_enabled ||
                      g_mouse.vertical_look_enabled ||
                      g_mouse.modern_controls_enabled ||
                      g_mouse.number_keys_enabled ||
                      g_mouse.high_precision_camera;

    if ((!g_mouse.mouse_aim_enabled || !g_mouse.high_precision_camera) &&
        g_mouse.fractional_yaw != 0.0) {
        reset_presentation_residual();
    } else if (!g_mouse.mouse_aim_enabled ||
               !g_mouse.high_precision_camera) {
        gpu_geometry_camera_yaw_residual_set(0.0);
    }

    /* Relative capture is only meaningful for the two input features.  Do
     * not leave the desktop pointer trapped when both are disabled live. */
    if (!g_mouse.mouse_aim_enabled &&
        !g_mouse.vertical_look_enabled &&
        !g_mouse.modern_controls_enabled && g_mouse.captured) {
        set_capture(false, "settings-disabled-release");
    }

    if (event) log_event(event);
}

}  // namespace

extern "C" int disruptor_mouse_aim_enabled(void) {
    return g_mouse.mouse_aim_enabled ? 1 : 0;
}

extern "C" void disruptor_mouse_aim_set_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.mouse_aim_enabled == next) return;
    g_mouse.mouse_aim_enabled = next;
    reset_presentation_residual();
    refresh_live_control_state(next ? "settings-mouse-aim-on"
                                    : "settings-mouse-aim-off");
}

extern "C" int disruptor_modern_controls_enabled(void) {
    return g_mouse.modern_controls_enabled ? 1 : 0;
}

extern "C" void disruptor_modern_controls_set_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.modern_controls_enabled == next) return;
    g_mouse.modern_controls_enabled = next;
    sample_weapon_keys(SDL_GetKeyboardState(nullptr), nullptr);
    refresh_live_control_state(next ? "settings-modern-controls-on"
                                    : "settings-modern-controls-off");
}

extern "C" void disruptor_weapon_keys_state_loaded(void) { forget_picks(); }

extern "C" void disruptor_pick_keys_set(const int *scancodes, int count) {
    for (size_t i = 0; i < g_mouse.pick_keys.size(); ++i)
        g_mouse.pick_keys[i] = scancodes && i < static_cast<size_t>(count) ? scancodes[i] : kPickKeys[i];
    forget_picks();
    sample_weapon_keys(SDL_GetKeyboardState(nullptr), nullptr);
}

extern "C" void disruptor_second_keys_set(const int *modern, const int *classic, int count) {
    g_mouse.second_modern = default_second_codes(true);
    g_mouse.second_classic = default_second_codes(false);
    for (size_t i = 0; i < g_mouse.second_modern.size() && i < static_cast<size_t>(std::max(count, 0)); ++i) {
        if (modern) g_mouse.second_modern[i] = modern[i];
        if (classic) g_mouse.second_classic[i] = classic[i];
    }
}

extern "C" int disruptor_second_key_held(int field, const uint8_t *keys, int key_total, uint32_t mouse) {
    if (field < 0 || static_cast<size_t>(field) >= second_keys().size()) return 0;
    return disruptor::settings::second_held(second_keys()[static_cast<size_t>(field)], keys,
                                            static_cast<size_t>(std::max(key_total, 0)), mouse) ? 1 : 0;
}

extern "C" int disruptor_second_key_bound(int scancode) {
    return scancode > 0 && std::find(second_keys().begin(), second_keys().end(), scancode) != second_keys().end() ? 1 : 0;
}

extern "C" int disruptor_number_keys_enabled(void) {
    return g_mouse.number_keys_enabled ? 1 : 0;
}

extern "C" void disruptor_number_keys_set_enabled(int enabled) {
    if (g_mouse.number_keys_enabled == (enabled != 0)) return;
    g_mouse.number_keys_enabled = enabled != 0;
    sample_weapon_keys(SDL_GetKeyboardState(nullptr), nullptr);
    refresh_live_control_state(enabled ? "settings-number-keys-on" : "settings-number-keys-off");
}

extern "C" int disruptor_psionic_keys_use_enabled(void) { return g_mouse.psionic_keys_use ? 1 : 0; }

extern "C" void disruptor_psionic_keys_use_set_enabled(int enabled) {
    g_mouse.psionic_keys_use = enabled != 0;
    if (!g_mouse.psionic_keys_use) g_mouse.using_power = -1;
}

extern "C" int disruptor_high_precision_camera_enabled(void) {
    return g_mouse.high_precision_camera ? 1 : 0;
}

extern "C" void disruptor_high_precision_camera_set_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.high_precision_camera == next) return;
    g_mouse.high_precision_camera = next;
    reset_presentation_residual();
    refresh_live_control_state(next ? "settings-high-precision-on"
                                    : "settings-high-precision-off");
}

extern "C" double disruptor_mouse_horizontal_sensitivity(void) {
    load_configuration();
    return g_mouse.horizontal_sensitivity;
}

extern "C" double disruptor_mouse_set_horizontal_sensitivity(
        double sensitivity) {
    load_configuration();
    if (!std::isfinite(sensitivity)) return g_mouse.horizontal_sensitivity;
    const double next = std::clamp(sensitivity,
                                   kMinimumSensitivity,
                                   kMaximumSensitivity);
    if (g_mouse.horizontal_sensitivity == next)
        return g_mouse.horizontal_sensitivity;
    g_mouse.horizontal_sensitivity = next;
    reset_presentation_residual();
    log_event("settings-sensitivity");
    return g_mouse.horizontal_sensitivity;
}

extern "C" int disruptor_mouse_invert_horizontal(void) {
    load_configuration();
    return g_mouse.invert_horizontal ? 1 : 0;
}

extern "C" void disruptor_mouse_set_invert_horizontal(int inverted) {
    load_configuration();
    const bool next = inverted != 0;
    if (g_mouse.invert_horizontal == next) return;
    g_mouse.invert_horizontal = next;
    reset_presentation_residual();
    log_event(next ? "settings-invert-x-on" : "settings-invert-x-off");
}

extern "C" int disruptor_mouse_vertical_look_enabled(void) {
    return g_mouse.vertical_look_enabled ? 1 : 0;
}

extern "C" void disruptor_mouse_set_vertical_look_enabled(int enabled) {
    load_configuration();
    const bool next = enabled != 0;
    if (g_mouse.vertical_look_enabled == next) return;
    g_mouse.vertical_look_enabled = next;
    if (next) {
        if (!psx_netplay_active())
            disruptor_vertical_camera_set_requested_pitch(
                g_mouse.vertical_pitch);
    } else {
        reset_vertical_pitch("settings-vertical-look-recenter");
    }
    refresh_live_control_state(next ? "settings-vertical-look-on"
                                    : "settings-vertical-look-off");
}

extern "C" int disruptor_mouse_vertical_follows_horizontal(void) {
    return g_mouse.vertical_follows_horizontal ? 1 : 0;
}

extern "C" void disruptor_mouse_set_vertical_follows_horizontal(int follows) {
    g_mouse.vertical_follows_horizontal = follows != 0;
}

extern "C" double disruptor_mouse_vertical_sensitivity(void) {
    load_configuration();
    return g_mouse.vertical_sensitivity;
}

extern "C" double disruptor_mouse_set_vertical_sensitivity(
        double sensitivity) {
    load_configuration();
    if (!std::isfinite(sensitivity)) return g_mouse.vertical_sensitivity;
    const double next = std::clamp(sensitivity,
                                   kMinimumSensitivity,
                                   kMaximumSensitivity);
    if (g_mouse.vertical_sensitivity == next)
        return g_mouse.vertical_sensitivity;
    g_mouse.vertical_sensitivity = next;
    log_event("settings-vertical-sensitivity");
    return g_mouse.vertical_sensitivity;
}

extern "C" int disruptor_mouse_invert_vertical(void) {
    load_configuration();
    return g_mouse.invert_vertical ? 1 : 0;
}

extern "C" void disruptor_mouse_set_invert_vertical(int inverted) {
    load_configuration();
    const bool next = inverted != 0;
    if (g_mouse.invert_vertical == next) return;
    g_mouse.invert_vertical = next;
    log_event(next ? "settings-invert-y-on" : "settings-invert-y-off");
}

extern "C" double disruptor_mouse_vertical_pitch(void) {
    return g_mouse.vertical_pitch;
}

extern "C" void disruptor_mouse_set_vertical_pitch(double pitch) {
    g_mouse.vertical_pitch = pitch;
}

extern "C" void disruptor_mouse_recenter_vertical(void) {
    load_configuration();
    reset_vertical_pitch("settings-vertical-recenter");
}

extern "C" int disruptor_mouse_captured(void) {
    return g_mouse.captured ? 1 : 0;
}

extern "C" int disruptor_mouse_set_captured(int captured) {
    load_configuration();
    const bool next = captured != 0;
    if (next && !g_mouse.mouse_aim_enabled &&
        !g_mouse.vertical_look_enabled &&
        !g_mouse.modern_controls_enabled) {
        return 0;
    }
    set_capture(next, next ? "settings-capture" : "settings-release");
    if (next && g_mouse.captured) {
        /* A host UI can restore capture from inside its SDL event callback.
         * Synchronise the polled edge detectors with that same event so a
         * still-held Escape or middle button cannot immediately release the
         * newly restored capture in mouse_aim_frame(). */
        const Uint8 *keys = SDL_GetKeyboardState(nullptr);
        const uint32_t buttons = get_mouse_buttons();
        g_mouse.escape_was_down =
            keys && keys[SDL_SCANCODE_ESCAPE] != 0;
        g_mouse.middle_was_down = (buttons & SDL_BUTTON_MMASK) != 0;
        g_mouse.left_was_down = g_mouse.swallow_left = (buttons & SDL_BUTTON_LMASK) != 0;
    }
    return g_mouse.captured == next ? 1 : 0;
}

disruptor::sticks::Tuning disruptor::sticks::tuning() { return g_mouse.sticks; }

void disruptor::sticks::tune(const Tuning &tuning) { g_mouse.sticks = tuning; }
