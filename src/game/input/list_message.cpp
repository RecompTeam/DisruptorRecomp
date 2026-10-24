#include "game/input/list_message.h"
#include "game/input/weapon_list.h"

#include "cpu_state.h"
#include "game/guest.h"
#include "game/language/language.h"
#include "lockstep.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

using namespace disruptor::list_message;
using disruptor::Guest;

constexpr uint32_t kMessageDraw = 0x800427D0u;
constexpr uint32_t kDrawList = 0x8004D684u;  /* the libgpu DrawOTag the message's draw sends its own list with */
constexpr uint32_t kSentByDraw = 0x800429BCu;
constexpr uint32_t kLoadImage = 0x8004D348u;
constexpr uint32_t kLoadedByDraw = 0x80042858u;
constexpr int kPrintedRegister = 16; /* $s0: how wide the printer made the line (0x8004284C) */
constexpr uint32_t kTick = 0x80039450u;

Kept g_kept;
bool g_off = false;

/* The comparator runs each block twice from one state: what is kept here would be taken by the first run. */
bool wanted() { return !g_off && g_ls_mode == 0 && g_ls_replay_active == 0; }

void draw(CPUState *, uint32_t) {
    if (wanted()) draw_begins(Guest{}, g_kept);
}

void loads(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[31] != kLoadedByDraw || !wanted() || g_kept.name[0] == 0u) return;
    const char *name = reinterpret_cast<const char *>(g_kept.name.data());
    char words[disruptor::stock_frames::kMessageBytes];
    const std::string_view shown = disruptor_language_words(name, words, sizeof words) ? words : name;
    Guest guest;
    print_name(guest, g_kept, cpu->gpr[kPrintedRegister], shown);
}

void sent(CPUState *cpu, uint32_t) {
    if (!cpu || cpu->gpr[31] != kSentByDraw || !wanted()) return;
    Guest guest;
    draw_ends(guest, g_kept);
}

void tick_begins(CPUState *, uint32_t) { frame_over(g_kept); }

PSX_MOD_CONSTRUCTOR(register_disruptor_list_message) {
    const char *asked = std::getenv("PSX_DISRUPTOR_COUNTDOWN_LINE");
    g_off = asked && asked[0] == '0';
    if (!psx_mod_register_function_entry_plugin("disruptor.list_message.draw", kMessageDraw, draw) ||
        !psx_mod_register_function_entry_plugin("disruptor.list_message.loads", kLoadImage, loads) ||
        !psx_mod_register_function_entry_plugin("disruptor.list_message.sent", kDrawList, sent) ||
        !psx_mod_register_function_entry_plugin("disruptor.list_message.tick", kTick, tick_begins)) {
        g_off = true;
        std::fprintf(stderr, "disruptor: failed to register the countdown line hooks\n");
    }
}

} // namespace

extern "C" void disruptor_list_message_builder_begins(void) {
    if (wanted()) builder_begins(Guest{}, g_kept);
}

extern "C" void disruptor_list_message_builder_ends(uint32_t forced) {
    if (!wanted()) return;
    Guest guest;
    builder_ends(guest, g_kept, forced);
}

extern "C" void disruptor_list_message_state_loaded(void) { forget(g_kept); }

extern "C" int disruptor_list_message_save_deferred(void) { return saving_deferred(g_kept) ? 1 : 0; }
