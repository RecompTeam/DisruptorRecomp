#include "game/session/card_choice.h"

#include "cpu_state.h"
#include "lockstep.h"
#include "memcard.h"
#include "mod_plugins.h"
#include "psx_netplay.h"

#include <cstdint>
#include <cstdio>

namespace {

namespace cards = disruptor::card_choice;

bool holds_game(int card) {
    uint8_t frame[cards::kFrameBytes];
    for (int index = 1; index <= cards::kDirectoryFrames; ++index)
        if (memcard_read_sector(card, index, frame) == 0 && cards::opens_game_file(frame)) return true;
    return false;
}

void menu_entry(CPUState *, uint32_t) {
    if (psx_netplay_active() || g_ls_mode != 0 || g_ls_replay_active != 0) return;
    const auto selected = static_cast<int32_t>(psx_mod_read_word(cards::kSelected));
    if (selected != 0) return;
    const int32_t to = cards::chosen(selected, holds_game(0), holds_game(1), psx_mod_read_word(cards::kPresent + 4u) == 1u);
    if (to != selected) psx_mod_write_word(cards::kSelected, static_cast<uint32_t>(to));
}

}  // namespace

PSX_MOD_CONSTRUCTOR(register_disruptor_card_choice) {
    if (!psx_mod_register_function_entry_plugin("disruptor.card_choice.menu", cards::kHandler, menu_entry))
        std::fprintf(stderr, "disruptor: no card choice hook, saves on the right card alone stay out of reach\n");
}
