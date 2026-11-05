#include "game/input/menu_pointer.h"
#include "game/ui/password_hint.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace {

namespace hint = disruptor::password_hint;
namespace pointer = disruptor::menu_pointer;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void test_words() {
    expect(hint::key_word("Return") == "ENTER" && hint::key_word("Keypad Enter") == "ENTER", "Return reads ENTER");
    expect(hint::key_word("Left Shift") == "LEFT SHIFT" && hint::key_word("F") == "F" && hint::key_word("Keypad 7") == "KEYPAD 7",
           "a key's name in capitals and digits");
    expect(hint::key_word("[") == "LEFT BRACKET" && hint::key_word("Keypad -") == "KEYPAD MINUS" && hint::key_word("=") == "EQUALS" &&
               hint::key_word("Keypad .") == "KEYPAD PERIOD" && hint::key_word("~") == "",
           "a sign the font lacks is named by words, one without a word is left out");
    expect(hint::key_word("Return", "French") == "ENTREE" && hint::key_word("Space", "German") == "LEERTASTE" &&
               hint::key_word("Left", "German") == "LINKS" && hint::key_word("F", "French") == "F" && hint::key_word("Space", "Japanese") == "SPACE",
           "a language disc names the keys with words its menus would use, the Japanese disc's menus are English");
    expect(hint::lines({"", "", "", ""}, "GAUCHE", "French")[0].items[0].word == "GAUCHE EFFACE" &&
               hint::lines({"", "", "", ""}, "LINKS", "German")[0].items[0].word == "LINKS LOESCHT",
           "and says what the delete key does in it");
}

void test_lines() {
    const auto both = hint::lines({"ENTER", "SPACE", "F", "E"}, "LEFT");
    expect(both.size() == 2 && both[0].y == hint::kFirstLine && both[1].y == hint::kSecondLine, "the face buttons, then the delete key below");
    expect(both[0].items.size() == 4 && both[0].items[0].symbol == hint::kCross && both[0].items[1].symbol == hint::kCircle &&
               both[0].items[2].symbol == hint::kSquare && both[0].items[3].symbol == hint::kTriangle && both[0].items[1].word == "SPACE",
           "in the pad's order Cross, Circle, Square, Triangle, each with its key");
    expect(both[1].items.size() == 1 && both[1].items[0].symbol < 0 && both[1].items[0].word == "LEFT DELETES", "the delete key's line");
    const auto unbound = hint::lines({"ENTER", "", "F", "E"}, "");
    expect(unbound.size() == 1 && unbound[0].items.size() == 3, "a button no key types and a missing delete key are left out");
    expect(hint::lines({"", "", "", ""}, "LEFT").size() == 1, "a PlayStation pad has the delete line alone");
}

void test_raster() {
    const std::vector<hint::Line> line = {{{{hint::kSquare, "AB"}}, 100}};
    std::set<std::pair<int, int>> symbol_texels, glyph_texels;
    const auto runs = hint::raster(
        line, [](int shape, int column, int row) { return shape == 10 && row == 0 && column < 3 ? 7 : (shape == 11 && row == 13 && column == 0 ? 9 : 0); },
        [](int symbol, int u, int v) { return symbol == hint::kSquare && v == 0 && u < 2 ? 5 : 0; },
        [](int index) { return index == 9 ? std::optional<pointer::Rgb>() : std::optional<pointer::Rgb>(pointer::Rgb{static_cast<uint8_t>(index), 0, 0}); },
        [](int) { return 10; });
    const int width = hint::kSymbolSize + hint::kAfterSymbol + 20;
    const std::vector<hint::Line> wide = {{{{hint::kCross, "AAAAAAAAAA"}, {hint::kCircle, "AAAAAAAAAA"}, {hint::kSquare, "AAAAAAAAAA"}}, 100},
                                          {{{-1, "A"}}, 130}};
    std::set<int> rows_used;
    int widest = 0, leftmost = pointer::kFrameWide;
    for (const hint::Run &run : hint::raster(wide, [](int, int column, int row) { return column == 0 && row == 0 ? 3 : 0; },
                                              [](int, int u, int v) { return u == 0 && v == 1 ? 3 : 0; },
                                              [](int) { return std::optional<pointer::Rgb>(pointer::Rgb{1, 1, 1}); }, [](int) { return 10; })) {
        rows_used.insert(run.y);
        widest = std::max(widest, run.x + run.length);
        leftmost = std::min(leftmost, run.x);
    }
    expect(rows_used == std::set<int>{100, 100 + hint::kWrapDown, 130 + hint::kWrapDown} && leftmost >= 0 && widest <= pointer::kFrameWide,
           "a line too wide for the frame goes on in a second row, and the lines under it move down");
    const int left = pointer::kFrameWide / 2 - width / 2;
    expect(runs.size() == 2, "a run a row and colour, a clear palette entry draws nothing");
    expect(runs.size() == 2 && runs[0].x == left && runs[0].y == 99 && runs[0].length == 2 && runs[0].colour.r == 5,
           "the symbol first, centred, a texel above the letters as 16 rows stand about 14");
    expect(runs.size() == 2 && runs[1].x == left + hint::kSymbolSize + hint::kAfterSymbol && runs[1].y == 100 && runs[1].length == 3 &&
               runs[1].colour.r == 7,
           "then the word after the symbol and its gap, a glyph's texels in one run");
}

void test_palette_and_window() {
    const auto white = hint::palette_colour(0x7FFFu);
    const auto shadow = hint::palette_colour(0x0400u);
    expect(!hint::palette_colour(0u) && white && white->r == 248 && white->b == 248 && shadow && shadow->r == 0 && shadow->b == 8,
           "fifteen bits a colour, five a channel, red first, 0 clear");
    for (const auto [num, den] : {std::pair{4, 3}, std::pair{16, 9}, std::pair{32, 9}}) {
        const auto window = pointer::window_point(70.0, 134.0, 2560, 1440, num, den);
        const auto back = window ? pointer::frame_point(window->x, window->y, 2560, 1440, num, den) : std::nullopt;
        expect(back && std::abs(back->x - 70.0) < 1e-6 && std::abs(back->y - 134.0) < 1e-6, "a frame point comes back from the window");
    }
    const auto middle = pointer::window_point(160.0, 120.0, 1920, 1080, 16, 9);
    expect(middle && std::abs(middle->x - 960.0) < 1e-6 && std::abs(middle->y - 540.0) < 1e-6, "the frame's middle is the window's");
    expect(!pointer::window_point(0.0, 0.0, 0, 1080, 16, 9), "no window, no point");
}

}  // namespace

int main() {
    test_words();
    test_lines();
    test_raster();
    test_palette_and_window();
    if (g_failures != 0) return 1;
    std::cout << "disruptor password hint: PASS\n";
    return 0;
}
