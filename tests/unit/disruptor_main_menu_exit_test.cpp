#include "game/session/main_menu_exit.h"

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

void test_draw() {
    expect(draws_exit(kItemDrawn, kExit), "the ninth item of the main menu's loop is EXIT");
    expect(!draws_exit(kItemDrawn, 7u), "CREDITS stays the game's");
    expect(!draws_exit(0x8001C174u, kExit), "a draw from elsewhere stays the game's");
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
    test_draw();
    test_word();
    if (g_failures != 0) return 1;
    std::cout << "disruptor main menu exit: PASS\n";
    return 0;
}
