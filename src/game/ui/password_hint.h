#pragma once

#include "game/input/menu_pointer.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

/* The keys of the password screen under its field: each face button's symbol and what types it, then the key that
 * takes the last symbol back, in the game's own letters and symbols over its picture. */
namespace disruptor::password_hint {

inline constexpr uint32_t kFont = 0x80071474u; /* the menu font's glyphs, 12 by 14 palette indices each */
inline constexpr int kGlyphWide = 12, kGlyphHigh = 14, kGlyphBytes = kGlyphWide * kGlyphHigh;
inline constexpr int kClutX = 0, kClutY = 481; /* the palette of the letters and of the symbols */
/* 0x8001A6A8 draws a symbol 16 by 16 from the 8-bit page at (320, 256), v 96 plus 16 a symbol. */
inline constexpr int kPageX = 320, kPageY = 256, kSymbolV = 96, kSymbolSize = 16;
inline constexpr int kFirstLine = 128, kSecondLine = 152, kGap = 12, kAfterSymbol = 3;
/* A line wider than this goes on in a second row this far down: RETURN TO MAIN MENU stands at 204. */
inline constexpr int kMostWide = 296, kWrapDown = 22;

/* The symbols as 0x8001E854..0x8001E8DC type them. */
enum Symbol : int { kCross = 0, kTriangle = 1, kSquare = 2, kCircle = 3 };

/* The words a language disc's menus would use, without the signs the US font lacks. The Japanese disc's menus are English. */
inline std::string_view in_language(std::string_view word, std::string_view language) {
    struct Words {
        std::string_view english, french, german;
    };
    constexpr std::array<Words, 9> words = {{{"ENTER", "ENTREE", "EINGABE"},
                                            {"SPACE", "ESPACE", "LEERTASTE"},
                                            {"LEFT", "GAUCHE", "LINKS"},
                                            {"RIGHT", "DROITE", "RECHTS"},
                                            {"UP", "HAUT", "OBEN"},
                                            {"DOWN", "BAS", "UNTEN"},
                                            {"BACKSPACE", "RETOUR", "RUECKTASTE"},
                                            {"TAB", "TAB", "TAB"},
                                            {"DELETES", "EFFACE", "LOESCHT"}}};
    for (const Words& known : words)
        if (known.english == word) return language == "French" ? known.french : language == "German" ? known.german : known.english;
    return word;
}

/* A key's name in the menu font's letters, capitals and digits, in the disc's language where it has a word for it. */
inline std::string key_word(std::string_view name, std::string_view language = {}) {
    std::string upper;
    for (const char letter : name) upper += static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
    if (upper == "RETURN" || upper == "KEYPAD ENTER") return std::string(in_language("ENTER", language));
    constexpr std::array<std::pair<char, std::string_view>, 18> signs = {{{'-', "MINUS"}, {'+', "PLUS"}, {'*', "MULTIPLY"},
        {'/', "SLASH"}, {'\\', "BACKSLASH"}, {'=', "EQUALS"}, {'[', "LEFT BRACKET"}, {']', "RIGHT BRACKET"},
        {';', "SEMICOLON"}, {'\'', "QUOTE"}, {',', "COMMA"}, {'.', "PERIOD"}, {'`', "GRAVE"}, {'#', "HASH"},
        {'&', "AND"}, {'(', "LEFT PAREN"}, {')', "RIGHT PAREN"}, {'!', "EXCLAIM"}}};
    std::string word;
    const auto put = [&word](char letter) {
        if (letter != ' ' || (!word.empty() && word.back() != ' ')) word += letter;
    };
    for (const char letter : upper) {
        if ((letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == ' ') {
            put(letter);
            continue;
        }
        for (const auto& [sign, named] : signs) {
            if (sign != letter) continue;
            put(' ');
            for (const char part : named) put(part);
            put(' ');
        }
    }
    while (!word.empty() && word.back() == ' ') word.pop_back();
    return std::string(in_language(word, language));
}

struct Item {
    int symbol = -1; /* none: a word alone */
    std::string word;
};

struct Line {
    std::vector<Item> items;
    int y = 0;
};

/* Words for Cross, Circle, Square and Triangle, an empty one for a button no key types, and the delete key's word. */
inline std::vector<Line> lines(const std::array<std::string, 4>& faces, const std::string& left, std::string_view language = {}) {
    constexpr std::array<int, 4> order = {kCross, kCircle, kSquare, kTriangle};
    std::vector<Line> out;
    Line first{{}, kFirstLine};
    for (size_t index = 0; index < faces.size(); ++index)
        if (!faces[index].empty()) first.items.push_back({order[index], faces[index]});
    if (!first.items.empty()) out.push_back(std::move(first));
    if (!left.empty()) out.push_back({{{-1, left + " " + std::string(in_language("DELETES", language))}}, kSecondLine});
    return out;
}

struct Run {
    int x = 0, y = 0, length = 0;
    menu_pointer::Rgb colour;
};

/* The texels of the lines in the frame, centred, as runs of one colour along a row. `glyph(g, column, row)` and
 * `symbol(s, u, v)` give palette indices, 0 for none, `colour(index)` a palette entry, none where it is clear. */
template <class Glyph, class SymbolTexel, class Colour, class Width>
std::vector<Run> raster(const std::vector<Line>& lines, Glyph glyph, SymbolTexel symbol, Colour colour, Width width_of) {
    std::vector<Run> runs;
    const auto put = [&](int x, int y, int index) {
        const std::optional<menu_pointer::Rgb> shade = index != 0 ? colour(index) : std::nullopt;
        if (!shade) return;
        Run* last = runs.empty() ? nullptr : &runs.back();
        if (last && last->y == y && last->x + last->length == x && last->colour.r == shade->r &&
            last->colour.g == shade->g && last->colour.b == shade->b)
            ++last->length;
        else
            runs.push_back({x, y, 1, *shade});
    };
    const auto text_width = [&](const std::string& word) { return menu_pointer::text_width(word, width_of); };
    const auto row_width = [&](const std::vector<Item>& items) {
        int width = 0;
        for (const Item& item : items)
            width += (item.symbol >= 0 ? kSymbolSize + kAfterSymbol : 0) + text_width(item.word) + (width > 0 ? kGap : 0);
        return width;
    };
    std::vector<Line> rows;
    int down = 0;
    for (const Line& line : lines) {
        if (row_width(line.items) <= kMostWide || line.items.size() < 2) {
            rows.push_back({line.items, line.y + down});
            continue;
        }
        const auto half = line.items.begin() + static_cast<std::ptrdiff_t>((line.items.size() + 1) / 2);
        rows.push_back({{line.items.begin(), half}, line.y + down});
        down += kWrapDown;
        rows.push_back({{half, line.items.end()}, line.y + down});
    }
    for (const Line& line : rows) {
        int x = menu_pointer::kFrameWide / 2 - row_width(line.items) / 2;
        for (const Item& item : line.items) {
            if (item.symbol >= 0) {
                for (int v = 0; v < kSymbolSize; ++v)
                    for (int u = 0; u < kSymbolSize; ++u) put(x + u, line.y - 1 + v, symbol(item.symbol, u, v));
                x += kSymbolSize + kAfterSymbol;
            }
            for (const char letter : item.word) {
                if (letter == ' ') {
                    x += menu_pointer::kSpace;
                    continue;
                }
                const int shape = menu_pointer::glyph(letter);
                for (int row = 0; row < kGlyphHigh; ++row)
                    for (int column = 0; column < kGlyphWide; ++column) put(x + column, line.y + row, glyph(shape, column, row));
                x += width_of(shape);
            }
            x += kGap;
        }
    }
    return runs;
}

/* A palette entry of the PlayStation's 15 bits, 0 clear. */
constexpr std::optional<menu_pointer::Rgb> palette_colour(uint16_t entry) {
    if (entry == 0u) return std::nullopt;
    return menu_pointer::Rgb{static_cast<uint8_t>((entry & 31u) << 3), static_cast<uint8_t>((entry >> 5 & 31u) << 3),
                             static_cast<uint8_t>((entry >> 10 & 31u) << 3)};
}

}  // namespace disruptor::password_hint
