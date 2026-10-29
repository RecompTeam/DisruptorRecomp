#include "game/session/main_menu_exit.h"

#include "cpu_state.h"
#include "game/language/language.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "psx_sdl.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

namespace menu_exit = disruptor::main_menu_exit;

/* A replay plays what was recorded and ends at its last frame: the press was the recording's own exit. */
const bool g_replaying = std::getenv("PSX_DISRUPTOR_CAPSULE_REPLAY") != nullptr;
uint32_t g_word = 0u;

/* Allocated at the first draw and not at the start: until then the expansion region stays open bus, as on the console. */
void draw_entry(CPUState *cpu, uint32_t) {
    if (!menu_exit::draws_exit(cpu->gpr[menu_exit::kReturn], cpu->gpr[menu_exit::kItem])) return;
    if (g_word == 0u) {
        const std::string_view word = menu_exit::word(disruptor_language_disc_name());
        const uint32_t at = psx_mod_alloc_guest_memory(static_cast<uint32_t>(word.size()) + 1u, 4u);
        if (at == 0u) return;
        for (size_t index = 0; index < word.size(); ++index)
            psx_mod_write_byte(at + static_cast<uint32_t>(index), static_cast<uint8_t>(word[index]));
        psx_mod_write_byte(at + static_cast<uint32_t>(word.size()), 0u);
        g_word = at;
    }
    cpu->gpr[menu_exit::kString] = g_word;
}

void menu_entry(CPUState *cpu, uint32_t) {
    const uint32_t pressed = cpu->gpr[menu_exit::kButtons];
    if (!menu_exit::exits(psx_mod_read_word(menu_exit::kCursor), pressed)) return;
    cpu->gpr[menu_exit::kButtons] = menu_exit::kept(pressed);
    if (g_replaying || psx_netplay_active() || g_ls_mode != 0 || g_ls_replay_active != 0) return;
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_main_menu_exit) {
    if (psx_mod_register_function_entry_plugin("disruptor.main_menu_exit.draw", menu_exit::kItemDraw, draw_entry) &&
        psx_mod_register_function_entry_plugin("disruptor.main_menu_exit.menu", menu_exit::kHandler, menu_entry))
        return;
    std::fprintf(stderr, "disruptor: no EXIT hooks, the patched main menu at %08X would open the death screen\n",
                 menu_exit::kHandler);
    std::abort();
}

}  // namespace
