#include "game/input/menu_pointer.h"
#include "game/session/main_menu_exit.h"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <iostream>

namespace {

using namespace disruptor::main_menu_exit;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr uint32_t kCross = 0x0040u, kStart = 0x0800u, kSelect = 0x0100u, kSquare = 0x0080u, kCircle = 0x0020u;
constexpr uint32_t kLeft = 0x8000u, kRight = 0x2000u, kTriangle = 0x0010u;

void test_moves() {
    expect(chosen(3u, 0u) == 3u, "no move keeps the cursor");
    expect(chosen(3u, kUp) == 2u && chosen(3u, kDown) == 4u, "Up and Down move by one");
    expect(chosen(0u, kUp) == kExit, "Up from NEW GAME wraps to EXIT");
    expect(chosen(kExit, kDown) == 0u, "Down from EXIT wraps to NEW GAME");
    expect(chosen(7u, kDown) == kExit && chosen(kExit, kUp) == 7u, "EXIT stands between CREDITS and the wrap");
    expect(chosen(kExit, kUp | kDown) == kExit && chosen(0u, kUp | kDown) == 0u, "Up and Down in one frame cancel out");
    expect(chosen(5u, kLeft | kRight | kTriangle) == 5u, "other buttons leave the cursor");
}

void test_exits() {
    expect(exits(kExit, kCross), "Cross on EXIT exits");
    expect(!exits(kExit, 0u) && !exits(kExit, kUp | kDown), "EXIT under the cursor alone does not exit");
    for (const uint32_t button : {kStart, kSelect, kSquare, kCross, kCircle})
        expect(exits(kExit, button), "each button the menu chooses with chooses EXIT");
    expect(!exits(kExit, kTriangle | kLeft | kRight), "a button the menu does not choose with leaves");
    expect(exits(7u, kDown | kCross), "Down and Cross in one frame on CREDITS choose EXIT, as the game moves first");
    expect(!exits(kExit, kUp | kCross), "Up and Cross in one frame on EXIT choose CREDITS");
    expect(exits(0u, kUp | kCross), "Up and Cross on NEW GAME choose EXIT through the wrap");
    expect(!exits(kExit, kDown | kCross), "Down and Cross on EXIT choose NEW GAME");
    for (uint32_t cursor = 0u; cursor < kItems; ++cursor) {
        for (const uint32_t moves : {0u, kUp, kDown, kUp | kDown}) {
            const uint32_t to = chosen(cursor, moves);
            expect(to < kItems, "every move lands on one of the nine items");
            expect(exits(cursor, moves | kCross) == (to == kExit), "a choice exits exactly when it lands on EXIT");
        }
    }
}

void test_kept() {
    expect(kept(kCross | kStart | kSelect | kSquare | kCircle) == 0u, "no choosing button reaches the game");
    expect(kept(kUp | kDown | kCross) == (kUp | kDown), "the moves reach the game");
    expect(kept(kTriangle | kLeft | kRight) == (kTriangle | kLeft | kRight), "other buttons reach the game");
}

void test_hidden() {
    expect(hidden(4u) && hidden(5u) && !hidden(3u) && !hidden(6u), "MEMORY CARD and CONTROLLER OPTIONS are hidden");
    expect(placed(4u) == 6u, "Down onto MEMORY CARD OPTIONS goes on to GENERAL OPTIONS");
    expect(placed(5u) == 3u, "Up onto CONTROLLER OPTIONS goes on to PASSWORD");
    for (uint32_t item = 0u; item < kItems; ++item) {
        if (hidden(item)) continue;
        expect(placed(item) == item, "a shown item keeps the cursor");
        expect(!hidden(placed(chosen(item, kDown))) && !hidden(placed(chosen(item, kUp))),
               "every move ends on a shown item once placed");
    }
    expect(placed(chosen(3u, kDown)) == 6u && placed(chosen(6u, kUp)) == 3u, "the cursor steps over the gap both ways");
    expect(raised(3u) == 0u && raised(6u) == 2u * kRowStep && raised(kExit) == 2u * kRowStep,
           "the items under the gap move up two rows");
}

void test_handed() {
    expect(handed(4u, 0u).cursor == 6u && handed(5u, 0u).cursor == 3u, "a cursor left on a hidden item is moved off");
    const Handed down = handed(3u, kDown | kCross);
    expect(down.cursor == 6u && down.pressed == kCross, "Down and Cross on PASSWORD choose GENERAL OPTIONS");
    const Handed up = handed(6u, kUp | kCross);
    expect(up.cursor == 3u && up.pressed == kCross, "Up and Cross on GENERAL OPTIONS choose PASSWORD");
    expect(handed(3u, kDown).cursor == 3u && handed(3u, kDown).pressed == kDown, "a move alone stays the game's");
    for (uint32_t cursor = 0u; cursor < kItems; ++cursor) {
        for (const uint32_t moves : {0u, kUp, kDown, kUp | kDown}) {
            const Handed hand = handed(cursor, moves | kCross);
            expect(!hidden(chosen(hand.cursor, hand.pressed)), "no choice opens a hidden screen");
        }
    }
    expect(exits(handed(7u, kDown | kCross).cursor, handed(7u, kDown | kCross).pressed), "EXIT is still chosen past CREDITS");
}

void test_draw() {
    expect(draws_exit(kItemDrawn, kExit), "the ninth item of the main menu's loop is EXIT");
    expect(!draws_exit(kItemDrawn, 7u), "CREDITS stays the game's");
    expect(!draws_exit(0x8001C174u, kExit), "a draw from elsewhere stays the game's");
}

void test_pointer_geometry() {
    namespace pointer = disruptor::menu_pointer;
    expect(pointer::glyph('a') == 0 && pointer::glyph('j') == 9, "the small letters are the font's signs");
    expect(pointer::glyph('A') == 10 && pointer::glyph('Z') == 35, "then the capitals");
    expect(pointer::glyph('0') == 36 && pointer::glyph('9') == 45, "then the digits");
    const auto ten = [](int) { return 10; };
    expect(pointer::text_width("A B", ten) == 26 && pointer::text_width("", ten) == 0, "a space is six wide, a letter its glyph's");
    const pointer::Box box = pointer::centred(132, 136);
    expect(box.x == 94 && box.w == 132 && box.y == 136 && box.h == pointer::kRowHigh, "an item stands centred on 160");
    expect(pointer::centred(61, 0).x == 130, "an odd width rounds as the game's halving does");
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    const auto middle = pointer::frame_point(960.0, 540.0, 1920, 1080, 16, 9);
    expect(middle && near(middle->x, 160.0) && near(middle->y, 120.0), "the window's middle is the frame's");
    const auto squashed = pointer::frame_point(960.0 + 0.75 * 66.0 / 320.0 * 1920.0, 0.0, 1920, 1080, 16, 9);
    expect(squashed && near(squashed->x, 226.0), "at 16:9 the text stands squashed to three quarters about the middle");
    const auto pillar = pointer::frame_point(240.0, 1080.0, 1920, 1080, 4, 3);
    expect(pillar && near(pillar->x, 0.0) && near(pillar->y, 240.0), "at 4:3 on a 16:9 window the bars stand beside the frame");
    const auto wide = pointer::frame_point(1920.0 + 0.375 * 160.0 / 320.0 * 3840.0, 540.0, 3840, 1080, 32, 9);
    expect(wide && near(wide->x, 320.0), "at 32:9 the text is squashed to three eighths");
    const auto letterbox = pointer::frame_point(960.0, 270.0, 1920, 1080, 32, 9);
    expect(letterbox && near(letterbox->y, 0.0), "32:9 on a 16:9 window has bars above and below");
    expect(!pointer::frame_point(1.0, 1.0, 0, 1080, 16, 9) && !pointer::frame_point(1.0, 1.0, 1920, 1080, 0, 9),
           "no frame without a window or an aspect");
    expect(pointer::reach(box, {94.0 - 24.0, 140.0}) == 24.0 && !pointer::reach(box, {94.0 - 25.0, 140.0}),
           "beside the letters an item is pointed at to 24 of the frame's pixels");
    expect(pointer::reach(box, {160.0, 136.0 - 12.0}) == 12.0 && !pointer::reach(box, {160.0, 136.0 - 13.0}) &&
               pointer::reach(box, {160.0, 150.0 + 12.0}) == 12.0 && pointer::reach(box, {160.0, 140.0}) == 0.0,
           "above and below to 12, inside at no distance");
    expect(!pointer::reach(pointer::Box{}, {0.0, 0.0}), "an item not drawn is never pointed at");
    expect(pointer::kArrow[0] == "@", "the arrow's tip is its top left texel");
    expect(pointer::arrow_texel(1920, 1080, 16, 9) == 3 && pointer::arrow_texel(2560, 1440, 16, 9) == 4,
           "the arrow's texel is a whole number of pixels, three quarters of a letter's");
    expect(pointer::arrow_texel(640, 240, 4, 3) == 1 && pointer::arrow_texel(0, 0, 4, 3) == 0, "at least one pixel, none without a window");
    for (const std::string_view line : pointer::kArrow)
        for (const char texel : line)
            expect(texel == ' ' || pointer::arrow_colour(texel).has_value(), "every texel of the arrow has a colour");
    expect(pointer::arrow_colour('a')->r > pointer::arrow_colour('j')->r, "its face darkens downwards as the letters do");
    expect(pointer::arrow_colour('@')->r < pointer::arrow_colour('#')->r, "its shadow is darker than its lit rim");
    expect(std::abs(pointer::texel_size(1920, 1080, 16, 9) - 4.5) < 1e-9, "at 1080 rows a texel is 4.5 pixels");
    expect(std::abs(pointer::texel_size(1920, 1080, 4, 3) - 4.5) < 1e-9, "pillarboxed the picture is still 1080 high");
    expect(std::abs(pointer::texel_size(1920, 1080, 32, 9) - 2.25) < 1e-9, "letterboxed at 32:9 it is 540 high");
    expect(pointer::texel_size(0, 1080, 16, 9) == 0.0, "no window, no arrow");
}

void test_word() {
    expect(word("") == "EXIT", "the US disc says EXIT");
    expect(word("Japanese") == "EXIT", "the Japanese disc's menus are English");
    expect(word("French") == "QUITTER", "the French disc says QUITTER");
    expect(word("German") == "BEENDEN", "the German disc says BEENDEN");
}

}  // namespace

int main() {
    test_moves();
    test_exits();
    test_kept();
    test_hidden();
    test_handed();
    test_draw();
    test_pointer_geometry();
    test_word();
    if (g_failures != 0) return 1;
    std::cout << "disruptor main menu exit: PASS\n";
    return 0;
}
