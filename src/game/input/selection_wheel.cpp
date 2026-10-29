#include "game/input/selection_wheel.h"

#include "game/input/baton_choice.h"
#include "game/input/mouse_aim.h"
#include "game/input/vertical_camera.h"
#include "game/session/capsule.h"
#include "psx_keybinds.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

extern "C" uint8_t psx_read_byte(uint32_t address);
extern "C" uint32_t psx_read_word(uint32_t address);
extern "C" void psx_host_write_byte(uint32_t address, uint8_t value);
extern "C" uint16_t sio_get_pad_buttons_slot(int slot);
extern "C" void sio_set_pad_state_slot(int slot, uint16_t buttons);
extern "C" void sio_get_pad_sticks(int slot, uint8_t out[4]);
extern "C" void sio_set_pad_sticks(int slot, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry);
extern "C" int psx_netplay_active(void);
extern "C" int psx_host_ui_game_input_captured(void);
extern "C" SDL_Window *sdl_window;
extern "C" void disruptor_name_entry_keys_for_pad(const uint8_t *, uint8_t *, int);
extern "C" int disruptor_key_bound(int scancode);

namespace disruptor::wheel {
namespace {

constexpr uint16_t kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7;
constexpr uint16_t kWeapon = 1u << 10, kPsionic = 1u << 11;
constexpr uint16_t kCancel = 1u << 13, kConfirm = 1u << 14;
constexpr uint16_t kPsionicAttack = 1u << 15;
constexpr uint16_t kSelectors = kWeapon | kPsionic;
constexpr uint32_t kSelectedWeapon = 0x80077627u, kSelectedPsionic = 0x80077629u;
constexpr uint32_t kWeaponOwned = 0x800770F4u, kPsionicOwned = 0x80077B5Cu;
constexpr uint32_t kAmmo = 0x80056A90u;
constexpr uint32_t kPrimaryAmmoIndex = 0x80056A54u, kSecondaryAmmoIndex = 0x80056A60u;
constexpr uint32_t kScene = 0x8007145Cu, kEnergy = 0x80077668u;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::array<const char *, 10> kWeapons = {
    "Baton", "18mm Semi", "18mm Auto", "Phase Rifle", "Phase Repeater",
    "Lockon Cannon", "AM Blaster", "AM Cyclone", "Zodiac", "Plasma Lance",
};
constexpr std::array<const char *, 5> kPsionics = {"Shock", "Drain", "Heal", "Shield", "Blast"};

struct State {
    bool enabled = false;
    bool available = false;
    bool armed = false;
    bool handling = false;
    bool closing = false;
    uint16_t previous = 0;
    uint16_t swallowed = 0;
    bool escape_down = false;
    bool guarded = false;
    uint16_t guarded_pad = 0xFFFFu;
    uint16_t guarded_output = 0xFFFFu;
    std::array<uint8_t, 4> guarded_sticks{128, 128, 128, 128};
    float controller_x = 0.0f, controller_y = 0.0f;
    View view;
};
State g_wheel;

bool key_down(const uint8_t *keys, int key) {
    return keys && key > 0 && key < 512 && keys[key] != 0;
}

void motion(float &x, float &y) {
#if defined(PSX_SDL3)
    SDL_GetRelativeMouseState(&x, &y);
#else
    int dx = 0, dy = 0;
    SDL_GetRelativeMouseState(&dx, &dy);
    x = static_cast<float>(dx);
    y = static_cast<float>(dy);
#endif
}

uint32_t mouse_buttons() {
#if defined(PSX_SDL3)
    return SDL_GetMouseState(nullptr, nullptr);
#else
    return static_cast<uint32_t>(SDL_GetMouseState(nullptr, nullptr));
#endif
}

bool gameplay_ready() {
    return sdl_window && (SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_INPUT_FOCUS) != 0 &&
           !psx_host_ui_game_input_captured() && !psx_netplay_active() &&
           !std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") &&
           disruptor_vertical_camera_input_allowed() && psx_read_byte(kScene) == 0u;
}

void inventory() {
    View &view = g_wheel.view;
    const bool weapons = view.kind == Kind::weapons;
    view.count = weapons ? 10 : 5;
    const uint8_t equipped = psx_read_byte(weapons ? kSelectedWeapon : kSelectedPsionic);
    const uint32_t owned = weapons ? kWeaponOwned : kPsionicOwned;
    for (int i = 0; i < view.count; ++i) {
        Entry &entry = view.entries[static_cast<size_t>(i)];
        entry.name = weapons ? kWeapons[static_cast<size_t>(i)] : kPsionics[static_cast<size_t>(i)];
        entry.owned = psx_read_word(owned + static_cast<uint32_t>(i) * 4u) == 1u;
        entry.equipped = equipped == i;
        entry.resource = -1;
        // The native HUD totals both pools for IDs 1..7, the rest show no ammo.
        if (weapons && i > 0 && i < 8) {
            const uint8_t primary = psx_read_byte(kPrimaryAmmoIndex + static_cast<uint32_t>(i));
            const uint8_t secondary = psx_read_byte(kSecondaryAmmoIndex + static_cast<uint32_t>(i));
            if (primary < 8u && secondary < 8u) {
                entry.resource = static_cast<int>(psx_read_word(kAmmo + primary * 4u) +
                                                  psx_read_word(kAmmo + secondary * 4u));
            }
        } else if (!weapons) {
            entry.resource = static_cast<int>(psx_read_word(kEnergy));
        }
    }
}

void equip() {
    if (!gameplay_ready()) return;
    inventory();
    const View &view = g_wheel.view;
    const int picked = view.highlighted;
    if (picked < 0 || picked >= view.count || !view.entries[static_cast<size_t>(picked)].owned) return;
    const uint32_t address = view.kind == Kind::weapons ? kSelectedWeapon : kSelectedPsionic;
    const uint8_t current = psx_read_byte(address);
    const bool baton = view.kind == Kind::weapons && picked == 0;
    if ((current >= view.count && current != 0xFFu) || (current == picked && !baton)) return;
    psx_host_write_byte(address, static_cast<uint8_t>(picked));
    disruptor_capsule_note_byte(address, static_cast<uint8_t>(picked));
    if (view.kind == Kind::weapons) disruptor_weapon_chosen(static_cast<uint8_t>(picked));
}

void cycle(int direction) {
    View &view = g_wheel.view;
    int picked = view.highlighted < 0 ? 0 : view.highlighted;
    for (int i = 0; i < view.count; ++i) {
        picked = (picked + direction + view.count) % view.count;
        if (view.entries[static_cast<size_t>(picked)].owned) {
            view.highlighted = picked;
            return;
        }
    }
}

void close(uint16_t pressed) {
    g_wheel.closing |= g_wheel.view.kind != Kind::none;
    g_wheel.view = View{};
    g_wheel.armed = false;
    g_wheel.swallowed |= pressed & (kSelectors | kConfirm | kCancel | kPsionicAttack);
}

} // namespace

const View &view() { return g_wheel.view; }

int sector(float x, float y, int count) {
    if (count <= 0 || !std::isfinite(x) || !std::isfinite(y) || x * x + y * y < 0.0625f) return -1;
    float angle = std::atan2(y, x) + kPi / 2.0f + kPi / static_cast<float>(count);
    if (angle < 0.0f) angle += 2.0f * kPi;
    return static_cast<int>(angle * static_cast<float>(count) / (2.0f * kPi)) % count;
}

} // namespace disruptor::wheel

using namespace disruptor::wheel;

extern "C" int disruptor_selection_wheels_enabled(void) { return g_wheel.enabled ? 1 : 0; }
extern "C" int disruptor_selection_wheel_open(void) { return g_wheel.view.kind != Kind::none; }

extern "C" void disruptor_selection_wheel_cancel(void) {
    if (disruptor_selection_wheel_open()) close(g_wheel.previous);
    else g_wheel.armed = false;
}

extern "C" void disruptor_selection_wheels_set_enabled(int enabled) {
    if (g_wheel.enabled == (enabled != 0)) return;
    disruptor_selection_wheel_cancel();
    g_wheel.enabled = enabled != 0;
    g_wheel.handling = false;
    if (!g_wheel.enabled) g_wheel.swallowed = 0;
}

extern "C" void disruptor_selection_wheels_set_available(int available) {
    disruptor_selection_wheel_cancel();
    g_wheel.available = available != 0;
    g_wheel.handling = false;
    g_wheel.controller_x = g_wheel.controller_y = 0.0f;
    if (!g_wheel.available) g_wheel.swallowed = 0;
}

extern "C" uint16_t disruptor_selection_wheel_filter_press(uint16_t press) {
    return static_cast<uint16_t>(press & ~(g_wheel.swallowed | (g_wheel.handling ? kSelectors : 0u)));
}

extern "C" void disruptor_selection_wheel_controller_axis(int axis, int value) {
    const float position = std::clamp(static_cast<float>(value) / 32768.0f, -1.0f, 1.0f);
    if (axis == 0) g_wheel.controller_x = position;
    if (axis == 1) g_wheel.controller_y = position;
}

extern "C" void disruptor_selection_wheel_guard_input(void) {
    g_wheel.guarded = g_wheel.enabled && g_wheel.available && gameplay_ready();
    if (!g_wheel.guarded) return;
    g_wheel.guarded_pad = sio_get_pad_buttons_slot(0);
    sio_get_pad_sticks(0, g_wheel.guarded_sticks.data());
    const bool open = disruptor_selection_wheel_open() != 0;
    g_wheel.guarded_output = open ? 0xFFFFu : static_cast<uint16_t>(g_wheel.guarded_pad | kSelectors);
    sio_set_pad_state_slot(0, g_wheel.guarded_output);
    if (open) sio_set_pad_sticks(0, 128, 128, 128, 128);
}

extern "C" int disruptor_selection_wheel_frame(void) {
    const bool was_open = disruptor_selection_wheel_open() != 0;
    g_wheel.handling = false;
    if (!g_wheel.enabled || !g_wheel.available || !sdl_window) return 0;

    std::array<uint8_t, 512> keys{};
    if (const auto *raw = SDL_GetKeyboardState(nullptr))
        disruptor_name_entry_keys_for_pad(raw, keys.data(), static_cast<int>(keys.size()));
    const uint16_t pad = sio_get_pad_buttons_slot(0);
    const bool guarded = g_wheel.guarded && pad == g_wheel.guarded_output;
    uint16_t pressed = static_cast<uint16_t>(~(guarded ? g_wheel.guarded_pad : pad));
    g_wheel.guarded = false;
    const uint32_t mouse = mouse_buttons();
    const uint32_t held_mouse = disruptor_mouse_captured() ? mouse : 0u;
    for (const auto [field, selector] : {std::pair{PSX_KB_L1, kWeapon}, std::pair{PSX_KB_R1, kPsionic}})
        if (key_down(keys.data(), psx_keybinds_get_button(1, field)) ||
            disruptor_second_key_held(field, keys.data(), static_cast<int>(keys.size()), held_mouse))
            pressed |= selector;
    if (disruptor_modern_controls_enabled()) {
        if (key_down(keys.data(), SDL_SCANCODE_UP) && !disruptor_key_bound(SDL_SCANCODE_UP)) pressed |= kUp;
        if (key_down(keys.data(), SDL_SCANCODE_DOWN) && !disruptor_key_bound(SDL_SCANCODE_DOWN)) pressed |= kDown;
    }
    if (mouse & SDL_BUTTON_LMASK) pressed |= kConfirm;
    if (mouse & SDL_BUTTON_RMASK) pressed |= kCancel | kPsionicAttack;
    if (key_down(keys.data(), SDL_SCANCODE_RETURN)) pressed |= kConfirm;
    const bool escape = key_down(keys.data(), SDL_SCANCODE_ESCAPE);
    const bool cancel = escape && !g_wheel.escape_down;
    g_wheel.escape_down = escape;
    const uint16_t edges = static_cast<uint16_t>(pressed & ~g_wheel.previous);
    g_wheel.previous = pressed;
    g_wheel.swallowed &= pressed;
    const uint16_t selectors = pressed & kSelectors;

    if (!gameplay_ready()) {
        close(pressed);
        const bool consumed = was_open || g_wheel.closing;
        g_wheel.closing = false;
        if (consumed) {
            sio_set_pad_state_slot(0, 0xFFFFu);
            sio_set_pad_sticks(0, 128, 128, 128, 128);
        }
        return consumed ? 1 : 0;
    }
    g_wheel.handling = true;
    bool opened = false;
    if (!was_open) {
        if (selectors == 0u) g_wheel.armed = true;
        if (g_wheel.armed && (edges & kSelectors) && selectors != kSelectors) {
            g_wheel.view.kind = selectors & kWeapon ? Kind::weapons : Kind::psionics;
            inventory();
            for (int i = 0; i < g_wheel.view.count; ++i) {
                const Entry &entry = g_wheel.view.entries[static_cast<size_t>(i)];
                if (entry.owned && (g_wheel.view.highlighted < 0 || entry.equipped))
                    g_wheel.view.highlighted = i;
            }
            if (g_wheel.view.highlighted < 0) {
                g_wheel.view = View{};
            } else {
                opened = true;
                float dx = 0, dy = 0;
                motion(dx, dy); // Opening never carries an old aiming movement into the wheel.
            }
        }
    }
    if (disruptor_selection_wheel_open()) {
        inventory();
        if (!opened) {
            float dx = 0, dy = 0;
            motion(dx, dy);
            View &view = g_wheel.view;
            bool pointed = false;
            if (std::isfinite(dx) && std::isfinite(dy) && (dx != 0 || dy != 0)) {
                pointed = true;
                view.pointer_x += std::clamp(dx, -1024.0f, 1024.0f) / 100.0f;
                view.pointer_y += std::clamp(dy, -1024.0f, 1024.0f) / 100.0f;
            } else {
                uint8_t sticks[4] = {128, 128, 128, 128};
                if (guarded) std::copy(g_wheel.guarded_sticks.begin(), g_wheel.guarded_sticks.end(), sticks);
                else sio_get_pad_sticks(0, sticks);
                float x = (static_cast<float>(sticks[0]) - 128.0f) / 128.0f;
                float y = (static_cast<float>(sticks[1]) - 128.0f) / 128.0f;
                if (x * x + y * y <= 0.09f) { x = g_wheel.controller_x; y = g_wheel.controller_y; }
                if (x * x + y * y > 0.09f) { view.pointer_x = x; view.pointer_y = y; pointed = true; }
            }
            const float length = std::hypot(view.pointer_x, view.pointer_y);
            if (length > 1.0f) { view.pointer_x /= length; view.pointer_y /= length; }
            if (const int picked = sector(view.pointer_x, view.pointer_y, view.count); picked >= 0)
                view.highlighted = picked;
            if (!pointed && (edges & (kLeft | kUp))) { cycle(-1); view.pointer_x = view.pointer_y = 0; }
            else if (!pointed && (edges & (kRight | kDown))) { cycle(1); view.pointer_x = view.pointer_y = 0; }
            const uint16_t trigger = view.kind == Kind::weapons ? kWeapon : kPsionic;
            if (cancel || (edges & kCancel)) close(pressed);
            else if (!(selectors & trigger) || (edges & kConfirm)) { equip(); close(pressed); }
        }
    }
    const bool consumed = was_open || opened || g_wheel.closing;
    g_wheel.closing = false;
    const uint16_t blocked = static_cast<uint16_t>(g_wheel.swallowed | kSelectors);
    sio_set_pad_state_slot(0, consumed ? 0xFFFFu : static_cast<uint16_t>(sio_get_pad_buttons_slot(0) | blocked));
    if (consumed) sio_set_pad_sticks(0, 128, 128, 128, 128);
    return consumed ? 1 : 0;
}
