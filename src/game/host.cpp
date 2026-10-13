#include "game_host.h"

#include "config_loader.h"
#include "game/language/language.h"
#include "game/input/selection_wheel.h"
#include "game/render/far_rendering.h"
#include "game/render/precision_routes.h"
#include "game/session/capsule.h"
#include "game/session/diagnostics.h"
#include "gpu.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int disruptor_name_entry_key(int scancode, uint32_t keycode, int down, int plain);
extern "C" void disruptor_name_entry_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count);
extern "C" void disruptor_name_entry_state_loaded(void);
extern "C" int disruptor_grown_quad_sources(uint32_t command_address, const uint32_t *packet, uint32_t source_address[4],
                                            uint32_t source_word[4]);

bool psx_game_settings_loaded(const PSXRecompV4::UserSettings &settings) {
    disruptor_diagnostics_configure(settings.has_detailed_logging && settings.detailed_logging);
    if (settings.has_language_disc && !std::getenv("PSX_DISRUPTOR_LANGUAGE_DISC"))
        disruptor_language_set_disc(settings.language_disc.c_str());
    return settings.has_skip_intro && settings.skip_intro && !std::getenv("PSX_DISRUPTOR_SKIP_INTRO");
}

extern "C" {

int psx_game_configure(const char *game_id, size_t length) {
    if (const char *asked = std::getenv("PSX_DISRUPTOR_IMPROVED_SHADOWS")) gpu_set_shadow_shape(asked[0] == '1');
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
    disruptor_far_rendering_abandon_metrics();
}

int psx_game_key(int scancode, uint32_t keycode, int down, int plain) {
    return disruptor_name_entry_key(scancode, keycode, down, plain);
}

int psx_game_grown_quad_sources(uint32_t command_address, const uint32_t *packet, uint32_t source_address[4],
                                uint32_t source_word[4]) {
    return disruptor_grown_quad_sources(command_address, packet, source_address, source_word);
}

void psx_game_keys_for_pad(const uint8_t *keys, uint8_t *kept, int count) {
    disruptor_name_entry_keys_for_pad(keys, kept, count);
}

void psx_game_before_input(void) { disruptor_capsule_before_input(); }

void psx_game_after_input(void) {
    disruptor_capsule_after_input();
    disruptor_selection_wheel_guard_input();
}

}  // extern "C"
