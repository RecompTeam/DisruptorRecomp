#pragma once

#include "game/frame/stock_frames.h"

#include <array>
#include <cstdint>
#include <string_view>

/* At the reactor the countdown writes its seconds into the HUD's message at every tick (0x80039C38), and an open weapon
 * or psionics list writes the chosen one's name over them in every drawn frame (0x8003DFD8, 0x8003E774), so in the stock
 * game the seconds are gone while a list is open. Here the countdown keeps the line, and the name is printed after it in
 * the line's picture and drawn where the list put it by a second sprite. The draw sends its sprite at once while the
 * builder's panel waits in the table drawn next frame, so a list's name came a frame before its panel: not drawn then. */
namespace disruptor::list_message {

inline constexpr uint32_t kLevel = 0x80071518u;     /* gp+972 */
inline constexpr uint32_t kReactor = 7u;
inline constexpr uint32_t kCountdown = 0x8007143Cu; /* gp+752: ticks left, the seconds written while above 0 */
inline constexpr uint32_t kMessageX = 0x8007147Cu;  /* gp+816: 0 centres the line, a list puts it beside itself */
inline constexpr uint32_t kMessageY = 0x80071480u;  /* gp+820 */
inline constexpr uint32_t kMode = 0x8007145Cu;      /* gp+784: 2 or 3 with a list open, and the draw then blacks the line */
inline constexpr uint32_t kNextSprite = 0x800714DCu; /* gp+912: where the message's draw builds its sprite */
inline constexpr uint32_t kSpriteColour = 4u;       /* its red, green and blue bytes */
inline constexpr uint32_t kFlashTime = 665u;        /* the draw's red flash: the time just after a countdown's write */
inline constexpr uint32_t kFlashTicks = 30u, kFlashFrom = 25u;
inline constexpr uint8_t kGrey = 128u, kRed = 255u;

/* The printer (0x80044A10) copies a glyph of 8 by 11 bytes from the font for each letter and moves on by its width, 4
 * for a space, into a picture 256 wide that the draw clears (0x8004280C) and loads at (512, 181) whole. */
inline constexpr uint32_t kPicture = 0x800776D8u; /* holds where the picture is, a buffer the menus print 3584 bytes of */
inline constexpr uint32_t kFont = 0x80071398u;    /* holds where the glyphs are */
inline constexpr uint32_t kGlyphWidths = 0x80056E40u;
inline constexpr uint32_t kPictureWide = 256u, kPictureBytes = 2816u;
inline constexpr uint32_t kGlyphWide = 8u, kGlyphRows = 11u, kGlyphBytes = 88u, kSpaceWide = 4u;
inline constexpr uint32_t kNameGap = 8u;
inline constexpr uint32_t kNameSprite = kPictureBytes; /* past the line in that buffer: every user prints before it reads */
inline constexpr uint32_t kSpriteWords = 4u;
inline constexpr uint32_t kCentre = 160u; /* x 0 centres a line on this column in a list's mode (0x80042954) */
inline constexpr uint32_t kListUnknown = 0xFFFFFFFFu; /* after a load: the table loaded may hold a panel */

using Text = std::array<uint8_t, stock_frames::kMessageBytes>;

struct Kept {
    stock_frames::Message message;
    uint32_t x = 0, y = 0;
    bool held = false;      /* from the builder's mode read to its end */
    bool shown = false;     /* written back this frame, to the message's draw */
    uint32_t sprite = 0;    /* the sprite that draw builds, 0 when it draws nothing */
    Text name{};            /* the list's name this frame, empty when there is none */
    uint32_t name_x = 0, name_y = 0;
    uint32_t name_at = 0, name_wide = 0; /* where it was printed in the picture, 0 wide when it was not */
    uint32_t list = 0;   /* the list the builder's last pass drew, 0 for none */
    bool opening = false; /* that pass opened it, its panel not yet drawn */
};

/* The glyph the printer takes for a letter that is not a space. */
inline uint32_t glyph(uint8_t letter) { return letter >= 97u ? letter - 97u : letter >= 65u ? letter - 39u : letter + 4u; }

template <class Memory>
bool counting(const Memory &memory) {
    return memory.byte(kLevel) == kReactor && static_cast<int32_t>(memory.word(kCountdown)) > 0;
}

/* The HUD builder's read of the mode (0x8003D840), before either list writes. */
template <class Memory>
void builder_begins(const Memory &memory, Kept &kept) {
    kept.held = counting(memory);
    kept.shown = false;
    kept.name[0] = 0u;
    if (!kept.held) return;
    kept.message = stock_frames::read_message(memory);
    kept.x = memory.word(kMessageX);
    kept.y = memory.word(kMessageY);
}

/* Where every path of the HUD builder ends (0x8003E8C8), before the message is drawn (0x80043C9C). `forced` is the list a
 * key shows, its mode given to the builder's register alone (weapon_list.cpp), 0 for none. */
template <class Memory>
void builder_ends(Memory &memory, Kept &kept, uint32_t forced) {
    const uint32_t mode = memory.byte(kMode);
    const uint32_t list = mode == 2u || mode == 3u ? mode : forced == 2u || forced == 3u ? forced : 0u;
    kept.opening = list != 0u && list != kept.list && kept.list != kListUnknown;
    kept.list = list;
    if (!kept.held) return;
    kept.held = false;
    kept.shown = true;
    const stock_frames::Message written = stock_frames::read_message(memory);
    if (list != 0u && !kept.opening && written.text != kept.message.text) {
        kept.name = written.text;
        kept.name.back() = 0u;
        kept.name_x = memory.word(kMessageX);
        kept.name_y = memory.word(kMessageY);
    }
    stock_frames::write_message(memory, kept.message);
    memory.write(kMessageX, kept.x);
    memory.write(kMessageY, kept.y);
}

/* The message draw's entry (0x800427D0): it draws only a message with time left, and sends it only then. */
template <class Memory>
void draw_begins(const Memory &memory, Kept &kept) {
    const bool drawn = (kept.opening || kept.shown) && memory.word(stock_frames::kMessageTime) != 0u;
    kept.sprite = drawn ? memory.word(kNextSprite) : 0u;
}

/* Where the draw loads the line's picture (LoadImage 0x8004D348 from 0x80042850), the countdown printed `printed` wide:
 * `words`, the list's name in the player's language, printed after it as the printer prints, when the whole fits. */
template <class Memory>
void print_name(Memory &memory, Kept &kept, uint32_t printed, std::string_view words) {
    kept.name_wide = 0u;
    if (kept.sprite == 0u || words.empty() || printed > kPictureWide) return;
    const uint32_t at = printed + kNameGap;
    int32_t x = static_cast<int32_t>(at);
    for (const char letter : words) {
        if (letter == ' ') {
            x += static_cast<int32_t>(kSpaceWide);
            continue;
        }
        if (x < 0 || x + static_cast<int32_t>(kGlyphWide) > static_cast<int32_t>(kPictureWide)) return;
        x += static_cast<int8_t>(memory.byte(kGlyphWidths + glyph(static_cast<uint8_t>(letter))));
    }
    if (x <= static_cast<int32_t>(at) || x > static_cast<int32_t>(kPictureWide)) return;
    const uint32_t picture = memory.word(kPicture), font = memory.word(kFont);
    uint32_t column = at;
    for (const char letter : words) {
        if (letter == ' ') {
            column += kSpaceWide;
            continue;
        }
        const uint32_t index = glyph(static_cast<uint8_t>(letter));
        for (uint32_t row = 0; row < kGlyphRows; ++row)
            for (uint32_t across = 0; across < kGlyphWide; ++across)
                memory.write_byte(picture + row * kPictureWide + column + across,
                                  static_cast<uint8_t>(memory.byte(font + index * kGlyphBytes + row * kGlyphWide + across)));
        column += static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(memory.byte(kGlyphWidths + index))));
    }
    kept.name_at = at;
    kept.name_wide = static_cast<uint32_t>(x) - at;
}

/* The name's sprite at `extra`, `wide` wide: the line's with the name's part of the picture, its place and `colour`,
 * sent after it. */
template <class Memory>
void add_name(Memory &memory, const Kept &kept, uint32_t wide, uint32_t sprite, uint32_t extra, uint32_t colour) {
    const uint32_t tag = memory.word(sprite);
    const uint32_t x = kept.name_x != 0u ? kept.name_x : kCentre - (wide >> 1);
    memory.write(extra, (tag & 0x00FFFFFFu) | kSpriteWords << 24);
    memory.write(extra + 4u, (memory.word(sprite + 4u) & 0xFF000000u) | colour);
    memory.write(extra + 8u, (kept.name_y & 0xFFFFu) << 16 | (x & 0xFFFFu));
    memory.write(extra + 12u, (memory.word(sprite + 12u) & 0xFFFFFF00u) | kept.name_at);
    memory.write(extra + 16u, (memory.word(sprite + 16u) & 0xFFFF0000u) | wide);
    memory.write(sprite, (tag & 0xFF000000u) | (extra & 0x00FFFFFFu));
}

/* Where the draw sends its own list (DrawOTag 0x8004D684 from 0x800429B4), the sprite built: the colour the draw gives
 * the line in play (0x8004286C..0x800428E0) in place of the black of a list's mode (0x800428E4..0x80042904), and the
 * name's sprite after it, black as the game draws a list's name, grey for a list a key shows, drawn in play. */
template <class Memory>
void draw_ends(Memory &memory, Kept &kept) {
    const uint32_t sprite = kept.sprite, wide = kept.name_wide;
    const bool line = kept.shown, opening = kept.opening;
    kept.sprite = 0u;
    kept.shown = kept.opening = false;
    kept.name[0] = 0u;
    kept.name_wide = 0u;
    if (sprite == 0u) return;
    if (opening && !line) {
        memory.write(sprite, memory.word(sprite) & 0x00FFFFFFu);
        return;
    }
    const uint32_t extra = memory.word(kPicture) + kNameSprite;
    const bool named = wide != 0u && (extra & 3u) == 0u;
    const uint32_t mode = memory.byte(kMode);
    if (mode != 2u && mode != 3u) {
        if (named) add_name(memory, kept, wide, sprite, extra, kGrey << 16 | kGrey << 8 | kGrey);
        return;
    }
    for (uint32_t at = 0; at < 3u; ++at)
        if (memory.byte(sprite + kSpriteColour + at) != 0u) return;
    const int32_t left = static_cast<int32_t>(memory.word(kCountdown));
    const bool red = memory.byte(kLevel) == kReactor && memory.word(stock_frames::kMessageTime) == kFlashTime &&
                     left % static_cast<int32_t>(kFlashTicks) >= static_cast<int32_t>(kFlashFrom);
    if (named) add_name(memory, kept, wide, sprite, extra, 0u);
    memory.write_byte(sprite + kSpriteColour, red ? kRed : kGrey);
    memory.write_byte(sprite + kSpriteColour + 1u, red ? 0u : kGrey);
    memory.write_byte(sprite + kSpriteColour + 2u, red ? 0u : kGrey);
}

/* A tick's entry: the frame's draw is over, whatever it left is the game's. */
inline void frame_over(Kept &kept) {
    kept.held = kept.shown = kept.opening = false;
    kept.sprite = 0u;
    kept.name[0] = 0u;
    kept.name_wide = 0u;
}

/* No state may be written while the line is the hooks' and not yet the game's. */
inline bool saving_deferred(const Kept &kept) { return kept.held || kept.shown || kept.opening || kept.sprite != 0u; }

inline void forget(Kept &kept) {
    kept = Kept{};
    kept.list = kListUnknown;
}

} // namespace disruptor::list_message
