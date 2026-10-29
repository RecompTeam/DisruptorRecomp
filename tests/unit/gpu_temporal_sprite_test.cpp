#include "gpu_temporal_sprite.h"

#include <array>
#include <cmath>
#include <iostream>
#include <utility>

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

bool near(double got, double wanted, double room) { return std::isfinite(got) && std::fabs(got - wanted) <= room; }

/* sized() moved along the row so that its point is `column`, its rectangle following to within a pixel. */
std::pair<GpuTemporalSprite, Drawn> at_column(float column, float scale = 1.0f) {
    GpuTemporalSprite sprite = sized();
    sprite.centre_x = column;
    Drawn drawn;
    drawn.scale = scale;
    drawn.w = static_cast<int>(std::lround(9.1f * scale));
    drawn.left = std::floor(column - 0.25f * scale - static_cast<float>(drawn.w) * 0.5f);
    return {sprite, drawn};
}

int card(const GpuTemporalSprite &sprite, const Drawn &drawn, GpuSpriteCard &out, float horizon = 120.0f) {
    return gpu_sprite_card(&sprite, drawn.left, drawn.top, drawn.w, drawn.h, drawn.raw_w, drawn.raw_h, drawn.scale, horizon, &out);
}

/* The camera-space point of a card's side, its depth a share of the middle's, from where it is drawn. */
std::array<double, 2> side_point(const GpuSpriteCard &turned, const Drawn &drawn, int side) {
    const double x = (side ? drawn.left + drawn.w : drawn.left) + turned.x[side], depth = 1.0 / turned.q[side];
    return {(x - 160.0) / (160.0 * drawn.scale) * depth, depth};
}

void test_a_card_in_the_middle_is_the_flat_sprite() {
    const auto [sprite, drawn] = at_column(160.15f);
    GpuSpriteCard turned{};
    std::array<float, 4> flat = sides(&sprite, drawn);
    expect(card(sprite, drawn, turned) == 1, "a sized sprite becomes a card");
    expect(near(turned.x[0], flat[0], 0.002) && near(turned.x[1], flat[2], 0.002), "in the view's middle its sides are the flat sprite's");
    expect(near(turned.y[0], flat[1], 0.002) && near(turned.y[1], flat[1], 0.002) && near(turned.y[2], flat[3], 0.002) &&
           near(turned.y[3], flat[3], 0.002), "and so are its corners' rows");
    expect(near(turned.q[0], 1.0, 0.0001) && near(turned.q[1], 1.0, 0.0001), "both sides stand at the middle's depth");
}

void test_a_card_aside_faces_the_camera() {
    for (const float scale : {1.0f, 0.75f}) {
        for (const float column : {40.0f, 104.65f, 230.0f, 300.0f}) {
            const auto [sprite, drawn] = at_column(column, scale);
            GpuSpriteCard turned{};
            expect(card(sprite, drawn, turned) == 1, "a card aside is made");
            const auto left = side_point(turned, drawn, 0), right = side_point(turned, drawn, 1);
            const double aside = (column - 0.25 * scale - 160.0) / (160.0 * scale);
            const double edge[2] = {right[0] - left[0], right[1] - left[1]};
            expect(near(edge[0] * aside + edge[1], 0.0, 1e-5), "its edge stands across the line from the camera to its middle");
            expect(near(std::hypot(edge[0], edge[1]), 9.1 / 160.0, 1e-5), "and is as wide in the world as the flat sprite at its middle");
            expect(near((left[0] + right[0]) * 0.5, aside, 1e-5) && near((left[1] + right[1]) * 0.5, 1.0, 1e-5),
                   "its middle stays where the game put the sprite");
            const double drawn_width = (drawn.w + turned.x[1]) - turned.x[0];
            expect(drawn_width > 9.1 * scale * std::sqrt(1.0 + aside * aside) - 0.01 && drawn_width > 9.1 * scale,
                   "on screen it is wider than the flat sprite, as the world is toward the sides");
            const int nearer = column > 160.0f ? 1 : 0;
            expect(turned.q[nearer] > 1.0f && turned.q[1 - nearer] < 1.0f, "its outer side is the nearer one");
            for (int side = 0; side < 2; ++side) {
                const double top = (drawn.top + turned.y[side] - 120.0) / turned.q[side];
                const double bottom = (drawn.top + drawn.h + turned.y[2 + side] - 120.0) / turned.q[side];
                expect(near(top, 59.75 - 9.35 - 120.0, 1e-4) && near(bottom, 59.75 + 9.35 - 120.0, 1e-4),
                       "each side's top and bottom are the flat sprite's at that side's depth");
            }
        }
    }
}

void test_cards_mirror_about_the_middle() {
    const auto [left_sprite, left_drawn] = at_column(100.25f);
    const auto [right_sprite, right_drawn] = at_column(220.25f);
    GpuSpriteCard a{}, b{};
    expect(card(left_sprite, left_drawn, a) == 1 && card(right_sprite, right_drawn, b) == 1, "both cards are made");
    const double a_left = left_drawn.left + a.x[0] - 160.0, a_right = left_drawn.left + left_drawn.w + a.x[1] - 160.0;
    const double b_left = right_drawn.left + b.x[0] - 160.0, b_right = right_drawn.left + right_drawn.w + b.x[1] - 160.0;
    expect(near(a_left, -b_right, 1e-3) && near(a_right, -b_left, 1e-3), "a card as far left as another is right mirrors it");
    expect(near(a.q[0], b.q[1], 1e-5) && near(a.q[1], b.q[0], 1e-5), "and so do the depths of its sides");
}

void test_the_horizon_turns_the_rows() {
    const auto [sprite, drawn] = at_column(250.0f);
    GpuSpriteCard low{}, high{};
    expect(card(sprite, drawn, low, 120.0f) == 1 && card(sprite, drawn, high, 20.0f) == 1, "cards under two horizons are made");
    expect(near(low.y[0] - high.y[0], (120.0 - 20.0) * (1.0 - low.q[0]), 1e-3),
           "a row moves toward the horizon by its distance from it, as its side's depth grows");
    const double tall_near = (drawn.h + low.y[3]) - low.y[1], tall_far = (drawn.h + low.y[2]) - low.y[0];
    expect(tall_near > tall_far, "the nearer side is the taller");
}

void test_what_stays_flat() {
    const auto [sprite, drawn] = at_column(230.0f);
    GpuSpriteCard turned{};
    GpuTemporalSprite other = sprite;
    other.shadow = 1;
    expect(card(other, drawn, turned) == 0, "a shadow lies on the floor and is not turned");
    other = sprite;
    other.sized = 0;
    expect(card(other, drawn, turned) == 0, "a sprite of no own size stays flat");
    other = sprite;
    other.placed = 0;
    expect(card(other, drawn, turned) == 0, "nor one without a place");
    other = sprite;
    other.width = 13.6f;
    expect(card(other, drawn, turned) == 0, "nor one whose size is not its packet's");
    Drawn moved = drawn;
    moved.left += 4.0f;
    expect(card(sprite, moved, turned) == 0, "nor one whose point is more than three pixels from its rectangle's middle");
    expect(gpu_sprite_card(nullptr, 0, 0, 9, 19, 10, 20, 1.0f, 120.0f, &turned) == 0, "nor one nothing is known about");
    GpuTemporalSprite thin = sprite;
    thin.width = 1.5f;
    Drawn narrow = drawn;
    narrow.w = 1;
    narrow.raw_w = 2;
    narrow.left = std::floor(thin.centre_x - 0.25f);
    expect(card(thin, narrow, turned) == 0, "nor one whose own width is nothing");
    thin.width = 2.2f;
    expect(card(thin, narrow, turned) == 0, "or under a pixel");
    thin = sprite;
    thin.height = 2.3f;
    Drawn low = drawn;
    low.h = 1;
    low.raw_h = 2;
    low.top = 59.0f;
    expect(card(thin, low, turned) == 0, "nor one under a pixel tall");
    thin.height = 3.0f;
    expect(card(thin, low, turned) == 1, "a pixel and a half tall is a card");
}

/* A sprite `wide` across and twice as tall around row 59.75, its point at `column`: half `wide` / 320 of its depth. */
std::pair<GpuTemporalSprite, Drawn> big_at(float column, float wide) {
    GpuTemporalSprite sprite = sized();
    sprite.centre_x = column;
    sprite.width = wide + 1.5f;
    sprite.height = 2.0f * wide + 1.5f;
    Drawn drawn;
    drawn.w = static_cast<int>(std::lround(wide));
    drawn.raw_w = drawn.w + 1;
    drawn.h = 2 * drawn.w;
    drawn.raw_h = drawn.h + 1;
    drawn.left = std::floor(column - 0.25f - static_cast<float>(drawn.w) * 0.5f);
    drawn.top = std::floor(59.75f - static_cast<float>(drawn.h) * 0.5f);
    return {sprite, drawn};
}

void test_a_near_sprite_turns_less() {
    expect(near(gpu_sprite_card_share(0.1f), 1.0, 1e-6) && near(gpu_sprite_card_share(0.15f), 1.0, 1e-6),
           "a card whose half width is up to 0.15 of its depth turns all the way");
    expect(near(gpu_sprite_card_share(0.25f), 0.5, 1e-6), "one at 0.25 half way");
    expect(near(gpu_sprite_card_share(0.35f), 0.0, 1e-6) && near(gpu_sprite_card_share(0.6f), 0.0, 1e-6),
           "and one at 0.35 or nearer not at all");
    for (const float column : {60.0f, 250.0f}) {
        const auto [sprite, drawn] = big_at(column, 80.0f);
        GpuSpriteCard turned{};
        expect(card(sprite, drawn, turned) == 1, "a sprite a quarter of its depth wide each way becomes a card");
        const auto left = side_point(turned, drawn, 0), right = side_point(turned, drawn, 1);
        const double aside = (column - 0.25 - 160.0) / 160.0;
        expect(near(std::atan2(left[1] - right[1], right[0] - left[0]), 0.5 * std::atan(aside), 1e-4), "and turns half way");
        expect(near(std::hypot(right[0] - left[0], right[1] - left[1]), 80.0 / 160.0, 1e-4), "keeping its width in the world");
        expect(near((left[0] + right[0]) * 0.5, aside, 1e-5) && near((left[1] + right[1]) * 0.5, 1.0, 1e-5),
               "and its middle where the game put it");
    }
    const auto [body, drawn] = big_at(300.0f, 140.0f);
    GpuSpriteCard turned{};
    expect(card(body, drawn, turned) == 0, "a body at the player's feet stays flat");
}

void test_a_cards_width_in_camera_space() {
    double across[2];
    gpu_sprite_card_across(0.0, 200.0, 8.0, across);
    expect(near(across[0], 8.0, 1e-9) && near(across[1], 0.0, 1e-9), "straight ahead a card's width runs along the camera's x");
    gpu_sprite_card_across(100.0, 100.0, 8.0, across);
    expect(near(across[0], 8.0 / std::sqrt(2.0), 1e-9) && near(across[1], -8.0 / std::sqrt(2.0), 1e-9),
           "at 45 degrees to the right it turns its right end toward the camera");
    gpu_sprite_card_across(100.0, 100.0, 25.0, across);
    expect(near(std::atan2(-across[1], across[0]), std::atan(1.0) * 0.5, 1e-6), "a card a quarter of its depth wide turns half way");
    gpu_sprite_card_across(100.0, 100.0, 50.0, across);
    expect(near(across[0], 50.0, 1e-9) && near(across[1], 0.0, 1e-9), "and one nearer keeps the camera's x");
    gpu_sprite_card_across(0.0, 0.0, 8.0, across);
    expect(near(across[0], 8.0, 1e-9) && near(across[1], 0.0, 1e-9), "at the camera itself it keeps the camera's x");
}

}  // namespace

int main() {
    test_the_seams_numbers_become_pixels();
    test_a_sprite_without_a_place_stays();
    test_a_placed_sprite_moves_whole();
    test_a_sized_sprite_takes_its_own_sides();
    test_a_shadow_lies_flat_when_asked();
    test_a_card_in_the_middle_is_the_flat_sprite();
    test_a_card_aside_faces_the_camera();
    test_cards_mirror_about_the_middle();
    test_the_horizon_turns_the_rows();
    test_what_stays_flat();
    test_a_near_sprite_turns_less();
    test_a_cards_width_in_camera_space();
    if (g_failures) return 1;
    std::cout << "GPU sprite side and card tests passed\n";
    return 0;
}
