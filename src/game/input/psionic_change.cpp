#include "game/input/psionic_change.h"

#include "cdrom.h"
#include "cpu_state.h"
#include "game/guest.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

extern "C" int psx_netplay_active(void);
extern "C" int disruptor_number_keys_enabled(void);
extern "C" int disruptor_psionic_keys_use_enabled(void);

namespace {

constexpr uint32_t kTick = 0x80039450u;

bool g_off = false;

bool wanted() {
    return !g_off && disruptor_number_keys_enabled() && disruptor_psionic_keys_use_enabled() && !psx_netplay_active();
}

/* Given at every tick and only there: a loaded state keeps the speed it was saved with until its next tick. */
void tick_begins(CPUState *, uint32_t) {
    cdrom_set_speed(disruptor::psionic_change::divisor(disruptor::Guest{}, wanted()));
}

PSX_MOD_CONSTRUCTOR(register_disruptor_psionic_change) {
    const char *asked = std::getenv("PSX_DISRUPTOR_QUICK_POWER");
    g_off = asked && asked[0] == '0';
    if (!psx_mod_register_function_entry_plugin("disruptor.psionic_change.tick", kTick, tick_begins)) {
        g_off = true;
        std::fprintf(stderr, "disruptor: failed to register the psionic change hook\n");
    }
}

} // namespace
