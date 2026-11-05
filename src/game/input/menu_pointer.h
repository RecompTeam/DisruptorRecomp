#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

/* The mouse on the game's menus: where an item of the front end lies in its 320 by 240 frame, and where the pointer
 * falls in that frame. The item draw 0x8001A46C sets a string in the menu font 0x80044BDC, then draws it as one sprite
 * 14 rows high at its x, or centred on 160 when x is 0. */
namespace disruptor::menu_pointer {

inline constexpr int kFrameWide = 320;
inline constexpr int kFrameHigh = 240;
inline constexpr int kRowHigh = 14;
inline constexpr int kSpace = 6;
inline constexpr uint32_t kGlyphWidths = 0x80056EC4u; /* a signed byte a glyph */

/* The menu font's glyph of a character, as 0x80044C14..0x80044C2C picks it: small letters are signs, then capitals,
 * then everything else from 36. */
constexpr int glyph(char letter) {
    const int code = static_cast<unsigned char>(letter);
    if (code - 'a' >= 0) return code - 'a';
    if (code - 'A' >= 0) return code - 'A' + 10;
    return code - 'A' + 10 + 43;
}

template <class Width>
constexpr int text_width(std::string_view text, Width width_of) {
    int width = 0;
    for (const char letter : text) width += letter == ' ' ? kSpace : width_of(glyph(letter));
    return width;
}

/* A string of the guest's memory in the menu font, read a byte at a time: as wide as the item draw's sprite of it. */
template <class ReadByte>
int guest_text_width(uint32_t address, ReadByte read_byte) {
    std::array<char, 40> text{};
    size_t length = 0;
    for (; length < text.size(); ++length) {
        text[length] = static_cast<char>(read_byte(address + static_cast<uint32_t>(length)));
        if (text[length] == '\0') break;
    }
    return text_width({text.data(), length}, [&](int glyph) {
        if (glyph < 0 || glyph >= 64) return 0;
        return static_cast<int>(static_cast<int8_t>(read_byte(kGlyphWidths + static_cast<uint32_t>(glyph))));
    });
}

/* The pause menu's font, as 0x80044A10 picks it: small letters, then capitals, then the rest from the character's
 * code. A space is 4, a line 11 rows. */
inline constexpr uint32_t kSmallGlyphWidths = 0x80056E40u;
inline constexpr int kSmallSpace = 4, kSmallRowHigh = 11;

constexpr int small_glyph(char letter) {
    const int code = static_cast<unsigned char>(letter);
    if (code >= 'a') return code - 'a';
    if (code >= 'A') return code - 'A' + 26;
    return code + 4;
}

template <class ReadByte>
int small_text_width(std::string_view text, ReadByte read_byte) {
    int width = 0;
    for (const char letter : text) {
        const int shape = small_glyph(letter);
        width += letter == ' ' ? kSmallSpace
                 : shape >= 0 && shape < 128
                     ? static_cast<int>(static_cast<int8_t>(read_byte(kSmallGlyphWidths + static_cast<uint32_t>(shape))))
                     : 0;
    }
    return width;
}

struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

constexpr Box centred(int width, int y) { return {kFrameWide / 2 - width / 2, y, width, kRowHigh}; }

/* Where the item draw puts a sprite that wide, as 0x8001A4FC..0x8001A528 do: x 0 centres it, a negative x is its right edge. */
constexpr Box placed(int32_t x, int32_t y, int width) {
    if (x == 0) return centred(width, y);
    return {x < 0 ? -x - width : x, y, width, kRowHigh};
}

struct Point {
    double x = 0.0, y = 0.0;
};

/* Where a point of the window's drawable falls in the frame. The picture is letterboxed at num:den as the renderer
 * does it, and wider than 4:3 the front end's text is squashed about the middle by 4:3 over the aspect. */
inline std::optional<Point> frame_point(double x, double y, int width, int height, int num, int den) {
    if (width <= 0 || height <= 0 || num <= 0 || den <= 0) return std::nullopt;
    int shown_w = width, shown_h = width * den / num;
    if (shown_h > height) {
        shown_h = height;
        shown_w = height * num / den;
    }
    if (shown_w <= 0 || shown_h <= 0) return std::nullopt;
    const int left = (width - shown_w) / 2, top = (height - shown_h) / 2;
    const double squash = num * 3 > den * 4 ? 4.0 * den / (3.0 * num) : 1.0;
    const double across = (x - left) / shown_w - 0.5;
    return Point{kFrameWide / 2 + across * kFrameWide / squash, (y - top) / shown_h * kFrameHigh};
}

/* Where a point of the frame falls in the window's drawable, the inverse of frame_point. */
inline std::optional<Point> window_point(double x, double y, int width, int height, int num, int den) {
    if (width <= 0 || height <= 0 || num <= 0 || den <= 0) return std::nullopt;
    int shown_w = width, shown_h = width * den / num;
    if (shown_h > height) {
        shown_h = height;
        shown_w = height * num / den;
    }
    if (shown_w <= 0 || shown_h <= 0) return std::nullopt;
    const int left = (width - shown_w) / 2, top = (height - shown_h) / 2;
    const double squash = num * 3 > den * 4 ? 4.0 * den / (3.0 * num) : 1.0;
    return Point{left + (0.5 + (x - kFrameWide / 2) * squash / kFrameWide) * shown_w, top + y / kFrameHigh * shown_h};
}

/* A point this near an item's letters still points at it. Rows stand 18 or more apart: the nearer one wins. */
inline constexpr double kReachAcross = 24.0, kReachDown = 12.0;

/* How far a point lies outside a box, 0 inside. A box not drawn is never near. */
inline double distance(const Box& box, const Point& point) {
    if (box.w <= 0) return INFINITY;
    const double across = std::max({box.x - point.x, 0.0, point.x - (box.x + box.w)});
    const double down = std::max({box.y - point.y, 0.0, point.y - (box.y + box.h)});
    return std::hypot(across, down);
}

/* The distance while the point is within reach of the box, none past it. */
inline std::optional<double> reach(const Box& box, const Point& point) {
    if (box.w <= 0) return std::nullopt;
    const bool across = point.x >= box.x - kReachAcross && point.x <= box.x + box.w + kReachAcross;
    const bool down = point.y >= box.y - kReachDown && point.y <= box.y + box.h + kReachDown;
    return across && down ? std::optional<double>(distance(box, point)) : std::nullopt;
}

/* The pointer's arrow, a character a texel, its tip the top left one: the shape of the system's own arrow, outlined in
 * the menu letters' shadow, its face darkening from a to j row by row as their faces do. */
inline constexpr std::array<std::string_view, 17> kArrow = {{
    "@",
    "@@",
    "@a@",
    "@bb@",
    "@bbb@",
    "@cccc@",
    "@ddddd@",
    "@dddddd@",
    "@eeeeeee@",
    "@ffffffff@",
    "@ggggg@@@@@",
    "@gg@gg@",
    "@h@ @hh@",
    "@@  @ii@",
    "@    @ii@",
    "     @jj@",
    "      @@",
}};

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
};

/* The colours the menu font's palette at (0, 481) gives its letters on the US disc. */
constexpr std::optional<Rgb> arrow_colour(char texel) {
    constexpr std::array<uint8_t, 10> face = {248, 248, 232, 216, 200, 184, 168, 152, 136, 120};
    if (texel == '#') return Rgb{104, 112, 120};
    if (texel == '@') return Rgb{0, 0, 8};
    if (texel >= 'a' && texel <= 'j') {
        const uint8_t grey = face[static_cast<size_t>(texel - 'a')];
        return Rgb{grey, grey, static_cast<uint8_t>(texel == 'j' ? 128 : grey)};
    }
    return std::nullopt;
}

/* How many of the drawable's pixels a texel of the front end's letters covers: the letterboxed picture's height
 * over 240. Their texels stay square at any aspect, the squash keeps them so. */
inline double texel_size(int width, int height, int num, int den) {
    if (width <= 0 || height <= 0 || num <= 0 || den <= 0) return 0.0;
    const int shown_h = width * den / num > height ? height : width * den / num;
    return static_cast<double>(shown_h) / kFrameHigh;
}

/* A whole number of pixels, about three quarters of a letter's texel: a fraction made rows of two widths. */
inline int arrow_texel(int width, int height, int num, int den) {
    const int pixels = static_cast<int>(texel_size(width, height, num, den) * 3.0 / 4.0);
    return width > 0 && height > 0 && num > 0 && den > 0 ? (pixels > 0 ? pixels : 1) : 0;
}

}  // namespace disruptor::menu_pointer
