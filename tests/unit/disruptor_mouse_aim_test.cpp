#include "psx_sdl.h"
#include "../../psxrecomp/runtime/include/gpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace {

SDL_Window g_window;
std::array<std::uint8_t, 2 * 1024 * 1024> g_ram{};
std::array<Uint8, 512> g_keys{};
std::array<bool, 512> g_typed_with{};  /* keys the name entry holds back from the pad */
void (*g_registered_hook)(void) = nullptr;
std::uint32_t g_noted_at = 0;
std::uint8_t g_noted = 0;
int g_notes = 0;
std::uint32_t g_buttons = 0;
std::uint16_t g_pad_buttons = 0xFFFFu;
float g_relative_x = 0.0f;
float g_relative_y = 0.0f;
SDL_WindowFlags g_window_flags = SDL_WINDOW_INPUT_FOCUS;
bool g_relative_mode = false;
bool g_relative_mode_succeeds = true;
std::string g_title;
int g_failures = 0;
int g_ws_margin = 0;
int g_ws_pinned_43 = 1;
int g_ws_native_active = 0;
int g_ws_extra = 0;
double g_camera_yaw_residual = 0.0;
int g_camera_yaw_residual_calls = 0;
int g_host_ui_captured = 0;
double g_requested_vertical_pitch = 0.0;
int g_vertical_pitch_calls = 0;
int g_vertical_recenter_calls = 0;
int g_netplay_active = 0;
int g_vertical_input_allowed = 1;

constexpr std::uint32_t kTestYawAddress = 0x80077624u;

}  // namespace

extern "C" {

SDL_Window *sdl_window = &g_window;

void mod_register_frame_hook(void (*hook)(void)) {
    g_registered_hook = hook;
}

void disruptor_capsule_note_byte(std::uint32_t address, std::uint8_t value) {
    g_noted_at = address;
    g_noted = value;
    ++g_notes;
}

void disruptor_name_entry_keys_for_pad(const std::uint8_t *keys, std::uint8_t *kept, int count) {
    for (int key = 0; key < count; ++key) kept[key] = g_typed_with[static_cast<std::size_t>(key)] ? std::uint8_t{0} : keys[key];
}

int psx_host_ui_game_input_captured(void) {
    return g_host_ui_captured;
}

std::uint8_t psx_read_byte(std::uint32_t address) {
    return g_ram[address & 0x001fffffu];
}

std::uint32_t psx_read_word(std::uint32_t address) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(psx_read_byte(address + i)) << (i * 8);
    return value;
}

void psx_write_byte(std::uint32_t address, std::uint8_t value) {
    g_ram[address & 0x001fffffu] = value;
}

std::uint16_t sio_get_pad_buttons_slot(int slot) {
    return slot == 0 ? g_pad_buttons : 0xFFFFu;
}

void sio_set_pad_state_slot(int slot, std::uint16_t buttons) {
    if (slot == 0) g_pad_buttons = buttons;
}

std::int32_t psx_mod_widescreen_x_margin() {
    return g_ws_margin;
}

int gpu_ws_present_native_43() {
    return g_ws_pinned_43;
}

int ws_native_wide_active() {
    return g_ws_native_active;
}

int ws_nw_extra() {
    return g_ws_extra;
}

void gpu_geometry_camera_yaw_residual_set(double yaw_units) {
    g_camera_yaw_residual = yaw_units;
    ++g_camera_yaw_residual_calls;
}

void disruptor_vertical_camera_set_requested_pitch(double pitch_units) {
    g_requested_vertical_pitch = pitch_units;
    ++g_vertical_pitch_calls;
}

void disruptor_vertical_camera_recenter() {
    g_requested_vertical_pitch = 0.0;
    ++g_vertical_recenter_calls;
}

int disruptor_vertical_camera_input_allowed() {
    return g_vertical_input_allowed;
}

int psx_netplay_active() {
    return g_netplay_active;
}

void gpu_geometry_correction_stats(std::uint64_t *world_triangles,
                                   std::uint64_t *precise_triangles) {
    if (world_triangles) *world_triangles = 0;
    if (precise_triangles) *precise_triangles = 0;
}

std::uint32_t gpu_texture_correction_hits() {
    return 0;
}

void gpu_geometry_correction_stats_detailed(
        GpuGeometryCorrectionStats *stats, std::uint32_t out_size) {
    if (!stats || out_size == 0) return;
    GpuGeometryCorrectionStats snapshot{};
    snapshot.version = GPU_GEOMETRY_DIAGNOSTICS_VERSION;
    snapshot.size = static_cast<std::uint32_t>(sizeof(snapshot));
    const std::size_t copy_size = std::min<std::size_t>(out_size,
                                                        sizeof(snapshot));
    std::memcpy(stats, &snapshot, copy_size);
}

}  // extern "C"

bool SDL_SetWindowRelativeMouseMode(SDL_Window *, bool enabled) {
    if (!g_relative_mode_succeeds) return false;
    g_relative_mode = enabled;
    return true;
}

std::uint32_t SDL_GetMouseState(float *x, float *y) {
    if (x) *x = 0.0f;
    if (y) *y = 0.0f;
    return g_buttons;
}

std::uint32_t SDL_GetRelativeMouseState(float *x, float *y) {
    if (x) *x = g_relative_x;
    if (y) *y = g_relative_y;
    g_relative_x = 0.0f;
    g_relative_y = 0.0f;
    return g_buttons;
}

const Uint8 *SDL_GetKeyboardState(int *count) {
    if (count) *count = static_cast<int>(g_keys.size());
    return g_keys.data();
}

SDL_WindowFlags SDL_GetWindowFlags(SDL_Window *) {
    return g_window_flags;
}

bool SDL_SetWindowTitle(SDL_Window *, const char *title) {
    g_title = title ? title : "";
    return true;
}

const char *SDL_GetError() {
    return "mock relative-mode failure";
}

// Include the implementation so its internal pure helpers and state can be
// exercised against the mocks above without exposing a test-only production ABI.
#include "../../src/game/input/mouse_aim.cpp"

namespace {

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect_close(double actual, double expected, const char *message) {
    expect(std::abs(actual - expected) < 0.000001, message);
}

void reset_state() {
    if (g_mouse.log) std::fclose(g_mouse.log);
    g_mouse = MouseAimState{};
    g_ram.fill(0);
    g_keys.fill(0);
    g_typed_with.fill(false);
    g_buttons = 0;
    g_pad_buttons = 0xFFFFu;
    g_relative_x = 0.0f;
    g_relative_y = 0.0f;
    g_window_flags = SDL_WINDOW_INPUT_FOCUS;
    g_relative_mode = false;
    g_relative_mode_succeeds = true;
    g_title.clear();
    g_ws_margin = 0;
    g_ws_pinned_43 = 1;
    g_ws_native_active = 0;
    g_ws_extra = 0;
    g_camera_yaw_residual = 0.0;
    g_camera_yaw_residual_calls = 0;
    g_host_ui_captured = 0;
    g_requested_vertical_pitch = 0.0;
    g_vertical_pitch_calls = 0;
    g_vertical_recenter_calls = 0;
    g_netplay_active = 0;
    g_vertical_input_allowed = 1;
    g_noted_at = 0;
    g_noted = 0;
    g_notes = 0;
    sdl_window = &g_window;
}

void test_parsing() {
    expect(parse_bool(" YES ", false), "boolean true parsing");
    expect(!parse_bool("off", true), "boolean false parsing");
    expect(parse_bool("invalid", true), "invalid boolean fallback");
    expect_close(parse_sensitivity("0.125", 0.08), 0.125,
                 "decimal sensitivity parsing");
    expect_close(parse_sensitivity("0", 0.08), 0.005,
                 "minimum sensitivity clamp");
    expect_close(parse_sensitivity("99", 0.08), 2.0,
                 "maximum sensitivity clamp");
    expect_close(parse_sensitivity("bad", 0.08), 0.08,
                 "invalid sensitivity fallback");
}

void test_ini_configuration() {
    reset_state();
    std::string original;
    bool had_original = false;
    {
        std::ifstream input("mouse-aim.ini", std::ios::binary);
        if (input) {
            had_original = true;
            original.assign(std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>());
        }
    }
    {
        std::ofstream output("mouse-aim.ini", std::ios::trunc);
        output << "# test configuration\n"
                  "horizontal_sensitivity = 0.375\n"
                  "invert_horizontal = yes\n"
                  "vertical_sensitivity = 0.250\n"
                  "invert_vertical = on\n";
    }
    load_configuration();
    expect_close(g_mouse.horizontal_sensitivity, 0.375,
                 "INI sensitivity load");
    expect(g_mouse.invert_horizontal, "INI inversion load");
    expect_close(g_mouse.vertical_sensitivity, 0.250,
                 "INI vertical sensitivity load");
    expect(g_mouse.invert_vertical, "INI vertical inversion load");
    if (had_original) {
        std::ofstream output("mouse-aim.ini",
                             std::ios::binary | std::ios::trunc);
        output << original;
    } else {
        std::remove("mouse-aim.ini");
    }
}

void test_direction_fraction_and_wrap() {
    reset_state();
    g_mouse.horizontal_sensitivity = 0.08;

    psx_write_byte(kTestYawAddress, 100);
    g_notes = 0;
    apply_mouse_x(25.0);
    expect(psx_read_byte(kTestYawAddress) == 98,
           "rightward motion decreases Disruptor yaw");
    expect(g_notes == 1 && g_noted_at == kTestYawAddress && g_noted == 98,
           "a recording is told the heading the mouse wrote");
    apply_mouse_x(-25.0);
    expect(psx_read_byte(kTestYawAddress) == 100,
           "leftward motion increases Disruptor yaw");

    psx_write_byte(kTestYawAddress, 100);
    g_mouse.fractional_yaw = 0.0;
    g_notes = 0;
    apply_mouse_x(6.0);
    expect(psx_read_byte(kTestYawAddress) == 100,
           "sub-step motion is retained without premature yaw change");
    expect(g_notes == 0, "and a recording is told nothing while nothing is written");
    apply_mouse_x(7.0);
    expect(psx_read_byte(kTestYawAddress) == 99,
           "fractional motion accumulates into one yaw step");
    expect_close(g_mouse.fractional_yaw, -0.04,
                 "fractional remainder is preserved");

    g_mouse.horizontal_sensitivity = 1.0;
    g_mouse.fractional_yaw = 0.0;
    psx_write_byte(kTestYawAddress, 1);
    apply_mouse_x(4.0);
    expect(psx_read_byte(kTestYawAddress) == 253,
           "negative yaw wraps through zero");
    psx_write_byte(kTestYawAddress, 254);
    apply_mouse_x(-4.0);
    expect(psx_read_byte(kTestYawAddress) == 2,
           "positive yaw wraps through 255");

    g_mouse.invert_horizontal = true;
    psx_write_byte(kTestYawAddress, 10);
    apply_mouse_x(3.0);
    expect(psx_read_byte(kTestYawAddress) == 13,
           "horizontal inversion reverses direction");
}

void test_nearest_step_diffusion_and_conservation() {
    reset_state();
    g_mouse.horizontal_sensitivity = 0.20;
    psx_write_byte(kTestYawAddress, 100);

    /* Three rightward counts are -0.6 yaw units.  Nearest-step diffusion
     * responds immediately, but carries +0.4 so the combined guest byte and
     * presentation remainder still equal the exact requested movement. */
    apply_mouse_x(3.0);
    expect(psx_read_byte(kTestYawAddress) == 99,
           "more than half a yaw unit advances without truncation delay");
    expect_close(g_mouse.fractional_yaw, 0.4,
                 "nearest-step extraction carries the opposite signed error");

    /* A direct reversal consumes that carried error first.  It must not leave
     * a one-frame smoothing tail or manufacture a compensating yaw step. */
    apply_mouse_x(-3.0);
    expect(psx_read_byte(kTestYawAddress) == 100,
           "equal reversal restores guest yaw immediately");
    expect_close(g_mouse.fractional_yaw, 0.0,
                 "equal reversal leaves no delayed correction backlog");

    /* Error diffusion must conserve long-run distance in both directions.
     * Seven counts at 0.2 are exactly 1.4 units: one byte plus -0.4 residual
     * rightward, then precisely zero after the mirrored input. */
    for (int i = 0; i < 7; ++i) apply_mouse_x(1.0);
    expect(psx_read_byte(kTestYawAddress) == 99,
           "repeated sub-byte samples diffuse to the nearest guest yaw");
    expect_close(g_mouse.fractional_yaw, -0.4,
                 "repeated samples retain only the exact bounded remainder");
    expect(std::abs(g_mouse.fractional_yaw) <= 0.5,
           "nearest-step remainder never approaches a full yaw byte");

    for (int i = 0; i < 7; ++i) apply_mouse_x(-1.0);
    expect(psx_read_byte(kTestYawAddress) == 100,
           "mirrored sub-byte samples conserve cumulative guest yaw");
    expect_close(g_mouse.fractional_yaw, 0.0,
                 "mirrored sub-byte samples conserve exact total motion");
}

void test_motion_safety_clamp() {
    reset_state();
    g_mouse.horizontal_sensitivity = 2.0;
    psx_write_byte(kTestYawAddress, 10);
    apply_mouse_x(5000.0);
    expect(psx_read_byte(kTestYawAddress) == 202,
           "single-frame yaw delta is clamped to 64 steps");
    expect_close(g_mouse.fractional_yaw, 0.0,
                 "pathological motion does not leave a large backlog");
}

void test_vertical_direction_inversion_and_clamp() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.vertical_sensitivity = 0.08;

    apply_mouse_y(-25.0);
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "mouse up requests positive look pitch");
    expect_close(g_requested_vertical_pitch, 2.0,
                 "vertical sink receives positive look pitch");
    apply_mouse_y(25.0);
    expect_close(g_mouse.vertical_pitch, 0.0,
                 "mouse down returns pitch toward centre");

    g_mouse.invert_vertical = true;
    apply_mouse_y(25.0);
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "vertical inversion reverses direction");

    g_mouse.invert_vertical = false;
    apply_mouse_y(-5000.0);
    expect_close(g_mouse.vertical_pitch, 22.0,
                 "positive pitch is clamped to the safe weapon envelope");
    apply_mouse_y(-5000.0);
    expect_close(g_mouse.vertical_pitch, 22.0,
                 "motion into the pitch stop creates no backlog");
    apply_mouse_y(1.0);
    expect_close(g_mouse.vertical_pitch, 21.92,
                 "reversing at the pitch stop responds immediately");

    g_mouse.vertical_pitch = 0.0;
    apply_mouse_y(5000.0);
    expect_close(g_mouse.vertical_pitch, -22.0,
                 "negative pitch is clamped symmetrically");
}

void test_simultaneous_relative_axes_sample() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.captured = true;
    g_mouse.configured = true;
    g_mouse.horizontal_sensitivity = 0.08;
    g_mouse.vertical_sensitivity = 0.08;
    g_mouse.log = std::tmpfile();
    psx_write_byte(kTestYawAddress, 100);
    g_relative_x = 25.0f;
    g_relative_y = -25.0f;

    mouse_aim_frame();

    expect(psx_read_byte(kTestYawAddress) == 98,
           "one relative sample applies horizontal motion");
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "the same relative sample applies vertical motion");
    expect_close(g_relative_x, 0.0,
                 "relative X is drained once");
    expect_close(g_relative_y, 0.0,
                 "relative Y is drained once");

    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.captured = true;
    g_mouse.configured = true;
    g_mouse.log = std::tmpfile();
    g_relative_y = -25.0f;
    mouse_aim_frame();
    expect_close(g_mouse.vertical_pitch, 0.0,
                 "relative Y is inert while vertical look is disabled");
    expect_close(g_relative_y, 0.0,
                 "disabled vertical motion is still drained");
}

void test_capture_release_and_frame_motion() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.configured = true;
    g_mouse.horizontal_sensitivity = 0.08;
    g_mouse.log = std::tmpfile();
    psx_write_byte(kTestYawAddress, 100);

    mouse_aim_frame();
    expect(g_mouse.captured && g_relative_mode,
           "the game holds the mouse as soon as its window has focus, with no click");

    g_relative_x = 25.0f;
    mouse_aim_frame();
    expect(psx_read_byte(kTestYawAddress) == 98,
           "captured frame applies relative mouse motion");

    g_mouse.vertical_look_enabled = true;
    apply_mouse_y(-25.0);

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "a middle click releases the mouse");
    expect_close(g_mouse.vertical_pitch, 2.0,
                 "capture release preserves the requested vertical view");
    g_buttons = 0;
    mouse_aim_frame();
    mouse_aim_frame();
    expect(!g_mouse.captured, "a mouse the player let go stays free while the window keeps focus");

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect(g_mouse.captured && g_relative_mode, "the next middle click takes it back");
    g_buttons = 0;
    mouse_aim_frame();
    g_keys[SDL_SCANCODE_ESCAPE] = 1;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "Escape releases a captured mouse");
    g_keys[SDL_SCANCODE_ESCAPE] = 0;
    mouse_aim_frame();
    expect(!g_mouse.captured, "and it stays free after Escape");

    g_buttons = SDL_BUTTON_LMASK;
    mouse_aim_frame();
    expect(g_mouse.captured && g_relative_mode, "a left click in the window takes the mouse back");
    g_buttons = 0;
    mouse_aim_frame();
    g_window_flags = 0;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "focus loss releases a captured mouse");
}

void test_focus_and_the_players_choice() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.configured = true;
    g_mouse.log = std::tmpfile();
    const SDL_WindowFlags focus = SDL_WINDOW_INPUT_FOCUS;
    const auto frame = [](SDL_WindowFlags flags, std::uint32_t buttons, bool escape = false) {
        g_window_flags = flags;
        g_buttons = buttons;
        g_keys[SDL_SCANCODE_ESCAPE] = escape ? 1 : 0;
        mouse_aim_frame();
    };

    frame(0, 0);
    expect(!g_mouse.captured && !g_relative_mode, "a window without focus leaves the mouse alone");
    frame(focus, 0);
    expect(g_mouse.captured && g_relative_mode, "focus coming takes the mouse");
    frame(0, 0);
    expect(!g_mouse.captured && !g_relative_mode, "focus loss releases the mouse");
    frame(0, 0);
    expect(!g_mouse.captured, "the mouse stays free while the window has no focus");
    frame(focus, 0);
    expect(g_mouse.captured && g_relative_mode, "focus coming back takes the mouse again");

    frame(focus, SDL_BUTTON_MMASK);
    expect(!g_mouse.captured, "a middle click gives the mouse up");
    frame(focus, SDL_BUTTON_MMASK);
    expect(!g_mouse.captured, "the held button toggles nothing on the next frame");
    frame(focus, 0);
    frame(0, 0);
    frame(focus, 0);
    expect(!g_mouse.captured && !g_relative_mode,
           "a mouse given up by a middle click stays free when focus comes back");
    frame(focus, SDL_BUTTON_MMASK);
    frame(focus, 0);
    expect(g_mouse.captured, "a middle click takes it back");

    frame(focus, 0, true);
    expect(!g_mouse.captured, "Escape gives the mouse up");
    frame(focus, 0);
    frame(0, 0);
    frame(focus, 0);
    expect(!g_mouse.captured && !g_relative_mode,
           "a mouse given up by Escape stays free when focus comes back");

    frame(0, SDL_BUTTON_LMASK);
    expect(!g_mouse.captured, "a click seen while the window has no focus takes nothing");
    frame(0, 0);
    frame(focus, SDL_BUTTON_LMASK);
    expect(g_mouse.captured && g_relative_mode, "a click in the focused window takes the mouse back");
    frame(focus, 0);

    frame(0, 0);
    frame(0, SDL_BUTTON_MMASK);
    frame(0, 0);
    frame(focus, 0);
    expect(!g_mouse.captured && !g_relative_mode,
           "a middle click seen while the window has no focus gives the mouse up too");
    frame(0, 0);
    frame(0, SDL_BUTTON_MMASK);
    expect(!g_mouse.captured && !g_relative_mode, "a middle click without focus takes nothing yet");
    frame(0, 0);
    frame(focus, 0);
    expect(g_mouse.captured, "but the mouse is wanted again and focus brings it");

    frame(focus, SDL_BUTTON_LMASK);
    frame(focus, SDL_BUTTON_LMASK, true);
    expect(!g_mouse.captured, "Escape lets the mouse go while the left button is down");
    frame(focus, SDL_BUTTON_LMASK);
    expect(!g_mouse.captured, "and the button still held takes nothing back");
    frame(focus, 0);
    frame(focus, SDL_BUTTON_LMASK);
    frame(focus, 0);
    expect(g_mouse.captured, "a new click does");

    frame(0, 0, true);
    expect(!g_mouse.captured, "Escape on the frame focus goes releases the mouse");
    frame(focus, 0);
    expect(!g_mouse.captured && !g_relative_mode,
           "and that was the player giving it up, not the focus taking it");
    frame(focus, SDL_BUTTON_LMASK);
    frame(focus, 0);
    expect(g_mouse.captured, "a click takes it back after that as well");

    frame(0, 0);
    frame(focus, 0, true);
    expect(!g_mouse.captured && !g_relative_mode,
           "Escape on the frame focus comes back keeps the mouse from being taken");
    frame(focus, 0);
    expect(!g_mouse.captured, "and it is not taken on the next frame either");
    frame(focus, SDL_BUTTON_LMASK);
    frame(focus, 0);

    frame(0, 0);
    g_relative_mode_succeeds = false;
    frame(focus, 0);
    expect(!g_mouse.captured, "a capture the host refuses is not claimed");
    g_relative_mode_succeeds = true;
    frame(focus, 0);
    expect(!g_mouse.captured, "and it is not tried again on every frame");
    frame(focus, SDL_BUTTON_LMASK);
    expect(g_mouse.captured && g_relative_mode, "a click asks again");
    frame(focus, 0);

    g_relative_mode_succeeds = false;
    frame(focus, 0, true);
    expect(g_mouse.captured, "an Escape release the host refuses leaves the mouse captured");
    frame(focus, 0);
    g_relative_mode_succeeds = true;
    frame(focus, 0, true);
    expect(!g_mouse.captured && !g_relative_mode, "and the next Escape lets it go");
    frame(focus, 0);
    frame(focus, SDL_BUTTON_LMASK);
    frame(focus, 0);

    g_relative_mode_succeeds = false;
    frame(0, 0);
    expect(g_mouse.captured, "a release the host refuses leaves the mouse captured");
    g_relative_mode_succeeds = true;
    frame(focus, SDL_BUTTON_MMASK);
    expect(!g_mouse.captured, "and the next middle click still gives it up");
    frame(focus, 0);
}

void prepare_weapon_input() {
    reset_state();
    g_mouse.enabled = g_mouse.modern_controls_enabled = g_mouse.configured = true;
    g_mouse.log = std::tmpfile();
    for (unsigned weapon = 0; weapon < 10; ++weapon)
        psx_write_byte(kWeaponOwnershipBase + weapon * 4, 1u);
    psx_write_byte(kCurrentWeaponAddress, 0xFFu);
}

void test_direct_weapon_mapping() {
    /* Exercise the complete digit row and keypad against guest memory. */
    for (unsigned i = 0; i < 20; ++i) {
        prepare_weapon_input();
        const unsigned first_key = i < 10 ? SDL_SCANCODE_1 : SDL_SCANCODE_KP_1;
        g_keys[first_key + i % 10] = 1;
        const std::vector<Uint8> before(g_ram.begin(), g_ram.end());
        mouse_aim_frame();
        const auto weapon = static_cast<std::uint8_t>(i % 10);
        expect(psx_read_byte(kCurrentWeaponAddress) == weapon,
               "each digit and keypad key selects its retail weapon ID");
        expect(g_noted_at == kCurrentWeaponAddress && g_noted == weapon && g_notes == 1,
               "direct weapon selection is recorded for replay");
        auto expected = before;
        expected[kCurrentWeaponAddress & 0x001fffffu] = weapon;
        expect(std::equal(g_ram.begin(), g_ram.end(), expected.begin()),
               "selection changes no ammunition, inventory or other game memory");
        expect(g_pad_buttons == 0xFFFFu, "direct selection does not press any controller buttons");
        mouse_aim_frame();
        expect(g_notes == 1, "holding a digit does not keep selecting");
    }
}

void test_weapon_selection_guards() {
    for (int guard = 0; guard < 9; ++guard) {
        prepare_weapon_input();
        psx_write_byte(kCurrentWeaponAddress, 6u);
        switch (guard) {
        case 0: psx_write_byte(kWeaponOwnershipBase, 0u); break;
        case 1: psx_write_byte(kWeaponOwnershipBase, 2u); break;
        case 2: psx_write_byte(kCurrentWeaponAddress, 10u); break;
        case 3: g_vertical_input_allowed = 0; break;
        case 4: g_netplay_active = 1; break;
        case 5: psx_write_byte(kSceneModeAddress, 1u); break;
        case 6: psx_write_byte(kSceneModeAddress, 2u); break;
        case 7: psx_write_byte(kSceneModeAddress, 3u); break;
        case 8: g_window_flags = 0; break;
        }
        g_keys[SDL_SCANCODE_1] = 1;
        const std::vector<Uint8> before(g_ram.begin(), g_ram.end());
        mouse_aim_frame();
        expect(std::equal(g_ram.begin(), g_ram.end(), before.begin()) && g_notes == 0,
               "unowned weapons, invalid state, menus, netplay and lost focus block selection");
    }

    prepare_weapon_input();
    psx_write_byte(kCurrentWeaponAddress, 0u);
    g_keys[SDL_SCANCODE_1] = 1;
    mouse_aim_frame();
    expect(g_notes == 0, "selecting the equipped weapon is a no-op");
}

void test_weapon_key_edges_through_menus() {
    prepare_weapon_input();
    g_host_ui_captured = 1;
    g_keys[SDL_SCANCODE_2] = 1;
    mouse_aim_frame();
    expect(g_notes == 0, "settings consume weapon keys");
    g_host_ui_captured = 0;
    mouse_aim_frame();
    expect(g_notes == 0, "a number held while closing settings cannot select");
    g_keys[SDL_SCANCODE_2] = 0;
    mouse_aim_frame();
    g_keys[SDL_SCANCODE_2] = 1;
    mouse_aim_frame();
    expect(psx_read_byte(kCurrentWeaponAddress) == 1u, "a fresh press after closing settings selects the pistol");
    psx_write_byte(kCurrentWeaponAddress, 6u);
    mouse_aim_frame();
    expect(psx_read_byte(kCurrentWeaponAddress) == 6u,
           "holding a shortcut leaves subsequent native weapon changes alone");

    prepare_weapon_input();
    g_keys[SDL_SCANCODE_2] = 1;
    g_typed_with[SDL_SCANCODE_2] = true;
    mouse_aim_frame();
    expect(g_notes == 0, "a number typed into a save name cannot select a weapon");
    g_typed_with[SDL_SCANCODE_2] = false;
    mouse_aim_frame();
    expect(g_notes == 0, "a typing key still held when the name screen closes remains inert");

    prepare_weapon_input();
    g_window_flags = 0;
    g_keys[SDL_SCANCODE_KP_2] = 1;
    mouse_aim_frame();
    g_window_flags = SDL_WINDOW_INPUT_FOCUS;
    mouse_aim_frame();
    expect(g_notes == 0, "focus returning cannot turn a held keypad key into a selection");
}

void test_weapon_keys_follow_modern_controls() {
    prepare_weapon_input();
    disruptor_modern_controls_set_enabled(0);
    g_keys[SDL_SCANCODE_2] = 1;
    mouse_aim_frame();
    expect(g_notes == 0, "original controls leave digit keys alone");
    disruptor_modern_controls_set_enabled(1);
    mouse_aim_frame();
    expect(g_notes == 0, "enabling modern controls while a number is held does not select");
    g_keys[SDL_SCANCODE_2] = 0;
    mouse_aim_frame();
    g_keys[SDL_SCANCODE_2] = 1;
    mouse_aim_frame();
    expect(psx_read_byte(kCurrentWeaponAddress) == 1u,
           "modern controls enable fresh direct-selection presses");
}

void test_weapon_selection_with_mouse_aim() {
    prepare_weapon_input();
    g_mouse.mouse_aim_enabled = g_mouse.captured = true;
    g_relative_mode = true;
    psx_write_byte(kTestYawAddress, 100u);
    g_relative_x = 25.0f;
    g_keys[SDL_SCANCODE_2] = 1;
    mouse_aim_frame();
    expect(psx_read_byte(kCurrentWeaponAddress) == 1u && psx_read_byte(kTestYawAddress) == 98u && g_notes == 2,
           "selecting a weapon and aiming in the same frame apply and record both writes");
}

void test_high_precision_camera_residual() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.high_precision_camera = true;
    g_mouse.configured = true;
    g_mouse.horizontal_sensitivity = 0.08;
    g_mouse.log = std::tmpfile();
    psx_write_byte(kTestYawAddress, 100);

    mouse_aim_frame();
    expect_close(g_camera_yaw_residual, 0.0,
                 "capture starts with zero presentation residual");

    g_relative_x = 6.0f;
    mouse_aim_frame();
    expect(psx_read_byte(kTestYawAddress) == 100,
           "sub-step high-precision motion leaves retail yaw unchanged");
    expect_close(g_camera_yaw_residual, 0.0,
                 "the remainder reaches the presentation camera a frame after the yaw it belongs to is written");

    g_relative_x = 7.0f;
    mouse_aim_frame();
    expect(psx_read_byte(kTestYawAddress) == 99,
           "crossing the byte boundary still advances retail yaw");
    expect_close(g_camera_yaw_residual, -0.48,
                 "the frame that still shows the old yaw keeps its own remainder");
    mouse_aim_frame();
    expect_close(g_camera_yaw_residual, -0.04,
                 "presentation residual is continuous across byte boundary");

    g_buttons = SDL_BUTTON_MMASK;
    mouse_aim_frame();
    expect(!g_mouse.captured, "high-precision test releases capture");
    expect_close(g_camera_yaw_residual, 0.0,
                 "release restores the exact retail presentation camera");
}

void expect_pressed(std::uint16_t mask, const char *message) {
    expect((g_pad_buttons & mask) == 0, message);
}

void expect_released(std::uint16_t mask, const char *message) {
    expect((g_pad_buttons & mask) == mask, message);
}

void test_modern_keyboard_mapping_and_merge() {
    reset_state();
    g_mouse.modern_controls_enabled = true;

    /* Preserve an action already supplied by the normal keyboard/controller
     * sampler while adding all of the modern keyboard actions. */
    g_pad_buttons = static_cast<std::uint16_t>(0xFFFFu & ~kPadSelect);
    g_keys[SDL_SCANCODE_W] = 1;
    g_keys[SDL_SCANCODE_S] = 1;
    g_keys[SDL_SCANCODE_A] = 1;
    g_keys[SDL_SCANCODE_D] = 1;
    g_keys[SDL_SCANCODE_SPACE] = 1;
    g_keys[SDL_SCANCODE_E] = 1;
    g_keys[SDL_SCANCODE_F] = 1;
    g_keys[SDL_SCANCODE_Q] = 1;
    g_keys[SDL_SCANCODE_R] = 1;
    g_keys[SDL_SCANCODE_P] = 1;
    g_keys[SDL_SCANCODE_RETURN] = 1;
    apply_modern_controls(g_keys.data(), 0);

    expect_pressed(kPadSelect, "existing sampled controller action is preserved");
    expect_pressed(kPadUp, "W maps to walk forward");
    expect_pressed(kPadDown, "S maps to walk backward");
    expect_pressed(kPadL2, "A maps to strafe left");
    expect_pressed(kPadR2, "D maps to strafe right");
    expect_pressed(kPadCircle, "Space maps to jump");
    expect_pressed(kPadTriangle, "E maps to use");
    expect_pressed(kPadSquare, "F maps to psionic attack");
    expect_pressed(kPadL1, "Q maps to weapon selection");
    expect_pressed(kPadR1, "R maps to psionic selection");
    expect_pressed(kPadStart, "P maps to pause");
    expect_pressed(kPadCross, "Return maps to confirm/fire");

    reset_state();
    g_mouse.modern_controls_enabled = true;
    g_keys[SDL_SCANCODE_UP] = 1;
    g_keys[SDL_SCANCODE_DOWN] = 1;
    g_keys[SDL_SCANCODE_LEFT] = 1;
    g_keys[SDL_SCANCODE_RIGHT] = 1;
    g_keys[SDL_SCANCODE_TAB] = 1;
    apply_modern_controls(g_keys.data(), 0);
    expect_pressed(kPadUp, "Up arrow remains a menu/gameplay fallback");
    expect_pressed(kPadDown, "Down arrow remains a menu/gameplay fallback");
    expect_pressed(kPadLeft, "Left arrow retains original turn/menu behavior");
    expect_pressed(kPadRight, "Right arrow retains original turn/menu behavior");
    expect_pressed(kPadSelect, "Tab maps to the automap");

    reset_state();
    g_mouse.modern_controls_enabled = true;
    g_typed_with[SDL_SCANCODE_E] = true;
    g_keys[SDL_SCANCODE_E] = 1;
    g_keys[SDL_SCANCODE_F] = 1;
    g_keys[SDL_SCANCODE_RETURN] = 1;
    apply_modern_controls(g_keys.data(), 0);
    expect_released(kPadTriangle, "a key a name is being typed with is no button");
    expect_pressed(kPadSquare, "the keys beside it still are");
    expect_pressed(kPadCross, "and so is Return");
    expect(g_keys[SDL_SCANCODE_E] == 1, "the keyboard's own state is left as it was");
}

void test_modern_mouse_buttons_require_capture() {
    reset_state();
    g_mouse.modern_controls_enabled = true;
    const std::uint32_t actions = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK |
                                  SDL_BUTTON_X1MASK | SDL_BUTTON_X2MASK;

    apply_modern_controls(g_keys.data(), actions);
    expect_released(kPadCross | kPadSquare | kPadL1 | kPadR1,
                    "desktop mouse clicks do not reach the game while released");

    g_mouse.captured = true;
    apply_modern_controls(g_keys.data(), actions);
    expect_pressed(kPadCross, "left mouse maps to fire");
    expect_pressed(kPadSquare, "right mouse maps to psionic attack");
    expect_pressed(kPadL1, "mouse X1 maps to weapon selection");
    expect_pressed(kPadR1, "mouse X2 maps to psionic selection");
}

void test_the_click_that_takes_the_mouse_is_no_shot() {
    reset_state();
    g_mouse.enabled = true;
    g_mouse.modern_controls_enabled = true;
    g_mouse.configured = true;
    g_mouse.log = std::tmpfile();
    const auto frame = [](std::uint32_t buttons, bool escape = false, SDL_WindowFlags flags = SDL_WINDOW_INPUT_FOCUS) {
        g_pad_buttons = 0xFFFFu;
        g_window_flags = flags;
        g_buttons = buttons;
        g_keys[SDL_SCANCODE_ESCAPE] = escape ? 1 : 0;
        mouse_aim_frame();
    };

    frame(0);
    expect(g_mouse.captured, "modern controls alone hold the mouse");
    frame(SDL_BUTTON_LMASK);
    expect_pressed(kPadCross, "a left click of a held mouse fires");
    frame(0, true);
    frame(0);
    expect(!g_mouse.captured, "Escape lets the mouse go");
    frame(SDL_BUTTON_LMASK);
    expect(g_mouse.captured, "a left click takes it back");
    expect_released(kPadCross, "and that click does not fire");
    frame(SDL_BUTTON_LMASK);
    expect_released(kPadCross, "nor while the button stays down");
    frame(0);
    frame(SDL_BUTTON_LMASK);
    expect_pressed(kPadCross, "the next click fires");
    frame(0);

    frame(0, false, 0);
    expect(!g_mouse.captured, "the mouse is free without focus");
    frame(SDL_BUTTON_LMASK);
    expect(g_mouse.captured, "the click that brings focus back takes the mouse with it");
    expect_released(kPadCross, "and is no shot either");
    frame(0);

    g_buttons = SDL_BUTTON_LMASK;
    (void)disruptor_mouse_set_captured(0);
    (void)disruptor_mouse_set_captured(1);
    frame(SDL_BUTTON_LMASK);
    expect(g_mouse.captured, "a menu that closes hands the mouse back");
    expect_released(kPadCross, "and the click that closed it is no shot");
    frame(0);
    frame(SDL_BUTTON_LMASK);
    expect_pressed(kPadCross, "the click after it fires");
    frame(0);

    g_mouse.modern_controls_enabled = false;
    refresh_live_control_state("test-off");
    expect(!g_mouse.captured && !g_relative_mode, "switching every mouse feature off frees the mouse");
    frame(0);
    frame(SDL_BUTTON_LMASK);
    frame(0);
    expect(!g_mouse.captured && !g_relative_mode, "with every mouse feature off nothing takes the mouse, a click neither");
    g_mouse.high_precision_camera = true;
    refresh_live_control_state("test-camera-alone");
    frame(0);
    frame(0);
    expect(!g_mouse.captured && !g_relative_mode, "smooth turning alone, with nothing to turn by, takes no mouse");
    g_mouse.modern_controls_enabled = true;
    refresh_live_control_state("test-on");
    frame(0);
    expect(g_mouse.captured && g_relative_mode, "and switching a feature on again takes it, with no click");
}

void test_live_settings_api() {
    reset_state();
    g_mouse.configured = true;

    expect(!disruptor_mouse_aim_enabled(),
           "mouse aim starts disabled in live settings test");
    expect(!disruptor_modern_controls_enabled(),
           "modern controls start disabled in live settings test");
    expect(!disruptor_mouse_set_captured(1),
           "capture is rejected while input features are disabled");

    disruptor_mouse_aim_set_enabled(1);
    expect(disruptor_mouse_aim_enabled() && g_mouse.enabled,
           "live mouse-aim enable wakes the dormant frame hook");
    expect(disruptor_mouse_set_captured(1) && g_relative_mode,
           "live API captures after mouse aim is enabled");

    expect_close(disruptor_mouse_set_horizontal_sensitivity(0.25), 0.25,
                 "live sensitivity accepts an in-range value");
    expect_close(disruptor_mouse_set_horizontal_sensitivity(99.0), 2.0,
                 "live sensitivity clamps to its safe maximum");
    expect_close(disruptor_mouse_set_horizontal_sensitivity(NAN), 2.0,
                 "live sensitivity rejects non-finite values");

    disruptor_mouse_set_vertical_look_enabled(1);
    expect(disruptor_mouse_vertical_look_enabled(),
           "live vertical-look enable is observable");
    expect_close(disruptor_mouse_set_vertical_sensitivity(0.25), 0.25,
                 "live vertical sensitivity accepts an in-range value");
    expect_close(disruptor_mouse_set_vertical_sensitivity(99.0), 2.0,
                 "live vertical sensitivity clamps to its safe maximum");
    expect_close(disruptor_mouse_set_vertical_sensitivity(NAN), 2.0,
                 "live vertical sensitivity rejects non-finite values");
    disruptor_mouse_set_invert_vertical(1);
    expect(disruptor_mouse_invert_vertical(),
           "live vertical inversion is observable");
    apply_mouse_y(-1.0);
    disruptor_mouse_recenter_vertical();
    expect_close(disruptor_mouse_vertical_pitch(), 0.0,
                 "live recenter clears requested vertical pitch");

    disruptor_mouse_set_invert_horizontal(1);
    expect(disruptor_mouse_invert_horizontal(),
           "live horizontal inversion is observable");
    disruptor_high_precision_camera_set_enabled(1);
    expect(disruptor_high_precision_camera_enabled(),
           "live high-precision camera is observable");

    g_mouse.fractional_yaw = 0.375;
    g_camera_yaw_residual = 0.375;
    disruptor_high_precision_camera_set_enabled(0);
    expect_close(g_mouse.fractional_yaw, 0.0,
                 "disabling high precision clears fractional yaw");
    expect_close(g_camera_yaw_residual, 0.0,
                 "disabling high precision clears presentation residual");

    disruptor_modern_controls_set_enabled(1);
    disruptor_mouse_aim_set_enabled(0);
    expect(disruptor_mouse_captured(),
           "modern controls keep an existing capture after mouse aim is disabled");
    disruptor_modern_controls_set_enabled(0);
    expect(disruptor_mouse_captured(),
           "vertical look keeps capture after other input features are disabled");
    disruptor_mouse_set_vertical_look_enabled(0);
    expect(!disruptor_mouse_captured() && !g_relative_mode,
           "disabling the last input feature releases capture");
    expect_close(g_requested_vertical_pitch, 0.0,
                 "disabling vertical look recenters the camera sink");
}

void test_vertical_only_capture() {
    reset_state();
    g_mouse.configured = true;
    disruptor_mouse_set_vertical_look_enabled(1);
    expect(disruptor_mouse_set_captured(1) && g_relative_mode,
           "vertical look alone permits relative mouse capture");
    disruptor_mouse_set_vertical_look_enabled(0);
    expect(!g_mouse.captured && !g_relative_mode,
           "disabling vertical-only input releases capture");
}

void test_host_ui_blocks_polled_mod_input() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.modern_controls_enabled = true;
    g_mouse.captured = true;
    g_relative_mode = true;
    g_mouse.fractional_yaw = 0.5;
    g_camera_yaw_residual = 0.5;
    psx_write_byte(kTestYawAddress, 77);
    g_keys[SDL_SCANCODE_W] = 1;
    g_buttons = SDL_BUTTON_LMASK;
    g_relative_x = 12.0f;
    g_relative_y = -12.0f;
    g_host_ui_captured = 1;

    mouse_aim_frame();

    expect(psx_read_byte(kTestYawAddress) == 77,
           "host UI blocks mouse yaw writes");
    expect(g_pad_buttons == 0xFFFFu,
           "host UI blocks modern-control button injection");
    expect_close(g_relative_x, 0.0,
                 "host UI drains relative motion while captured");
    expect_close(g_relative_y, 0.0,
                 "host UI drains relative vertical motion while captured");
    expect_close(g_mouse.vertical_pitch, 0.0,
                 "host UI blocks vertical camera changes");
    expect_close(g_camera_yaw_residual, 0.0,
                 "host UI clears presentation residual while captured");
}

void test_host_ui_escape_close_restores_capture() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.mouse_aim_enabled = true;
    g_mouse.log = std::tmpfile();

    /* Closing the host UI happens inside the Escape key event callback.  The
     * key is still down when its frame hook next polls SDL state. */
    g_keys[SDL_SCANCODE_ESCAPE] = 1;
    expect(disruptor_mouse_set_captured(1) && g_relative_mode,
           "host UI restores relative capture while Escape is held");
    mouse_aim_frame();
    expect(g_mouse.captured && g_relative_mode,
           "held close key does not immediately release restored capture");

    g_keys[SDL_SCANCODE_ESCAPE] = 0;
    mouse_aim_frame();
    g_keys[SDL_SCANCODE_ESCAPE] = 1;
    mouse_aim_frame();
    expect(!g_mouse.captured && !g_relative_mode,
           "a later Escape press still releases capture normally");
}

void test_netplay_recenters_and_blocks_vertical_motion() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.captured = true;
    g_mouse.vertical_pitch = 4.0;
    g_requested_vertical_pitch = 4.0;
    g_relative_y = -25.0f;
    g_netplay_active = 1;
    g_mouse.log = std::tmpfile();

    mouse_aim_frame();

    expect_close(g_mouse.vertical_pitch, 0.0,
                 "netplay transition clears host vertical pitch");
    expect_close(g_requested_vertical_pitch, 0.0,
                 "netplay transition recenters the camera sink");
    expect_close(g_relative_y, 0.0,
                 "netplay still drains relative vertical motion");
    expect(disruptor_mouse_vertical_look_enabled(),
           "netplay guard preserves the user's vertical preference");
}

void test_gameplay_gate_discards_new_vertical_motion() {
    reset_state();
    g_mouse.configured = true;
    g_mouse.enabled = true;
    g_mouse.vertical_look_enabled = true;
    g_mouse.captured = true;
    g_mouse.vertical_pitch = 3.0;
    g_requested_vertical_pitch = 3.0;
    g_vertical_input_allowed = 0;
    g_relative_y = -25.0f;
    g_mouse.log = std::tmpfile();

    mouse_aim_frame();

    expect_close(g_mouse.vertical_pitch, 3.0,
                 "scripted/non-gameplay gate preserves existing pitch");
    expect_close(g_requested_vertical_pitch, 3.0,
                 "scripted/non-gameplay gate preserves the sink request");
    expect_close(g_relative_y, 0.0,
                 "scripted/non-gameplay gate discards new mouse Y");
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::strcmp(argv[1], "--replay") == 0) {
        const bool given_up = g_mouse.given_up;
        reset_state();
        g_mouse.given_up = given_up;
        g_mouse.enabled = g_mouse.mouse_aim_enabled = g_mouse.configured = true;
        g_mouse.log = std::tmpfile();
        mouse_aim_frame();
        mouse_aim_frame();
        expect(!g_mouse.captured && !g_relative_mode, "a capsule's replay leaves the mouse alone though its window has focus");
        g_buttons = SDL_BUTTON_MMASK;
        mouse_aim_frame();
        expect(g_mouse.captured && g_relative_mode, "until the viewer clicks for it");
    } else if (argc == 2 && std::strcmp(argv[1], "--registration") == 0) {
        expect(g_registered_hook != nullptr,
               "environment opt-in registers the frame hook");
    } else {
        expect(g_registered_hook != nullptr,
               "normal launch installs a dormant hook for live settings");
        g_mouse.vertical_pitch = 4.0;
        disruptor_mouse_set_vertical_pitch(-12.5);
        expect(disruptor_mouse_vertical_pitch() == -12.5, "a replay puts the pitch back");
        g_mouse.vertical_pitch = 0.0;
        test_parsing();
        test_ini_configuration();
        test_direction_fraction_and_wrap();
        test_nearest_step_diffusion_and_conservation();
        test_motion_safety_clamp();
        test_vertical_direction_inversion_and_clamp();
        test_simultaneous_relative_axes_sample();
        test_capture_release_and_frame_motion();
        test_focus_and_the_players_choice();
        test_high_precision_camera_residual();
        test_modern_keyboard_mapping_and_merge();
        test_direct_weapon_mapping();
        test_weapon_selection_guards();
        test_weapon_key_edges_through_menus();
        test_weapon_keys_follow_modern_controls();
        test_weapon_selection_with_mouse_aim();
        test_modern_mouse_buttons_require_capture();
        test_the_click_that_takes_the_mouse_is_no_shot();
        test_live_settings_api();
        test_vertical_only_capture();
        test_host_ui_blocks_polled_mod_input();
        test_host_ui_escape_close_restores_capture();
        test_netplay_recenters_and_blocks_vertical_motion();
        test_gameplay_gate_discards_new_vertical_motion();
    }

    if (g_mouse.log) {
        std::fclose(g_mouse.log);
        g_mouse.log = nullptr;
    }
    if (g_failures != 0) return 1;
    std::cout << "Disruptor mouse-aim tests passed\n";
    return 0;
}
