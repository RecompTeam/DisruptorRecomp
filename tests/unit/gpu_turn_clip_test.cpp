#include "gpu_turn_clip.h"
#include "gpu_turned_perspective.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr float kCenter = 160.0f;
constexpr float kFocal = 120.0f;
constexpr double kStep = 6.28318530717958647692 / 256.0;

float denominator(float x, float yaw_sin, float yaw_cos) { return yaw_cos - (x - kCenter) / kFocal * yaw_sin; }

double area(const PsxTurnCorner *corners, int count) {
    double twice = 0.0;
    for (int i = 0; i < count; ++i) {
        const PsxTurnCorner &a = corners[i], &b = corners[(i + 1) % count];
        twice += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return std::fabs(twice) / 2.0;
}

void band() {
    float low = 0.0f, high = 0.0f;
    expect(!psx_turn_band(0.0f, 1.0f, kCenter, kFocal, &low, &high), "no turn has no band");
    expect(!psx_turn_band(0.01f, 1.0f, kCenter, 0.0f, &low, &high), "a picture without a focal length has no band");
    for (const double steps : {0.48, -0.48, 0.05, -1.0}) {
        const float yaw_sin = static_cast<float>(std::sin(steps * kStep)), yaw_cos = static_cast<float>(std::cos(steps * kStep));
        expect(psx_turn_band(yaw_sin, yaw_cos, kCenter, kFocal, &low, &high) && low < kCenter && kCenter < high,
               "a turn has a band around the picture's centre");
        const float at_low = denominator(low, yaw_sin, yaw_cos), at_high = denominator(high, yaw_sin, yaw_cos);
        const float near_edge = steps > 0 ? at_high : at_low, far_edge = steps > 0 ? at_low : at_high;
        expect(std::fabs(near_edge - PSX_TURN_CLIP_NEAR) < 0.001f && std::fabs(far_edge - PSX_TURN_CLIP_FAR) < 0.001f,
               "the band ends where the denominator reaches its bounds, the low bound on the side the view turns to");
    }
    const float yaw_sin = static_cast<float>(std::sin(4.0 * kStep)), yaw_cos = static_cast<float>(std::cos(4.0 * kStep));
    expect(psx_turn_band(yaw_sin, yaw_cos, kCenter, kFocal, &low, &high) && low == kCenter - kFocal * PSX_TURN_CLIP_PICTURE &&
               high == kCenter + kFocal * PSX_TURN_CLIP_PICTURE,
           "under the largest turn the band is still the picture's own width and more");
    expect(denominator(low, yaw_sin, yaw_cos) <= 2.0f && denominator(high, yaw_sin, yaw_cos) >= 0.5f,
           "and the turn is trusted to its ends");
}

void whole_and_gone() {
    const float x[3] = {10.0f, 300.0f, 150.0f}, y[3] = {20.0f, 40.0f, 200.0f};
    PsxTurnCorner out[PSX_TURN_CLIP_MAX];
    expect(psx_turn_clip(x, y, -1000.0f, 1000.0f, out) == 3, "a triangle inside the band keeps its three corners");
    for (int i = 0; i < 3; ++i)
        expect(out[i].x == x[i] && out[i].y == y[i] && out[i].share[i] == 1.0f && out[i].share[(i + 1) % 3] == 0.0f &&
                   out[i].share[(i + 2) % 3] == 0.0f,
               "an uncut corner is the triangle's own");
    expect(psx_turn_clip(x, y, 400.0f, 1000.0f, out) == 0 && psx_turn_clip(x, y, -500.0f, 0.0f, out) == 0,
           "a triangle outside the band leaves nothing");
    const float broken[3] = {10.0f, INFINITY, 150.0f};
    expect(psx_turn_clip(broken, y, -1000.0f, 1000.0f, out) == 0, "a corner without a place leaves nothing");
}

void one_side() {
    const float x[3] = {0.0f, 100.0f, 0.0f}, y[3] = {0.0f, 0.0f, 100.0f};
    PsxTurnCorner out[PSX_TURN_CLIP_MAX];
    const int count = psx_turn_clip(x, y, -1000.0f, 50.0f, out);
    expect(count == 4, "a corner past one column leaves four");
    expect(std::fabs(area(out, count) - 3750.0) < 0.01, "what is left is the part of the triangle inside the band");
    int own = 0;
    for (int i = 0; i < count; ++i) {
        expect(out[i].x <= 50.0f, "no corner stays past the column");
        own += out[i].share[0] == 1.0f || out[i].share[2] == 1.0f;
    }
    expect(own == 2, "the two corners inside stay the triangle's own");
    const float colour[3] = {0.0f, 1.0f, 0.5f};
    for (int i = 0; i < count; ++i)
        if (out[i].x == 50.0f && out[i].y == 0.0f)
            expect(std::fabs(psx_turn_mix(&out[i], colour) - 0.5f) < 0.0001f, "a cut corner takes its values from the edge it lies on");
    expect(psx_turn_clip(x, y, 50.0f, 1000.0f, out) == 3 && std::fabs(area(out, 3) - 1250.0) < 0.01,
           "two corners past a column leave the third with two cuts");
    const float wide_x[3] = {0.0f, 100.0f, 40.0f}, wide_y[3] = {0.0f, 0.0f, 100.0f};
    expect(psx_turn_clip(wide_x, wide_y, 20.0f, 60.0f, out) == 5 && std::fabs(area(out, 5) - 3166.667) < 0.01,
           "a triangle across both columns with a corner between them leaves five");
}

void depths() {
    const PsxTurnCorner own{0.0f, 0.0f, {0.0f, 1.0f, 0.0f}}, cut{0.0f, 0.0f, {0.5f, 0.25f, 0.25f}};
    const float deep[3] = {100.0f, 400.0f, 50.0f}, mirror[3] = {100.0f, -400.0f, 50.0f}, none[3] = {100.0f, 400.0f, 0.0f};
    const float shallow[3] = {100.0f, 400.0f, 16.0f};
    expect(psx_turn_own(&own) == 1 && psx_turn_own(&cut) == -1, "a corner is the triangle's own when it has all of one share");
    expect(psx_turn_depth(&own, deep) == 400.0f && psx_turn_depth(&own, mirror) == -400.0f && psx_turn_depth(&own, none) == 400.0f,
           "a corner of the triangle keeps its depth as it is");
    const float near_own[3] = {100.0f, 10.0f, 50.0f};
    expect(psx_turn_depth(&own, near_own) == 10.0f, "even a depth the mirror takes as none");
    const float between = psx_turn_depth(&cut, deep);
    expect(std::fabs(between - 1.0f / (0.5f / 100.0f + 0.25f / 400.0f + 0.25f / 50.0f)) < 0.001f, "a cut's depth mixes the reciprocals");
    float stored = 0.0f;
    for (int i = 0; i < 3; ++i) stored += cut.share[i] * (1.0f - 32.0f / deep[i]);
    expect(std::fabs((1.0f - 32.0f / between) - stored) < 0.00001f, "so the depth the mirror stores stays one plane across the pieces");
    expect(psx_turn_depth(&cut, mirror) == -between, "beside a depth that is the mirror's alone a cut's depth is the mirror's alone too");
    expect(psx_turn_depth(&cut, none) == 0.0f && psx_turn_depth(&cut, shallow) == 0.0f,
           "a cut with a share of a corner without a depth has none");
    const PsxTurnCorner edge{0.0f, 0.0f, {0.5f, 0.5f, 0.0f}};
    const float beside[3] = {100.0f, 400.0f, -100.0f};
    expect(std::fabs(psx_turn_depth(&edge, none) - 160.0f) < 0.001f && std::fabs(psx_turn_depth(&edge, beside) - 160.0f) < 0.001f,
           "a cut on an edge takes its depth from that edge's two corners, whatever the third has");
}

std::uint32_t next(std::uint32_t &state) {
    state = state * 1664525u + 1013904223u;
    return state >> 8;
}

void many() {
    std::uint32_t state = 20261012u;
    int cut = 0;
    for (int round = 0; round < 20000; ++round) {
        float x[3], y[3];
        for (int i = 0; i < 3; ++i) {
            x[i] = static_cast<float>(next(state) % 40000u) - 20000.0f;
            y[i] = static_cast<float>(next(state) % 2000u) - 1000.0f;
        }
        const double steps = (static_cast<double>(next(state) % 2000u) - 1000.0) / 1000.0;
        const float yaw_sin = static_cast<float>(std::sin(steps * kStep)), yaw_cos = static_cast<float>(std::cos(steps * kStep));
        float low = 0.0f, high = 0.0f;
        if (!psx_turn_band(yaw_sin, yaw_cos, kCenter, kFocal, &low, &high)) continue;
        PsxTurnCorner out[PSX_TURN_CLIP_MAX], whole[3];
        const int count = psx_turn_clip(x, y, low, high, out);
        for (int i = 0; i < 3; ++i) whole[i] = {x[i], y[i], {0.0f, 0.0f, 0.0f}};
        bool sound = count == 0 || (count >= 3 && count <= PSX_TURN_CLIP_MAX);
        for (int i = 0; sound && i < count; ++i) {
            const PsxTurnCorner &corner = out[i];
            const float sum = corner.share[0] + corner.share[1] + corner.share[2];
            const float den = denominator(corner.x, yaw_sin, yaw_cos);
            sound = std::fabs(sum - 1.0f) < 0.0001f && corner.share[0] >= 0.0f && corner.share[1] >= 0.0f && corner.share[2] >= 0.0f &&
                    std::fabs(psx_turn_mix(&corner, x) - corner.x) < 0.02f + 0.00001f * std::fabs(corner.x) &&
                    std::fabs(psx_turn_mix(&corner, y) - corner.y) < 0.02f && den >= 0.5f && den <= 2.0f &&
                    corner.x >= low && corner.x <= high;
        }
        expect(sound, "every corner left lies in the triangle, inside the band, where the turn is trusted");
        if (count) expect(area(out, count) <= area(whole, 3) * 1.0001 + 0.01, "what is left is no larger than the triangle");
        cut += count > 3 || count == 0;
        for (int piece = 0; piece + 2 < count; ++piece) {
            const float px[3] = {out[0].x, out[piece + 1].x, out[piece + 2].x};
            float q[3] = {0.25f, 0.5f, 1.0f};
            psx_turned_perspective(px, q, yaw_sin, yaw_cos, kCenter, kFocal);
            expect(q[0] > 0.0f && q[1] > 0.0f && q[2] > 0.0f, "a piece keeps its perspective under the turn");
        }
    }
    expect(cut > 1000, "the rounds cut many triangles");
}

} // namespace

int main() {
    band();
    whole_and_gone();
    one_side();
    depths();
    many();
    if (failures) return 1;
    std::cout << "GPU turn clip tests passed\n";
    return 0;
}
