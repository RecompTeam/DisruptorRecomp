#include "gpu_world_depth.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {

void expect(bool condition, const char *message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

constexpr float kMargin = 0.03f;
constexpr int kExact = 2, kWorld = 1, kGrownCopy = 4;

void only_exact_polygons_beyond_the_nearest_depth_are_drawn_by_depth() {
    const float far_away[3] = {200.0f, 640.0f, 3200.0f};
    expect(psx_world_depth_class(1, kWorld | kExact, far_away, kMargin) == 1, "an exact polygon with three depths is of the class");
    expect(psx_world_depth_class(1, kWorld | kExact | kGrownCopy, far_away, kMargin) == 1, "the game's grown copy of one too");
    expect(psx_world_depth_class(0, kWorld | kExact, far_away, kMargin) == 0, "with the setting off no polygon is");
    expect(psx_world_depth_class(1, kWorld, far_away, kMargin) == 0, "a polygon on whole pixels is not");
    expect(psx_world_depth_class(1, 0, far_away, kMargin) == 0, "nor one with no flags at all");

    const float nearest = psx_world_depth_nearest(kMargin);
    expect(std::fabs(nearest - 16.49f) < 0.0001f, "the nearest depth is 16 and the margin, and a hundredth");
    for (int corner = 0; corner < 3; ++corner) {
        float depths[3] = {200.0f, 640.0f, 3200.0f};
        depths[corner] = nearest;
        expect(psx_world_depth_class(1, kExact, depths, kMargin) == 0, "a corner at the nearest depth keeps the polygon out");
        depths[corner] = std::nextafter(nearest, 100.0f);
        expect(psx_world_depth_class(1, kExact, depths, kMargin) == 1, "and one just beyond lets it in");
        depths[corner] = 0.0f;
        expect(psx_world_depth_class(1, kExact, depths, kMargin) == 0, "a corner without a depth keeps it out");
        depths[corner] = -640.0f;
        expect(psx_world_depth_class(1, kExact, depths, kMargin) == 0, "so does a depth meant for the mirror alone, which is negative");
        depths[corner] = std::numeric_limits<float>::quiet_NaN();
        expect(psx_world_depth_class(1, kExact, depths, kMargin) == 0, "and a depth that is no number");
    }
}

void a_near_face_is_classed_by_its_own_w() {
    const float pinned_w[3] = {216.0f, 688.0f, 792.0f}, at_viewer_w[3] = {12.0f, 688.0f, 792.0f};
    const float far_turned[3] = {200.0f, 640.0f, 3200.0f}, no_turned[3] = {0.0f, 0.0f, 0.0f};
    expect(psx_world_depth_triangle_class(1, kExact, 1, pinned_w, no_turned, kMargin) == 1,
           "a near face whose corner the game pinned, every w beyond the nearest depth, is drawn by depth");
    expect(psx_world_depth_triangle_class(1, kExact, 1, at_viewer_w, far_turned, kMargin) == 0,
           "a near face with a corner at the viewer is not, whatever its turned depths");
    expect(psx_world_depth_triangle_class(0, kExact, 1, pinned_w, far_turned, kMargin) == 0, "with the setting off no near face is");
    expect(psx_world_depth_triangle_class(1, kWorld, 1, pinned_w, far_turned, kMargin) == 0, "nor one that is not exact");
    expect(psx_world_depth_triangle_class(1, kExact, 0, no_turned, far_turned, kMargin) == 1,
           "any other polygon goes by its turned depths, not by a near face's w");
    expect(psx_world_depth_triangle_class(1, kExact, 0, pinned_w, no_turned, kMargin) == 0, "and without them it stays out");
}

void a_pulled_depth_stays_between_the_eye_and_the_far_end() {
    expect(psx_world_depth_stored(32.0f, 0.0f) == 0.0f && std::fabs(psx_world_depth_stored(3200.0f, 0.0f) - 0.99f) < 1e-6f,
           "a depth is stored as one less 32 over it");
    expect(psx_world_depth_stored(640.0f, kMargin) < psx_world_depth_stored(640.0f, 0.0f), "a pull brings a depth nearer");
    expect(std::fabs(psx_world_depth_stored(640.0f, kMargin) - psx_world_depth_stored(640.0f / 1.03f, 0.0f)) < 1e-6f,
           "by as much as the corner itself coming nearer by the margin");
    expect(psx_world_depth_stored(16.0f, kMargin) == -1.0f && psx_world_depth_stored(8.0f, 0.0f) == -1.0f,
           "nothing is stored nearer than the eye");
    for (const float margin : {0.0f, 0.01f, kMargin, 0.25f}) {
        const float depths[3] = {std::nextafter(psx_world_depth_nearest(margin), 100.0f), 64.0f, 4096.0f};
        expect(psx_world_depth_class(1, kExact, depths, margin) == 1, "a polygon just beyond the nearest depth is of the class");
        for (const float depth : depths)
            expect(psx_world_depth_stored(depth, margin) > -1.0f && psx_world_depth_stored(depth, margin) < 1.0f,
                   "and its pulled depth stays inside the clip range, which is what the nearest depth is for");
    }
}

}  // namespace

int main() {
    only_exact_polygons_beyond_the_nearest_depth_are_drawn_by_depth();
    a_near_face_is_classed_by_its_own_w();
    a_pulled_depth_stays_between_the_eye_and_the_far_end();
    std::puts("GPU world depth tests passed");
    return 0;
}
