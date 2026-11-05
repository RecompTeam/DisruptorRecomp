#include "game/session/main_menu_exit.h"

#include "cpu_state.h"
#include "func_override.h"
#include "game/input/menu_pointer.h"
#include "game/input/mouse_aim.h"
#include "game/language/language.h"
#include "game/session/capsule.h"
#include "game/session/menu_pointing.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "psx_netplay.h"
#include "psx_sdl.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

namespace menu_exit = disruptor::main_menu_exit;
namespace pointer = disruptor::menu_pointer;
namespace pointing = disruptor::menu_pointing;

uint32_t g_word = 0u;
/* Each shown item as it was drawn last, where the pointer was, the item a press asked for and when the menu ran. */
std::array<pointer::Box, menu_exit::kItems> g_boxes{};
std::optional<pointer::Point> g_pointed;
int g_clicked = -1;
std::atomic<std::chrono::steady_clock::rep> g_shown{0}; /* asked by the frame's drawing on another thread too */
constexpr std::chrono::milliseconds kStillShown{200};
constexpr uint32_t kScreen = 0x800715B0u; /* gp+1124: 0 while the main menu is the screen */

/* The row nearest the pointer within reach, as wide as the widest item: the items stand centred one under another. */
int item_under(const pointer::Point& point) {
    int widest = 0;
    for (const pointer::Box& box : g_boxes) widest = std::max(widest, box.w);
    int nearest = -1;
    double best = 0.0;
    for (uint32_t item = 0; item < menu_exit::kItems; ++item) {
        if (g_boxes[item].w <= 0 || menu_exit::hidden(item)) continue;
        const std::optional<double> near = pointer::reach(pointer::centred(widest, g_boxes[item].y), point);
        if (near && (nearest < 0 || *near < best)) {
            nearest = static_cast<int>(item);
            best = *near;
        }
    }
    return nearest;
}

/* Noted for a capsule, as a turn of the view is: a replay puts the item back where the pointer put it. */
void put_cursor(int item) {
    if (psx_mod_read_word(menu_exit::kCursor) == static_cast<uint32_t>(item)) return;
    psx_mod_write_word(menu_exit::kCursor, static_cast<uint32_t>(item));
    disruptor_capsule_note_byte(menu_exit::kCursor, static_cast<uint8_t>(item));
}

/* A press's item first, held until this entry, whatever the button did since. Else an item the pointer moved onto. */
void point() {
    const int clicked = pointing::allowed() ? std::exchange(g_clicked, -1) : (g_clicked = -1);
    const std::optional<pointer::Point> at = pointing::now();
    const bool moved = at && g_pointed && (at->x != g_pointed->x || at->y != g_pointed->y);
    g_pointed = at;
    if (clicked >= 0) {
        put_cursor(clicked);
    } else if (moved) {
        if (const int item = item_under(*at); item >= 0) put_cursor(item);
    }
}

/* Before the guest runs, as long as any word: taken at the first draw it left states saved earlier unloadable. */
void reserve_word() {
    if (g_word == 0u) g_word = psx_mod_alloc_guest_memory(static_cast<uint32_t>(menu_exit::kLongestWord) + 1u, 4u);
}

void draw_entry(CPUState *cpu, uint32_t) {
    if (cpu->gpr[menu_exit::kReturn] != menu_exit::kItemDrawn) return;
    const uint32_t item = cpu->gpr[menu_exit::kItem];
    if (menu_exit::hidden(item)) {
        cpu->gpr[2] = 0u;
        psx_mod_finish_function(cpu);
        return;
    }
    cpu->gpr[menu_exit::kY] -= menu_exit::raised(item);
    if (g_word != 0u && menu_exit::draws_exit(cpu->gpr[menu_exit::kReturn], item)) {
        const std::string_view word = menu_exit::word(disruptor_language_disc_name());
        for (size_t index = 0; index < word.size(); ++index)
            psx_mod_write_byte(g_word + static_cast<uint32_t>(index), static_cast<uint8_t>(word[index]));
        psx_mod_write_byte(g_word + static_cast<uint32_t>(word.size()), 0u);
        cpu->gpr[menu_exit::kString] = g_word;
    }
    if (item < menu_exit::kItems)
        g_boxes[item] = pointer::centred(pointer::guest_text_width(cpu->gpr[menu_exit::kString], psx_mod_read_byte),
                                         static_cast<int32_t>(cpu->gpr[menu_exit::kY]));
}

void default_layout(CPUState *cpu) {
    if (psx_mod_read_word(menu_exit::kLayout) == 0u && psx_mod_read_byte(menu_exit::kLayoutChanged) == 0u) return;
    psx_mod_write_word(menu_exit::kLayout, 0u);
    psx_mod_write_byte(menu_exit::kLayoutChanged, 0u);
    std::array<uint32_t, 32> saved{};
    std::copy(std::begin(cpu->gpr), std::end(cpu->gpr), saved.begin());
    const uint32_t hi = cpu->hi, lo = cpu->lo, pc = cpu->pc;
    cpu->gpr[4] = 0u;
    func_override_guest_call(cpu, menu_exit::kApplyLayout, menu_exit::kApplyReturn);
    std::copy(saved.begin(), saved.end(), std::begin(cpu->gpr));
    cpu->hi = hi;
    cpu->lo = lo;
    cpu->pc = pc;
}

void menu_entry(CPUState *cpu, uint32_t) {
    g_shown.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    default_layout(cpu);
    point();
    const uint32_t cursor = psx_mod_read_word(menu_exit::kCursor);
    const menu_exit::Handed hand = menu_exit::handed(cursor, cpu->gpr[menu_exit::kButtons]);
    if (hand.cursor != cursor) psx_mod_write_word(menu_exit::kCursor, hand.cursor);
    const uint32_t pressed = cpu->gpr[menu_exit::kButtons] = hand.pressed;
    if (!menu_exit::exits(hand.cursor, pressed)) return;
    cpu->gpr[menu_exit::kButtons] = menu_exit::kept(pressed);
    if (pointing::replaying() || psx_netplay_active() || g_ls_mode != 0 || g_ls_replay_active != 0) return;
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    SDL_PushEvent(&quit);
}

extern "C" int disruptor_main_menu_pointing(void) {
    if (!pointing::allowed() || psx_mod_read_word(kScreen) != 0u) return 0;
    const std::chrono::steady_clock::duration shown{g_shown.load(std::memory_order_relaxed)};
    return std::chrono::steady_clock::now().time_since_epoch() - shown < kStillShown ? 1 : 0;
}

extern "C" int disruptor_main_menu_shown(void) {
    const std::chrono::steady_clock::duration shown{g_shown.load(std::memory_order_relaxed)};
    return psx_mod_read_word(kScreen) == 0u && std::chrono::steady_clock::now().time_since_epoch() - shown < kStillShown ? 1 : 0;
}

extern "C" int disruptor_main_menu_click(void) {
    const std::optional<pointer::Point> at = disruptor_main_menu_pointing() ? pointing::now() : std::nullopt;
    g_clicked = at ? item_under(*at) : -1;
    return g_clicked;
}

PSX_MOD_CONSTRUCTOR(register_disruptor_main_menu_exit) {
    if (psx_mod_register_activation_plugin("disruptor.main_menu_exit.draw", reserve_word) &&
        psx_mod_register_function_entry_plugin("disruptor.main_menu_exit.draw", menu_exit::kItemDraw, draw_entry) &&
        psx_mod_register_function_entry_plugin("disruptor.main_menu_exit.menu", menu_exit::kHandler, menu_entry))
        return;
    std::fprintf(stderr, "disruptor: no EXIT hooks, the patched main menu at %08X would open the death screen\n",
                 menu_exit::kHandler);
    std::abort();
}

}  // namespace
