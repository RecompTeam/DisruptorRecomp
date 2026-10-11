#include "gpu_temporal_sprite.h"

#include <array>
#include <cmath>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++g_failures;
    std::cerr << "FAIL: " << message << '\n';
}

/* A packet 10 x 20 drawn as 9 x 19 at (100, 50), unless a case says otherwise. */
struct Drawn {
    float left = 100.0f;
    float top = 50.0f;
    int w = 9;
    int h = 19;
    int raw_w = 10;
    int raw_h = 20;
    float scale = 1.0f;
    int flat = 0;
};

std::array<float, 4> sides(const GpuTemporalSprite *sprite, const Drawn &drawn) {
    std::array<float, 4> moved{9.0f, 9.0f, 9.0f, 9.0f};
    gpu_temporal_sprite_sides(sprite, drawn.left, drawn.top, drawn.w, drawn.h, drawn.raw_w, drawn.raw_h, drawn.scale, drawn.flat, moved.data());
    return moved;
}

bool is(const std::array<float, 4> &moved, float left, float top, float right, float bottom) {
    const std::array<float, 4> wanted{left, top, right, bottom};
    for (std::size_t side = 0; side < 4; ++side)
        if (!std::isfinite(moved[side]) || std::fabs(moved[side] - wanted[side]) > 0.0005f) return false;
    return true;
}

GpuTemporalSprite placed() {
    GpuTemporalSprite sprite{};
    sprite.placed = 1;
    sprite.centre_x = 104.9f;
    sprite.offset_y = -0.3f;
    return sprite;
}

GpuTemporalSprite sized() {
    GpuTemporalSprite sprite = placed();
    sprite.sized = 1;
    sprite.width = 10.6f;
    sprite.height = 20.2f;
    sprite.row = 60.0f;
    return sprite;
}

/* A shadow 15 x 16 drawn as 14 x 15 at (100, 160), centred on row 168: 48 rows under the horizon. */
GpuTemporalSprite shadow() {
    GpuTemporalSprite sprite{};
    sprite.placed = sprite.sized = sprite.shadow = 1;
    sprite.centre_x = 107.25f;
    sprite.width = 15.6f;
    sprite.height = 16.9f;
    sprite.row = 168.0f;
    return sprite;
}

Drawn under(int flat) {
    Drawn drawn;
    drawn.top = 160.0f;
    drawn.w = 14;
    drawn.h = 15;
    drawn.raw_w = 15;
    drawn.raw_h = 16;
    drawn.flat = flat;
    return drawn;
}

void test_the_seams_numbers_become_pixels() {
    const GpuTemporalSprite one = gpu_temporal_sprite_from_fixed(1, 0, 4, 0, 3 * 65536 + 32768, -16384, 7 * 65536, 11 * 65536, 13 * 65536 + 16384);
    expect(one.placed == 1 && one.sized == 0 && one.shadow == 1 && one.whole == 0, "each flag keeps its place and is 0 or 1");
    expect(one.centre_x == 3.5f && one.offset_y == -0.25f && one.row == 7.0f && one.width == 11.0f && one.height == 13.25f,
           "each 16.16 number becomes its own field in pixels");
    const GpuTemporalSprite other = gpu_temporal_sprite_from_fixed(0, 1, 0, 1, 0, 0, 0, 65536, 2 * 65536);
    expect(other.placed == 0 && other.sized == 1 && other.shadow == 0 && other.whole == 1 && other.width == 1.0f && other.height == 2.0f,
           "and the other way round");
}

void test_a_sprite_without_a_place_stays() {
    GpuTemporalSprite none = sized();
    none.placed = 0;
    expect(is(sides(&none, Drawn{}), 0, 0, 0, 0), "a sprite without a place keeps its rectangle");
    expect(is(sides(nullptr, Drawn{}), 0, 0, 0, 0), "and so does one nothing is known about");
}

void test_a_placed_sprite_moves_whole() {
    const GpuTemporalSprite sprite = placed();
    /* Middle 104.9 - 0.25, the rectangle's own is 104.5. */
    expect(is(sides(&sprite, Drawn{}), 0.15f, -0.3f, 0.15f, -0.3f), "a placed sprite moves as one piece to its unrounded point");
    Drawn squashed;
    squashed.w = 7;
    squashed.scale = 0.75f;
    expect(is(sides(&sprite, squashed), 1.2125f, -0.3f, 1.2125f, -0.3f), "under the squash the quarter pixel is squashed too");
    GpuTemporalSprite far = sprite;
    far.centre_x = 108.5f;
    expect(is(sides(&far, Drawn{}), 0, 0, 0, 0), "a side that would move more than three pixels moves none");
    far = sprite;
    far.offset_y = 3.2f;
    expect(is(sides(&far, Drawn{}), 0, 0, 0, 0), "up and down too");
    /* 8.7 wide around 107.55: the left side would move 3.2 px, the right one 2.9. */
    GpuTemporalSprite left = sized();
    left.width = 10.2f;
    left.centre_x = 107.8f;
    expect(is(sides(&left, Drawn{}), 0, 0, 0, 0), "one side over the limit is enough: the left");
    /* 18.5 tall around 62.45: the top would move 3.2 px, the bottom 2.7. */
    GpuTemporalSprite top = sized();
    top.height = 20.0f;
    top.row = 62.7f;
    expect(is(sides(&top, Drawn{}), 0, 0, 0, 0), "or the top");
}

void test_a_sized_sprite_takes_its_own_sides() {
    const GpuTemporalSprite sprite = sized();
    /* 10.6 - 1.5 = 9.1 wide and 20.2 - 1.5 = 18.7 tall around (104.65, 59.75). */
    expect(is(sides(&sprite, Drawn{}), 0.10f, 0.40f, 0.20f, 0.10f), "a direct packet is a pixel and a half under its size");
    Drawn rebuilt;
    rebuilt.w = 10;
    rebuilt.h = 20;
    rebuilt.raw_w = 11;
    rebuilt.raw_h = 21;
    GpuTemporalSprite whole = sprite;
    whole.whole = 1;
    /* 10.1 wide and 19.7 tall around (105.15, 60.25). */
    expect(is(sides(&whole, rebuilt), 0.10f, 0.40f, 0.20f, 0.10f), "a rebuilt packet is half a pixel under its size and leans the other way");
    /* 10.6 - 0.5 is 1.1 from the 9 the packet draws: only the place is used, leaning the rebuilt way. */
    expect(is(sides(&whole, Drawn{}), 0.65f, -0.3f, 0.65f, -0.3f), "a rebuilt size beside a direct packet is not that packet's size");
    Drawn squashed;
    squashed.w = 7;
    squashed.scale = 0.75f;
    expect(is(sides(&sprite, squashed), 1.30f, 0.40f, 1.125f, 0.10f), "the squash narrows the width and leaves the height");
    GpuTemporalSprite other = sprite;
    other.width = 12.2f;
    expect(is(sides(&other, Drawn{}), 0.15f, -0.3f, 0.15f, -0.3f), "a width more than a pixel from the packet's is not used, the place still is");
    other = sprite;
    other.height = 17.3f;
    expect(is(sides(&other, Drawn{}), 0.15f, -0.3f, 0.15f, -0.3f), "nor a height");
    other = sprite;
    other.sized = 0;
    expect(is(sides(&other, Drawn{}), 0.15f, -0.3f, 0.15f, -0.3f), "nor a size nobody handed over");
    GpuTemporalSprite thin = sprite;
    thin.width = 2.2f;
    thin.height = 2.3f;
    Drawn small;
    small.w = 1;
    small.h = 1;
    small.raw_w = 2;
    small.raw_h = 2;
    small.left = 104.0f;
    small.top = 59.0f;
    expect(is(sides(&thin, small), 0.15f, 0.25f, 0.15f, 0.25f), "a rectangle is never thinner or lower than a pixel");
}

void test_a_shadow_lies_flat_when_asked() {
    const GpuTemporalSprite sprite = shadow();
    /* 14.1 wide around 107, 15.4 tall around 167.75. */
    expect(is(sides(&sprite, under(0)), -0.05f, 0.05f, 0.05f, 0.45f), "a shadow keeps the game's height by default");
    /* 14.1 * 48 / 160 = 4.23 tall: the top comes down 5.635 px, more than the limit of the other sides. */
    expect(is(sides(&sprite, under(1)), -0.05f, 5.635f, 0.05f, -5.135f), "flat, it is as tall as its row under the horizon makes a disc");
    /* Squashed to three quarters: 10.575 wide around 107.0625, and still 14.1 * 48 / 160 tall. */
    Drawn narrow = under(1);
    narrow.w = 10;
    narrow.scale = 0.75f;
    expect(is(sides(&sprite, narrow), 1.775f, 5.635f, 2.35f, -5.135f), "the height of a flat shadow comes from its width before the squash");
    GpuTemporalSprite actor = sprite;
    actor.shadow = 0;
    expect(is(sides(&actor, under(1)), -0.05f, 0.05f, 0.05f, 0.45f), "what is not a shadow is not laid flat");
    GpuTemporalSprite above = sprite;
    above.row = 120.0f;
    Drawn high = under(1);
    high.top = 112.0f;
    expect(is(sides(&above, high), -0.05f, 0.05f, 0.05f, 0.45f), "a shadow on the horizon or above it keeps the game's height");
    GpuTemporalSprite low = sprite;
    low.row = 120.5f;
    high.top = 113.0f;
    expect(is(sides(&low, high), -0.05f, 6.75f, 0.05f, -7.25f), "just under the horizon it is one pixel tall");
    GpuTemporalSprite stray = sprite;
    stray.row = 190.0f;
    expect(is(sides(&stray, under(1)), 0, 0, 0, 0), "a flat shadow whose row is farther from its packet than the packet is tall moves nothing");
    stray = sprite;
    stray.centre_x = 111.0f;
    expect(is(sides(&stray, under(1)), 0, 0, 0, 0), "and its left and right keep the limit of three pixels");
}

}  // namespace

int main() {
    test_the_seams_numbers_become_pixels();
    test_a_sprite_without_a_place_stays();
    test_a_placed_sprite_moves_whole();
    test_a_sized_sprite_takes_its_own_sides();
    test_a_shadow_lies_flat_when_asked();
    if (g_failures) return 1;
    std::cout << "GPU sprite side tests passed\n";
    return 0;
}
