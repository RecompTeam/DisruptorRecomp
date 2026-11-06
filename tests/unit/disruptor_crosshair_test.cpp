#include "game/ui/crosshair_style.h"
#include "game/settings/game_user_settings.h"

#include <iostream>

namespace {

using namespace disruptor::crosshair;

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

void test_a_plain_dot_keeps_its_size() {
    expect(radius(1080, 100) == 3.0f && radius(720, 100) == 2.0f, "the plain dot is 3 pixels at 1080 rows and never under 2");
    expect(radius(1080, 200) == 6.0f && radius(1080, 50) == 1.5f, "the size scales it and keeps it at 1.5 pixels at least");
    expect(radius(1080, 9000) == radius(1080, kLargest) && radius(1080, 1) == radius(1080, kSmallest),
           "a size out of range is held at its bounds");
}

void test_the_colour_and_its_rim() {
    const Rgba white = fill(Style{});
    expect(white.r == 255 && white.g == 255 && white.b == 255 && white.a == 230, "at first a white dot at 90 percent");
    expect(fill(Style{.colour = 2, .opacity = 100}).r == 255 && fill(Style{.colour = 2, .opacity = 100}).g == 70 &&
               fill(Style{.colour = 2, .opacity = 100}).a == 255,
           "the colour list's third is red, opaque at 100 percent");
    expect(rim(Style{.opacity = 100}).a == 170 && rim(Style{.opacity = 100}).r == 0, "the rim is black, two thirds as opaque");
    expect(fill(Style{.colour = 7}).g == 255 && fill(Style{.colour = -1}).b == 255, "an unknown colour is white");
}

void test_a_hand_edited_style_falls_back() {
    const Style kept = clamped(Style{.shape = 5, .size = 10, .colour = 9, .opacity = 400});
    expect(kept.shape == 0 && kept.size == kSmallest && kept.colour == 0 && kept.opacity == kOpaque,
           "a shape or colour out of range is the first, a size or opacity is held at its bound");
    expect(clamped(Style{.shape = 2, .size = 150, .colour = 4, .opacity = 40}).shape == 2, "and one in range stays");
}

void test_the_lists_match_the_settings_names() {
    expect(kShapeCount == PSXRecompV4::kDisruptorCrosshairShapeCount && kColourCount == PSXRecompV4::kDisruptorCrosshairColourCount,
           "the drawn shapes and colours are as many as settings.toml names");
}

}  // namespace

int main() {
    test_a_plain_dot_keeps_its_size();
    test_the_colour_and_its_rim();
    test_a_hand_edited_style_falls_back();
    test_the_lists_match_the_settings_names();
    if (g_failures) return 1;
    std::cout << "Disruptor crosshair tests passed\n";
    return 0;
}
