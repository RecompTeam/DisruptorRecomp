#pragma once

#include <cstdint>
#include <string_view>

/* EXIT under CREDITS on the main menu: three words patched in game.toml give it a ninth item, which gets the word, and
 * choosing it closes the window as its close button does, where the game would open screen 9, the death screen. */
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
inline constexpr int kString = 4;
inline constexpr int kButtons = 4;
inline constexpr int kItem = 16;
inline constexpr int kReturn = 31;

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

/* The buttons the handler gets in their place: the moves stay, so the cursor and its sound are the game's. */
constexpr uint32_t kept(uint32_t pressed) { return pressed & ~kChoose; }

constexpr bool draws_exit(uint32_t returns_to, uint32_t item) { return returns_to == kItemDrawn && item == kExit; }

/* By the language of the disc laid over the game disc, as a launcher names it. The Japanese disc's menus are English. */
constexpr std::string_view word(std::string_view language) {
    return language == "French" ? "QUITTER" : language == "German" ? "BEENDEN" : "EXIT";
}

} // namespace disruptor::main_menu_exit
