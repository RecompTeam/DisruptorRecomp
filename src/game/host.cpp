#include "game_host.h"

#include "config_loader.h"
#include "game/achievements/game_client.h"
#include "game/audio/bass_cut.h"
#include "game/language/language.h"
#include "game/input/baton_choice.h"
#include "game/input/gamepad.h"
#include "game/input/menu_rows.h"
#include "game/input/mouse_aim.h"
#include "game/input/selection_wheel.h"
#include "game/input/sticks.h"
#include "game/input/weapon_list.h"
#include "game/render/far_rendering.h"
#include "game/render/archive_terminal.h"
#include "game/render/ddgi_level.h"
#include "game/render/movie_smoothing.h"
#include "game/session/capsule.h"
#include "game/session/diagnostics.h"
#include "game/session/state_screen.h"
#include "game/settings/game_mark.h"
#include "game/settings/game_package.h"
#include "game/settings/options.h"
#include "game/settings/pick_keys.h"
#include "game/settings/second_keys.h"
#include "game/settings/user_folder.h"
#include "game/render/precision_routes.h"
#include "gpu.h"
#include "host_ui.h"
#include "host_osd.h"
#include "mod_plugins.h"
#include "mod_runtime.h"
#include "psx_keybinds.h"
#include "render_thread.h"
#include "savestate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <span>
#include <string>
#include <string_view>

extern "C" int disruptor_name_entry_key(int scancode, uint32_t keycode, int down, int plain);
extern "C" int disruptor_name_entry_taking_text(void);
extern "C" int disruptor_name_entry_withholds(int scancode);
extern "C" void disruptor_name_entry_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count);
extern "C" void disruptor_name_entry_state_loaded(void);
extern "C" void disruptor_reset_draw_headings();
extern "C" int disruptor_draw_camera(int top, int fallback, int32_t *eye, int64_t *tick);
extern "C" void disruptor_mover_motion_state_loaded(void);
extern "C" void disruptor_mover_motion_state_saving(void);
extern "C" void disruptor_stock_frames_state_loaded(void);
extern "C" void disruptor_sprite_depth_state_loaded(void);
extern "C" void disruptor_stock_frames_state_saving(void);
extern "C" int disruptor_stock_frames_save_deferred(void);
extern "C" int disruptor_grown_quad_sources(uint32_t command_address, const uint32_t *packet, uint32_t source_address[4],
                                            uint32_t source_word[4]);

std::filesystem::path psx_game_user_folder(const std::filesystem::path &program, std::string &problem) {
    try {
        std::filesystem::path folder = disruptor::user_folder::prepare(program);
        static const disruptor::game_mark::Mark mark(folder);
        return folder;
    } catch (const std::exception &error) {
        problem = error.what();
        return {};
    }
}

namespace {

// The keys settings.toml gives the weapons and the psionic powers, as scancodes: every first key, then every second.
void apply_pick_keys(const PSXRecompV4::UserSettings &settings) {
    namespace keys = disruptor::settings;
    const auto scancode = [](std::string_view name) { return SDL_GetScancodeFromName(std::string(name).c_str()); };
    const keys::PickKeys chosen = keys::pick_keys_from(
        {settings.weapon_keys, settings.weapon_keys_second, settings.psionic_keys, settings.psionic_keys_second},
        [&](std::string_view name) { return scancode(name) != SDL_SCANCODE_UNKNOWN; });
    std::array<int, 2 * keys::pick_count> scancodes{};
    for (std::size_t row = 0; row < keys::pick_count; ++row)
        for (std::size_t which = 0; which < 2; ++which)
            scancodes[which * keys::pick_count + row] =
                keys::no_key(chosen[row][which]) ? SDL_SCANCODE_UNKNOWN : scancode(chosen[row][which]);
    disruptor_pick_keys_set(scancodes.data(), static_cast<int>(scancodes.size()));
}

// Every field's second key under each scheme: settings.toml's list where it has one, else the scheme's own.
void apply_second_keys(const PSXRecompV4::UserSettings &settings) {
    namespace keys = disruptor::settings;
    const auto scancode = [](std::string_view name) { return static_cast<int>(SDL_GetScancodeFromName(std::string(name).c_str())); };
    const auto known = [&](std::string_view name) { return scancode(name) != SDL_SCANCODE_UNKNOWN; };
    const auto codes = [&](bool modern) {
        return keys::second_codes(keys::second_keys_from(settings.key_second, settings.has_key_second, modern, known), scancode);
    };
    const keys::SecondCodes modern = codes(true), classic = codes(false);
    disruptor_second_keys_set(modern.data(), classic.data(), static_cast<int>(modern.size()));
}

}  // namespace

void psx_game_settings_defaults(PSXRecompV4::UserSettings &settings) {
    settings = disruptor::settings::with_defaults(settings);
}

bool psx_game_settings_loaded(const PSXRecompV4::UserSettings &settings) {
    disruptor_diagnostics_configure(settings.has_detailed_logging && settings.detailed_logging);
    (void)psx_host_audio_set_mute_in_background(settings.has_mute_in_background && settings.mute_in_background);
    disruptor_bass_cut_set(settings.has_bass_cut ? settings.bass_cut : PSXRecompV4::kDisruptorBassCutDefault);
    disruptor_achievements_configure(!settings.has_achievements || settings.achievements);
    disruptor::achievements::configure(settings);
    disruptor::gamepad::apply_settings(settings);
    apply_pick_keys(settings);
    apply_second_keys(settings);
    if (settings.has_language_disc && !std::getenv("PSX_DISRUPTOR_LANGUAGE_DISC"))
        disruptor_language_set_disc(settings.language_disc.c_str());
    return settings.has_skip_intro && settings.skip_intro && !std::getenv("PSX_DISRUPTOR_SKIP_INTRO");
}

extern "C" {

int psx_game_configure(const char *game_id, size_t length) {
    return disruptor_precision_routes_configure(game_id, length);
}

const char *psx_game_session_problem(int netplay) {
    return disruptor::game_package_problem(netplay != 0, mod_runtime_snapshot_package_active(disruptor::kGamePackage) != 0);
}

const PsxNightVertex *psx_game_night_geometry(int area, int *vertices) {
    return disruptor::ddgi_level::night_geometry(area, vertices);
}

static uint32_t triton_room(uint8_t sector) {
    const uint32_t room = psx_mod_read_word(0x800776E4u + 4u * sector);
    return !(room & 3u) && room >= 0x80000000u && room <= 0x801FFFD4u ? room : 0u;
}

using TritonPortal = std::array<int, 4>;

static float triton_portal_sky(int32_t x, int32_t z, std::span<const TritonPortal> exits) {
    constexpr double width = 256.0;
    double nearest = width * width;
    for (const auto &[x0, z0, x1, z1] : exits) {
        const double dx = x1 - x0, dz = z1 - z0;
        const double px = static_cast<double>(x) - x0, pz = static_cast<double>(z) - z0;
        const double along = std::clamp((px * dx + pz * dz) / (dx * dx + dz * dz), 0.0, 1.0);
        const double across_x = px - along * dx, across_z = pz - along * dz;
        nearest = std::min(nearest, across_x * across_x + across_z * across_z);
    }
    const double amount = std::sqrt(nearest) / width;
    return static_cast<float>(std::lround(255.0 * amount * amount * (3.0 - 2.0 * amount))) / 255.0f;
}

static float triton_outdoor_sky(int32_t x, int32_t z) {
    constexpr std::array<TritonPortal, 11> basin_exits{{
        {11840, 10400, 11840, 11352}, {11456, 10400, 11840, 10400}, {11456, 11352, 11456, 10400},
        {8992, 11762, 9672, 11264}, {8879, 12611, 8992, 11762},
        {8776, 12880, 8879, 12611}, {7816, 12952, 8496, 13104}, {7440, 12888, 7456, 12632},
        {8776, 12880, 8840, 13200}, {8496, 13104, 8640, 13256}, {8640, 13256, 8840, 13200}
    }};
    constexpr std::array<TritonPortal, 4> valley_exits{{
        {4984, 12656, 5864, 12656}, {3088, 5952, 3088, 6176}, {5428, 5952, 5428, 6176}, {4224, 4064, 4352, 4064}
    }};
    const uint8_t sector = psx_mod_read_byte(0x80077630u);
    const uint32_t room = triton_room(sector);
    if (!room) return 0.0f;
    const uint32_t entry = psx_mod_read_word(room + 40u);
    if ((entry & 255u) != sector) return 0.0f;
    const uint32_t source = entry >> 16u;
    if (source == 4u && (sector <= 4u || (sector >= 7u && sector <= 17u) || sector == 21u || sector == 22u || sector == 27u))
        return triton_portal_sky(x, z, basin_exits);
    if ((source == 5u && sector >= 69u && sector <= 79u) ||
        (source == 6u && ((sector >= 128u && sector <= 136u) || (sector >= 143u && sector <= 147u))))
        return triton_portal_sky(x, z, valley_exits);
    return 0.0f;
}

static float triton_open_sky(int32_t x, int32_t y, int32_t z, int *balcony, float &basin) {
    *balcony = 0;
    basin = triton_outdoor_sky(x, z);
    const uint32_t room = triton_room(86u);
    if (!room || psx_mod_read_word(room + 24u) != 12224u || psx_mod_read_word(room + 28u) != 11616u ||
        psx_mod_read_word(room + 32u) != 1408u) return basin;
    const double distance = std::max({11776.0 - x, x - 12736.0, 11616.0 - z, z - 12288.0,
                                      1408.0 - y, y - 1664.0});
    const double t = std::clamp(1.0 - distance / 256.0, 0.0, 1.0);
    const float opening = static_cast<float>(t * t * (3.0 - 2.0 * t));
    *balcony = opening > 0.0f;
    return std::max(opening, basin);
}

static int sky_direction() {
    const double angle = disruptor_mouse_vertical_pitch();
    if (!std::isfinite(angle)) return -1;
    const double pitch = std::clamp(angle, -22.0, 22.0) * (6.283185307179586 / 256.0);
    const int horizon = static_cast<int>(std::lround(120.0 + 160.0 * std::tan(pitch)));
    return (horizon << 8) | psx_mod_read_byte(0x80077624u);
}

static const int chemical_preview_start = [] {
    const char *value = std::getenv("DISRUPTOR_CHEMICAL_SKY_PREVIEW");
    if (!value) return 0;
    return value[0] >= '1' && value[0] <= '3' && !value[1] ? value[0] - '0' : -1;
}();
static std::atomic<int> chemical_variant{std::max(1, chemical_preview_start)};

static bool sky_level_visible(uint8_t level) {
    if (!psx_mod_game_started() || psx_mod_read_byte(0x80071354u) != level ||
        psx_mod_read_byte(0x80071518u) != level || psx_mod_read_byte(0x800715FCu) == 0x18u) return false;
    const uint8_t scene = psx_mod_read_byte(disruptor::menu_rows::kPauseState);
    return scene == 0u || scene == disruptor::menu_rows::kPaused ||
           scene == disruptor::weapon_list::kWeaponList || scene == disruptor::weapon_list::kPsionicList;
}

static bool chemical_factory_playing() {
    return sky_level_visible(2u) && psx_mod_read_byte(0x8007145Cu) == 0u;
}

static int sky_draw_camera(int top, int yaw, int32_t camera[3], int64_t *tick = nullptr) {
    camera[0] = static_cast<int32_t>(psx_mod_read_word(0x800775BCu));
    camera[1] = static_cast<int32_t>(psx_mod_read_word(0x800775D0u));
    camera[2] = static_cast<int32_t>(psx_mod_read_word(0x800775C0u));
    return disruptor_draw_camera(top, yaw, camera, tick);
}

int psx_game_chemical_sky_view(int top, int32_t camera[4], int64_t *smoke_tick) {
    if (chemical_preview_start < 0 || !sky_level_visible(2u)) return -1;
    const int view = sky_direction();
    if (view < 0) return -1;
    camera[3] = -chemical_variant.load(std::memory_order_relaxed);
    const bool world = psx_mod_read_byte(0x8007145Cu) == 0u;
    return (view & ~255) | sky_draw_camera(top, view & 255, camera, world ? smoke_tick : nullptr);
}

int psx_game_rooftops_sky_view(int top, int32_t camera[4]) {
    if (!sky_level_visible(3u)) return -1;
    const int direction = sky_direction();
    if (direction < 0) return -1;
    camera[3] = PSX_ROOFTOPS_SKY;
    return (direction & ~255) | disruptor_draw_camera(top, direction & 255, nullptr, nullptr);
}

int psx_game_night_sky_view(int top, int32_t camera[4], int *balcony) {
    if (!sky_level_visible(5u)) return -1;
    const int direction = sky_direction();
    if (direction < 0) return -1;
    const int yaw = sky_draw_camera(top, direction & 255, camera);
    RtStats render{};
    rt_get_stats(&render);
    camera[3] = disruptor::ddgi_level::night_area(render.running ? static_cast<size_t>(render.max_frames) : 0u);
    float basin;
    const float clear = triton_open_sky(camera[0], camera[1], camera[2], balcony, basin);
    const int opening = static_cast<int>(std::lround(127.0f * clear));
    return (opening << 24) | (static_cast<int>(std::lround(255.0f * basin)) << 16) | (direction & ~255) | yaw;
}

int psx_game_fast_boot_forced(void) {
    const char *forced = std::getenv("PSX_DISRUPTOR_SKIP_INTRO");
    return forced && forced[0] && forced[0] != '0';
}

int psx_game_states_configured(void) { return disruptor_language_settle(); }

void psx_game_state_loaded(void) {
    disruptor::sticks::set_movement({});
    disruptor::gamepad::stop_vibration();
    disruptor::ddgi_level::invalidate_night_area();
    disruptor::archive_terminal::state_loaded();
    disruptor_sprite_depth_state_loaded();
    disruptor_reset_draw_headings();
    disruptor_capsule_state_loaded();
    disruptor_selection_wheel_cancel();
    disruptor_name_entry_state_loaded();
    disruptor_weapon_choice_state_loaded();
    disruptor_weapon_keys_state_loaded();
    disruptor_weapon_list_state_loaded();
    disruptor_mover_motion_state_loaded();
    disruptor_stock_frames_state_loaded();
    disruptor_list_message_state_loaded();
    disruptor_language_movie_sound_state_loaded();
    disruptor_achievements_state_loaded(savestate_active_path());
    disruptor_state_screen_loaded();
    disruptor_far_rendering_abandon_metrics();
}

void psx_game_state_saving(void) {
    disruptor_mover_motion_state_saving();
    disruptor_stock_frames_state_saving();
    disruptor_far_rendering_state_saving();
    disruptor_achievements_state_saving(savestate_active_path());
    disruptor_state_screen_saving(savestate_active_path());
}

void psx_game_state_saved(int written) {
    disruptor_achievements_state_saved(savestate_active_path(), written);
    disruptor_state_screen_saved(savestate_active_path(), written);
}

int psx_game_state_load_allowed(void) { return disruptor_achievements_allows(DISRUPTOR_ACHIEVEMENTS_LOAD_STATE); }

int psx_game_state_save_deferred(void) {
    return disruptor_far_rendering_save_deferred() || disruptor_language_movie_sound_save_deferred() ||
           disruptor_stock_frames_save_deferred() || disruptor_list_message_save_deferred();
}

int psx_game_packet_fade(uint32_t resolved_packet, uint32_t signature) {
    return disruptor_far_rendering_packet_fraction(resolved_packet, signature);
}

// Whether player 1's keybinds.ini or the second keys put the key on a button.
int disruptor_key_bound(int scancode) {
    for (int index = 0; index < PSX_KB_COUNT; ++index)
        if (psx_keybinds_get_button(1, index) == static_cast<SDL_Scancode>(scancode)) return 1;
    return disruptor_second_key_bound(scancode);
}

int psx_game_key(int scancode, uint32_t keycode, int down, int plain) {
    if (scancode == SDL_SCANCODE_PAGEDOWN || scancode == SDL_SCANCODE_PAGEUP) {
        static bool held[2]{};
        bool &pressed = held[scancode == SDL_SCANCODE_PAGEUP];
        const bool first_press = down && !pressed;
        pressed = down != 0;
        if (down && plain && chemical_preview_start > 0 && chemical_factory_playing()) {
            if (first_press) {
                const int variant = chemical_variant.load(std::memory_order_relaxed);
                const int next = 1 + (variant - 1 + (scancode == SDL_SCANCODE_PAGEUP ? 2 : 1)) % 3;
                chemical_variant.store(next, std::memory_order_relaxed);
                constexpr const char *names[] = {"Sky 1/3: Sulphur clouds", "Sky 2/3: Factory smog", "Sky 3/3: Dusty sun"};
                host_osd_push(names[next - 1], 1800);
            }
            return 1;
        }
    }
    return disruptor_name_entry_key(scancode, keycode, down, plain);
}

int psx_game_grown_quad_sources(uint32_t command_address, const uint32_t *packet, uint32_t source_address[4],
                                uint32_t source_word[4]) {
    return disruptor_grown_quad_sources(command_address, packet, source_address, source_word);
}

int psx_game_fast_forward_key(void) {
    if (disruptor_name_entry_taking_text() || disruptor_name_entry_withholds(SDL_SCANCODE_B)) return -1;
    return SDL_SCANCODE_B;
}

void psx_game_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count) {
    disruptor_name_entry_keys_for_pad(keys, kept, count);
}

void psx_game_before_input(void) {
    disruptor_capsule_before_input();
    disruptor::sticks::set_movement({});
}

void psx_game_after_input(void) {
    disruptor_capsule_after_input();
    disruptor::gamepad::update_vibration();
    disruptor_selection_wheel_guard_input();
    disruptor::archive_terminal::after_input();
    disruptor_achievements_frame();
    disruptor_state_screen_frame();
}

void psx_game_audio_output(int16_t *stereo, int frames, int rate) {
    disruptor::audio::shape_output(stereo, frames, rate);
    disruptor_achievements_audio(stereo, frames, rate);
}

int psx_game_smooth_movies(void) { return disruptor_smooth_movies_enabled(); }

}  // extern "C"

