#include "gpu_pinned_corner.h"

#include <cstdio>
#include <cstdlib>

namespace {

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

constexpr int32_t kUnread = 0x5A5A5A5A;
constexpr int32_t kUntouched = -77;

PsxPinnedPlaces fresh() {
    PsxPinnedPlaces places{};
    for (int i = 0; i < 4; ++i) places.x16[i] = places.y16[i] = kUntouched;
    places.unpinned = 9;
    return places;
}

void a_pinned_corner_goes_where_it_projects() {
    /* The game pinned corner 1 to (576, -256). It projects to (3000.25, -1040.5). The second buffer adds 240 rows. */
    const int pinned[4] = {0, 1, 0, 0};
    const int32_t precise_x[4] = {kUnread, 3000 * 65536 + 0x4000, kUnread, kUnread};
    const int32_t precise_y[4] = {kUnread, -1041 * 65536 + 0x8000, kUnread, kUnread};
    const int32_t raw_x[4] = {kUnread, 576, kUnread, kUnread}, raw_y[4] = {kUnread, -256, kUnread, kUnread};
    const int32_t drawn_x[4] = {223, 576, 223, 364}, drawn_y[4] = {306, -16, 455, 736};
    PsxPinnedPlaces places = fresh();
    psx_pinned_places(4, 1, pinned, precise_x, precise_y, raw_x, raw_y, drawn_x, drawn_y, &places);
    expect(places.unpinned == 1, "a polygon with a pinned corner is marked");
    expect(places.x16[1] == 3000 * 65536 + 0x4000 && places.y16[1] == (-1041 + 240) * 65536 + 0x8000,
           "the pinned corner is at its projection, moved by the drawing offset");
    expect(places.x16[0] == 223 * 65536 && places.y16[0] == 306 * 65536 && places.x16[2] == 223 * 65536 &&
               places.y16[2] == 455 * 65536 && places.x16[3] == 364 * 65536 && places.y16[3] == 736 * 65536,
           "every other corner is on the game's pixel, whatever its projection holds");

    places = fresh();
    psx_pinned_places(4, 0, pinned, precise_x, precise_y, raw_x, raw_y, drawn_x, drawn_y, &places);
    expect(places.unpinned == 0 && places.x16[1] == 576 * 65536 && places.y16[1] == -16 * 65536,
           "with the switch off the pinned corner stays where the game put it and the polygon is not marked");

    const int none[4] = {0, 0, 0, 0};
    places = fresh();
    psx_pinned_places(4, 1, none, precise_x, precise_y, raw_x, raw_y, drawn_x, drawn_y, &places);
    expect(places.unpinned == 0 && places.x16[1] == 576 * 65536, "a polygon without a pinned corner is not marked");

    places = fresh();
    psx_pinned_places(3, 1, pinned, precise_x, precise_y, raw_x, raw_y, drawn_x, drawn_y, &places);
    expect(places.unpinned == 1 && places.x16[3] == kUntouched && places.y16[3] == kUntouched,
           "a triangle has three corners");

    const int far_pinned[4] = {1, 0, 0, 0};
    const int32_t far_x[4] = {(INT32_C(1) << 30) + (160 << 16), 0, 0, 0}, far_y[4] = {-(INT32_C(1) << 30), 0, 0, 0};
    const int32_t far_raw_x[4] = {576, 0, 0, 0}, far_raw_y[4] = {-256, 0, 0, 0};
    const int32_t far_drawn_x[4] = {576 + 1023, 0, 0, 0}, far_drawn_y[4] = {-256 - 1024, 0, 0, 0};
    places = fresh();
    psx_pinned_places(4, 1, far_pinned, far_x, far_y, far_raw_x, far_raw_y, far_drawn_x, far_drawn_y, &places);
    expect(places.x16[0] == (INT32_C(1) << 30) + ((160 + 1023) << 16) && places.y16[0] == -(INT32_C(1) << 30) - (1024 << 16),
           "the farthest projection kept and the largest drawing offset still fit a 16.16 coordinate");
}

void a_pinned_corner_hands_on_no_depth() {
    expect(psx_pinned_corner_depth(1, 1, 6) == 0, "a pinned corner has no depth for the in-between frames");
    expect(psx_pinned_corner_depth(1, 0, 318) == 318, "any other corner keeps its depth");
    expect(psx_pinned_corner_depth(0, 1, 6) == 6, "with the switch off a pinned corner keeps its depth");
    expect(psx_pinned_depth_mode(318, 607, 295) == 1, "three depths ask for the strict mode");
    expect(psx_pinned_depth_mode(0, 607, 295) == 2 && psx_pinned_depth_mode(318, 0, 295) == 2 &&
               psx_pinned_depth_mode(318, 607, 0) == 2 && psx_pinned_depth_mode(0, 0, 0) == 2,
           "a missing depth asks for the mode that keeps the others");
}

}  // namespace

int main() {
    a_pinned_corner_goes_where_it_projects();
    a_pinned_corner_hands_on_no_depth();
    std::puts("GPU pinned corner tests passed");
    return 0;
}
