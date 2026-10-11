#include "gpu_temporal_match.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

GpuTemporalTriangle triangle(float x = 0.0f, float y = 0.0f,
                             std::uint64_t material = 1) {
    return {material, {x, x + 8.0f, x}, {y, y, y + 8.0f},
            {0.1f, 0.05f, 0.025f}, 1};
}

bool single_match(const GpuTemporalTriangle &a, const GpuTemporalTriangle &b) {
    int match = -2;
    gpu_temporal_match(&a, 1, &b, 1, &match);
    return match == 0;
}

void reject(const GpuTemporalTriangle &a, const GpuTemporalTriangle &b,
            const char *message) {
    int match = -2;
    gpu_temporal_match(&a, 1, &b, 1, &match);
    expect(match == -1, message);
}

void interpolation() {
    const auto a = triangle();
    auto b = triangle(8.0f, -6.0f);
    for (float &q : b.q) q *= 2.0f;
    expect(single_match(a, b), "ordinary motion matches");
    GpuTemporalTriangle out{};
    gpu_temporal_lerp(&a, &b, 0.5f, &out);
    for (int vertex = 0; vertex < 3; ++vertex) {
        expect(out.x[vertex] == a.x[vertex] + 4.0f &&
                   out.y[vertex] == a.y[vertex] - 3.0f,
               "midpoint moves actual vertices");
        expect(std::fabs(out.q[vertex] - a.q[vertex] * 1.5f) < 0.000001f,
               "midpoint retains interpolated reciprocal depth");
    }
    gpu_temporal_lerp(&a, &b, -1.0f, &out);
    expect(out.x[0] == a.x[0] && out.q[0] == a.q[0],
           "negative alpha clamps to previous endpoint");
    gpu_temporal_lerp(&a, &b, 2.0f, &out);
    expect(out.x[0] == b.x[0] && out.q[0] == b.q[0],
           "alpha beyond one clamps to current endpoint");
    gpu_temporal_lerp(&a, &b, 0.0f, &out);
    expect(out.x[0] == a.x[0], "zero alpha is exact");
    gpu_temporal_lerp(&a, &b, 1.0f, &out);
    expect(out.x[0] == b.x[0], "one alpha is exact");
    gpu_temporal_lerp(&a, &b, std::numeric_limits<float>::quiet_NaN(), &out);
    expect(out.x[0] == b.x[0], "invalid alpha falls back to current");
    auto alias = a;
    gpu_temporal_lerp(&alias, &b, 0.5f, &alias);
    expect(alias.x[0] == 4.0f && alias.y[0] == -3.0f,
           "interpolation permits an aliased output");
}

void correspondence() {
    const std::array<GpuTemporalTriangle, 3> previous{
        triangle(-100.0f, 0.0f), triangle(100.0f, 0.0f), triangle(0.0f, 100.0f)};
    const std::array<GpuTemporalTriangle, 3> current{
        triangle(4.0f, 102.0f), triangle(-96.0f, 2.0f), triangle(104.0f, 2.0f)};
    std::array<int, 3> matches{};
    gpu_temporal_match(previous.data(), 3, current.data(), 3, matches.data());
    expect(matches == std::array<int, 3>{2, 0, 1},
           "material reuse matches spatially despite reordered commands");
    const std::array<GpuTemporalTriangle, 2> duplicates{triangle(), triangle(1.0f)};
    const auto one = triangle(2.0f);
    int match = -2;
    gpu_temporal_match(duplicates.data(), 2, &one, 1, &match);
    expect(match == -1, "two previous candidates are ambiguous");
    std::array<int, 2> reverse{};
    gpu_temporal_match(&one, 1, duplicates.data(), 2, reverse.data());
    expect(reverse == std::array<int, 2>{-1, -1},
           "two current candidates cannot share a previous triangle");
    expect(single_match(triangle(-33.0f), triangle(-1.0f)),
           "negative spatial cells support exact motion limit");
    reject(triangle(), triangle(24.0f, 24.0f),
           "diagonal motion uses Euclidean distance");
    reject(triangle(), triangle(32.01f), "teleport rejects correspondence");
}

void invalid_geometry() {
    const auto a = triangle();
    auto b = a;
    b.world = 0;
    reject(a, b, "HUD is not interpolated");
    reject(b, a, "previous HUD is not a world correspondence");
    b = a; b.material = 2;
    reject(a, b, "different materials cannot match");
    b = a; std::swap(b.x[1], b.x[2]); std::swap(b.y[1], b.y[2]);
    reject(a, b, "reversed winding is rejected");
    b = a; b.y[2] = 0.0f;
    reject(a, b, "degenerate triangle is rejected");
    b = a; b.x[1] = 32.0f;
    reject(a, b, "large area change is rejected");
    b = a; b.q[0] *= 2.01f;
    reject(a, b, "depth discontinuity is rejected");
    b = a; b.q[0] = 0.0f;
    reject(a, b, "partial affine sentinel is rejected");
    b = a; b.q[0] = -0.1f;
    reject(a, b, "negative reciprocal depth is rejected");
    for (int coordinate = 0; coordinate < 3; ++coordinate) {
        b = a;
        float *values = coordinate == 0 ? b.x : coordinate == 1 ? b.y : b.q;
        values[1] = std::numeric_limits<float>::quiet_NaN();
        reject(a, b, "NaN coordinate or depth is rejected");
        values[1] = std::numeric_limits<float>::infinity();
        reject(a, b, "infinite coordinate or depth is rejected");
    }
    b = a;
    for (int vertex = 0; vertex < 3; ++vertex) {
        b.x[vertex] = -a.x[vertex];
        b.y[vertex] = -a.y[vertex];
    }
    reject(a, b, "same winding endpoints cannot collapse between frames");
    auto affine_a = a, affine_b = triangle(2.0f);
    std::fill(std::begin(affine_a.q), std::end(affine_a.q), 0.0f);
    std::fill(std::begin(affine_b.q), std::end(affine_b.q), 0.0f);
    expect(single_match(affine_a, affine_b), "affine world geometry may match");
    reject(a, affine_b, "perspective-to-affine transition is rejected");
    GpuTemporalTriangle out{};
    gpu_temporal_lerp(&affine_a, &affine_b, 0.5f, &out);
    expect(out.q[0] == 0.0f && out.q[1] == 0.0f && out.q[2] == 0.0f,
           "affine interpolation retains the zero-depth sentinel");
}

void scale_and_fallback() {
    constexpr int count = GPU_TEMPORAL_MAX_TRIANGLES;
    std::vector<GpuTemporalTriangle> previous, current;
    std::vector<int> matches(count, -2);
    previous.reserve(count); current.reserve(count);
    for (int index = 0; index < count; ++index)
        previous.push_back(triangle(static_cast<float>(index % 128) * 80.0f,
                                    static_cast<float>(index / 128) * 80.0f));
    for (int index = count - 1; index >= 0; --index) {
        auto value = previous[index];
        for (float &x : value.x) x += 2.0f;
        current.push_back(value);
    }
    gpu_temporal_match(previous.data(), count, current.data(), count, matches.data());
    bool all_match = true;
    for (int index = 0; index < count; ++index)
        all_match = all_match && matches[index] == count - index - 1;
    expect(all_match, "8192 repeated-material triangles remain spatially scalable");
    std::fill(previous.begin(), previous.end(), triangle());
    std::fill(current.begin(), current.end(), triangle());
    gpu_temporal_match(previous.data(), count, current.data(), count, matches.data());
    expect(std::all_of(matches.begin(), matches.end(), [](int value) { return value == -1; }),
           "dense duplicate sets all fail closed");
    matches[0] = -2;
    gpu_temporal_match(nullptr, 0, current.data(), 1, matches.data());
    expect(matches[0] == -1, "missing history initializes current fallback");
    matches[0] = -2;
    gpu_temporal_match(previous.data(), count + 1, current.data(), 1, matches.data());
    expect(matches[0] == -1, "unsupported history size fails closed");
}

} // namespace

int main() {
    interpolation();
    correspondence();
    invalid_geometry();
    scale_and_fallback();
    if (failures) return 1;
    std::cout << "GPU temporal geometry matching tests passed\n";
    return 0;
}
