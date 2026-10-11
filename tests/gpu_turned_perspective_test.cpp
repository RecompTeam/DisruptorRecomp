#include "gpu_turned_perspective.h"

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

constexpr float kCenter = 480.0f, kFocal = 240.0f;
constexpr double kQuarterStep = 3.14159265358979323846 * 2.0 / 256.0 / 4.0;

bool close(float actual, double expected) { return std::fabs(actual - expected) <= 1e-5 * std::fabs(expected); }

struct Corner { double x, z; };  // in the camera's units: x to the right, z ahead

float screen_x(const Corner &corner, float focal) { return kCenter + focal * static_cast<float>(corner.x / corner.z); }
float weight(const Corner &corner) { return static_cast<float>(16.0 / corner.z); }
double ahead_after(const Corner &corner, double turn) { return corner.z * std::cos(turn) - corner.x * std::sin(turn); }

void a_turn_gives_the_weights_of_the_turned_view() {
    const Corner corners[3] = {{-90.0, 200.0}, {120.0, 360.0}, {15.0, 64.0}};
    for (const float focal : {kFocal, 360.0f}) {
        for (const double turn : {kQuarterStep, -kQuarterStep, 2.0 * kQuarterStep}) {
            float px[3], pq[3];
            for (int i = 0; i < 3; ++i) {
                px[i] = screen_x(corners[i], focal);
                pq[i] = weight(corners[i]);
            }
            psx_turned_perspective(px, pq, static_cast<float>(std::sin(turn)), static_cast<float>(std::cos(turn)), kCenter, focal);
            for (int i = 0; i < 3; ++i)
                expect(close(pq[i], 16.0 / ahead_after(corners[i], turn)),
                       "a corner's weight is one over its depth along the turned view, whatever the focal length");
        }
    }
}

void no_turn_and_no_focal_length_change_nothing() {
    const float px[3] = {200.0f, 480.0f, 900.0f};
    float pq[3] = {0.25f, 0.5f, 1.0f};
    psx_turned_perspective(px, pq, 0.0f, 1.0f, kCenter, kFocal);
    expect(pq[0] == 0.25f && pq[1] == 0.5f && pq[2] == 1.0f, "without a turn the weights stay");
    psx_turned_perspective(px, pq, 0.00000005f, 1.0f, kCenter, kFocal);
    expect(pq[0] == 0.25f && pq[1] == 0.5f && pq[2] == 1.0f, "a turn too small to count changes nothing");
    psx_turned_perspective(px, pq, 0.01f, 0.99995f, kCenter, 0.0f);
    expect(pq[0] == 0.25f && pq[1] == 0.5f && pq[2] == 1.0f, "without a focal length the weights stay");
    psx_turned_perspective(px, pq, 0.01f, 0.99995f, kCenter, -240.0f);
    expect(pq[0] == 0.25f && pq[1] == 0.5f && pq[2] == 1.0f, "a negative focal length is none");
}

bool all_affine(const float pq[3]) { return pq[0] == 0.0f && pq[1] == 0.0f && pq[2] == 0.0f; }

void a_corner_that_cannot_be_trusted_makes_the_whole_triangle_affine() {
    const float sine = 0.0245412f, cosine = 0.999699f;  // one whole step
    const float near_px[3] = {400.0f, 480.0f, 560.0f};
    {
        float pq[3] = {0.25f, 0.0f, 0.5f};
        psx_turned_perspective(near_px, pq, sine, cosine, kCenter, kFocal);
        expect(all_affine(pq), "a corner without a weight leaves no corner with one");
    }
    {
        float pq[3] = {0.25f, 0.5f, 1.5f};
        psx_turned_perspective(near_px, pq, sine, cosine, kCenter, kFocal);
        expect(all_affine(pq), "a weight above one is no weight");
    }
    {
        float pq[3] = {0.25f, -0.5f, 0.5f};
        psx_turned_perspective(near_px, pq, sine, cosine, kCenter, kFocal);
        expect(all_affine(pq), "a negative weight is no weight");
    }
    {
        float pq[3] = {0.25f, std::numeric_limits<float>::quiet_NaN(), 0.5f};
        psx_turned_perspective(near_px, pq, sine, cosine, kCenter, kFocal);
        expect(all_affine(pq), "a weight that is no number is no weight");
    }
    {
        // den = cosine - ux * sine: 0.49 at ux = 20.77, 2.01 at ux = -41.17.
        const float far_right[3] = {400.0f, 480.0f, kCenter + kFocal * 20.9f};
        float pq[3] = {0.25f, 0.5f, 0.5f};
        psx_turned_perspective(far_right, pq, sine, cosine, kCenter, kFocal);
        expect(all_affine(pq), "a corner so far aside that its depth halves is not trusted");
        const float far_left[3] = {kCenter - kFocal * 41.3f, 480.0f, 560.0f};
        float again[3] = {0.25f, 0.5f, 0.5f};
        psx_turned_perspective(far_left, again, sine, cosine, kCenter, kFocal);
        expect(all_affine(again), "nor one so far aside that its depth doubles");
    }
    {
        const float within[3] = {kCenter + kFocal * 20.3f, 480.0f, kCenter - kFocal * 40.7f};
        float pq[3] = {0.25f, 0.5f, 0.5f};
        psx_turned_perspective(within, pq, sine, cosine, kCenter, kFocal);
        expect(pq[0] > 0.0f && pq[1] > 0.0f && pq[2] > 0.0f, "corners just inside both limits keep their weights");
        expect(close(pq[0], 0.25 / (cosine - 20.3 * sine)) && close(pq[2], 0.5 / (cosine + 40.7 * sine)),
               "and those weights are divided by the turn's denominator");
    }
    {
        const float lost[3] = {400.0f, std::numeric_limits<float>::infinity(), 560.0f};
        float pq[3] = {0.25f, 0.5f, 0.5f};
        psx_turned_perspective(lost, pq, sine, cosine, kCenter, kFocal);
        expect(all_affine(pq), "a corner at no place leaves no weight behind");
        const float unknown[3] = {400.0f, 480.0f, std::numeric_limits<float>::quiet_NaN()};
        float again[3] = {0.25f, 0.5f, 0.5f};
        psx_turned_perspective(unknown, again, sine, cosine, kCenter, kFocal);
        expect(all_affine(again), "nor does a place that is no number");
    }
    {
        // A quarter turn with the corner two focal lengths left of the centre: the denominator is exactly 2.
        const float aside[3] = {-2.0f, -1.0f, -1.0f};
        float pq[3] = {std::numeric_limits<float>::denorm_min(), 1.0f, 1.0f};
        psx_turned_perspective(aside, pq, 1.0f, 0.0f, 0.0f, 1.0f);
        expect(all_affine(pq), "a weight so small that it turns into 0 is no weight");
        float kept[3] = {0.5f, 1.0f, 1.0f};
        psx_turned_perspective(aside, kept, 1.0f, 0.0f, 0.0f, 1.0f);
        expect(kept[0] == 0.25f && kept[1] == 1.0f && kept[2] == 1.0f, "while an ordinary weight there is halved");
    }
}

}  // namespace

int main() {
    a_turn_gives_the_weights_of_the_turned_view();
    no_turn_and_no_focal_length_change_nothing();
    a_corner_that_cannot_be_trusted_makes_the_whole_triangle_affine();
    std::puts("GPU turned perspective tests passed");
    return 0;
}
