#include "gpu_temporal_mesh.h"

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

GpuTemporalTriangle first_triangle(float offset = 0.0f) {
    GpuTemporalTriangle t{};
    t.material = 1;
    t.world = 1;
    t.x[0] = offset; t.x[1] = offset + 8; t.x[2] = offset;
    t.y[0] = 0; t.y[1] = 0; t.y[2] = 8;
    t.depth[0] = 100; t.depth[1] = 200; t.depth[2] = 150;
    t.q[0] = 1; t.q[1] = 0.5f; t.q[2] = 100.0f / 150.0f;
    return t;
}

GpuTemporalTriangle second_triangle() {
    auto t = first_triangle();
    t.material = 2;
    t.x[0] = 0; t.x[1] = 8; t.x[2] = 8;
    t.y[0] = 8; t.y[1] = 0; t.y[2] = 8;
    t.depth[0] = 150; t.depth[1] = 200; t.depth[2] = 300;
    t.q[0] = 1; t.q[1] = 0.75f; t.q[2] = 0.5f;
    return t;
}

GpuTemporalTriangle translated(GpuTemporalTriangle t, float dx, float dy = 0) {
    for (int vertex = 0; vertex < 3; ++vertex) { t.x[vertex] += dx; t.y[vertex] += dy; }
    return t;
}

bool same_positions(const GpuTemporalTriangle &a, const GpuTemporalTriangle &b) {
    for (int vertex = 0; vertex < 3; ++vertex)
        if (a.x[vertex] != b.x[vertex] || a.y[vertex] != b.y[vertex]) return false;
    return true;
}

void expect_shared_edge(const std::array<GpuTemporalTriangle, 2> &from,
                        const std::array<GpuTemporalTriangle, 2> &current) {
    for (float alpha : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        std::array<GpuTemporalTriangle, 2> out{};
        for (int face = 0; face < 2; ++face)
            gpu_temporal_lerp(&from[face], &current[face], alpha, &out[face]);
        expect(out[0].x[1] == out[1].x[1] && out[0].y[1] == out[1].y[1] &&
                   out[0].x[2] == out[1].x[0] && out[0].y[2] == out[1].y[0],
               "all intermediate shared-edge endpoints coincide");
        if (alpha == 1.0f)
            expect(same_positions(out[0], current[0]) && same_positions(out[1], current[1]),
                   "alpha-one geometry remains exactly current");
    }
}

void half_matched_quad() {
    const std::array<GpuTemporalTriangle, 2> current{first_triangle(), second_triangle()};
    auto previous = translated(current[0], -4);
    for (float &q : previous.q) q *= 0.9f;
    const std::array<int, 2> matches{0, -1};
    std::array<GpuTemporalTriangle, 2> from{};
    GpuTemporalMeshStats stats{};
    expect(gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(),
                                      from.data(), &stats) == 2,
           "matched and unmatched neighboring faces retain motion");
    expect_shared_edge(from, current);
    expect(from[1].x[2] == current[1].x[2], "unseeded outer corner stays current");
    expect(from[0].q[1] == previous.q[1] && from[1].q[1] == current[1].q[1] &&
               from[0].q[1] != from[1].q[1],
           "differently normalized polygon q is never averaged across a shared vertex");
    expect(stats.seeded_vertices == 3 && stats.shared_vertices == 2 &&
               stats.propagated_vertices == 2 && stats.conflicting_vertices == 0,
           "statistics distinguish seeded groups and propagated unmatched corners");

    auto unknown = current;
    for (auto &triangle : unknown) std::fill(std::begin(triangle.depth), std::end(triangle.depth), 0.0f);
    auto unknown_previous = translated(unknown[0], -4);
    expect(gpu_temporal_mesh_prepare(&unknown_previous, 1, unknown.data(), 2, matches.data(),
                                      from.data(), &stats) == 2,
           "unknown depth uses a complete unique reversed shared edge");
    expect_shared_edge(from, unknown);
}

void depth_and_topology_guards() {
    auto a = first_triangle();
    auto previous = translated(a, -4);
    std::array<GpuTemporalTriangle, 2> current{a, a}, from{};
    current[1].material = 2;
    for (float &depth : current[1].depth) depth *= 2;
    const std::array<int, 2> matches{0, -1};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
    expect(!same_positions(from[0], current[0]) && same_positions(from[1], current[1]),
           "overlapping independent depth planes never share motion");

    current[1] = current[0];
    current[1].material = 2;
    std::fill(std::begin(current[0].depth), std::end(current[0].depth), 0.0f);
    std::fill(std::begin(current[1].depth), std::end(current[1].depth), 0.0f);
    previous = translated(current[0], -4);
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
    expect(same_positions(from[1], current[1]),
           "unknown depth does not weld overlapping faces with the same winding");

    current[1] = translated(current[0], 0, -8);
    /* A single coincident projected vertex is not an unknown-depth weld. */
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
    expect(same_positions(from[1], current[1]), "isolated XY equality cannot create adjacency");
}

void conflicts_and_order() {
    const std::array<GpuTemporalTriangle, 2> current{first_triangle(), second_triangle()};
    const std::array<GpuTemporalTriangle, 2> previous{
        translated(current[0], -4), translated(current[1], 4)};
    const std::array<int, 2> matches{0, 1};
    std::array<GpuTemporalTriangle, 2> from{};
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare(previous.data(), 2, current.data(), 2, matches.data(), from.data(), &stats);
    expect_shared_edge(from, current);
    expect(from[0].x[1] == current[0].x[1] && from[0].x[2] == current[0].x[2],
           "conflicting histories hold the same shared corners on both faces");
    expect(from[0].x[0] != current[0].x[0] && from[1].x[2] != current[1].x[2],
           "a conflict preserves independent neighboring motion");
    expect(stats.conflicting_vertices == 2, "conflicts count vertex groups");
    const std::array<GpuTemporalTriangle, 2> reordered_current{current[1], current[0]};
    const std::array<GpuTemporalTriangle, 2> reordered_previous{previous[1], previous[0]};
    std::array<GpuTemporalTriangle, 2> reordered_from{};
    gpu_temporal_mesh_prepare(reordered_previous.data(), 2, reordered_current.data(), 2,
                              matches.data(), reordered_from.data(), nullptr);
    expect(same_positions(from[0], reordered_from[1]) && same_positions(from[1], reordered_from[0]),
           "triangle submission order does not decide shared vertex history");
}

void invalid_and_hud() {
    const auto current = first_triangle();
    const auto previous = translated(current, -4);
    const int match = 0;
    GpuTemporalTriangle from{};
    for (float depth : {-1.0f, 65535.0f, std::numeric_limits<float>::infinity(),
                         std::numeric_limits<float>::quiet_NaN()}) {
        std::array<GpuTemporalTriangle, 2> overlap{current, current}, history{};
        for (auto &triangle : overlap)
            std::fill(std::begin(triangle.depth), std::end(triangle.depth), depth);
        overlap[1].material = 2;
        const std::array<int, 2> matches{0, -1};
        gpu_temporal_mesh_prepare(&previous, 1, overlap.data(), 2, matches.data(), history.data(), nullptr);
        expect(!same_positions(history[0], overlap[0]) && same_positions(history[1], overlap[1]),
               "invalid or saturated depth cannot create a known-depth point weld");
    }
    auto invalid = current;
    invalid.y[2] = 0;
    expect(gpu_temporal_mesh_prepare(&previous, 1, &invalid, 1, &match, &from, nullptr) == 0 &&
               same_positions(from, invalid), "degenerate geometry stays current");
    auto hud = current;
    hud.world = 0;
    expect(gpu_temporal_mesh_prepare(&previous, 1, &hud, 1, &match, &from, nullptr) == 0 &&
               same_positions(from, hud), "HUD geometry is unchanged");
    expect(gpu_temporal_mesh_prepare(nullptr, 0, &current, 1, &match, &from, nullptr) == 0 &&
               same_positions(from, current), "missing history initializes a safe current fallback");
}

void held_world_anchors() {
    const auto a = first_triangle();
    const auto previous = translated(a, -4);
    const std::array<int, 2> matches{0, -1};
    for (int kind = 0; kind < 3; ++kind) {
        std::array<GpuTemporalTriangle, 2> current{a, second_triangle()}, from{};
        if (kind < 2) {
            current[1].x[2] = 4;
            current[1].y[2] = kind == 0 ? 4.03f : 4;
        } else current[1].q[0] = 0;
        gpu_temporal_mesh_prepare(&previous, 1, current.data(), 2, matches.data(), from.data(), nullptr);
        expect(same_positions(from[1], current[1]),
               "tiny, degenerate and mixed-q world faces remain fixed anchors");
        expect(from[0].x[1] == current[0].x[1] && from[0].x[2] == current[0].x[2] &&
                   from[0].x[0] != current[0].x[0],
               "ineligible world faces hold shared groups while preserving other corner motion");
        expect_shared_edge(from, current);
    }
}

void safety_propagation() {
    const std::array<GpuTemporalTriangle, 3> current{
        first_triangle(), second_triangle(), first_triangle(100)};
    const std::array<GpuTemporalTriangle, 2> previous{
        translated(current[0], 20), translated(current[2], -4)};
    const std::array<int, 3> matches{0, -1, 1};
    std::array<GpuTemporalTriangle, 3> from{};
    GpuTemporalMeshStats stats{};
    expect(gpu_temporal_mesh_prepare(previous.data(), 2, current.data(), 3, matches.data(),
                                      from.data(), &stats) == 1,
           "unsafe local reconciliation preserves unrelated scene interpolation");
    expect(same_positions(from[0], current[0]) && same_positions(from[1], current[1]) &&
               !same_positions(from[2], current[2]),
           "winding safety pins and rechecks only incident vertex groups");
    expect(stats.held_vertices >= 3, "winding safeguards report held groups");
    auto reversed_current = current;
    std::reverse(reversed_current.begin(), reversed_current.end());
    const std::array<int, 3> reversed_matches{1, -1, 0};
    std::array<GpuTemporalTriangle, 3> reversed_from{};
    gpu_temporal_mesh_prepare(previous.data(), 2, reversed_current.data(), 3,
                              reversed_matches.data(), reversed_from.data(), nullptr);
    expect(same_positions(from[0], reversed_from[2]) && same_positions(from[1], reversed_from[1]) &&
               same_positions(from[2], reversed_from[0]),
           "cascading winding safeguards are independent of submission order");
}

void connected_and_scale() {
    std::array<GpuTemporalTriangle, 3> current{first_triangle(), second_triangle(), second_triangle()};
    current[2].x[0] = 0; current[2].y[0] = 8;
    current[2].x[1] = 8; current[2].y[1] = 8;
    current[2].x[2] = 0; current[2].y[2] = 16;
    current[2].depth[0] = 150; current[2].depth[1] = 300; current[2].depth[2] = 300;
    current[2].material = 3;
    const auto previous = translated(current[0], -2);
    const std::array<int, 3> matches{0, -1, -1};
    std::array<GpuTemporalTriangle, 3> from{};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), nullptr);
    expect(from[0].x[2] == from[1].x[0] && from[1].x[0] == from[2].x[0] && from[2].x[0] == -2,
           "one shared vertex history reaches every incident triangle");

    constexpr int count = GPU_TEMPORAL_MAX_TRIANGLES;
    std::vector<GpuTemporalTriangle> many_current, many_previous, many_from(count);
    std::vector<int> many_matches;
    for (int face = 0; face < count; ++face) {
        many_current.push_back(first_triangle(static_cast<float>(face) * 40));
        many_previous.push_back(translated(many_current.back(), -2));
        many_matches.push_back(face);
    }
    expect(gpu_temporal_mesh_prepare(many_previous.data(), count, many_current.data(), count,
                                      many_matches.data(), many_from.data(), nullptr) == count,
           "maximum scene capacity preserves independent smooth geometry");
}

void unknown_depth_bridge() {
    std::array<GpuTemporalTriangle, 3> current{first_triangle(), first_triangle(), second_triangle()};
    std::fill(std::begin(current[0].depth), std::end(current[0].depth), 0.0f);
    current[1].material = 2;
    current[1].x[0] = 8; current[1].y[0] = 0;
    current[1].x[1] = 0; current[1].y[1] = 0;
    current[1].x[2] = 8; current[1].y[2] = -8;
    std::fill(std::begin(current[1].depth), std::end(current[1].depth), 100.0f);
    current[2].material = 3;
    std::fill(std::begin(current[2].depth), std::end(current[2].depth), 200.0f);
    const auto previous = translated(current[1], -2);
    const std::array<int, 3> matches{-1, 0, -1};
    std::array<GpuTemporalTriangle, 3> from{};
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), &stats);
    expect(same_positions(from[2], current[2]),
           "unknown-depth triangle cannot transfer history between different known-depth planes");
    expect(from[1].x[2] != current[1].x[2],
           "ambiguous depth bridge does not stop independent motion on the known face");
    expect(stats.conflicting_vertices > 0, "transitive depth ambiguity is reported");

    const std::array<GpuTemporalTriangle, 2> both_previous{
        translated(current[1], -2), translated(current[2], 2)};
    const std::array<int, 3> both_matches{-1, 0, 1};
    gpu_temporal_mesh_prepare(both_previous.data(), 2, current.data(), 3, both_matches.data(),
                              from.data(), &stats);
    expect(from[0].x[1] == from[2].x[1] && from[0].y[1] == from[2].y[1] &&
               from[0].x[2] == from[2].x[0] && from[0].y[2] == from[2].y[0],
           "rejected whole-edge weld pins both endpoints on both incident faces");
    expect(from[2].x[2] != current[2].x[2],
           "whole-edge rejection still preserves the adjacent independent corner's motion");
}

void t_junctions() {
    std::array<GpuTemporalTriangle, 3> current{};
    for (int face = 0; face < 3; ++face) {
        current[face].world = 1;
        current[face].material = static_cast<std::uint64_t>(face + 1);
        std::fill(std::begin(current[face].q), std::end(current[face].q), 1.0f);
    }
    current[0].x[0] = 0; current[0].y[0] = 0; current[0].depth[0] = 100;
    current[0].x[1] = 8; current[0].y[1] = 0; current[0].depth[1] = 300;
    current[0].x[2] = 0; current[0].y[2] = -8; current[0].depth[2] = 200;
    current[1].x[0] = 0; current[1].y[0] = 0; current[1].depth[0] = 100;
    current[1].x[1] = 4; current[1].y[1] = 4; current[1].depth[1] = 200;
    current[1].x[2] = 4; current[1].y[2] = 0; current[1].depth[2] = 150;
    current[2].x[0] = 4; current[2].y[0] = 0; current[2].depth[0] = 150;
    current[2].x[1] = 4; current[2].y[1] = 4; current[2].depth[1] = 200;
    current[2].x[2] = 8; current[2].y[2] = 0; current[2].depth[2] = 300;
    const auto previous = translated(current[0], 0, -2);
    const std::array<int, 3> matches{0, -1, -1};
    std::array<GpuTemporalTriangle, 3> from{};
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), &stats);
    expect(stats.anchored_junctions == 1, "A-M/M-B plus A-B identifies a depth-confirmed T junction");
    expect(from[0].y[0] == 0 && from[0].y[1] == 0 && from[1].y[2] == 0 &&
               from[0].y[2] == -10,
           "T-junction anchors only its endpoints and midpoint, preserving other motion");
    for (float alpha : {0.25f, 0.5f, 0.75f}) {
        GpuTemporalTriangle long_edge{}, short_edge{};
        gpu_temporal_lerp(&from[0], &current[0], alpha, &long_edge);
        gpu_temporal_lerp(&from[1], &current[1], alpha, &short_edge);
        expect(short_edge.y[2] == long_edge.y[0] && long_edge.y[0] == long_edge.y[1],
               "intermediate T-junction midpoint stays on the long edge");
    }
    current[1].depth[2] = current[2].depth[0] = 400;
    gpu_temporal_mesh_prepare(&previous, 1, current.data(), 3, matches.data(), from.data(), &stats);
    expect(stats.anchored_junctions == 0 && from[0].y[0] == -2,
           "projected collinearity alone cannot anchor an unrelated depth plane");
}

struct Point {
    double x, y, z;
};

/* The previous camera as seen from the current one: a turn about the vertical
 * axis and a position in whole level units, as the game moves. */
struct View {
    double yaw;
    std::array<int, 3> step;
};
constexpr View kStep{0.03, {-6, 0, 12}};
constexpr View kFlick{0.40, {0, 0, 0}};
constexpr View kStride{0.20, {-40, 0, 80}};
constexpr double kGameFocal = 120.0;  /* 160 squashed to 16:9: the screen edge is 53 degrees off axis */
constexpr double kNearPlane = 50.0;

Point seen_before(const Point &p, const View &view, const std::array<int, 3> &moved) {
    const double c = std::cos(view.yaw), s = std::sin(view.yaw);
    const double x = p.x - view.step[0] - moved[0], y = p.y - view.step[1] - moved[1];
    const double z = p.z - view.step[2] - moved[2];
    return {c * x + s * z, y, -s * x + c * z};
}

GpuTemporalTriangle projected(const std::array<Point, 3> &corners, const std::array<Point, 3> &level,
                              const std::array<int, 3> &offset, std::uint64_t material,
                              double focal = 256.0) {
    GpuTemporalTriangle t{};
    t.material = material;
    t.world = 1;
    t.modelled = 1;
    for (int vertex = 0; vertex < 3; ++vertex) {
        t.x[vertex] = static_cast<float>(160.0 + focal * corners[vertex].x / corners[vertex].z);
        t.y[vertex] = static_cast<float>(120.0 + focal * corners[vertex].y / corners[vertex].z);
        t.depth[vertex] = static_cast<float>(corners[vertex].z);
        t.model[vertex][0] = static_cast<std::int16_t>(level[vertex].x - offset[0]);
        t.model[vertex][1] = static_cast<std::int16_t>(level[vertex].y - offset[1]);
        t.model[vertex][2] = static_cast<std::int16_t>(level[vertex].z - offset[2]);
    }
    return t;
}

struct Scene {
    View view;
    std::vector<GpuTemporalTriangle> previous, current;
    std::vector<int> matches;                  /* what a window matcher would have said */
    std::vector<std::array<Point, 3>> before;  /* where every current corner really was */

    double focal = 256.0;

    explicit Scene(const View &camera, double lens = 256.0) : view(camera), focal(lens) {}

    void add_face(const std::array<Point, 3> &face, std::uint64_t material,
                  const std::array<int, 3> &moved = {0, 0, 0}) {
        const std::array<Point, 3> past{seen_before(face[0], view, moved), seen_before(face[1], view, moved),
                                        seen_before(face[2], view, moved)};
        current.push_back(projected(face, face, {0, 0, 0}, material, focal));
        before.push_back(past);
        matches.push_back(-1);
        if (past[0].z < kNearPlane || past[1].z < kNearPlane || past[2].z < kNearPlane) return;
        matches.back() = static_cast<int>(previous.size());
        previous.push_back(projected(past, face, {view.step[0] + moved[0], view.step[1] + moved[1],
                                                  view.step[2] + moved[2]}, material, focal));
    }

    void add_quad(Point a, Point b, Point c, Point d, std::uint64_t material,
                  const std::array<int, 3> &moved = {0, 0, 0}) {
        add_face({a, b, c}, material, moved);
        add_face({a, c, d}, material, moved);
    }

    int faces() const { return static_cast<int>(current.size()); }

    int prepare(std::vector<GpuTemporalTriangle> &from, GpuTemporalMeshStats &stats) const {
        from.assign(current.size(), GpuTemporalTriangle{});
        return gpu_temporal_mesh_prepare(previous.data(), static_cast<int>(previous.size()), current.data(),
                                         faces(), matches.data(), from.data(), &stats);
    }

    /* Largest distance between where a corner is sent and where it was, over corners that were in view. */
    double worst_error(int first, int last, const std::vector<GpuTemporalTriangle> &from) const {
        double worst = 0.0;
        for (int face = first; face < last; ++face)
            for (int vertex = 0; vertex < 3; ++vertex) {
                const Point &was = before[face][vertex];
                if (!std::isfinite(from[face].x[vertex]) || !std::isfinite(from[face].y[vertex]) || !std::isfinite(from[face].w[vertex]))
                    return std::numeric_limits<double>::infinity();
                const double weight = from[face].w[vertex] != 0.0f ? from[face].w[vertex] : 1.0;
                if (was.z < kNearPlane) continue;
                const double error = std::hypot(from[face].x[vertex] / weight - (160.0 + focal * was.x / was.z),
                                                from[face].y[vertex] / weight - (120.0 + focal * was.y / was.z));
                if (!std::isfinite(error)) return std::numeric_limits<double>::infinity();
                worst = std::max(worst, error);
            }
        return worst;
    }
};

constexpr int kFloorFaces = 32;
constexpr int kWallFaces = 64;

void add_floor(Scene &scene) {
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double x = -400.0 + 200.0 * i, z = 400.0 + 300.0 * j;
        scene.add_quad({x, 120, z}, {x + 200, 120, z}, {x + 200, 120, z + 300}, {x, 120, z + 300}, 10 + i * 4 + j);
    }
}

Scene room(const View &view, double focal = 256.0) {
    Scene scene(view, focal);
    add_floor(scene);
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double y = -300.0 + 100.0 * i, z = 400.0 + 300.0 * j;
        scene.add_quad({420, y, z}, {420, y + 100, z}, {420, y + 100, z + 300}, {420, y, z + 300}, 100 + i * 4 + j);
    }
    for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) {
        const double y = -300.0 + 100.0 * i, x = -400.0 + 200.0 * j;
        scene.add_quad({x, y, 1700}, {x + 200, y, 1700}, {x + 200, y + 100, 1700}, {x, y + 100, 1700}, 200 + i * 4 + j);
    }
    return scene;
}

void twins_move_the_static_world() {
    Scene scene = room(kStep);
    std::fill(scene.matches.begin(), scene.matches.end(), -1);
    std::vector<GpuTemporalTriangle> from;
    GpuTemporalMeshStats stats{};
    expect(scene.prepare(from, stats) == scene.faces(), "every static face moves without one windowed match");
    expect(stats.twin_faces == scene.faces(), "every face finds its twin by the camera's step");
    expect(scene.worst_error(0, scene.faces(), from) < 0.02,
           "the camera puts every corner where the previous frame had it");
    expect(stats.camera_inliers >= GPU_TEMPORAL_CAMERA_MIN_INLIERS && stats.reprojected_vertices > 0 &&
               stats.held_vertices == 0 && stats.anchored_junctions == 0,
           "a fitted camera leaves nothing pinned");

    Scene blind = room(kStep);
    for (auto *list : {&blind.previous, &blind.current})
        for (GpuTemporalTriangle &face : *list) face.modelled = 0;
    for (int face = 7; face < blind.faces(); ++face) blind.matches[face] = -1;
    blind.prepare(from, stats);
    expect(stats.twin_faces == 0 && stats.camera_inliers == 0 && stats.reprojected_vertices == 0,
           "without model coordinates too few matches must not fit a camera");

    Scene flat(kStep);
    add_floor(flat);
    flat.prepare(from, stats);
    expect(flat.faces() == kFloorFaces && stats.camera_inliers == 0 && stats.reprojected_vertices == 0,
           "one visible plane cannot determine a camera");
}

void look_alikes_and_moving_objects() {
    Scene scene = room(kStep);
    const int object = scene.faces();
    scene.add_quad({-100, -50, 800}, {0, -50, 800}, {0, 50, 800}, {-100, 50, 800}, 900, {5, 0, 0});
    scene.add_quad({0, -50, 800}, {100, -50, 800}, {100, 50, 800}, {0, 50, 800}, 901, {5, 0, 0});
    /* Two neighbouring far-wall faces share a texture, and the matcher took one for the other. */
    const int right = kWallFaces + 2, wrong = kWallFaces;
    scene.current[right].material = scene.previous[right].material = scene.current[wrong].material;
    scene.matches[right] = wrong;
    std::vector<GpuTemporalTriangle> from;
    GpuTemporalMeshStats stats{};
    scene.prepare(from, stats);
    expect(scene.worst_error(object, scene.faces(), from) < 0.02,
           "an object that moved against the camera keeps its own motion");
    expect(stats.seeded_vertices == 6 && stats.twin_faces == object,
           "the object is the only voted geometry and the world is all twins");
    expect(scene.worst_error(0, object, from) < 0.02,
           "a face matched to a look-alike follows the camera instead");

    Scene far(kStep);
    far.add_quad({-100, -50, 800}, {0, -50, 800}, {0, 50, 800}, {-100, 50, 800}, 900, {512, 0, 0});
    far.add_quad({0, -50, 800}, {100, -50, 800}, {100, 50, 800}, {0, 50, 800}, 901, {512, 0, 0});
    const int tiles = far.faces();
    Scene around = room(kStep);
    far.previous.insert(far.previous.end(), around.previous.begin(), around.previous.end());
    far.current.insert(far.current.end(), around.current.begin(), around.current.end());
    far.before.insert(far.before.end(), around.before.begin(), around.before.end());
    for (int match : around.matches) far.matches.push_back(match + tiles);
    far.prepare(from, stats);
    expect(stats.seeded_vertices == 0, "a tile's width away is another tile, not a moving object");
}

void fast_turn_keeps_lines_straight() {
    Scene scene = room(kFlick);
    /* A long floor edge with a corner of the next face in its middle, and a face that was behind the viewer. */
    const int junction = scene.faces();
    scene.add_face({Point{-300, 120, 500}, Point{300, 120, 500}, Point{0, 120, 900}}, 950);
    scene.add_face({Point{0, 120, 500}, Point{-150, 120, 300}, Point{150, 120, 300}}, 951);
    const int behind = scene.faces();
    scene.add_face({Point{2000, -100, 400}, Point{2200, -100, 400}, Point{2000, 100, 400}}, 952);
    std::fill(scene.matches.begin(), scene.matches.end(), -1);
    std::vector<GpuTemporalTriangle> from;
    GpuTemporalMeshStats stats{};
    scene.prepare(from, stats);
    expect(stats.camera_inliers >= GPU_TEMPORAL_CAMERA_MIN_INLIERS && stats.held_vertices == 0,
           "a 23 degree turn in one frame still finds the camera and pins nothing");
    expect(scene.worst_error(0, scene.faces(), from) < 0.05,
           "corners that were in view start where they were, however far that is");
    for (float alpha : {0.25f, 0.5f, 0.75f}) {
        GpuTemporalTriangle edge{}, corner{};
        gpu_temporal_lerp(&from[junction], &scene.current[junction], alpha, &edge);
        gpu_temporal_lerp(&from[junction + 1], &scene.current[junction + 1], alpha, &corner);
        const double dx = edge.x[1] - edge.x[0], dy = edge.y[1] - edge.y[0];
        const double off = ((corner.x[0] - edge.x[0]) * dy - (corner.y[0] - edge.y[0]) * dx) / std::hypot(dx, dy);
        expect(std::fabs(off) < 0.01, "a corner on a long edge stays on it at every step of a fast turn");
    }
    GpuTemporalTriangle early{}, late{};
    gpu_temporal_lerp(&from[behind], &scene.current[behind], 0.1f, &early);
    gpu_temporal_lerp(&from[behind], &scene.current[behind], 0.97f, &late);
    expect(!gpu_temporal_mesh_facing(&scene.current[behind], &early) &&
               gpu_temporal_mesh_facing(&scene.current[behind], &late),
           "a face that was behind the viewer is drawn only once it has come round");
}

/* The game stops submitting the second half of a quad: the half it kept must not come apart from it. */
struct Departure {
    Scene scene;
    std::vector<GpuTemporalTriangle> from, start, to;
    std::vector<int> departed;
    GpuTemporalMeshStats stats{};
    GpuTemporalCamera camera{};
    std::array<Point, 3> gone{};
    int kept = 0, left = 0;

    /* `depth` replaces the depths the dropped face had in the previous frame, where it is not negative. */
    explicit Departure(const View &view, double focal = 256.0, std::array<float, 3> depth = {-1.0f, -1.0f, -1.0f})
        : scene(room(view, focal)) {
        kept = scene.faces();
        gone = {Point{-100, -50, 800}, Point{100, 50, 800}, Point{-100, 50, 800}};
        scene.add_face({Point{-100, -50, 800}, Point{100, -50, 800}, Point{100, 50, 800}}, 900);
        scene.add_face(gone, 901);
        left = static_cast<int>(scene.previous.size()) - 1;
        for (int vertex = 0; vertex < 3; ++vertex)
            if (depth[vertex] >= 0.0f) scene.previous[left].depth[vertex] = depth[vertex];
        scene.current.pop_back();
        scene.matches.pop_back();
        scene.before.pop_back();
        std::fill(scene.matches.begin(), scene.matches.end(), -1);
        const int before = static_cast<int>(scene.previous.size());
        from.assign(scene.current.size(), GpuTemporalTriangle{});
        start.assign(before, GpuTemporalTriangle{});
        to.assign(before, GpuTemporalTriangle{});
        departed.assign(before, 0);
        gpu_temporal_mesh_prepare_scene(scene.previous.data(), before, scene.current.data(), scene.faces(),
                                        scene.matches.data(), from.data(), &stats, start.data(), to.data(),
                                        departed.data(), &camera);
    }
};

void departed_faces_meet_their_neighbours() {
    Departure d(kStride);
    expect(d.stats.departed_faces == 1 && d.departed[d.left] == 1 && d.stats.welded_corners == 2,
           "the face the game dropped is the only departed one, welded by its two shared corners");
    /* Corner 0 of the dropped half is corner 0 of the kept half, and its corner 1 is the kept corner 2. */
    const int shared[2][2] = {{0, 0}, {1, 2}};
    for (float alpha : {0.0f, 0.3f, 0.7f}) {
        GpuTemporalTriangle kept{}, left{};
        gpu_temporal_lerp(&d.from[d.kept], &d.scene.current[d.kept], alpha, &kept);
        gpu_temporal_lerp(&d.start[d.left], &d.to[d.left], alpha, &left);
        for (const auto &pair : shared)
            expect(left.x[pair[0]] == kept.x[pair[1]] && left.y[pair[0]] == kept.y[pair[1]],
                   "a shared corner is in one place for both faces at every step");
        if (alpha == 0.0f)
            expect(d.from[d.kept].w[0] != 0.0f && d.from[d.kept].w[0] != 1.0f &&
                       std::fabs(kept.x[0] - d.from[d.kept].x[0] / d.from[d.kept].w[0]) < 0.001,
                   "the first step divides a weighted corner by its weight like every other");
    }
    const Point &own = d.gone[2];
    const double weight = d.to[d.left].w[2];
    expect(d.start[d.left].w[2] == 0.0f && d.start[d.left].x[2] == d.scene.previous[d.left].x[2] && weight > 0.0,
           "a corner no current face has starts where it was");
    expect(std::hypot(d.to[d.left].x[2] / weight - (160.0 + 256.0 * own.x / own.z),
                      d.to[d.left].y[2] / weight - (120.0 + 256.0 * own.y / own.z)) < 0.05,
           "and ends where the current camera sees it");
    for (float last : {1.0f, 2.5f, std::numeric_limits<float>::quiet_NaN()}) {
        GpuTemporalTriangle end{};
        gpu_temporal_lerp(&d.start[d.left], &d.to[d.left], last, &end);
        expect(std::hypot(end.x[2] - (160.0 + 256.0 * own.x / own.z), end.y[2] - (120.0 + 256.0 * own.y / own.z)) < 0.05 &&
                   end.x[0] == d.scene.current[d.kept].x[0],
               "at and past the last step a weighted end is a screen position, not a numerator");
    }
    GpuTemporalTriangle alone{};
    gpu_temporal_lerp(nullptr, &d.to[d.left], 0.5f, &alone);
    expect(std::fabs(alone.x[2] - d.to[d.left].x[2] / weight) < 0.001,
           "a face without a start is drawn at its weighted end");
    expect(std::fabs(d.to[d.left].depth[2] - own.z) < own.z * 0.005 &&
               std::fabs(d.to[d.left].depth[0] - d.gone[0].z) < 0.001,
           "a departed corner carries its depth in the current view");

    GpuTemporalCamera forward{};
    expect(gpu_temporal_camera_invert(&d.camera, &forward) == 1, "a fitted camera has an inverse");
    double worst = 0.0;
    for (const GpuTemporalTriangle &face : d.scene.current)
        for (int vertex = 0; vertex < 3; ++vertex) {
            double px = 0.0, py = 0.0, x = 0.0, y = 0.0, back = 0.0, there = 0.0;
            gpu_temporal_camera_project_h(&d.camera, face.x[vertex], face.y[vertex], face.depth[vertex],
                                          &px, &py, &there);
            gpu_temporal_camera_project_h(&forward, static_cast<float>(px / there), static_cast<float>(py / there),
                                          static_cast<float>(face.depth[vertex] * there), &x, &y, &back);
            worst = std::max({worst, std::hypot(x / back - face.x[vertex], y / back - face.y[vertex]),
                              std::fabs(there * back - 1.0) * 100.0});
        }
    expect(worst < 0.01, "there and back returns every corner, and the two depth ratios are reciprocal");

    GpuTemporalCamera flat = d.camera;
    flat.scale = 0.0;
    expect(gpu_temporal_camera_invert(&flat, &forward) == 0, "a camera without a depth scale has no inverse");

    Departure blind(kStride);
    std::vector<GpuTemporalTriangle> from(blind.scene.current.size());
    GpuTemporalMeshStats stats{};
    gpu_temporal_mesh_prepare_scene(blind.scene.previous.data(), static_cast<int>(blind.scene.previous.size()),
                                    blind.scene.current.data(), blind.scene.faces(), blind.scene.matches.data(),
                                    from.data(), &stats, nullptr, blind.to.data(), blind.departed.data(), nullptr);
    expect(stats.departed_faces == 0, "departed faces need both ends of their path");
}

/* The game pins a corner behind the viewer far off the screen and gives it no depth. */
void departed_face_keeps_a_corner_without_depth() {
    Departure d(kStride, 256.0, {-1.0f, -1.0f, 0.0f});
    const GpuTemporalTriangle &was = d.scene.previous[d.left];
    expect(d.stats.departed_faces == 1 && d.departed[d.left] == 2 && d.stats.welded_corners == 2,
           "a dropped face with one corner without depth is still carried, and marked");
    expect(d.start[d.left].x[2] == was.x[2] && d.start[d.left].y[2] == was.y[2] && d.start[d.left].w[2] == 0.0f &&
               d.to[d.left].x[2] == was.x[2] && d.to[d.left].y[2] == was.y[2] && d.to[d.left].w[2] == 0.0f &&
               d.to[d.left].depth[2] == 0.0f,
           "the corner without depth stays where it was drawn, at both ends");
    for (float alpha : {0.0f, 0.3f, 0.7f}) {
        GpuTemporalTriangle kept{}, left{};
        gpu_temporal_lerp(&d.from[d.kept], &d.scene.current[d.kept], alpha, &kept);
        gpu_temporal_lerp(&d.start[d.left], &d.to[d.left], alpha, &left);
        expect(left.x[0] == kept.x[0] && left.y[0] == kept.y[0] && left.x[1] == kept.x[2] && left.y[1] == kept.y[2] &&
                   left.x[2] == was.x[2] && left.y[2] == was.y[2],
               "its other corners still meet the neighbour at every step, and the held one does not move");
    }

    Departure shared(kStride, 256.0, {0.0f, -1.0f, -1.0f});
    expect(shared.departed[shared.left] == 2 && shared.to[shared.left].x[0] == shared.scene.previous[shared.left].x[0] &&
               shared.to[shared.left].w[0] == 0.0f && shared.to[shared.left].w[2] > 0.0f,
           "a corner without depth is held even where a current face has that corner, and the rest is carried");

    Departure flat(kStride, 256.0, {0.0f, 0.0f, 0.0f});
    expect(flat.stats.departed_faces == 0 && flat.departed[flat.left] == 0,
           "a face with no depth at all has nothing to be carried by");
    Departure distant(kStride, 256.0, {-1.0f, -1.0f, 65535.0f});
    expect(distant.stats.departed_faces == 0 && distant.departed[distant.left] == 0,
           "a saturated far depth is not a missing one: that corner turns with the view and cannot be held");
    Departure whole(kStride);
    expect(whole.departed[whole.left] == 1, "a face with all its depths keeps the plain mark");
}

void whip_turn_past_the_screen_edge() {
    /* 43 degrees in one frame with the game's lens: from the screen corner the old model's denominator was negative. */
    for (double yaw : {0.75, -0.75}) {
        Departure d(View{yaw, {0, 0, 0}}, kGameFocal);
        expect(d.stats.camera_inliers >= GPU_TEMPORAL_CAMERA_MIN_INLIERS && d.stats.held_vertices == 0,
               "a 43 degree turn in one frame still finds the camera");
        expect(d.scene.worst_error(0, d.scene.faces(), d.from) < 0.05,
               "and starts every corner that was in view where it was");
        expect(d.stats.departed_faces == 1 && d.to[d.left].w[2] > 0.0f, "and still places the face the game dropped");
    }
}

/* The two halves of a billboard around a point of the current view, and where they were. */
struct Billboard {
    std::array<GpuTemporalTriangle, 2> now, before;
};

Billboard billboard(const View &view, const Point &centre, const std::array<int, 3> &moved,
                    std::array<float, 2> size_now, std::array<float, 2> size_before,
                    std::uint64_t material_now, std::uint64_t material_before) {
    const auto halves = [](double x, double y, std::array<float, 2> size, float depth, std::uint64_t material) {
        const float left = static_cast<float>(x) - size[0] / 2, top = static_cast<float>(y) - size[1] / 2;
        std::array<GpuTemporalTriangle, 2> pair{};
        const float xs[2][3] = {{left, left + size[0], left}, {left + size[0], left, left + size[0]}};
        const float ys[2][3] = {{top, top, top + size[1]}, {top, top + size[1], top + size[1]}};
        for (int half = 0; half < 2; ++half) {
            pair[half].material = material + static_cast<std::uint64_t>(half);
            pair[half].world = pair[half].sprite = 1;
            for (int vertex = 0; vertex < 3; ++vertex) {
                pair[half].x[vertex] = xs[half][vertex];
                pair[half].y[vertex] = ys[half][vertex];
                pair[half].depth[vertex] = depth;
            }
        }
        return pair;
    };
    const Point was = seen_before(centre, view, moved);
    return {halves(160.0 + 256.0 * centre.x / centre.z, 120.0 + 256.0 * centre.y / centre.z, size_now,
                   static_cast<float>(centre.z), material_now),
            halves(160.0 + 256.0 * was.x / was.z, 120.0 + 256.0 * was.y / was.z, size_before,
                   static_cast<float>(was.z), material_before)};
}

int add_billboard(Scene &scene, const Billboard &sprite, bool with_past = true, bool matched = false) {
    const int first = scene.faces();
    for (int half = 0; half < 2; ++half) {
        scene.current.push_back(sprite.now[half]);
        scene.before.push_back({});
        scene.matches.push_back(matched ? static_cast<int>(scene.previous.size()) : -1);
        if (with_past) scene.previous.push_back(sprite.before[half]);
    }
    return first;
}

bool starts_at(const GpuTemporalTriangle &from, const GpuTemporalTriangle &place) {
    /* Half a pixel tells a corner of the previous self from where the camera alone would put it. */
    for (int vertex = 0; vertex < 3; ++vertex) {
        if (!std::isfinite(from.x[vertex]) || !std::isfinite(from.y[vertex]) || !std::isfinite(from.w[vertex]) ||
            !std::isfinite(place.x[vertex]) || !std::isfinite(place.y[vertex])) return false;
        const float weight = from.w[vertex] != 0.0f ? from.w[vertex] : 1.0f;
        const float x = from.x[vertex] / weight, y = from.y[vertex] / weight;
        if (std::fabs(x - place.x[vertex]) > 0.5f || std::fabs(y - place.y[vertex]) > 0.5f) return false;
    }
    return true;
}

void helpers_refuse_numbers_that_are_not_numbers() {
    Scene scene = room(kStep);
    std::vector<GpuTemporalTriangle> from;
    GpuTemporalMeshStats stats{};
    scene.prepare(from, stats);
    expect(scene.worst_error(0, scene.faces(), from) < 0.02, "the helpers pass a sound result");
    const float infinity = std::numeric_limits<float>::infinity();
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(), infinity, -infinity}) for (int field = 0; field < 3; ++field) {
        std::vector<GpuTemporalTriangle> broken = from;
        GpuTemporalTriangle place = from[0];
        for (int vertex = 0; vertex < 3; ++vertex) place.x[vertex] /= place.w[vertex] != 0.0f ? place.w[vertex] : 1.0f;
        for (int vertex = 0; vertex < 3; ++vertex) place.y[vertex] /= place.w[vertex] != 0.0f ? place.w[vertex] : 1.0f;
        expect(starts_at(from[0], place), "a face starts where it starts");
        (field == 0 ? broken[0].x[1] : field == 1 ? broken[0].y[1] : broken[0].w[1]) = bad;
        expect(!starts_at(broken[0], place) && !(scene.worst_error(0, scene.faces(), broken) < 0.02),
               "a coordinate or a weight that is not a finite number fails both helpers");
        GpuTemporalTriangle nowhere = place;
        nowhere.x[1] = nowhere.y[1] = 0.0f;
        expect(!starts_at(broken[0], nowhere), "and does not pass as the origin once divided");
    }
}

void billboards_keep_their_motion_through_a_new_picture() {
    const Point centre{-60, 20, 900};
    std::vector<GpuTemporalTriangle> from;
    GpuTemporalMeshStats stats{};

    /* The next animation frame: another texture, another size, five level units to the side. */
    Scene scene = room(kStep);
    const Billboard animated = billboard(kStep, centre, {5, 0, 0}, {24, 28}, {20, 30}, 700, 800);
    const int sprite = add_billboard(scene, animated);
    scene.prepare(from, stats);
    expect(starts_at(from[sprite], animated.before[0]) && starts_at(from[sprite + 1], animated.before[1]),
           "a billboard with a new picture starts on the corners of its previous self, half by half");

    /* Two unclaimed billboards within reach: the nearer one is the previous self. */
    Scene crowd = room(kStep);
    const int own = add_billboard(crowd, animated);
    Billboard neighbour = animated;
    for (auto &half : neighbour.before) {
        for (int vertex = 0; vertex < 3; ++vertex) half.x[vertex] += 14.0f;
        crowd.previous.push_back(half);
    }
    crowd.prepare(from, stats);
    expect(starts_at(from[own], animated.before[0]) && starts_at(from[own + 1], animated.before[1]),
           "of two billboards in reach the nearer is the previous self");

    /* A narrow billboard between two wide ones: its halves lie nearer to different rectangles. */
    Scene between = room(kStep);
    const Billboard narrow = billboard(kStep, centre, {0, 0, 0}, {12, 30}, {24, 30}, 700, 800);
    std::array<Billboard, 2> wide{narrow, narrow};
    for (int side = 0; side < 2; ++side)
        for (auto &half : wide[side].before)
            for (int vertex = 0; vertex < 3; ++vertex) half.x[vertex] += side ? 2.0f : -2.0f;
    const int thin = between.faces();
    for (int half = 0; half < 2; ++half) {
        between.current.push_back(narrow.now[half]);
        between.before.push_back({});
        between.matches.push_back(-1);
    }
    for (const Billboard &other : wide)
        for (const auto &half : other.before) between.previous.push_back(half);
    between.prepare(from, stats);
    expect((starts_at(from[thin], wide[0].before[0]) && starts_at(from[thin + 1], wide[0].before[1])) ||
               (starts_at(from[thin], wide[1].before[0]) && starts_at(from[thin + 1], wide[1].before[1])),
           "both halves of a billboard start on one and the same previous rectangle");

    /* Two look-alikes side by side, and the picture matcher gave each the other's past. */
    Scene twins_apart = room(kStep);
    const Billboard left = billboard(kStep, {-60, 20, 900}, {5, 0, 0}, {20, 30}, {20, 30}, 800, 800);
    const Billboard right = billboard(kStep, {10, 20, 900}, {-5, 0, 0}, {20, 30}, {20, 30}, 800, 800);
    const int first = add_billboard(twins_apart, left, true, true);
    const int other = add_billboard(twins_apart, right, true, true);
    for (int half = 0; half < 2; ++half) std::swap(twins_apart.matches[first + half], twins_apart.matches[other + half]);
    twins_apart.prepare(from, stats);
    expect(starts_at(from[first], left.before[0]) && starts_at(from[first + 1], left.before[1]) &&
               starts_at(from[other], right.before[0]) && starts_at(from[other + 1], right.before[1]),
           "look-alike billboards keep their own past, whatever the picture matcher picked");

    /* Two level units a frame is about half a pixel here, less than the camera's tolerance. */
    Scene slow = room(kStep);
    const Billboard drifting = billboard(kStep, centre, {2, 0, 0}, {20, 30}, {20, 30}, 800, 800);
    const int drift = add_billboard(slow, drifting);
    slow.prepare(from, stats);
    bool exact = true;
    for (int half = 0; half < 2; ++half)
        for (int vertex = 0; vertex < 3; ++vertex)
            exact &= from[drift + half].w[vertex] == 0.0f &&
                     std::fabs(from[drift + half].x[vertex] - drifting.before[half].x[vertex]) < 0.01f &&
                     std::fabs(from[drift + half].y[vertex] - drifting.before[half].y[vertex]) < 0.01f;
    expect(exact,
           "a slow billboard starts exactly where it was, not where the camera alone would put it");

    /* Two billboards want one past: the nearer one gets it and the other follows the camera. */
    Scene rivals = room(kStep);
    const Billboard heir = billboard(kStep, centre, {5, 0, 0}, {20, 30}, {20, 30}, 800, 800);
    const Billboard latecomer = billboard(kStep, {-52, 20, 900}, {0, 0, 0}, {20, 30}, {20, 30}, 700, 700);
    const int late = add_billboard(rivals, latecomer, false);
    const int near = add_billboard(rivals, heir);
    rivals.prepare(from, stats);
    expect(starts_at(from[near], heir.before[0]) && starts_at(from[late], latecomer.before[0]) &&
               !starts_at(from[late], heir.before[0]),
           "one past goes to the nearer billboard, the other follows the camera");

    /* Farther from where the camera puts it than its own size, or at another depth: someone else. */
    for (int variant = 0; variant < 2; ++variant) {
        Scene other = room(kStep);
        /* 20 px is past this billboard's size of 14 and inside the 32 px every motion is held to. */
        Billboard stranger = billboard(kStep, centre, {0, 0, 0}, {12, 14}, {10, 15}, 700, 800);
        const Billboard still = billboard(kStep, centre, {0, 0, 0}, {12, 14}, {12, 14}, 700, 700);
        for (auto &half : stranger.before)
            for (int vertex = 0; vertex < 3; ++vertex) {
                if (variant == 0) half.x[vertex] += 20.0f;
                else half.depth[vertex] *= 1.4f;
            }
        const int index = add_billboard(other, stranger);
        other.prepare(from, stats);
        expect(starts_at(from[index], still.before[0]),
               variant == 0 ? "a billboard farther off than this one's size is another actor"
                            : "and so is one at another depth");
    }

    /* A previous rectangle whose upper half lists its corners from another one, a wall where the billboard was,
     * a wall where it is, and a rectangle whose halves have two depths. Small, so that no corner moves 32 px. */
    const Billboard small = billboard(kStep, centre, {5, 0, 0}, {12, 14}, {10, 15}, 700, 800);
    const Billboard still = billboard(kStep, centre, {0, 0, 0}, {12, 14}, {12, 14}, 700, 700);
    for (int variant = 0; variant < 7; ++variant) {
        Scene odd = room(kStep);
        Billboard changed = small;
        if (variant == 0) {
            for (int vertex = 0; vertex < 3; ++vertex) {
                changed.before[0].x[vertex] = small.before[0].x[(vertex + 1) % 3];
                changed.before[0].y[vertex] = small.before[0].y[(vertex + 1) % 3];
            }
        }
        if (variant == 1) changed.before[0].sprite = 0;
        if (variant == 2) changed.before[1].sprite = 0;
        if (variant == 3) changed.now[0].sprite = 0;
        if (variant == 4) changed.now[1].sprite = 0;
        if (variant == 5)
            for (int vertex = 0; vertex < 3; ++vertex) changed.before[1].depth[vertex] += 8.0f;
        if (variant == 6) {
            /* The lower right triangle once more in the upper half's place, from its third corner. */
            const GpuTemporalTriangle &lower = small.before[1];
            const int order[3] = {2, 0, 1};
            for (int vertex = 0; vertex < 3; ++vertex) {
                changed.before[0].x[vertex] = lower.x[order[vertex]];
                changed.before[0].y[vertex] = lower.y[order[vertex]];
            }
        }
        /* A real billboard elsewhere, so that both frames have one. */
        add_billboard(odd, billboard(kStep, {300, 20, 900}, {0, 0, 0}, {12, 14}, {12, 14}, 600, 600));
        const int index = add_billboard(odd, changed);
        odd.prepare(from, stats);
        expect(starts_at(from[index], still.before[0]) && starts_at(from[index + 1], still.before[1]),
               variant == 0 || variant == 6 ? "a rectangle is paired only with one whose halves list their corners alike"
               : variant < 3 ? "a billboard does not start on a wall"
               : variant < 5 ? "a wall does not start on a billboard"
                             : "two halves at two depths are not one billboard");
    }
}

void facing_and_camera_bounds() {
    const auto current = first_triangle();
    auto flipped = current;
    std::swap(flipped.x[1], flipped.x[2]);
    std::swap(flipped.y[1], flipped.y[2]);
    expect(gpu_temporal_mesh_facing(&current, &current) && !gpu_temporal_mesh_facing(&current, &flipped),
           "an interpolated face of the opposite winding is not drawn");
    auto ghost = current;
    ghost.w[0] = static_cast<float>(GPU_TEMPORAL_MIN_WEIGHT) * 0.5f;
    expect(!gpu_temporal_mesh_facing(&current, &ghost), "a corner not yet in front of the viewer hides its face");
    auto broken = current;
    broken.x[1] = std::numeric_limits<float>::infinity();
    expect(gpu_temporal_mesh_camera_safe(&current, &flipped) && !gpu_temporal_mesh_camera_safe(&current, &broken),
           "only a non-finite corner makes camera motion unsafe");
}

} // namespace

int main() {
    twins_move_the_static_world();
    look_alikes_and_moving_objects();
    fast_turn_keeps_lines_straight();
    departed_faces_meet_their_neighbours();
    departed_face_keeps_a_corner_without_depth();
    whip_turn_past_the_screen_edge();
    facing_and_camera_bounds();
    helpers_refuse_numbers_that_are_not_numbers();
    billboards_keep_their_motion_through_a_new_picture();
    half_matched_quad();
    depth_and_topology_guards();
    conflicts_and_order();
    invalid_and_hud();
    held_world_anchors();
    safety_propagation();
    connected_and_scale();
    unknown_depth_bridge();
    t_junctions();
    if (failures) return 1;
    std::cout << "GPU temporal mesh continuity tests passed\n";
    return 0;
}
