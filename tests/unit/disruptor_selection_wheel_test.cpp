#include "psx_sdl.h"
#include "psx_keybinds.h"
#include "game/session/capsule.h"
#include "game/settings/second_keys.h"

#include <array>
#include <cstring>
#include <iostream>

namespace {
SDL_Window g_window;
std::array<uint8_t, 0x200000> g_ram{};
std::array<uint8_t, 512> g_keys{};
std::array<bool, 512> g_typing{};
std::array<bool, 512> g_bound{};
std::array<uint8_t, 4> g_sticks{128, 128, 128, 128};
uint16_t g_pad = 0xFFFF;
uint32_t g_mouse_buttons = 0;
float g_dx = 0, g_dy = 0;
int g_modern = 1, g_captured = 1, g_ready = 1, g_netplay = 0, g_ui = 0;
int g_weapon_key = SDL_SCANCODE_Q, g_psionic_key = SDL_SCANCODE_R;
SDL_WindowFlags g_flags = SDL_WINDOW_INPUT_FOCUS;
disruptor::capsule::Frame g_recorded;
int g_writes = 0, g_failures = 0, g_chosen = -1;
}

extern "C" {
SDL_Window *sdl_window = &g_window;
uint8_t psx_read_byte(uint32_t address) { return g_ram[address & 0x1FFFFFu]; }
uint32_t psx_read_word(uint32_t address) {
    uint32_t value = 0;
    std::memcpy(&value, g_ram.data() + (address & 0x1FFFFFu), sizeof value);
    return value;
}
void psx_host_write_byte(uint32_t address, uint8_t value) { g_ram[address & 0x1FFFFFu] = value; ++g_writes; }
uint16_t sio_get_pad_buttons_slot(int) { return g_pad; }
void sio_set_pad_state_slot(int, uint16_t buttons) { g_pad = buttons; }
void sio_get_pad_sticks(int, uint8_t out[4]) { std::memcpy(out, g_sticks.data(), 4); }
void sio_set_pad_sticks(int, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry) { g_sticks = {lx, ly, rx, ry}; }
int disruptor_modern_controls_enabled() { return g_modern; }
int disruptor_mouse_captured() { return g_captured; }
int disruptor_vertical_camera_input_allowed() { return g_ready; }
int psx_netplay_active() { return g_netplay; }
int psx_host_ui_game_input_captured() { return g_ui; }
void disruptor_capsule_note_byte(uint32_t address, uint8_t value) { g_recorded.note_byte(address, value); }
void disruptor_weapon_chosen(uint8_t weapon) { g_chosen = weapon; }
void disruptor_name_entry_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count) {
    for (int i = 0; i < count; ++i) kept[i] = g_typing[static_cast<size_t>(i)] ? 0 : keys[i];
}
SDL_Scancode psx_keybinds_get_button(int, int button) { return button == PSX_KB_L1 ? g_weapon_key : g_psionic_key; }
int disruptor_key_bound(int scancode) { return g_bound[static_cast<size_t>(scancode)] ? 1 : 0; }
int disruptor_second_key_held(int field, const uint8_t *keys, int key_total, uint32_t mouse) {
    namespace keys_of = disruptor::settings;
    const keys_of::SecondCodes codes =
        keys_of::second_codes(keys_of::default_second_keys(g_modern != 0), [](std::string_view) { return 0; });
    return keys_of::second_held(codes[static_cast<size_t>(field)], keys, static_cast<size_t>(key_total), mouse) ? 1 : 0;
}
}

const Uint8 *SDL_GetKeyboardState(int *) { return g_keys.data(); }
SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *) { return g_flags; }
uint32_t SDL_GetMouseState(float *, float *) { return g_mouse_buttons; }
uint32_t SDL_GetRelativeMouseState(float *x, float *y) {
    if (x) *x = g_dx;
    if (y) *y = g_dy;
    g_dx = g_dy = 0;
    return g_mouse_buttons;
}

#include "../../src/game/input/selection_wheel.cpp"

namespace {

void expect(bool value, const char *message) {
    if (!value) { ++g_failures; std::cerr << "FAIL: " << message << '\n'; }
}
void word(uint32_t address, uint32_t value) { std::memcpy(g_ram.data() + (address & 0x1FFFFFu), &value, 4); }
int frame(uint16_t pressed = 0) {
    g_pad = static_cast<uint16_t>(~pressed);
    return disruptor_selection_wheel_frame();
}
void reset(bool enabled = true) {
    g_wheel = State{};
    g_ram.fill(0); g_keys.fill(0); g_typing.fill(false); g_bound.fill(false);
    g_sticks = {128, 128, 128, 128}; g_mouse_buttons = 0; g_dx = g_dy = 0;
    g_modern = g_captured = g_ready = 1; g_netplay = g_ui = 0;
    g_flags = SDL_WINDOW_INPUT_FOCUS;
    g_weapon_key = SDL_SCANCODE_Q; g_psionic_key = SDL_SCANCODE_R;
    g_recorded = {}; g_writes = 0; g_chosen = -1;
    for (uint32_t i = 0; i < 10; ++i) word(0x800770F4u + i * 4, 1);
    for (uint32_t i = 0; i < 5; ++i) word(0x80077B5Cu + i * 4, 1);
    constexpr std::array<uint8_t, 10> primary = {7, 1, 1, 2, 2, 4, 6, 6, 7, 7};
    constexpr std::array<uint8_t, 10> secondary = {7, 0, 0, 3, 3, 5, 0, 0, 7, 7};
    std::copy(primary.begin(), primary.end(), g_ram.begin() + 0x56A54);
    std::copy(secondary.begin(), secondary.end(), g_ram.begin() + 0x56A60);
    g_ram[0x77627] = 1; g_ram[0x77629] = 0;
    word(0x80077668u, 100);
    disruptor_selection_wheels_set_available(1);
    disruptor_selection_wheels_set_enabled(enabled);
    frame(); // Neither selector held: arm fresh presses.
}

void test_default_and_vanilla_fallback() {
    reset(false);
    expect(!disruptor_selection_wheels_enabled() && !frame(kWeapon), "both wheels start disabled");
    expect(g_pad == static_cast<uint16_t>(~kWeapon), "disabled mode leaves the vanilla selector intact");
    expect(disruptor_selection_wheel_filter_press(kSelectors | kConfirm) == (kSelectors | kConfirm),
           "disabled wheels do not remove modern controls");
    reset(); disruptor_selection_wheels_set_available(0);
    expect(!frame(kPsionic) && g_pad == static_cast<uint16_t>(~kPsionic), "a session without an overlay keeps vanilla controls");
}

void test_bindings_and_input_capture() {
    for (int source = 0; source < 4; ++source) {
        reset();
        if (source == 0) g_keys[SDL_SCANCODE_Q] = 1;
        if (source == 1) { g_modern = 0; g_weapon_key = SDL_SCANCODE_E; g_keys[SDL_SCANCODE_E] = 1; }
        if (source == 2) g_mouse_buttons = SDL_BUTTON_X1MASK;
        frame(source == 3 ? kWeapon : 0);
        expect(view().kind == Kind::weapons && g_pad == 0xFFFFu, "keyboard, rebound key, side button and mapped controller open the weapon wheel");
        expect(disruptor_selection_wheel_filter_press(kSelectors | kConfirm) == kConfirm,
               "modern control injection cannot reintroduce the vanilla selectors");
        frame(kWeapon | kConfirm); // No movement, aiming, firing or buttons reach the guest.
        expect(g_pad == 0xFFFFu, "wheel frames neutralise the pad, including confirmation");
    }
    reset(); g_keys[SDL_SCANCODE_R] = 1; frame();
    expect(view().kind == Kind::psionics && view().count == 5, "the same toggle enables the psionic wheel on its binding");
    reset(); for (uint32_t i = 0; i < 5; ++i) word(0x80077B5Cu + i * 4, 0);
    g_keys[SDL_SCANCODE_R] = 1; frame();
    expect(!disruptor_selection_wheel_open(), "the psionic wheel stays shut before the first psionic is found");
    g_keys[SDL_SCANCODE_R] = 0; frame(); word(0x80077B60u, 1); g_keys[SDL_SCANCODE_R] = 1; frame();
    expect(view().kind == Kind::psionics && view().highlighted == 1, "and opens on the first psionic picked up");
    reset(); g_weapon_key = SDL_SCANCODE_E; g_keys[SDL_SCANCODE_Q] = 1; frame();
    expect(!disruptor_selection_wheel_open(), "Q opens no wheel once the weapon choice is bound to another key");
    reset(); g_psionic_key = SDL_SCANCODE_F; g_keys[SDL_SCANCODE_R] = 1; frame();
    expect(!disruptor_selection_wheel_open(), "R opens no wheel once the psionic choice is bound to another key");
}

void test_equip_and_record() {
    reset(); frame(kWeapon); g_dx = 100; frame(kWeapon);
    const int picked = view().highlighted;
    expect(picked == 3, "moving right highlights Phase Rifle in the ten-slot wheel");
    frame();
    expect(!disruptor_selection_wheel_open() && psx_read_byte(0x80077627u) == 3 && g_writes == 1,
           "releasing the selector equips exactly one owned weapon");
    expect(g_recorded.byte_at == 0x80077627u && g_recorded.byte == 3, "a weapon wheel selection is recorded");
    expect(g_chosen == 3, "and told as the player's choice");
    reset(); frame(kPsionic); g_dx = 100; frame(kPsionic); frame();
    expect(g_chosen == -1, "a psionic is no weapon choice");
    expect(psx_read_byte(0x80077629u) == 1 && g_writes == 1 && g_recorded.byte_at == 0x80077629u,
           "the psionic wheel selects Drain and records only its selected ID");
    reset(); g_ram[0x77627] = 0; frame(kWeapon); frame();
    expect(g_chosen == 0 && g_recorded.byte_at == 0x80077627u && g_recorded.byte == 0,
           "confirming the baton already in hand still tells the player's choice and records it");
    expect(psx_read_word(0x80077668u) == 100 && psx_read_word(0x80077B60u) == 1,
           "selecting a psionic changes neither energy nor ownership");
}

void test_ammo_totals() {
    reset();
    constexpr std::array<uint32_t, 8> ammo = {0, 123, 34, 17, 8, 9, 99, 1};
    for (uint32_t i = 0; i < ammo.size(); ++i) word(0x80056A90u + i * 4u, ammo[i]);
    frame(kWeapon);
    constexpr std::array<int, 10> expected = {-1, 123, 123, 51, 51, 17, 99, 99, -1, -1};
    for (size_t i = 0; i < expected.size(); ++i) {
        if (view().entries[i].resource != expected[i]) {
            ++g_failures;
            std::cerr << "FAIL: " << view().entries[i].name << " ammo: expected " << expected[i]
                      << ", got " << view().entries[i].resource << '\n';
        }
    }
    word(0x80056A90u + 2u * 4u, 0);
    word(0x80056A90u + 4u * 4u, 0);
    frame(kWeapon);
    expect(view().entries[3].resource == 17 && view().entries[4].resource == 17 &&
               view().entries[5].resource == 9, "secondary ammo remains visible when the primary pool is empty");
    for (uint32_t i = 1; i < 7; ++i) word(0x80056A90u + i * 4u, 0);
    frame(kWeapon);
    for (size_t i = 1; i < 8; ++i) expect(view().entries[i].resource == 0, "empty ammo pools display zero");
    word(0x80056A90u + 6u * 4u, 98);
    frame(kWeapon);
    expect(view().entries[6].resource == 98 && view().entries[7].resource == 98,
           "live ammo changes refresh both weapons sharing the pool");
    expect(g_writes == 0, "reading ammo never changes guest inventory or equipment");
}

void test_locked_items_and_navigation() {
    reset(); word(0x800770FCu, 0); frame(kWeapon); frame(kWeapon | kRight);
    expect(view().highlighted == 3, "keyboard and D-pad navigation skip unowned weapons");
    frame(kWeapon); frame(kWeapon | kLeft);
    expect(view().highlighted == 1, "reverse navigation also skips unowned weapons");
    reset(); word(0x800770FCu, 0); frame(kWeapon); g_keys[SDL_SCANCODE_DOWN] = 1; frame(kWeapon);
    expect(view().highlighted == 3, "the Modern scheme's free Down arrow steps through the wheel");
    reset(); word(0x800770FCu, 0); frame(kWeapon); frame(kWeapon | kRight); frame(kWeapon);
    g_keys[SDL_SCANCODE_UP] = 1; frame(kWeapon);
    expect(view().highlighted == 1, "and its free Up arrow steps back");
    reset(); g_bound[SDL_SCANCODE_DOWN] = true; frame(kWeapon); g_keys[SDL_SCANCODE_DOWN] = 1; frame(kWeapon);
    expect(view().highlighted == 1, "a Down arrow bound to an action steps only through that action's button");
    reset(); g_modern = 0; frame(kWeapon); g_keys[SDL_SCANCODE_DOWN] = 1; frame(kWeapon);
    expect(view().highlighted == 1, "the Classic scheme steps only through the arrows its key layout binds");
    reset(); word(0x80077100u, 0); frame(kWeapon); g_dx = 100; frame(kWeapon); frame();
    expect(psx_read_byte(0x80077627u) == 1 && g_writes == 0, "pointing at a locked slice cannot equip it");
    reset(); frame(kPsionic); g_dx = 100; frame(kPsionic); word(0x80077B60u, 0); frame();
    expect(g_writes == 0, "ownership is rechecked at confirmation");
    reset(); frame(kPsionic); g_sticks = {255, 128, 128, 128}; frame(kPsionic);
    expect(view().highlighted == 1 && g_sticks[0] == 128, "the left stick points at a psionic while guest sticks remain centered");
    reset(); frame(kPsionic); disruptor_selection_wheel_controller_axis(0, 32767); frame(kPsionic);
    expect(view().highlighted == 1, "the host left stick also points when the emulated pad is digital");
    reset(); frame(kWeapon); disruptor_selection_wheel_controller_axis(0, 32767); frame(kWeapon | kRight);
    expect(view().highlighted == 3, "a digital pad's folded stick direction cannot also cycle past the pointed slice");
}

void test_early_sample_guard() {
    reset();
    g_pad = static_cast<uint16_t>(~(kWeapon | kUp));
    disruptor_selection_wheel_guard_input();
    expect((g_pad & kWeapon) != 0 && (g_pad & kUp) == 0,
           "enabled wheels suppress original selectors on skipped frames and preserve idle gameplay movement");
    disruptor_selection_wheel_frame();
    expect(disruptor_selection_wheel_open() && g_pad == 0xFFFFu,
           "a guarded selector still opens the wheel when the frame is presented");
    reset(); frame(kWeapon);
    g_pad = static_cast<uint16_t>(~(kWeapon | kConfirm));
    g_sticks = {255, 128, 128, 128};
    disruptor_selection_wheel_guard_input();
    expect(g_pad == 0xFFFFu && g_sticks[0] == 128, "an early sample is neutral even if fast-forward skips the frame hook");
    disruptor_selection_wheel_frame();
    expect(g_writes == 1 && psx_read_byte(0x80077627u) == 3,
           "a presented frame can still consume the original guarded controller input");
}

void test_cancellation_and_held_buttons() {
    reset(); frame(kWeapon); g_dx = 100; frame(kWeapon); g_keys[SDL_SCANCODE_ESCAPE] = 1; frame(kWeapon);
    expect(!disruptor_selection_wheel_open() && g_writes == 0, "Escape cancels without changing equipment");
    frame(kWeapon); expect(!disruptor_selection_wheel_open() && (g_pad & kWeapon), "a canceled selector held down cannot reopen or reach the vanilla menu");
    g_keys[SDL_SCANCODE_ESCAPE] = 0; frame(); frame(kWeapon);
    expect(disruptor_selection_wheel_open(), "a new press after release opens again");
    disruptor_selection_wheel_cancel();
    expect(frame(kWeapon | kConfirm) && g_pad == 0xFFFFu, "event-side cancellation consumes the closing frame too");
    reset(); frame(kPsionic); g_mouse_buttons = SDL_BUTTON_LMASK; frame(kPsionic);
    expect(!disruptor_selection_wheel_open(), "left-click confirms the wheel");
    frame();
    expect((disruptor_selection_wheel_filter_press(kConfirm) & kConfirm) == 0,
           "the confirmation click cannot become a shot while held after closing");
    g_mouse_buttons = 0; frame();
    expect(disruptor_selection_wheel_filter_press(kConfirm) == kConfirm, "fire becomes available again after release");
    reset(); frame(kWeapon); g_mouse_buttons = SDL_BUTTON_RMASK; frame(kWeapon); frame();
    expect(!disruptor_selection_wheel_open() && g_writes == 0 &&
               disruptor_selection_wheel_filter_press(kPsionicAttack) == 0,
           "holding the right-click cancellation cannot become a psionic attack");
    g_mouse_buttons = 0; frame();
    expect(disruptor_selection_wheel_filter_press(kPsionicAttack) == kPsionicAttack,
           "psionic attack becomes available again after releasing right-click");
}

void test_guards_and_enable_edges() {
    for (int guard = 0; guard < 5; ++guard) {
        reset();
        if (guard == 0) g_ready = 0;
        if (guard == 1) g_netplay = 1;
        if (guard == 2) g_ui = 1;
        if (guard == 3) g_flags = 0;
        if (guard == 4) g_ram[0x7145C] = 2;
        frame(kWeapon);
        expect(!disruptor_selection_wheel_open() && g_writes == 0, "menus, transitions, lost focus and netplay cannot open wheels");
        g_ready = 1; g_netplay = g_ui = 0; g_flags = SDL_WINDOW_INPUT_FOCUS; g_ram[0x7145C] = 0;
        frame(kWeapon);
        expect(!disruptor_selection_wheel_open(), "returning to gameplay while holding a selector cannot open a wheel");
    }
    reset(false); frame(kWeapon); disruptor_selection_wheels_set_enabled(1); frame(kWeapon);
    expect(!disruptor_selection_wheel_open(), "enabling wheels while a binding is held requires a fresh press");
    frame(); frame(kWeapon); expect(disruptor_selection_wheel_open(), "a fresh press opens after enabling");
    g_ready = 0; frame(); expect(g_writes == 0 && !disruptor_selection_wheel_open(), "losing gameplay eligibility cancels an open wheel");
}

void test_sector_boundaries() {
    expect(sector(0, 0, 10) == -1 && sector(0.1f, 0.1f, 5) == -1, "the center is a stable dead zone");
    expect(sector(0, -1, 10) == 0 && sector(1, 0, 10) == 3 && sector(0, 1, 10) == 5 && sector(-1, 0, 10) == 8,
           "weapon slices run clockwise from the baton at the top");
    expect(sector(0, -1, 5) == 0 && sector(1, 0, 5) == 1 && sector(0, 1, 5) == 3 && sector(-1, 0, 5) == 4,
           "psionic slices run clockwise from Shock at the top");
}
}

int main() {
    test_default_and_vanilla_fallback(); test_bindings_and_input_capture(); test_equip_and_record();
    test_locked_items_and_navigation(); test_cancellation_and_held_buttons(); test_guards_and_enable_edges(); test_sector_boundaries();
    test_early_sample_guard();
    test_ammo_totals();
    if (g_failures) return 1;
    std::cout << "Weapon and psionic wheel tests passed\n";
    return 0;
}
