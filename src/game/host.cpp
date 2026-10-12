#include "game_host.h"

#include "config_loader.h"
#include "game/language/language.h"
#include "game/render/far_rendering.h"
#include "game/render/precision_routes.h"
#include "game/session/capsule.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

extern "C" int disruptor_name_entry_key(int scancode, uint32_t keycode, int down, int plain);

bool psx_game_settings_loaded(const PSXRecompV4::UserSettings &settings) {
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
    disruptor_far_rendering_abandon_metrics();
}

int psx_game_key(int scancode, uint32_t keycode, int down, int plain) {
    return disruptor_name_entry_key(scancode, keycode, down, plain);
}

void psx_game_before_input(void) { disruptor_capsule_before_input(); }

void psx_game_after_input(void) { disruptor_capsule_after_input(); }

}  // extern "C"
