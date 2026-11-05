#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

/* EXIT under CREDITS on the main menu: three words patched in game.toml give it a ninth item, which gets the word, and
 * choosing it closes the window as its close button does, where the game would open screen 9, the death screen.
 * MEMORY CARD OPTIONS and CONTROLLER OPTIONS are not drawn and the cursor steps over them: the launcher has the keys. */
namespace disruptor::main_menu_exit {

inline constexpr uint32_t kHandler = 0x8001C044u;   /* a0: the buttons pressed this frame */
inline constexpr uint32_t kItemDraw = 0x8001A46Cu;  /* a0: the string */
inline constexpr uint32_t kItemDrawn = 0x8001C170u; /* the return of the main menu's call, with the item in s0 */
inline constexpr uint32_t kCursor = 0x8007176Cu;    /* gp+0x620 */
inline constexpr uint32_t kUp = 0x1000u;
inline constexpr uint32_t kDown = 0x4000u;
inline constexpr uint32_t kChoose = 0x09E0u; /* Start, Select, Square, Cross, Circle */
inline constexpr uint32_t kItems = 9u;
inline constexpr uint32_t kExit = 8u;
inline constexpr uint32_t kFirstHidden = 4u; /* MEMORY CARD OPTIONS */
inline constexpr uint32_t kLastHidden = 5u;  /* CONTROLLER OPTIONS */
inline constexpr uint32_t kRowStep = 18u;    /* 0x8001C170 */
/* The button layout, 0 the launcher's. A save carries it, and a state taken after a change under CONTROLLER OPTIONS
 * has the change mark set, which keeps a loaded save's layout off: both are put back at the menu. */
inline constexpr uint32_t kLayout = 0x80071730u;        /* gp+1508 */
inline constexpr uint32_t kLayoutChanged = 0x8007153Cu; /* gp+1008, a byte */
inline constexpr uint32_t kApplyLayout = 0x80012344u;   /* a0: the layout, a leaf */
inline constexpr uint32_t kApplyReturn = 0x8001DBC8u;   /* LOAD GAME's own call returns here */
inline constexpr int kString = 4;
inline constexpr int kButtons = 4;
inline constexpr int kY = 6;
inline constexpr int kItem = 16;
inline constexpr int kReturn = 31;

constexpr bool hidden(uint32_t item) { return item >= kFirstHidden && item <= kLastHidden; }

/* Off a hidden item the way the game's own move came onto it: Down reaches the first, Up the last. A cursor a save
 * state left there goes the same way. */
constexpr uint32_t placed(uint32_t cursor) {
    if (cursor == kFirstHidden) return kLastHidden + 1u;
    if (cursor == kLastHidden) return kFirstHidden - 1u;
    return cursor;
}

/* How far up an item is drawn, by the rows of the hidden items above it. */
constexpr uint32_t raised(uint32_t item) { return item > kLastHidden ? (kLastHidden - kFirstHidden + 1u) * kRowStep : 0u; }

/* The cursor the handler chooses with: Up moves it first, then Down, each wrapping, as 0x8001C184..0x8001C1E0 do. */
constexpr uint32_t chosen(uint32_t cursor, uint32_t pressed) {
    if (pressed & kUp) {
        cursor -= 1u;
        if (static_cast<int32_t>(cursor) < 0) cursor = kItems - 1u;
    }
    if (pressed & kDown) {
        cursor += 1u;
        if (static_cast<int32_t>(cursor) >= static_cast<int32_t>(kItems)) cursor = 0u;
    }
    return cursor;
}

constexpr bool exits(uint32_t cursor, uint32_t pressed) { return (pressed & kChoose) != 0u && chosen(cursor, pressed) == kExit; }

struct Handed {
    uint32_t cursor, pressed;
};

/* What the handler gets: the cursor off a hidden item, and a choice in the frame a move lands on one goes past it. */
constexpr Handed handed(uint32_t cursor, uint32_t pressed) {
    cursor = placed(cursor);
    const uint32_t to = chosen(cursor, pressed);
    if ((pressed & kChoose) != 0u && hidden(to)) return {placed(to), pressed & ~(kUp | kDown)};
    return {cursor, pressed};
}

/* The buttons the handler gets in their place: the moves stay, so the cursor and its sound are the game's. */
constexpr uint32_t kept(uint32_t pressed) { return pressed & ~kChoose; }

constexpr bool draws_exit(uint32_t returns_to, uint32_t item) { return returns_to == kItemDrawn && item == kExit; }

/* By the language of the disc laid over the game disc, as a launcher names it. The Japanese disc's menus are English. */
constexpr std::string_view word(std::string_view language) {
    return language == "French" ? "QUITTER" : language == "German" ? "BEENDEN" : "EXIT";
}

inline constexpr size_t kLongestWord = std::max({word("French").size(), word("German").size(), word("").size()});

} // namespace disruptor::main_menu_exit

extern "C" {
/* The main menu ran a moment ago and the pointer may choose on it: the mouse is let go and shown there. */
int disruptor_main_menu_pointing(void);
/* A left press: the shown item under the pointer now, chosen at the menu's next entry, -1 for none, which presses
 * nothing. */
int disruptor_main_menu_click(void);
}
