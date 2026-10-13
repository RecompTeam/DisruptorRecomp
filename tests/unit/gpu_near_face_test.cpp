#include "gpu_near_face.h"

#include <cmath>
#include <cstdint>
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

bool close(double seen, double expected, double tolerance) { return std::fabs(seen - expected) <= tolerance; }

GtePrecisionView corner(double x, double y, int w, uint16_t focal = 160, double centre_x = 160.0, double centre_y = 120.0) {
    GtePrecisionView view{};
    view.hx = x * focal;
    view.hy = y * focal;
    view.centre_x = centre_x;
    view.centre_y = centre_y;
    view.w = w;
    view.focal = focal;
    return view;
}

struct Point {
    double x, y, z;
};

// A point of the level as the GTE projects it, with its depth: a focal length of 160, the centre at 160, 120.
void projected(const Point &point, float place[3]) {
    place[0] = static_cast<float>(160.0 + 160.0 * point.x / point.z);
    place[1] = static_cast<float>(120.0 + 160.0 * point.y / point.z);
    place[2] = static_cast<float>(point.z);
}

Point along(const Point &before, const Point &now, double share) {
    return {before.x + (now.x - before.x) * share, before.y + (now.y - before.y) * share, before.z + (now.z - before.z) * share};
}

void test_near_depth() {
    expect(psx_near_depth(80, 160), "twice the depth equal to the focal length is not where the divide stops");
    expect(!psx_near_depth(81, 160), "a depth the GTE divides in full is called near");
    expect(psx_near_depth(0, 160) && psx_near_depth(-500, 160), "the viewer's plane or what lies behind it is not near");
    expect(psx_near_depth(std::numeric_limits<int32_t>::min(), 65535) && !psx_near_depth(std::numeric_limits<int32_t>::max(), 65535),
           "the test wraps at the ends of the depth's range");
    expect(!psx_near_depth(1, 0) && psx_near_depth(0, 0), "without a focal length only the viewer's plane is near");
}

void test_near_polygon() {
    const int none[4] = {0, 0, 0, 0};
    const int last[4] = {0, 0, 0, 1};
    GtePrecisionView quad[4] = {corner(10, 10, 300), corner(20, 10, 300), corner(20, 30, 60), corner(10, 30, 300)};
    expect(psx_near_polygon(quad, none, 4) == 1, "a quad with a corner where the divide stops is not near");
    quad[2].w = 300;
    expect(!psx_near_polygon(quad, none, 4), "a quad the GTE divided in full is near");
    expect(psx_near_polygon(quad, last, 4) == 1, "a quad with a corner the game pinned is not near");
    expect(!psx_near_polygon(quad, last, 3), "a pinned corner past the count made a triangle near");
    quad[0].w = -40;
    expect(psx_near_polygon(quad, none, 3) == 1, "a triangle with a corner behind the viewer is not near");
    for (int other = 1; other < 4; ++other) {
        GtePrecisionView mixed[4] = {quad[0], quad[1], quad[2], quad[3]};
        mixed[other].focal = 200;
        expect(!psx_near_polygon(mixed, last, 4), "corners of two focal lengths are taken for one projection");
        mixed[other] = quad[other];
        mixed[other].centre_x = 161.0;
        expect(!psx_near_polygon(mixed, last, 4), "corners of two centres across are taken for one projection");
        mixed[other] = quad[other];
        mixed[other].centre_y = 119.0;
        expect(!psx_near_polygon(mixed, last, 4), "corners of two centres down are taken for one projection");
    }
    GtePrecisionView blind[3] = {corner(10, 10, 0, 0), corner(20, 10, 0, 0), corner(20, 30, 0, 0)};
    expect(!psx_near_polygon(blind, last, 3), "corners with no focal length are taken for a projection");
}

void test_near_place() {
    float place[3] = {0.0f, 0.0f, 0.0f};
    GtePrecisionView ahead = corner(40, -20, 80);
    psx_near_place(&ahead, 0.0, 0.0, place);
    expect(place[0] == 240.0f && place[1] == 80.0f && place[2] == 80.0f, "a corner ahead is not where the divide puts it");
    psx_near_place(&ahead, 5.5, -3.0, place);
    expect(place[0] == 245.5f && place[1] == 77.0f && place[2] == 80.0f, "the drawing offset does not move a corner");
    GtePrecisionView behind = corner(40, -20, -80);
    psx_near_place(&behind, 0.0, 0.0, place);
    expect(place[0] == 80.0f && place[1] == 160.0f && place[2] == -80.0f, "a corner behind is not mirrored through the centre with its own w");
    GtePrecisionView on_plane = corner(40, -20, 0);
    psx_near_place(&on_plane, 0.0, 0.0, place);
    expect(place[2] == -0.25f && place[0] == 160.0f - 40.0f * 160.0f * 4.0f && place[1] == 120.0f + 20.0f * 160.0f * 4.0f,
           "a corner on the viewer's plane does not count as a quarter behind it");
    const GtePrecisionView flat_on[3] = {corner(-500, 300, 0), corner(700, 300, 0), corner(0, -900, 0)};
    for (const GtePrecisionView &each : flat_on) {
        psx_near_place(&each, 0.0, 0.0, place);
        expect(place[2] < 0.0f, "a face wholly on the viewer's plane has a corner ahead, which the rasterizer would draw");
    }
    GtePrecisionView off_centre = corner(40, -20, 80, 160, 200.0, 100.0);
    psx_near_place(&off_centre, 0.0, 0.0, place);
    expect(place[0] == 280.0f && place[1] == 60.0f, "the projection's own centre is not used");
}

// A corner between two frames stands where its point of the level does, moved straight in space.
void test_between_is_a_straight_line_in_space() {
    const Point before{-30.0, 12.0, 15.0}, now{-90.0, 40.0, 93.0};
    for (const double share : {0.0, 0.02, 0.3, 0.5, 0.98, 1.0}) {
        float start[3], place[3], other[3], expected[3];
        projected(before, start);
        projected(now, place);
        projected(now, other);
        projected(along(before, now, share), expected);
        const double weight = before.z / now.z;
        psx_near_between(start[0] * weight, start[1] * weight, weight, share, place);
        expect(close(place[0], expected[0], 0.01) && close(place[1], expected[1], 0.01) && close(place[2], expected[2], 0.001),
               "a corner the camera carried is not on the straight line of its point");
        psx_near_towards(start, share, other);
        expect(close(other[0], expected[0], 0.01) && close(other[1], expected[1], 0.01) && close(other[2], expected[2], 0.001),
               "a corner paired with its own place before is not on the straight line of its point");
    }
}

void test_between_across_the_viewers_plane() {
    const Point before{20.0, -10.0, -30.0}, now{50.0, 5.0, 60.0};
    for (const double share : {0.0, 0.2, 0.5, 0.9, 1.0}) {
        float start[3], place[3], expected[3];
        projected(before, start);
        projected(now, place);
        projected(along(before, now, share), expected);
        psx_near_towards(start, share, place);
        expect(close(place[0], expected[0], 0.05) && close(place[1], expected[1], 0.05) && close(place[2], expected[2], 0.001),
               "a corner that came from behind the viewer is not on the straight line of its point");
        expect((place[2] > 0.0f) == (share > 1.0 / 3.0), "the corner's w does not change sign where its point crosses the viewer's plane");
    }
    float start[3], place[3];
    projected({20.0, -10.0, -30.0}, start);
    projected({50.0, 5.0, 30.0}, place);
    psx_near_towards(start, 0.5, place);
    expect(std::isfinite(place[0]) && std::isfinite(place[1]) && place[2] < 0.0f,
           "a corner on the viewer's plane between frames has no finite place, or counts as ahead");
}

// A face left out is three corners at one place: with unlike w, one of them under zero, the rasterizer drew it over the whole picture.
void test_hide() {
    float corners[3][3] = {{317.3f, 124.8f, 264.0f}, {-1704.6f, 1350.8f, -26.0f}, {2955.3f, 609.4f, 17.0f}};
    for (float *corner : corners) psx_near_hide(corner);
    for (int i = 1; i < 3; ++i)
        for (int part = 0; part < 3; ++part)
            expect(corners[i][part] == corners[0][part], "the corners of a face left out differ in place or in w");
    expect(corners[0][2] > 0.0f, "a face left out has no w ahead of the viewer");
}

// Over the scene a dropped near face shows only where it is nearer by a clearance: its w grows, its place on the screen stays.
void test_farther() {
    float ahead[3] = {317.3f, 124.8f, 264.0f}, behind[3] = {-1704.6f, 1350.8f, -26.0f}, left_out[3] = {5.0f, 6.0f, 7.0f};
    psx_near_farther(ahead, 1.03f);
    psx_near_farther(behind, 1.03f);
    expect(ahead[0] == 317.3f && ahead[1] == 124.8f && behind[0] == -1704.6f && behind[1] == 1350.8f,
           "a corner handed over farther left its projected place");
    expect(ahead[2] == 264.0f * 1.03f && behind[2] == -26.0f * 1.03f && behind[2] < 0.0f,
           "a corner handed over farther does not have its w times the clearance, or changed its side of the viewer");
    float corners[3][3] = {{317.3f, 124.8f, 264.0f}, {-1704.6f, 1350.8f, -26.0f}, {2955.3f, 609.4f, 17.0f}};
    for (float *corner : corners) {
        psx_near_hide(corner);
        psx_near_farther(corner, 1.03f);
    }
    psx_near_hide(left_out);
    psx_near_farther(left_out, 1.03f);
    for (const float *corner : corners)
        for (int part = 0; part < 3; ++part)
            expect(corner[part] == left_out[part], "a face left out and handed over farther is no longer one point with one w");
}

void test_turn() {
    expect(psx_near_turn(0.5f) == 0.5f && psx_near_turn(-0.02f) == -0.02f && psx_near_turn(0.01f) == 0.01f && psx_near_turn(-0.01f) == -0.01f,
           "a denominator a hundredth or more from zero is changed");
    expect(psx_near_turn(0.0f) == 0.01f && psx_near_turn(0.004f) == 0.01f && psx_near_turn(-0.004f) == -0.01f,
           "a denominator near zero does not keep a hundredth and its sign");
}

void test_find() {
    const int step[3] = {2, 16, 80};
    const PsxNearCorner own[5] = {
        {1, {100, -20, 300}, {}}, {4, {110, -20, 300}, {}}, {9, {120, -20, 300}, {}}, {16, {130, -20, 300}, {}}, {25, {140, -20, 300}, {}}};
    PsxNearCorner other[5];
    for (int i = 0; i < 5; ++i)
        other[i] = {40 + i, {static_cast<int16_t>(own[i].model[0] + 2), -4, 380}, {160.0f + static_cast<float>(i), 120.0f, 100.0f}};
    float place[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < 5; ++i)
        expect(psx_near_find(own, 5, own[i].vertex, other, 5, step, 1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[0] == 160.0f + static_cast<float>(i) &&
                   place[1] == 120.0f && place[2] == 100.0f,
               "a corner is not found by its vertex and its model a step on");
    expect(!psx_near_find(own, 5, 5, other, 5, step, 1, 80.0, 60.0, 0.5, 200.0, place) && !psx_near_find(own, 0, 4, other, 5, step, 1, 80.0, 60.0, 0.5, 200.0, place) &&
           !psx_near_find(own, 5, 4, other, 0, step, 1, 80.0, 60.0, 0.5, 200.0, place),
           "a vertex outside the table, or an empty table, gives a pair");
    expect(!psx_near_find(own, 5, 4, other, 5, step, -1, 80.0, 60.0, 0.5, 200.0, place), "a step is taken the wrong way");
    const PsxNearCorner later[1] = {{7, {114, 12, 460}, {}}};
    expect(psx_near_find(later, 1, 7, other, 5, step, -1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[0] == 161.0f, "a step back does not find the corner");
    expect(!psx_near_find(own, 5, 4, other, 5, step, 1, 900.0 * 0.5, 60.0, 0.5, 200.0, place), "a corner that stands elsewhere than the camera expects is its pair");

    // Rooms repeat a model's numbers: the look-alike 10 pixels off must lose to the corner where the camera expects it, in either order.
    const PsxNearCorner exact = {50, {112, -4, 380}, {160.0f, 120.0f, 100.0f}}, alike = {51, {112, -4, 380}, {170.0f, 120.0f, 100.0f}};
    const PsxNearCorner first_alike[2] = {alike, exact}, first_exact[2] = {exact, alike};
    expect(psx_near_find(own, 5, 4, first_alike, 2, step, 1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[0] == 160.0f,
           "a look-alike that comes first in the table is taken for the corner");
    expect(psx_near_find(own, 5, 4, first_exact, 2, step, 1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[0] == 160.0f,
           "a look-alike that comes last in the table is taken for the corner");
    expect(psx_near_miss(alike.place, 80.0, 60.0, 0.5, 200.0) == 10.0 && psx_near_miss(exact.place, 80.0, 60.0, 0.5, 200.0) == 0.0,
           "the distance of a pair is not its pixels and depth together");

    const PsxNearCorner deeper = {52, {112, -4, 380}, {160.0f, 120.0f, 110.0f}};
    const PsxNearCorner first_deeper[2] = {deeper, exact}, last_deeper[2] = {exact, deeper};
    expect(psx_near_find(own, 5, 4, first_deeper, 2, step, 1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[2] == 100.0f &&
               psx_near_find(own, 5, 4, last_deeper, 2, step, 1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[2] == 100.0f &&
               psx_near_miss(deeper.place, 80.0, 60.0, 0.5, 200.0) == 10.0,
           "a look-alike at the same place and another depth is taken for the corner");

    // A corner of another model at the exact place must not win over the right model a little off.
    for (int axis = 0; axis < 3; ++axis) {
        PsxNearCorner wrong = exact;
        wrong.model[axis] = static_cast<int16_t>(wrong.model[axis] + 1);
        const PsxNearCorner mixed[2] = {wrong, alike};
        expect(psx_near_find(own, 5, 4, mixed, 2, step, 1, 80.0, 60.0, 0.5, 200.0, place) == 1 && place[0] == 170.0f,
               "a corner of another point of the level is taken for the pair");
    }
}

void test_agrees() {
    const float found[3] = {400.0f, 60.0f, 100.0f};
    expect(psx_near_agrees(found, 400.0 * 0.5, 60.0 * 0.5, 0.5, 200.0), "a corner where the camera expects it is refused");
    expect(psx_near_agrees(found, 400.0 * 0.5, 60.0 * 0.5, 0.5, 268.0) == 1 && !psx_near_agrees(found, 400.0 * 0.5, 60.0 * 0.5, 0.5, 274.0),
           "the depth may be short of the expected one by a fifth of it and eight units and no more");
    expect(psx_near_agrees(found, 400.0 * 0.5, 60.0 * 0.5, 0.5, 156.0) == 1 && !psx_near_agrees(found, 400.0 * 0.5, 60.0 * 0.5, 0.5, 150.0),
           "the depth may be past the expected one by a fifth of it and eight units and no more");
    expect(!psx_near_agrees(found, 400.0 * -0.5, 60.0 * -0.5, -0.5, 200.0), "a corner ahead is paired with one expected behind");
    const float shallow[3] = {400.0f, 60.0f, 5.0f};
    expect(psx_near_agrees(shallow, 400.0 * 0.015, 60.0 * 0.015, 0.015, 200.0) == 1 &&
           !psx_near_agrees(shallow, 400.0 * -0.015, 60.0 * -0.015, -0.015, 200.0),
           "a corner just ahead is paired with one expected just behind, inside the depth's tolerance");
    const float behind[3] = {400.0f, 60.0f, -100.0f};
    expect(psx_near_agrees(behind, 400.0 * -0.5, 60.0 * -0.5, -0.5, 200.0), "a corner behind where the camera expects it is refused");
    expect(psx_near_agrees(found, (400.0 + 76.0) * 0.5, 60.0 * 0.5, 0.5, 200.0) == 1 &&
           !psx_near_agrees(found, (400.0 + 100.0) * 0.5, 60.0 * 0.5, 0.5, 200.0),
           "the place may differ by a fifth of its distance from the centre and sixteen pixels and no more");
    const float centre[3] = {160.0f, 120.0f, 100.0f};
    expect(psx_near_agrees(centre, 170.0 * 0.5, 129.0 * 0.5, 0.5, 200.0) == 1 && !psx_near_agrees(centre, 170.0 * 0.5, 132.0 * 0.5, 0.5, 200.0),
           "near the centre the place may differ by sixteen pixels and a fifth of its distance and no more");
    expect(!psx_near_agrees(found, 0.0, 0.0, 0.0, 200.0), "a corner with no weight is paired");
    expect(!psx_near_agrees(found, std::numeric_limits<double>::quiet_NaN(), 30.0, 0.5, 200.0) &&
           !psx_near_agrees(found, 200.0, 30.0, 0.5, std::numeric_limits<double>::infinity()),
           "a corner the camera places nowhere is paired");
}

void test_identity_mark() {
    expect((PSX_NEAR_IDENTITY & UINT64_C(0x8000FFFFFFFFFFFF)) == 0u, "the mark of a view's level point covers the point's own bits");
    expect(PSX_NEAR_IDENTITY != 0u && (PSX_NEAR_IDENTITY & (PSX_NEAR_IDENTITY - 1u)) == 0u, "the mark is not one bit");
}

}  // namespace

int main() {
    test_near_depth();
    test_near_polygon();
    test_near_place();
    test_between_is_a_straight_line_in_space();
    test_between_across_the_viewers_plane();
    test_hide();
    test_farther();
    test_turn();
    test_agrees();
    test_find();
    test_identity_mark();
    std::puts("PASS: near faces: the class, a corner's place and w, the way between frames, the turn, pairing by the camera");
    return 0;
}
