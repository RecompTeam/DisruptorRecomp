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

struct Point {
    double x, y, z;
};

float screen_x(const Point &point) { return static_cast<float>(160.0 + 160.0 * point.x / point.z); }
float screen_y(const Point &point) { return static_cast<float>(120.0 + 160.0 * point.y / point.z); }

GpuTemporalTriangle placed(const std::array<Point, 3> &points) {
    auto face = triangle();
    for (int vertex = 0; vertex < 3; ++vertex) {
        face.x[vertex] = screen_x(points[vertex]);
        face.y[vertex] = screen_y(points[vertex]);
        face.depth[vertex] = static_cast<float>(points[vertex].z);
    }
    return face;
}

GpuTemporalTriangle weighted(const std::array<Point, 3> &points, const std::array<Point, 3> &other) {
    auto face = placed(other);
    for (int vertex = 0; vertex < 3; ++vertex) {
        face.w[vertex] = static_cast<float>(points[vertex].z / other[vertex].z);
        face.x[vertex] = screen_x(points[vertex]) * face.w[vertex];
        face.y[vertex] = screen_y(points[vertex]) * face.w[vertex];
    }
    return face;
}

bool close(double value, double expected) { return std::fabs(value - expected) <= 0.001 * std::fabs(expected); }

void interpolated_depth() {
    const Point a_then{-50.0, 10.0, 600.0}, b_then{350.0, -20.0, 900.0}, c_then{-60.0, 110.0, 610.0};
    const Point a_now{-100.0, 0.0, 200.0}, b_now{300.0, 0.0, 800.0}, c_now{-100.0, 100.0, 200.0};
    const Point m_then{(a_then.x + b_then.x) / 2, (a_then.y + b_then.y) / 2, (a_then.z + b_then.z) / 2};
    const Point m_now{(a_now.x + b_now.x) / 2, (a_now.y + b_now.y) / 2, (a_now.z + b_now.z) / 2};
    const std::array<Point, 3> wall_then{a_then, b_then, c_then}, wall_now{a_now, b_now, c_now};
    const std::array<Point, 3> piece_then{a_then, m_then, c_then}, piece_now{a_now, m_now, c_now};
    const auto wall = placed(wall_now), piece = placed(piece_now);
    const auto wall_from = weighted(wall_then, wall_now), piece_from = weighted(piece_then, piece_now);
    GpuTemporalTriangle out{}, part{};
    for (const float alpha : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) {
        gpu_temporal_lerp(&wall_from, &wall, alpha, &out);
        for (int vertex = 0; vertex < 3; ++vertex)
            expect(close(gpu_temporal_lerp_depth(&wall, &out, vertex),
                         wall_then[vertex].z + (wall_now[vertex].z - wall_then[vertex].z) * alpha),
                   "a corner placed from its earlier weight is as deep as it is on the way");
        const auto start = placed(wall_then), gone_to = weighted(wall_now, wall_then);
        auto end = gone_to;
        for (int vertex = 0; vertex < 3; ++vertex) end.depth[vertex] = static_cast<float>(wall_now[vertex].z);
        gpu_temporal_lerp(&start, &end, alpha, &out);
        for (int vertex = 0; vertex < 3; ++vertex)
            expect(close(gpu_temporal_lerp_depth(&end, &out, vertex),
                         wall_then[vertex].z + (wall_now[vertex].z - wall_then[vertex].z) * alpha),
                   "a departed corner placed toward its later weight is as deep as it is on the way");
    }
    gpu_temporal_lerp(&wall_from, &wall, 0.5f, &out);
    gpu_temporal_lerp(&piece_from, &piece, 0.5f, &part);
    const double edge_x = out.x[1] - out.x[0], edge_y = out.y[1] - out.y[0];
    const double along = ((part.x[1] - out.x[0]) * edge_x + (part.y[1] - out.y[0]) * edge_y) / (edge_x * edge_x + edge_y * edge_y);
    const double off = (part.x[1] - out.x[0]) * edge_y - (part.y[1] - out.y[0]) * edge_x;
    expect(std::fabs(off) < 0.01 * std::sqrt(edge_x * edge_x + edge_y * edge_y) && along > 0.0 && along < 1.0,
           "a corner in the middle of a wall's edge stays on that edge between the frames");
    const double wall_there = 1.0 / ((1.0 - along) / gpu_temporal_lerp_depth(&wall, &out, 0) +
                                     along / gpu_temporal_lerp_depth(&wall, &out, 1));
    expect(close(wall_there, gpu_temporal_lerp_depth(&piece, &part, 1)),
           "a wall and the piece drawn over it are equally deep where they meet between the frames");
    const double stale = 1.0 / ((1.0 - along) / wall.depth[0] + along / wall.depth[1]);
    expect(std::fabs(stale - piece.depth[1]) > 0.03 * piece.depth[1],
           "the depths of the later frame alone would part the wall from its piece by more than the margin");
    const auto still = triangle(), moved = triangle(8.0f, -6.0f);
    auto deep = moved;
    for (float &depth : deep.depth) depth = 300.0f;
    gpu_temporal_lerp(&still, &deep, 0.5f, &out);
    expect(gpu_temporal_lerp_depth(&deep, &out, 0) == 300.0f, "a corner without a weight keeps the depth it ends at");
    auto behind = wall_from;
    behind.w[0] = -0.5f;
    gpu_temporal_lerp(&behind, &wall, 0.2f, &out);
    expect(out.w[0] < 0.0f && gpu_temporal_lerp_depth(&wall, &out, 0) == wall.depth[0],
           "a corner not yet in front of the viewer keeps the depth it ends at");
    behind.w[0] = -1.0f;
    gpu_temporal_lerp(&behind, &wall, 0.5f, &out);
    expect(out.w[0] < 0.0f && gpu_temporal_lerp_depth(&wall, &out, 0) == wall.depth[0],
           "a weight that comes to nothing on the way is not read as a corner without a weight");
    auto leaving = placed(wall_then);
    leaving.w[0] = -0.5f;
    leaving.depth[0] = wall.depth[0] * leaving.w[0];
    gpu_temporal_lerp(&wall, &leaving, 0.2f, &out);
    expect(close(gpu_temporal_lerp_depth(&leaving, &out, 0), wall.depth[0] + (leaving.depth[0] - wall.depth[0]) * 0.2),
           "a departed corner on its way behind the viewer is as deep as it is on the way");
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
    interpolated_depth();
    scale_and_fallback();
    if (failures) return 1;
    std::cout << "GPU temporal geometry matching tests passed\n";
    return 0;
}
