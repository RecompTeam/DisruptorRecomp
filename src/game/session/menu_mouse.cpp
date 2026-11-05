#include "game/session/menu_mouse.h"

#include "cpu_state.h"
#include "game/input/menu_pointer.h"
#include "game/input/menu_rows.h"
#include "game/input/states_row.h"
#include "game/language/language.h"
#include "game/session/capsule.h"
#include "game/session/main_menu_exit.h"
#include "game/session/menu_pointing.h"
#include "game/session/state_screen.h"
#include "game/ui/state_browser.h"
#include "gpu.h"
#include "mod_plugins.h"
#include "savestate.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace rows = disruptor::menu_rows;
namespace pointer = disruptor::menu_pointer;
namespace pointing = disruptor::menu_pointing;
namespace states = disruptor::states_row;
namespace screen = disruptor::state_screen;
namespace browser = disruptor::state_browser;

constexpr uint32_t kItemDraw = 0x8001A46Cu;
constexpr uint32_t kTick = 0x80039450u; /* reads the pause menu's buttons before its lines are drawn */
constexpr uint32_t kIdleFrames = 0x80071428u; /* gp+0x2DC: front end frames with no pad button held, the demo starts past 900 */
constexpr int kString = 4, kHighlight = 7, kX = 5, kY = 6, kCounter = 16, kStack = 29, kReturn = 31;
constexpr int kPauseString = 5, kPauseX = 6, kPauseY = 7;
constexpr std::chrono::milliseconds kStillShown{200};
/* A row not drawn again for this long is gone: a pass ends only where a row is drawn again, and none is while a
 * screen saves or loads. */
constexpr std::chrono::milliseconds kFresh{100};
/* The pause tick's buttons: this frame's and the last frame's, a press is one down now and not then. */
constexpr uint32_t kPadNow = 0x80077670u, kPadBefore = 0x80077674u;
constexpr uint32_t kPauseBlink = 0x800716DCu; /* gp+1424, counts the pause's ticks: a chosen line is lit 16 of 32 */
constexpr uint32_t kTinyFont = 0x80071358u, kTinyWidths = 0x80056E80u, kSmallFont = 0x80071398u;
constexpr int kMenuClutX = 0, kMenuClutY = 481, kPlayClutX = 512, kPlayClutY = 177;
constexpr uint32_t kMissionNames = 108u; /* the menu text's offset of the first mission's name */
/* LOAD GAME: its handler, a0 the presses; the mission icons' draw; the saved games' records, 5 bytes each from 256 past
 * the word's address, the first the furthest mission; the mission each goes on from; the items' colours and blink. */
constexpr uint32_t kLoadHandler = 0x8001D3D8u, kIconDraw = 0x8001A794u, kRecords = 0x800776D4u, kLevels = 0x80071504u;
constexpr uint32_t kShades = 0x80057004u, kItemBlink = 0x800716F8u;
constexpr uint32_t kMenuFont = 0x80071474u;
constexpr uint32_t kCross = 0x40u, kTriangle = 0x10u;
/* The icons: 24 by 18 direct colour at (512, 256), seven down 36 apart, columns 48 apart, by mission (0x8001A7C8:
 * u 48 (n / 7), v 36 (n % 7)). gp+944 is 0 until the handler's first entry set LOAD GAME up. */
constexpr int kIconsX = 512, kIconsY = 256, kIconStepU = 48, kIconStepV = 36, kIconsDown = 7;
constexpr uint32_t kLoadSetUp = 0x800714FCu;

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
    bool any_recent() const {
        const std::chrono::steady_clock::duration last{alive.load(std::memory_order_relaxed)};
        return std::chrono::steady_clock::now().time_since_epoch() - last < kStillShown;
    }
};

struct Button {
    bool was_down = false;
    uint16_t held = 0;
    uint32_t place = 0; /* Esc's: the screen or the pause it was pressed on */
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
Button g_left, g_right, g_escape;
/* Set from the events' thread when Esc closed a menu of the host's. */
std::atomic<bool> g_escape_taken{false};
/* The mouse or a key was used: the game counts only its pad's buttons as someone there. */
std::atomic<bool> g_active{false};
/* SAVE STATES chosen on LOAD GAME and in the pause: the game's cursor stands on RETURN or MUSIC meanwhile. */
/* Written by the hooks, and cleared from the screen's input when a state is loaded. */
std::atomic<bool> g_pause_states{false};
/* LOAD GAME is the screen's only with every hook it needs: the draws, the icons and the handler. */
bool g_load_hooks = false;
/* What LOAD GAME drew this frame, for the screen that draws it instead. */
screen::GameSaves g_drawn;
/* LOAD GAME's saves went to the screen and no other screen has drawn since. */
bool g_load_shown = false;
constexpr uint32_t kEmptyWord = 276u, kUnavailableWord = 264u; /* the menu text's offsets of EMPTY and UNAVAILABLE */

bool holding() { return g_left.held != 0u || g_right.held != 0u || g_escape.held != 0u; }

/* The row is offered wherever the run plays alone: a replay steps through it as its recording did, but opens nothing. */
bool states_offered() {
    const char *folder = savestate_dir();
    return !psx_netplay_active() && g_ls_mode == 0 && g_ls_replay_active == 0 && savestate_title_blocked() == 0 && folder && folder[0];
}

bool states_open() { return states_offered() && !pointing::replaying(); }

/* LOAD GAME is the screen's wherever it opens and is ready: a replay, netplay or lockstep keep the game's own. */
bool load_screen() { return g_load_hooks && states_open() && screen::screen_ready(); }

std::string_view states_word() { return browser::word(browser::Word::title, disruptor_language_disc_name()); }

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

void put_word(uint32_t address, uint32_t value) {
    if (psx_mod_read_word(address) == value) return;
    psx_mod_write_word(address, value);
    disruptor_capsule_note_byte(address, static_cast<uint8_t>(value));
}

/* Noted for a capsule, as a turn of the view is: a replay puts the cursor back where the pointer put it. */
void write(const rows::Row &row, uint32_t counter) {
    for (const rows::Write &each : row.writes) {
        if (each.address == 0u) continue;
        const uint32_t value = rows::value_of(each, counter);
        if (each.address == rows::kStates) {
            g_pause_states = value != 0u;
            continue;
        }
        put_word(each.address, value);
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

/* A font of the game's as the host draws it, `count` glyphs of `wide` by `high` bytes at the word's address. */
screen::Font font_of(uint32_t glyphs_word, uint32_t widths, int wide, int high, int space, bool menu, int clut_x, int clut_y) {
    screen::Font font;
    font.wide = wide;
    font.high = high;
    font.space = space;
    font.menu = menu;
    font.glyphs.resize(static_cast<size_t>(64 * wide * high));
    const uint32_t glyphs = psx_mod_read_word(glyphs_word);
    for (size_t at = 0; at < font.glyphs.size(); ++at) font.glyphs[at] = psx_mod_read_byte(glyphs + static_cast<uint32_t>(at));
    for (size_t glyph = 0; glyph < font.widths.size(); ++glyph)
        font.widths[glyph] = static_cast<int8_t>(psx_mod_read_byte(widths + static_cast<uint32_t>(glyph)));
    for (size_t entry = 0; entry < font.palette.size(); ++entry)
        font.palette[entry] = gpu_vram_peek(clut_x + static_cast<int>(entry), clut_y);
    return font;
}

/* The 8 by 8 font, with its palette in the front end or in play. */
void publish_font(int clut_x, int clut_y) {
    screen::publish_font(font_of(kTinyFont, kTinyWidths, 8, 8, 4, false, clut_x, clut_y));
}

std::string guest_string(uint32_t at, uint32_t most = 40u) {
    std::string out;
    for (uint32_t index = 0; index < most; ++index) {
        const char letter = static_cast<char>(psx_mod_read_byte(at + index));
        if (letter == '\0') break;
        out += letter;
    }
    return out;
}

/* The missions' names as the front end's menu text has them, a language disc's own. */
std::array<std::string, 13> mission_names() {
    std::array<std::string, 13> names;
    const uint32_t text = psx_mod_read_word(rows::kMenuText), offsets = psx_mod_read_word(rows::kMenuOffsets);
    for (size_t mission = 0; mission < names.size(); ++mission)
        names[mission] = guest_string(text + psx_mod_read_word(offsets + kMissionNames + static_cast<uint32_t>(4u * mission)), 32u);
    return names;
}

disruptor::state_files::Picture icon(uint32_t mission) {
    const int left = kIconsX + static_cast<int>(mission / kIconsDown) * kIconStepU, top = kIconsY + static_cast<int>(mission % kIconsDown) * kIconStepV;
    disruptor::state_files::Picture picture{browser::kIconWide, browser::kIconHigh,
                                            std::vector<uint8_t>(static_cast<size_t>(browser::kIconWide * browser::kIconHigh) * 3u)};
    for (int row = 0; row < browser::kIconHigh; ++row)
        for (int column = 0; column < browser::kIconWide; ++column) {
            const uint16_t pixel = gpu_vram_peek(left + column, top + row);
            uint8_t *out = &picture.rgb[(static_cast<size_t>(row) * browser::kIconWide + column) * 3u];
            out[0] = disruptor::state_files::widened(pixel & 31u);
            out[1] = disruptor::state_files::widened(pixel >> 5 & 31u);
            out[2] = disruptor::state_files::widened(pixel >> 10 & 31u);
        }
    return picture;
}

/* LOAD GAME's draws when the screen draws it instead: what each would draw is kept, nothing is drawn. */
void take_load_draw(CPUState *cpu, uint32_t returns) {
    const std::string drawn = guest_string(cpu->gpr[kString]);
    const uint32_t slot = cpu->gpr[kCounter];
    if (returns == rows::kTitleDrawn) g_drawn.title = drawn;
    else if (returns == rows::kReturnDrawn) g_drawn.back = drawn;
    else if (returns == rows::kNameDrawn && slot < g_drawn.slots.size()) {
        g_drawn.slots[slot].name = drawn;
        g_drawn.slots[slot].hard = (cpu->gpr[kHighlight] & ~1u) == 6u;
    } else if (returns == rows::kSecondDrawn && slot < g_drawn.slots.size())
        g_drawn.slots[slot].second = drawn;
    cpu->gpr[2] = 0u;
    psx_mod_finish_function(cpu);
}

void icon_entry(CPUState *cpu, uint32_t) {
    if (psx_mod_read_word(rows::kScreen) == rows::kLoadGame && load_screen()) psx_mod_finish_function(cpu);
}

/* At the handler's entry the frame before is whole: it goes to the screen with the saved games' records, the names,
 * icons, fonts and colours. A choice the screen made is put in as the game's own: the slot and its mission, then Cross,
 * or Triangle back. */
void load_entry(CPUState *cpu, uint32_t) {
    if (!load_screen()) return;
    /* The first entry reads the card while the main menu's last frame stays shown: the screen waits for the next. */
    if (psx_mod_read_word(kLoadSetUp) == 0u) {
        g_drawn = {};
        return;
    }
    screen::note_context(browser::Place::front_end);
    publish_font(kMenuClutX, kMenuClutY);
    screen::publish_menu_font(font_of(kMenuFont, pointer::kGlyphWidths, 12, 14, pointer::kSpace, true, kMenuClutX, kMenuClutY));
    const uint32_t records = psx_mod_read_word(kRecords) + 256u;
    g_drawn.message = psx_mod_read_word(rows::kLoadMessage) != 0u;
    for (uint32_t slot = 0; slot < g_drawn.slots.size(); ++slot) {
        screen::GameSlot &game = g_drawn.slots[slot];
        game.state = psx_mod_read_byte(rows::kSlots + slot);
        game.most = std::max<uint32_t>(1u, psx_mod_read_byte(records + 5u * slot));
        game.level = std::clamp<uint32_t>(psx_mod_read_byte(kLevels + slot), 1u, game.most);
    }
    g_drawn.missions = mission_names();
    const uint32_t text = psx_mod_read_word(rows::kMenuText), offsets = psx_mod_read_word(rows::kMenuOffsets);
    g_drawn.empty = guest_string(text + psx_mod_read_word(offsets + kEmptyWord));
    g_drawn.unavailable = guest_string(text + psx_mod_read_word(offsets + kUnavailableWord));
    for (uint32_t mission = 0; mission < g_drawn.icons.size(); ++mission) g_drawn.icons[mission] = icon(mission);
    for (size_t shade = 0; shade < g_drawn.shades.size(); ++shade)
        for (size_t channel = 0; channel < 3; ++channel)
            g_drawn.shades[shade][channel] = psx_mod_read_byte(kShades + static_cast<uint32_t>(3u * shade + channel));
    g_drawn.blink = psx_mod_read_word(kItemBlink);
    screen::note_mission_names(g_drawn.missions);
    screen::publish_game_saves(g_drawn);
    g_load_shown = true;
    g_drawn = {};
    const screen::GameAsk ask = screen::take_game_ask();
    if (ask.kind == screen::GameAsk::Kind::back) {
        cpu->gpr[4] = kTriangle;
    } else if (ask.kind == screen::GameAsk::Kind::play) {
        const bool plays = ask.slot >= 0 && ask.slot < 4 && psx_mod_read_word(rows::kLoadMessage) == 0u &&
                           psx_mod_read_byte(rows::kSlots + static_cast<uint32_t>(ask.slot)) == 1u && ask.mission >= 1u &&
                           ask.mission <= std::max<uint32_t>(1u, psx_mod_read_byte(records + 5u * static_cast<uint32_t>(ask.slot)));
        if (!plays) {
            screen::note_ask_refused();
            return;
        }
        psx_mod_write_byte(kLevels + static_cast<uint32_t>(ask.slot), static_cast<uint8_t>(ask.mission));
        put_word(rows::kCursor, static_cast<uint32_t>(ask.slot));
        cpu->gpr[4] = kCross;
    }
}

/* Every screen of the front end draws its rows before it reads the pad: a row's writes are made at its draw. */
void draw_entry(CPUState *cpu, uint32_t) {
    if (g_active.exchange(false) && pointing::allowed()) psx_mod_write_word(kIdleFrames, 0u);
    const uint32_t returns = cpu->gpr[kReturn], screen = psx_mod_read_word(rows::kScreen);
    if (g_load_shown && screen != rows::kLoadGame) {
        g_load_shown = false;
        screen::note_load_left();
    }
    if (!rows::draws_for(screen, returns)) return;
    keep_alive(g_screens, screen);
    if (screen == rows::kLoadGame && load_screen()) {
        take_load_draw(cpu, returns);
        return;
    }
    const uint32_t counter = cpu->gpr[kCounter], string = cpu->gpr[kString];
    if (const rows::Row *row = rows::match(returns, screen, counter, string, Guest{}); row) {
        const int width = pointer::guest_text_width(string, psx_mod_read_byte);
        record(g_screens, screen, returns, *row, counter,
               pointer::placed(static_cast<int32_t>(cpu->gpr[kX]), static_cast<int32_t>(cpu->gpr[kY]), width));
        if (!pointing::allowed()) {
            g_screens.asked.reset();
            g_screens.moved_onto.reset();
        } else {
            const int32_t id = rows::row_of(*row, counter);
            const bool asked = g_screens.asked && g_screens.asked->screen == screen && g_screens.asked->row == id;
            const bool moved = g_screens.moved_onto == id;
            if (asked) g_screens.asked.reset();
            if (moved) g_screens.moved_onto.reset();
            if (asked || moved) write(*row, counter);
        }
    }
}

bool paused() { return psx_mod_read_byte(rows::kPauseState) == rows::kPaused; }

int pause_width(std::string_view text) { return pointer::small_text_width(text, psx_mod_read_byte); }

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
    const int width = pause_width(other ? std::string_view(words.data()) : std::string_view(text.data(), length));
    return {x == 0 ? pointer::kFrameWide / 2 - width / 2 : x, y, width, pointer::kSmallRowHigh};
}

/* The pause's four lines, RESUME to MUSIC, by their calls' returns. */
constexpr std::array<uint32_t, 4> kPauseLines = {rows::kResumeDrawn, 0x80042E1Cu, 0x80042EB0u, rows::kMusicDrawn};

void pause_draw_entry(CPUState *cpu, uint32_t) {
    const uint32_t returns = cpu->gpr[kReturn];
    if (!rows::pause_draws(returns) || !paused()) return;
    keep_alive(g_pause, rows::kPause);
    if (states_offered())
        for (uint32_t line = 0; line < kPauseLines.size(); ++line)
            if (returns == kPauseLines[line]) cpu->gpr[kPauseY] = static_cast<uint32_t>(states::pause_line_y(line));
    const uint32_t string = cpu->gpr[kPauseString];
    const rows::Row *row = rows::match(returns, rows::kPause, 0u, string, Guest{});
    if (!row) return;
    record(g_pause, rows::kPause, returns, *row, 0u,
           pause_box(string, static_cast<int32_t>(cpu->gpr[kPauseX]), static_cast<int32_t>(cpu->gpr[kPauseY])));
    if (returns != rows::kResumeDrawn || !states_offered()) return;
    /* SAVE STATES under RESUME: the host draws it, its row is kept with this pass, and RESUME is not lit while it is. */
    if (const rows::Row *states_row = rows::match(rows::kPauseStatesDrawn, rows::kPause, 0u, 0u, Guest{}); states_row) {
        const int width = pause_width(states_word());
        record(g_pause, rows::kPause, rows::kPauseStatesDrawn, *states_row, 0u,
               {pointer::kFrameWide / 2 - width / 2, states::kPauseY, width, pointer::kSmallRowHigh});
    }
    if (g_pause_states) psx_mod_write_word(cpu->gpr[kStack] + 16u, 0u);
}

/* The pause's SAVE STATES in its small font: lit full and dim by turns while chosen, dim else, as the game's lines are. */
std::vector<screen::Run> pause_row_runs(bool lit) {
    std::vector<screen::Run> runs;
    const std::string_view word = states_word();
    const uint32_t glyphs = psx_mod_read_word(kSmallFont);
    const uint32_t shade = lit && psx_mod_read_word(kPauseBlink) % 32u < 16u ? 128u : 60u;
    int x = pointer::kFrameWide / 2 - pause_width(word) / 2;
    for (const char letter : word) {
        if (letter == ' ') {
            x += pointer::kSmallSpace;
            continue;
        }
        const int glyph = pointer::small_glyph(letter);
        if (glyph < 0 || glyph >= 64) continue;
        for (int row = 0; row < pointer::kSmallRowHigh; ++row)
            for (int column = 0; column < 8; ++column) {
                const uint8_t index = psx_mod_read_byte(glyphs + static_cast<uint32_t>(glyph * 88 + row * 8 + column));
                if (index == 0u) continue;
                const uint16_t entry = gpu_vram_peek(kPlayClutX + index, kPlayClutY);
                if ((entry & 0x7FFFu) == 0u) continue;
                const auto channel = [shade](uint32_t five) {
                    return static_cast<uint8_t>(std::min<uint32_t>(255u, ((five << 3) * shade) / 128u));
                };
                const screen::Run run{x + column, states::kPauseY + row, 1, channel(entry & 31u), channel(entry >> 5 & 31u), channel(entry >> 10 & 31u)};
                if (!runs.empty() && runs.back().y == run.y && runs.back().x + runs.back().length == run.x && runs.back().r == run.r &&
                    runs.back().g == run.g && runs.back().b == run.b)
                    ++runs.back().length;
                else
                    runs.push_back(run);
            }
        x += static_cast<int8_t>(psx_mod_read_byte(pointer::kSmallGlyphWidths + static_cast<uint32_t>(glyph)));
    }
    return runs;
}

/* After the pointer's choice of a line: the presses that step onto SAVE STATES or choose it are taken from this frame's
 * by marking them down the frame before too, and the row is given to the host to draw. */
void pause_states() {
    if (!paused() || !states_offered()) {
        g_pause_states = false;
        return;
    }
    screen::note_context(browser::Place::pause);
    publish_font(kPlayClutX, kPlayClutY);
    const bool asking = psx_mod_read_word(rows::kPauseAsks) != 0u;
    const uint32_t cursor = psx_mod_read_word(rows::kPauseCursor);
    const uint32_t now = psx_mod_read_word(kPadNow), before = psx_mod_read_word(kPadBefore);
    const uint32_t pressed = (now ^ before) & now;
    const states::Step step = states::pause(cursor, g_pause_states, pressed, asking);
    if (step.cursor != cursor) put_word(rows::kPauseCursor, step.cursor);
    g_pause_states = step.on;
    if (const uint32_t taken = pressed & ~step.pressed; taken != 0u) psx_mod_write_word(kPadBefore, before | taken);
    if (step.open && states_open()) screen::ask_open(browser::Place::pause);
    screen::publish_pause_row(asking ? std::vector<screen::Run>{} : pause_row_runs(g_pause_states && step.cursor == states::kResume));
}

/* The tick reads the pause menu's buttons and its lines are drawn after: a press's row or the one the pointer moved onto
 * is put under the cursor here, as the conditions stand now. */
void tick_entry(CPUState *, uint32_t) {
    if (!paused() || !pointing::allowed()) {
        g_pause.asked.reset();
    } else {
        const std::optional<int32_t> moved = moved_onto(g_pause, rows::kPause);
        std::optional<int32_t> row = moved;
        if (g_pause.asked) row = g_pause.asked->row;
        g_pause.asked.reset();
        if (row) {
            const Live live(g_pause.shown_on(rows::kPause));
            const rows::Drawn *draw = rows::live_draw(live.view(), *row, Guest{});
            if (draw) write(*draw->source, draw->counter);
        }
    }
    pause_states();
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

uint32_t place_now() { return paused() ? rows::kPause : psx_mod_read_word(rows::kScreen); }

uint16_t back_on(Surface &surface, uint32_t screen) {
    const Live live(surface.shown_on(screen));
    const rows::Drawn *draw = rows::back_draw(screen, live.view(), Guest{});
    if (!draw) return 0;
    surface.asked = Asked{screen, draw->row, surface.pass};
    return draw->left;
}

/* Esc chooses the back row of where it went down, as a click on it would. Its press ends where that place does: a
 * Cross held on into play would fire. */
uint16_t hold_back(bool allowed, bool down) {
    Button &key = g_escape;
    if (g_escape_taken.exchange(false)) key.was_down = true;
    if (!down || !allowed) {
        key.held = 0;
    } else if (!key.was_down) {
        key.place = place_now();
        key.held = pause_pointing() ? back_on(g_pause, rows::kPause)
                   : rows_pointing() ? back_on(g_screens, psx_mod_read_word(rows::kScreen))
                                     : uint16_t{0};
    } else if (place_now() != key.place) {
        key.held = 0;
    }
    key.was_down = down;
    return key.held;
}

PSX_MOD_CONSTRUCTOR(register_disruptor_menu_mouse) {
    const bool drawn = psx_mod_register_function_entry_plugin("disruptor.menu_mouse.draw", kItemDraw, draw_entry) != 0;
    if (!drawn) std::fprintf(stderr, "disruptor: no menu mouse hook, the pointer stays off the front end's other screens\n");
    if (!psx_mod_register_function_entry_plugin("disruptor.menu_mouse.pause", rows::kPauseDraw, pause_draw_entry) ||
        !psx_mod_register_function_entry_plugin("disruptor.menu_mouse.tick", kTick, tick_entry))
        std::fprintf(stderr, "disruptor: no pause menu mouse hooks, the pointer stays off the pause menu\n");
    g_load_hooks = drawn && psx_mod_register_function_entry_plugin("disruptor.menu_mouse.load", kLoadHandler, load_entry) &&
                   psx_mod_register_function_entry_plugin("disruptor.menu_mouse.icon", kIconDraw, icon_entry);
    if (!g_load_hooks) std::fprintf(stderr, "disruptor: no LOAD GAME hooks, it stays the game's own without the save states\n");
}

}  // namespace

extern "C" int disruptor_menu_pointing(void) {
    return disruptor_main_menu_pointing() || pause_pointing() || rows_pointing() ? 1 : 0;
}

extern "C" uint16_t disruptor_menu_buttons(int left, int right) {
    return static_cast<uint16_t>(hold(g_left, left != 0, false) | hold(g_right, right != 0, true));
}

extern "C" uint16_t disruptor_menu_back(int allowed, int escape) { return hold_back(allowed != 0, escape != 0); }

extern "C" void disruptor_menu_back_taken(void) { g_escape_taken = true; }

extern "C" void disruptor_menu_activity(void) { g_active.store(true); }

extern "C" uint16_t disruptor_menu_sides(int left, int right) {
    if (paused() || (!disruptor_main_menu_pointing() && !rows_pointing())) return 0;
    return static_cast<uint16_t>((left != 0 ? rows::kLeft : 0u) | (right != 0 ? rows::kRight : 0u));
}

extern "C" int disruptor_front_end_shown(void) { return g_screens.any_recent() || disruptor_main_menu_shown() ? 1 : 0; }

extern "C" void disruptor_states_row_reset(void) {
    g_pause_states = false;
}
