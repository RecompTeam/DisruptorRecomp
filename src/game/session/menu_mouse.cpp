#include "game/session/menu_mouse.h"

#include "cpu_state.h"
#include "game/input/menu_pointer.h"
#include "game/input/menu_rows.h"
#include "game/language/language.h"
#include "game/session/capsule.h"
#include "game/session/main_menu_exit.h"
#include "game/session/menu_pointing.h"
#include "mod_plugins.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string_view>

namespace {

namespace rows = disruptor::menu_rows;
namespace pointer = disruptor::menu_pointer;
namespace pointing = disruptor::menu_pointing;

constexpr uint32_t kItemDraw = 0x8001A46Cu;
constexpr uint32_t kTick = 0x80039450u; /* reads the pause menu's buttons before its lines are drawn */
constexpr int kString = 4, kX = 5, kY = 6, kCounter = 16, kReturn = 31;
constexpr int kPauseString = 5, kPauseX = 6, kPauseY = 7;
constexpr std::chrono::milliseconds kStillShown{200};
/* A row not drawn again for this long is gone: a pass ends only where a row is drawn again, and none is while a
 * screen saves or loads. */
constexpr std::chrono::milliseconds kFresh{100};

struct Guest {
    uint32_t read_word(uint32_t address) const { return psx_mod_read_word(address); }
    uint8_t read_byte(uint32_t address) const { return psx_mod_read_byte(address); }
};

/* The rows one pass of a screen drew. A pass ends where one of them is drawn again or another screen draws. */
struct Pass {
    uint32_t screen = 0xFFFFFFFFu;
    std::array<rows::Drawn, 24> drawn{};
    size_t count = 0;

    std::span<const rows::Drawn> view() const { return {drawn.data(), count}; }
    bool has(uint32_t returns, int32_t row) const {
        for (const rows::Drawn &draw : view())
            if (draw.returns == returns && draw.row == row) return true;
        return false;
    }
};

/* A press's row, put under the cursor once before the game reads the press. */
struct Asked {
    uint32_t screen = 0;
    int32_t row = 0;
    uint64_t pass = 0;
};

/* The front end's screens, or the pause menu in play: what they drew and where the pointer moved. The time and
 * screen are asked by the frame's drawing on another thread too. */
struct Surface {
    Pass drawing, shown;
    uint64_t pass = 0;
    std::atomic<uint32_t> alive_screen{0xFFFFFFFFu};
    std::atomic<std::chrono::steady_clock::rep> alive{0};
    std::optional<pointer::Point> pointed;
    std::optional<int32_t> moved_onto;
    std::optional<Asked> asked;

    std::span<const rows::Drawn> shown_on(uint32_t screen) const {
        return shown.screen == screen ? shown.view() : std::span<const rows::Drawn>{};
    }
    bool recent(uint32_t screen) const {
        const std::chrono::steady_clock::duration last{alive.load(std::memory_order_relaxed)};
        return alive_screen.load(std::memory_order_relaxed) == screen &&
               std::chrono::steady_clock::now().time_since_epoch() - last < kStillShown;
    }
};

struct Button {
    bool was_down = false;
    uint16_t held = 0;
};

/* The draws of a pass the game would still draw as rows now: the pass before a question opens holds the lines under it. */
struct Live {
    std::array<rows::Drawn, 24> drawn{};
    size_t count = 0;

    explicit Live(std::span<const rows::Drawn> pass) {
        const int64_t now = std::chrono::steady_clock::now().time_since_epoch().count();
        const int64_t fresh = std::chrono::duration_cast<std::chrono::steady_clock::duration>(kFresh).count();
        for (const rows::Drawn &draw : pass)
            if (count < drawn.size() && now - draw.at < fresh &&
                rows::still_drawn(std::span<const rows::Drawn>(&draw, 1), draw.row, Guest{}))
                drawn[count++] = draw;
    }
    std::span<const rows::Drawn> view() const { return {drawn.data(), count}; }
};

Surface g_screens, g_pause;
Button g_left, g_right;

bool holding() { return g_left.held != 0u || g_right.held != 0u; }

void keep_alive(Surface &surface, uint32_t screen) {
    surface.alive.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    surface.alive_screen.store(screen, std::memory_order_relaxed);
}

/* The row the pointer moved onto since it was last looked at. While a press holds its buttons the game sees them on the
 * press's row: a move made meanwhile counts after it. */
std::optional<int32_t> moved_onto(Surface &surface, uint32_t screen) {
    if (holding()) return std::nullopt;
    const std::optional<pointer::Point> at = pointing::now();
    const bool moved = at && surface.pointed && (at->x != surface.pointed->x || at->y != surface.pointed->y);
    surface.pointed = at;
    return moved ? rows::row_under(Live(surface.shown_on(screen)).view(), *at) : std::nullopt;
}

void begin_pass(Surface &surface, uint32_t screen) {
    surface.shown = surface.drawing;
    surface.drawing = Pass{};
    surface.drawing.screen = screen;
    ++surface.pass;
    if (surface.asked && surface.pass > surface.asked->pass + 1u) surface.asked.reset();
}

/* Noted for a capsule, as a turn of the view is: a replay puts the cursor back where the pointer put it. */
void write(const rows::Row &row, uint32_t counter) {
    for (const rows::Write &each : row.writes) {
        if (each.address == 0u) continue;
        const uint32_t value = rows::value_of(each, counter);
        if (psx_mod_read_word(each.address) == value) continue;
        psx_mod_write_word(each.address, value);
        disruptor_capsule_note_byte(each.address, static_cast<uint8_t>(value));
    }
}

void record(Surface &surface, uint32_t screen, uint32_t returns, const rows::Row &row, uint32_t counter, const pointer::Box &box) {
    const int32_t id = rows::row_of(row, counter);
    if (screen != surface.drawing.screen || surface.drawing.has(returns, id)) {
        begin_pass(surface, screen);
        if (&surface == &g_screens) surface.moved_onto = moved_onto(surface, screen);
    }
    if (surface.drawing.count < surface.drawing.drawn.size())
        surface.drawing.drawn[surface.drawing.count++] = {returns, id, box, row.left, row.right, &row, counter,
                                                          std::chrono::steady_clock::now().time_since_epoch().count()};
}

/* Every screen of the front end draws its rows before it reads the pad: a row's writes are made at its draw. */
void draw_entry(CPUState *cpu, uint32_t) {
    const uint32_t returns = cpu->gpr[kReturn], screen = psx_mod_read_word(rows::kScreen);
    if (!rows::draws_for(screen, returns)) return;
    keep_alive(g_screens, screen);
    const uint32_t counter = cpu->gpr[kCounter], string = cpu->gpr[kString];
    const rows::Row *row = rows::match(returns, screen, counter, string, Guest{});
    if (!row) return;
    const int width = pointer::guest_text_width(string, psx_mod_read_byte);
    record(g_screens, screen, returns, *row, counter,
           pointer::placed(static_cast<int32_t>(cpu->gpr[kX]), static_cast<int32_t>(cpu->gpr[kY]), width));
    if (!pointing::allowed()) {
        g_screens.asked.reset();
        g_screens.moved_onto.reset();
        return;
    }
    const int32_t id = rows::row_of(*row, counter);
    const bool asked = g_screens.asked && g_screens.asked->screen == screen && g_screens.asked->row == id;
    const bool moved = g_screens.moved_onto == id;
    if (!asked && !moved) return;
    if (asked) g_screens.asked.reset();
    if (moved) g_screens.moved_onto.reset();
    write(*row, counter);
}

bool paused() { return psx_mod_read_byte(rows::kPauseState) == rows::kPaused; }

/* A pause line as 0x800429DC draws it: the string in the small font, the language disc's words where it has them, centred
 * on 160 at x 0. */
pointer::Box pause_box(uint32_t string, int32_t x, int32_t y) {
    std::array<char, 64> text{};
    size_t length = 0;
    for (; length + 1 < text.size(); ++length) {
        text[length] = static_cast<char>(psx_mod_read_byte(string + static_cast<uint32_t>(length)));
        if (text[length] == '\0') break;
    }
    text[length] = '\0';
    std::array<char, 96> words{};
    const bool other = disruptor_language_words(text.data(), words.data(), static_cast<uint32_t>(words.size())) != 0;
    const int width = pointer::small_text_width(other ? std::string_view(words.data()) : std::string_view(text.data(), length),
                                                psx_mod_read_byte);
    return {x == 0 ? pointer::kFrameWide / 2 - width / 2 : x, y, width, pointer::kSmallRowHigh};
}

void pause_draw_entry(CPUState *cpu, uint32_t) {
    const uint32_t returns = cpu->gpr[kReturn];
    if (!rows::pause_draws(returns) || !paused()) return;
    keep_alive(g_pause, rows::kPause);
    const uint32_t string = cpu->gpr[kPauseString];
    const rows::Row *row = rows::match(returns, rows::kPause, 0u, string, Guest{});
    if (!row) return;
    record(g_pause, rows::kPause, returns, *row, 0u,
           pause_box(string, static_cast<int32_t>(cpu->gpr[kPauseX]), static_cast<int32_t>(cpu->gpr[kPauseY])));
}

/* The tick reads the pause menu's buttons and its lines are drawn after: a press's row or the one the pointer moved onto
 * is put under the cursor here, as the conditions stand now. */
void tick_entry(CPUState *, uint32_t) {
    if (!paused() || !pointing::allowed()) {
        g_pause.asked.reset();
        return;
    }
    const std::optional<int32_t> moved = moved_onto(g_pause, rows::kPause);
    std::optional<int32_t> row = moved;
    if (g_pause.asked) row = g_pause.asked->row;
    g_pause.asked.reset();
    if (!row) return;
    const Live live(g_pause.shown_on(rows::kPause));
    const rows::Drawn *draw = rows::live_draw(live.view(), *row, Guest{});
    if (draw) write(*draw->source, draw->counter);
}

bool rows_pointing() {
    return pointing::allowed() && g_screens.recent(psx_mod_read_word(rows::kScreen));
}

bool pause_pointing() { return pointing::allowed() && paused() && g_pause.recent(rows::kPause); }

uint16_t pressed_on(Surface &surface, uint32_t screen, bool right) {
    const std::optional<pointer::Point> at = pointing::now();
    if (!at) return 0;
    const Live live(surface.shown_on(screen));
    const std::span<const rows::Drawn> drawn = live.view();
    const std::optional<int32_t> row = rows::row_under(drawn, *at);
    if (!row) return 0;
    surface.asked = Asked{screen, *row, surface.pass};
    return rows::press_at(drawn, *row, *at, right);
}

uint16_t pressed(bool right) {
    if (disruptor_main_menu_pointing()) return !right && disruptor_main_menu_click() >= 0 ? rows::kCross : uint16_t{0};
    if (pause_pointing()) return pressed_on(g_pause, rows::kPause, right);
    return rows_pointing() ? pressed_on(g_screens, psx_mod_read_word(rows::kScreen), right) : uint16_t{0};
}

uint16_t hold(Button &button, bool down, bool right) {
    if (!down) button.held = 0;
    else if (!button.was_down) button.held = pressed(right);
    button.was_down = down;
    return button.held;
}

PSX_MOD_CONSTRUCTOR(register_disruptor_menu_mouse) {
    if (!psx_mod_register_function_entry_plugin("disruptor.menu_mouse.draw", kItemDraw, draw_entry))
        std::fprintf(stderr, "disruptor: no menu mouse hook, the pointer stays off the front end's other screens\n");
    if (!psx_mod_register_function_entry_plugin("disruptor.menu_mouse.pause", rows::kPauseDraw, pause_draw_entry) ||
        !psx_mod_register_function_entry_plugin("disruptor.menu_mouse.tick", kTick, tick_entry))
        std::fprintf(stderr, "disruptor: no pause menu mouse hooks, the pointer stays off the pause menu\n");
}

}  // namespace

extern "C" int disruptor_menu_pointing(void) {
    return disruptor_main_menu_pointing() || pause_pointing() || rows_pointing() ? 1 : 0;
}

extern "C" uint16_t disruptor_menu_buttons(int left, int right) {
    return static_cast<uint16_t>(hold(g_left, left != 0, false) | hold(g_right, right != 0, true));
}
