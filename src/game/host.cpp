#include "game_host.h"

#include "config_loader.h"
#include "game/audio/bass_cut.h"
#include "game/language/language.h"
#include "game/input/baton_choice.h"
#include "game/input/mouse_aim.h"
#include "game/input/selection_wheel.h"
#include "game/input/weapon_list.h"
#include "game/render/far_rendering.h"
#include "game/render/movie_smoothing.h"
#include "game/render/precision_routes.h"
#include "game/session/capsule.h"
#include "game/session/diagnostics.h"
#include "game/settings/options.h"
#include "game/settings/pick_keys.h"
#include "game/settings/user_folder.h"
#include "gpu.h"
#include "host_ui.h"
#include "mod_plugins.h"
#include "psx_keybinds.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

extern "C" int disruptor_name_entry_key(int scancode, uint32_t keycode, int down, int plain);
extern "C" int disruptor_name_entry_taking_text(void);
extern "C" int disruptor_name_entry_withholds(int scancode);
extern "C" void disruptor_name_entry_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count);
extern "C" void disruptor_name_entry_state_loaded(void);
extern "C" void disruptor_mover_motion_state_loaded(void);
extern "C" void disruptor_mover_motion_state_saving(void);
extern "C" int disruptor_grown_quad_sources(uint32_t command_address, const uint32_t *packet, uint32_t source_address[4],
                                            uint32_t source_word[4]);

std::filesystem::path psx_game_user_folder(const std::filesystem::path &program, std::string &problem) {
    try {
        return disruptor::user_folder::prepare(program);
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

}  // namespace

void psx_game_settings_defaults(PSXRecompV4::UserSettings &settings) {
    settings = disruptor::settings::with_defaults(settings);
}

bool psx_game_settings_loaded(const PSXRecompV4::UserSettings &settings) {
    disruptor_diagnostics_configure(settings.has_detailed_logging && settings.detailed_logging);
    (void)psx_host_audio_set_mute_in_background(settings.has_mute_in_background && settings.mute_in_background);
    disruptor_bass_cut_set(settings.has_bass_cut ? settings.bass_cut : 0);
    apply_pick_keys(settings);
    if (settings.has_language_disc && !std::getenv("PSX_DISRUPTOR_LANGUAGE_DISC"))
        disruptor_language_set_disc(settings.language_disc.c_str());
    return settings.has_skip_intro && settings.skip_intro && !std::getenv("PSX_DISRUPTOR_SKIP_INTRO");
}

extern "C" {

int psx_game_configure(const char *game_id, size_t length) {
    return disruptor_precision_routes_configure(game_id, length);
}

int psx_game_fast_boot_forced(void) {
    const char *forced = std::getenv("PSX_DISRUPTOR_SKIP_INTRO");
    return forced && forced[0] && forced[0] != '0';
}

void psx_game_states_configured(void) { disruptor_language_settle(); }

void psx_game_state_loaded(void) {
    disruptor_capsule_state_loaded();
    disruptor_selection_wheel_cancel();
    disruptor_name_entry_state_loaded();
    disruptor_weapon_choice_state_loaded();
    disruptor_weapon_keys_state_loaded();
    disruptor_weapon_list_state_loaded();
    disruptor_mover_motion_state_loaded();
    disruptor_language_movie_sound_state_loaded();
    disruptor_far_rendering_abandon_metrics();
}

void psx_game_state_saving(void) {
    disruptor_mover_motion_state_saving();
    disruptor_far_rendering_state_saving();
}

int psx_game_state_save_deferred(void) {
    return disruptor_far_rendering_save_deferred() || disruptor_language_movie_sound_save_deferred();
}

int psx_game_packet_fade(uint32_t resolved_packet, uint32_t signature) {
    return disruptor_far_rendering_packet_fraction(resolved_packet, signature);
}

// Whether player 1's keybinds.ini puts the key on a button.
int disruptor_key_bound(int scancode) {
    for (int index = 0; index < PSX_KB_COUNT; ++index)
        if (psx_keybinds_get_button(1, index) == static_cast<SDL_Scancode>(scancode)) return 1;
    return 0;
}

int psx_game_key(int scancode, uint32_t keycode, int down, int plain) {
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

void psx_game_before_input(void) { disruptor_capsule_before_input(); }

void psx_game_after_input(void) {
    disruptor_capsule_after_input();
    disruptor_selection_wheel_guard_input();
}

void psx_game_audio_output(int16_t *stereo, int frames, int rate) { disruptor::audio::shape_output(stereo, frames, rate); }

int psx_game_smooth_movies(void) { return disruptor_smooth_movies_enabled(); }

}  // extern "C"

#ifdef PSX_HAS_GAME_DISPATCH
extern "C" unsigned psx_game_dispatch_lookup_mismatches(void);

/* PSX_DISRUPTOR_DISPATCH_CHECK=1 compares the dispatch lookup with a binary search around every key and ends the run. */
PSX_MOD_CONSTRUCTOR(disruptor_check_dispatch_lookup) {
    if (!std::getenv("PSX_DISRUPTOR_DISPATCH_CHECK")) return;
    const unsigned mismatches = psx_game_dispatch_lookup_mismatches();
    std::fprintf(stderr, "disruptor: dispatch lookup: %u mismatches with the binary search\n", mismatches);
    std::_Exit(mismatches == 0 ? 0 : 1);
}
#endif
