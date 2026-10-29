#include "game/render/marked_corners.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace disruptor::marked_corners;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

uint32_t at(int32_t x, int32_t y) {
    return static_cast<uint32_t>(static_cast<uint16_t>(y)) << 16 | static_cast<uint16_t>(x);
}

uint32_t marked(uint32_t depth) { return kMarked | depth << 16 | 37u << 6 | kSides; }

void test_an_unmarked_corner_keeps_the_games_sides() {
    expect(real_sides(56u << 16 | 37u << 6 | 32u, at(260, 244), -8, 328) == 32u, "below the screen, its index left out");
    expect(real_sides(56u << 16 | 37u << 6 | 13u, at(-900, 900), -8, 328) == 13u, "the game's bits, not its place");
}

void test_a_corner_marked_for_its_depth_keeps_every_side() {
    expect(real_sides(marked(900u), at(543, 100), -8, 328) == kSides, "383 from the light table's centre");
    expect(real_sides(marked(900u), at(-223, 100), -8, 328) == kSides, "and 383 to its left");
    expect(real_sides(marked(900u), at(544, 100), -8, 328) == kRight, "384 is past the light table: its real side");
    expect(real_sides(marked(900u), at(-224, 100), -8, 328) == kLeft, "and so on the left");
}

void test_a_corner_marked_for_the_light_table_lies_on_its_real_sides() {
    expect(real_sides(marked(100u), at(600, 100), -8, 328) == kRight, "right of the span");
    expect(real_sides(marked(100u), at(-300, 100), -8, 328) == kLeft, "left of it");
    expect(real_sides(marked(100u), at(-300, 100), -301, 328) == 0u, "the span is the room's");
    expect(real_sides(marked(100u), at(600, -1), -8, 328) == (kRight | kAbove), "and above the screen");
    expect(real_sides(marked(100u), at(600, 0), -8, 328) == kRight, "a row at the top is on screen");
    expect(real_sides(marked(100u), at(600, 241), -8, 328) == (kRight | kBelow), "below it");
    expect(real_sides(marked(100u), at(600, 240), -8, 328) == kRight, "the bottom row is on screen");
    expect(real_sides(marked(100u), at(600, 100), -8, 600) == 0u, "a right edge reaching it");
    expect(real_sides(marked(100u), at(600, 100), -8, 599) == kRight, "one short of it");
}

void test_a_corner_at_the_viewer_counts_on_no_side() {
    expect(real_sides(marked(16u), at(552, 81), -8, 328) == kNear, "at the near limit");
    expect(real_sides(marked(0u), at(1023, 695), -8, 328) == kNear, "behind the viewer");
    expect(real_sides(marked(17u), at(552, 81), -8, 328) == kRight, "past the limit it has a place");
}

void test_the_floor_under_the_wall_button_stays() {
    const std::array<uint32_t, 4> words = {marked(1u), 56u << 16 | 32u, 56u << 16 | 32u, marked(16u)};
    const std::array<uint32_t, 4> screen = {at(1023, 695), at(260, 244), at(260, 244), at(552, 81)};
    expect((words[0] & words[1] & words[2] & words[3] & kSides) == kBelow, "the game finds it below the screen");
    expect(shared_sides(words, screen, -8, 328) == 0u, "it covers the screen's corner");
    const std::array<uint32_t, 4> right = {marked(100u), 56u << 16 | 8u, 56u << 16 | 8u, marked(100u)};
    expect(shared_sides(right, {at(700, 10), at(400, 20), at(400, 30), at(800, 40)}, -8, 328) == kRight,
           "a face wholly right of the span is still dropped");
    expect(shared_sides({56u << 16 | 8u, 56u << 16 | 8u, 56u << 16 | 8u, marked(100u)},
                        {at(400, 10), at(400, 20), at(400, 30), at(-300, 300)}, -8, 328) == 0u,
           "the fourth corner counts too");
}

}  // namespace

int main() {
    test_an_unmarked_corner_keeps_the_games_sides();
    test_a_corner_marked_for_its_depth_keeps_every_side();
    test_a_corner_marked_for_the_light_table_lies_on_its_real_sides();
    test_a_corner_at_the_viewer_counts_on_no_side();
    test_the_floor_under_the_wall_button_stays();
    if (g_failures) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "marked_corners: PASS\n";
    return 0;
}
