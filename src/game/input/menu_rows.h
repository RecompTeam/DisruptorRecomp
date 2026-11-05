#pragma once

#include "game/input/menu_pointer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

/* The mouse on the front end's other screens: the item draw's calls that draw a row, what pointing at it writes and
 * what a press over each draw holds. Every screen draws its rows before it reads the pad, which then sees those writes. */
namespace disruptor::menu_rows {

inline constexpr uint32_t kScreen = 0x800715B0u;      /* gp+1124 */
inline constexpr uint32_t kCursor = 0x80071488u;      /* gp+828, the cursor of every screen here */
inline constexpr uint32_t kSlotSide = 0x80071688u;    /* gp+1340: 1 on NEW GAME's RETURN TO MAIN MENU, 0 on a slot or the difficulty */
inline constexpr uint32_t kLetter = 0x80071684u;      /* gp+1336: the name's letter, 8 its DONE */
inline constexpr uint32_t kOverwrite = 0x80071554u;   /* gp+1032: nonzero while NEW GAME asks to overwrite a slot */
inline constexpr uint32_t kLoadMessage = 0x8007149Cu; /* gp+848: nonzero while LOAD GAME shows a message for its slots */
inline constexpr uint32_t kFormatState = 0x80071414u; /* gp+712: 0 while the format screen asks */
inline constexpr uint32_t kSlots = 0x800714C0u;       /* a byte a save slot, 1 when it holds a save */
inline constexpr uint32_t kMenuText = 0x800776DCu;    /* where the menu text lies, its strings by the offsets at gp+760 */
inline constexpr uint32_t kMenuOffsets = 0x80071444u;

/* The pad's own bits, as a press is put in. */
inline constexpr uint16_t kUp = 1u << 4, kRight = 1u << 5, kDown = 1u << 6, kLeft = 1u << 7, kCross = 1u << 14;

/* A written value that is the loop's counter s0, the item the loop draws now. */
inline constexpr int32_t kCounter = INT32_MIN;

inline constexpr uint32_t kNewGame = 1u, kLoadGame = 2u, kPractice = 3u, kPassword = 4u, kOptions = 7u, kDeath = 9u,
                          kMissionDone = 10u, kFormat = 12u;
/* The pause menu in play, no screen of the front end: its lines are 0x800429DC's, its buttons read by the tick. */
inline constexpr uint32_t kPause = 0xFFFF0000u;
inline constexpr uint32_t kPauseDraw = 0x800429DCu; /* (list, string, x, y, highlight, line): x 0 centres */
inline constexpr uint32_t kPauseState = 0x8007145Cu;  /* gp+784, a byte, 4 while paused */
inline constexpr uint32_t kPauseCursor = 0x800716C0u; /* gp+1396 */
inline constexpr uint32_t kPauseAsks = 0x800712F4u;   /* gp+424: nonzero while ABORT asks YES or NO */
inline constexpr uint8_t kPaused = 4u;

struct Write {
    uint32_t address = 0;
    int32_t value = 0;
};

/* Holds while the word, or byte, lies in [low, high]. No address holds always. */
struct When {
    uint32_t address = 0;
    uint32_t low = 0, high = 0;
    bool byte = false;
    bool plus_counter = false;
};

struct Row {
    uint32_t returns = 0;
    uint32_t screen = 0;
    int32_t row = 0;      /* a draw's box joins the others of its row */
    bool looped = false;  /* drawn an item at a time by a loop: the row adds the counter */
    uint16_t left = 0, right = 0;
    uint32_t string = 0;  /* nonzero: only while it draws the menu text's string at this offset */
    std::array<Write, 2> writes{};
    std::array<When, 2> when{};
};

inline constexpr std::array kRows = {
    /* GENERAL OPTIONS: two volumes and CAMERA SWAY, a title and a value each */
    Row{.returns = 0x8001F248u, .screen = kOptions, .row = 0, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001F268u, .screen = kOptions, .row = 0, .left = kRight, .right = kLeft, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001F2E4u, .screen = kOptions, .row = 1, .writes = {{{kCursor, 1}}}},
    Row{.returns = 0x8001F308u, .screen = kOptions, .row = 1, .left = kRight, .right = kLeft, .writes = {{{kCursor, 1}}}},
    Row{.returns = 0x8001F328u, .screen = kOptions, .row = 2, .writes = {{{kCursor, 2}}}},
    Row{.returns = 0x8001F368u, .screen = kOptions, .row = 2, .left = kRight, .right = kLeft, .writes = {{{kCursor, 2}}}},
    Row{.returns = 0x8001F394u, .screen = kOptions, .row = 3, .left = kCross, .writes = {{{kCursor, 3}}}},
    /* YOU DIED: CONTINUE FROM MID MISSION and its count, RESTART MISSION, REVIEW MOVIE or RESTART in its place */
    Row{.returns = 0x8001AD58u, .screen = kDeath, .row = 0, .left = kCross, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001AD70u, .screen = kDeath, .row = 0, .left = kCross, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001ADE0u, .screen = kDeath, .row = 1, .left = kCross, .writes = {{{kCursor, 1}}}},
    Row{.returns = 0x8001AE08u, .screen = kDeath, .row = 1, .left = kCross, .string = 180u, .writes = {{{kCursor, 1}}}},
    Row{.returns = 0x8001AE08u, .screen = kDeath, .row = 2, .left = kCross, .string = 184u, .writes = {{{kCursor, 2}}}},
    Row{.returns = 0x8001AE34u, .screen = kDeath, .row = 3, .left = kCross, .writes = {{{kCursor, 3}}}},
    /* MISSION COMPLETED: OVERWRITE M%d WITH M%d and its answer in one string, CONTINUE, alone after the last mission */
    Row{.returns = 0x8001B8DCu, .screen = kMissionDone, .row = 0, .left = kCross, .right = kLeft},
    Row{.returns = 0x8001BEA4u, .screen = kMissionDone, .row = 1, .left = kCross},
    Row{.returns = 0x8001BEE4u, .screen = kMissionDone, .row = 1, .left = kCross, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001BF0Cu, .screen = kMissionDone, .row = 2, .left = kCross, .writes = {{{kCursor, 1}}}},
    Row{.returns = 0x8001BF58u, .screen = kMissionDone, .row = 2, .left = kCross, .writes = {{{kCursor, 1}}}},
    /* PRACTICE MODE: five missions */
    Row{.returns = 0x8001C2D8u, .screen = kPractice, .row = 0, .looped = true, .left = kCross, .writes = {{{kCursor, kCounter}}}},
    Row{.returns = 0x8001C37Cu, .screen = kPractice, .row = 5, .left = kCross, .writes = {{{kCursor, 5}}}},
    /* LOAD GAME: a saved game's name, then its mission, which Left raises */
    Row{.returns = 0x8001D6F8u, .screen = kLoadGame, .row = 4, .left = kCross, .writes = {{{kCursor, 4}}}},
    Row{.returns = 0x8001D7C8u, .screen = kLoadGame, .row = 0, .looped = true, .left = kCross,
        .writes = {{{kCursor, kCounter}}}, .when = {{{kLoadMessage, 0, 0}}}},
    Row{.returns = 0x8001D818u, .screen = kLoadGame, .row = 0, .looped = true, .left = kLeft, .right = kRight,
        .writes = {{{kCursor, kCounter}}}, .when = {{{kLoadMessage, 0, 0}, {kSlots, 1, 1, true, true}}}},
    /* NEW GAME: RETURN TO MAIN MENU by its phase, the difficulty, the slots, overwrite or not, the name */
    Row{.returns = 0x8001CA50u, .screen = kNewGame, .row = 10, .left = kCross, .writes = {{{kSlotSide, 1}}},
        .when = {{{kCursor, 0, 3}, {kOverwrite, 0, 0}}}},
    /* YES to overwrite leaves the question's word at 2 (0x8001CEA8): the difficulty no longer asks it. */
    Row{.returns = 0x8001CA50u, .screen = kNewGame, .row = 10, .left = kCross, .writes = {{{kSlotSide, 1}}},
        .when = {{{kCursor, 4, 4}}}},
    Row{.returns = 0x8001CA50u, .screen = kNewGame, .row = 10, .left = kCross, .writes = {{{kCursor, 7}}},
        .when = {{{kCursor, 5, 7}}}},
    Row{.returns = 0x8001CA88u, .screen = kNewGame, .row = 11, .left = kCross, .writes = {{{kSlotSide, 0}}},
        .when = {{{kCursor, 4, 4}}}},
    Row{.returns = 0x8001CAD0u, .screen = kNewGame, .row = 11, .left = kRight, .right = kLeft, .writes = {{{kSlotSide, 0}}},
        .when = {{{kCursor, 4, 4}}}},
    Row{.returns = 0x8001CD64u, .screen = kNewGame, .row = 0, .looped = true, .left = kCross,
        .writes = {{{kCursor, kCounter}, {kSlotSide, 0}}}, .when = {{{kCursor, 0, 3}, {kOverwrite, 0, 0}}}},
    Row{.returns = 0x8001CDC4u, .screen = kNewGame, .row = 0, .looped = true, .left = kCross,
        .writes = {{{kCursor, kCounter}, {kSlotSide, 0}}}, .when = {{{kCursor, 0, 3}, {kOverwrite, 0, 0}}}},
    Row{.returns = 0x8001CE14u, .screen = kNewGame, .row = 12, .left = kCross, .right = kLeft,
        .when = {{{kCursor, 0, 3}, {kOverwrite, 1, 0xFFFFFFFFu}}}},
    Row{.returns = 0x8001CE5Cu, .screen = kNewGame, .row = 12, .left = kRight, .right = kLeft,
        .when = {{{kCursor, 0, 3}, {kOverwrite, 1, 0xFFFFFFFFu}}}},
    Row{.returns = 0x8001D01Cu, .screen = kNewGame, .row = 13, .writes = {{{kCursor, 5}}}, .when = {{{kCursor, 5, 7}}}},
    Row{.returns = 0x8001D06Cu, .screen = kNewGame, .row = 20, .looped = true, .left = kUp, .right = kDown,
        .writes = {{{kCursor, 5}, {kLetter, kCounter}}}, .when = {{{kCursor, 5, 7}}}},
    Row{.returns = 0x8001D10Cu, .screen = kNewGame, .row = 30, .left = kCross, .writes = {{{kCursor, 5}, {kLetter, 8}}},
        .when = {{{kCursor, 5, 7}}}},
    Row{.returns = 0x8001D12Cu, .screen = kNewGame, .row = 31, .left = kCross, .writes = {{{kCursor, 6}}},
        .when = {{{kCursor, 5, 7}}}},
    /* PASSWORD: the field, typed in with the face buttons */
    Row{.returns = 0x8001E810u, .screen = kPassword, .row = 0, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001E92Cu, .screen = kPassword, .row = 0, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001E9E8u, .screen = kPassword, .row = 0, .writes = {{{kCursor, 0}}}},
    Row{.returns = 0x8001E830u, .screen = kPassword, .row = 1, .left = kCross, .writes = {{{kCursor, 1}}}},
    Row{.returns = 0x8001E94Cu, .screen = kPassword, .row = 1, .left = kCross, .writes = {{{kCursor, 1}}}},
    /* The pause menu: RESUME MISSION, ABORT MISSION, the two volumes, which Left and Right step; ABORT's YES and NO */
    Row{.returns = 0x80042DE0u, .screen = kPause, .row = 0, .left = kCross, .writes = {{{kPauseCursor, 0}}}, .when = {{{kPauseAsks, 0, 0}}}},
    Row{.returns = 0x80042E1Cu, .screen = kPause, .row = 1, .left = kCross, .writes = {{{kPauseCursor, 1}}}, .when = {{{kPauseAsks, 0, 0}}}},
    Row{.returns = 0x80042EB0u, .screen = kPause, .row = 2, .left = kRight, .right = kLeft, .writes = {{{kPauseCursor, 2}}},
        .when = {{{kPauseAsks, 0, 0}}}},
    Row{.returns = 0x80042F48u, .screen = kPause, .row = 3, .left = kRight, .right = kLeft, .writes = {{{kPauseCursor, 3}}},
        .when = {{{kPauseAsks, 0, 0}}}},
    Row{.returns = 0x80042C70u, .screen = kPause, .row = 5, .left = kCross, .writes = {{{kPauseCursor, 0}}},
        .when = {{{kPauseAsks, 1, 0xFFFFFFFFu}}}},
    Row{.returns = 0x80042CA0u, .screen = kPause, .row = 6, .left = kCross, .writes = {{{kPauseCursor, 1}}},
        .when = {{{kPauseAsks, 1, 0xFFFFFFFFu}}}},
    /* The format screen's question and its answer */
    Row{.returns = 0x8001E228u, .screen = kFormat, .row = 0, .left = kCross, .right = kLeft, .when = {{{kFormatState, 0, 0}}}},
    Row{.returns = 0x8001E264u, .screen = kFormat, .row = 0, .left = kRight, .right = kLeft, .when = {{{kFormatState, 0, 0}}}},
};

struct Screen {
    uint32_t screen = 0, first = 0, end = 0;
};

/* The screens' functions: a draw from one of them while its screen runs keeps the pointer there, rows or none. */
inline constexpr std::array kScreens = {
    Screen{kDeath, 0x8001A924u, 0x8001B034u},     Screen{kMissionDone, 0x8001B034u, 0x8001C044u},
    Screen{kPractice, 0x8001C230u, 0x8001C484u},  Screen{kNewGame, 0x8001C484u, 0x8001D3D8u},
    Screen{kLoadGame, 0x8001D3D8u, 0x8001DC70u},  Screen{kFormat, 0x8001E19Cu, 0x8001E468u},
    Screen{kPassword, 0x8001E7B0u, 0x8001EDB4u},  Screen{kOptions, 0x8001F1BCu, 0x8001F59Cu},
};

/* The pause menu's lines are drawn by its own function 0x80042BB8. */
constexpr bool pause_draws(uint32_t returns) { return returns >= 0x80042BB8u && returns < 0x80043000u; }

constexpr bool draws_for(uint32_t screen, uint32_t returns) {
    for (const Screen &known : kScreens)
        if (known.screen == screen && returns >= known.first && returns < known.end) return true;
    return false;
}

template <class Guest>
bool holds(const When &when, uint32_t counter, const Guest &guest) {
    if (when.address == 0u) return true;
    const uint32_t address = when.address + (when.plus_counter ? counter : 0u);
    const uint32_t value = when.byte ? guest.read_byte(address) : guest.read_word(address);
    return value >= when.low && value <= when.high;
}

/* The row a call draws, the first whose conditions hold. `string` is the string it draws. */
template <class Guest>
const Row *match(uint32_t returns, uint32_t screen, uint32_t counter, uint32_t string, const Guest &guest) {
    for (const Row &row : kRows) {
        if (row.returns != returns || row.screen != screen) continue;
        if (row.string != 0u &&
            string != guest.read_word(kMenuText) + guest.read_word(guest.read_word(kMenuOffsets) + row.string))
            continue;
        if (holds(row.when[0], counter, guest) && holds(row.when[1], counter, guest)) return &row;
    }
    return nullptr;
}

constexpr int32_t row_of(const Row &row, uint32_t counter) {
    return row.row + (row.looped ? static_cast<int32_t>(counter) : 0);
}

constexpr uint32_t value_of(const Write &write, uint32_t counter) {
    return write.value == kCounter ? counter : static_cast<uint32_t>(write.value);
}

struct Drawn {
    uint32_t returns = 0;
    int32_t row = 0;
    menu_pointer::Box box;
    uint16_t left = 0, right = 0;
    const Row *source = nullptr;
    uint32_t counter = 0;
    int64_t at = 0; /* when it was drawn, in the host clock's ticks */
};

/* The row of the draw nearest the point within reach, the first drawn of equals. */
inline std::optional<int32_t> row_under(std::span<const Drawn> drawn, const menu_pointer::Point &point) {
    std::optional<int32_t> nearest;
    double best = 0.0;
    for (const Drawn &draw : drawn) {
        const std::optional<double> near = menu_pointer::reach(draw.box, point);
        if (near && (!nearest || *near < best)) {
            nearest = draw.row;
            best = *near;
        }
    }
    return nearest;
}

/* What a press of the left or right button there holds: the row's draw nearest the point, the first of equals. */
inline uint16_t press_at(std::span<const Drawn> drawn, int32_t row, const menu_pointer::Point &point, bool right) {
    const Drawn *chosen = nullptr;
    double best = 0.0;
    for (const Drawn &draw : drawn) {
        if (draw.row != row) continue;
        const double away = menu_pointer::distance(draw.box, point);
        if (!chosen || away < best) {
            chosen = &draw;
            best = away;
        }
    }
    return chosen ? (right ? chosen->right : chosen->left) : uint16_t{0};
}

/* Whether a row drawn a pass ago would still be drawn as one now: a press on a row the game has since left
 * would hold its buttons for whatever stands there now. */
template <class Guest>
const Drawn *live_draw(std::span<const Drawn> drawn, int32_t row, const Guest &guest) {
    for (const Drawn &draw : drawn)
        if (draw.row == row && draw.source && holds(draw.source->when[0], draw.counter, guest) &&
            holds(draw.source->when[1], draw.counter, guest))
            return &draw;
    return nullptr;
}

template <class Guest>
bool still_drawn(std::span<const Drawn> drawn, int32_t row, const Guest &guest) {
    return live_draw(drawn, row, guest) != nullptr;
}

}  // namespace disruptor::menu_rows
