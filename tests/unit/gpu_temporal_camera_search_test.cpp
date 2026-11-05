#include "gpu_temporal_mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

int scalar_camera_search(const GpuTemporalCameraSamples *samples, int count, int voters,
                         const GpuTemporalCamera *prior, GpuTemporalCamera *camera);
void scalar_camera_normal(const GpuTemporalCameraSamples *samples, int count,
                          double (*normal)[GPU_TEMPORAL_CAMERA_TERMS + 1]);
int scalar_camera_count(const double *m, const GpuTemporalCameraSamples *samples, int count, double limit,
                        int floor);
int scalar_camera_vector_path();

namespace {

int failures = 0;

void expect(bool condition, const char *message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

constexpr int kTerms = GPU_TEMPORAL_CAMERA_TERMS;
using Normal = double[kTerms][kTerms + 1];

/* The search as it was before the pairwise one: whole rows into the normal equations, every count to the end. */
namespace oracle {

struct Sample {
    double u, v, w, tx, ty;
};

int agrees(const double *m, const Sample *sample, double limit) {
    const double denominator = m[8] * sample->u + m[9] * sample->v + 1.0 + m[10] * sample->w;
    double dx, dy;
    if (!(denominator > 0.25 && denominator < 4.0)) return 0;
    dx = (m[0] * sample->u + m[1] * sample->v + m[2] + m[3] * sample->w) / denominator - sample->tx;
    dy = (m[4] * sample->u + m[5] * sample->v + m[6] + m[7] * sample->w) / denominator - sample->ty;
    return dx * dx + dy * dy <= limit * limit;
}

int count(const double *m, const Sample *samples, int n, double limit) {
    int agreeing = 0;
    for (int index = 0; index < n; ++index) agreeing += agrees(m, &samples[index], limit);
    return agreeing;
}

void accumulate(double (*normal)[kTerms + 1], const Sample *sample) {
    enum { N = kTerms };
    double rows[2][kTerms + 1];
    std::memset(rows, 0, sizeof(rows));
    rows[0][0] = sample->u; rows[0][1] = sample->v; rows[0][2] = 1.0; rows[0][3] = sample->w;
    rows[0][8] = -sample->tx * sample->u; rows[0][9] = -sample->tx * sample->v;
    rows[0][10] = -sample->tx * sample->w; rows[0][N] = sample->tx;
    rows[1][4] = sample->u; rows[1][5] = sample->v; rows[1][6] = 1.0; rows[1][7] = sample->w;
    rows[1][8] = -sample->ty * sample->u; rows[1][9] = -sample->ty * sample->v;
    rows[1][10] = -sample->ty * sample->w; rows[1][N] = sample->ty;
    for (int r = 0; r < 2; ++r)
        for (int i = 0; i < N; ++i) {
            if (rows[r][i] == 0.0) continue;
            for (int k = 0; k <= N; ++k) normal[i][k] += rows[r][i] * rows[r][k];
        }
}

int refit(const double *reference, double limit, const Sample *samples, int n, double *out) {
    Normal normal;
    int used = 0;
    std::memset(normal, 0, sizeof(normal));
    for (int index = 0; index < n; ++index) {
        if (reference && !agrees(reference, &samples[index], limit)) continue;
        accumulate(normal, &samples[index]);
        ++used;
    }
    return used >= GPU_TEMPORAL_CAMERA_MIN_INLIERS && gpu_temporal_camera_solve(normal, out);
}

int search(const Sample *samples, int n, int voters, const GpuTemporalCamera *prior, GpuTemporalCamera *camera) {
    static const double limits[4] = {2.0, 1.0, GPU_TEMPORAL_CAMERA_TOLERANCE, GPU_TEMPORAL_CAMERA_TOLERANCE};
    double best[kTerms], trial[kTerms];
    int faces = voters / 3, best_count = -1, agreeing;
    unsigned random = 0x9E3779B9u;
    if (prior && prior->inliers > 0) {
        std::memcpy(best, prior->m, sizeof(best));
        best_count = count(best, samples, voters, 1.0);
    }
    if (refit(nullptr, 0.0, samples, voters, trial)) {
        agreeing = count(trial, samples, voters, 1.0);
        if (agreeing > best_count) { best_count = agreeing; std::memcpy(best, trial, sizeof(best)); }
    }
    for (int draw = 0; draw < GPU_TEMPORAL_CAMERA_DRAWS && faces >= 3; ++draw) {
        Normal normal;
        int picked[3];
        std::memset(normal, 0, sizeof(normal));
        for (int slot = 0; slot < 3; ++slot) {
            random = random * 1664525u + 1013904223u;
            picked[slot] = static_cast<int>((random >> 8) % static_cast<unsigned>(faces));
        }
        if (picked[0] == picked[1] || picked[0] == picked[2] || picked[1] == picked[2]) continue;
        for (int slot = 0; slot < 3; ++slot)
            for (int vertex = 0; vertex < 3; ++vertex) accumulate(normal, &samples[picked[slot] * 3 + vertex]);
        if (!gpu_temporal_camera_solve(normal, trial)) continue;
        agreeing = count(trial, samples, voters, 1.0);
        if (agreeing > best_count) { best_count = agreeing; std::memcpy(best, trial, sizeof(best)); }
    }
    if (best_count < GPU_TEMPORAL_CAMERA_MIN_INLIERS || best_count * 3 < voters) return 0;
    for (int pass = 0; pass < 4; ++pass)
        if (refit(best, limits[pass], samples, n, trial) &&
            count(trial, samples, n, limits[pass]) >= count(best, samples, n, limits[pass]))
            std::memcpy(best, trial, sizeof(best));
    std::memcpy(camera->m, best, sizeof(best));
    camera->inliers = count(best, samples, n, GPU_TEMPORAL_CAMERA_TOLERANCE);
    return camera->inliers >= GPU_TEMPORAL_CAMERA_MIN_INLIERS;
}

}  // namespace oracle

struct Scene {
    std::vector<oracle::Sample> rows;
    std::vector<double> u, v, w, tx, ty;
    double truth[kTerms]{};

    GpuTemporalCameraSamples fields() {
        return GpuTemporalCameraSamples{u.data(), v.data(), w.data(), tx.data(), ty.data()};
    }

    void add(float x, float y, float depth, float before_x, float before_y) {
        const oracle::Sample sample{gpu_temporal_camera_u(x), gpu_temporal_camera_v(y), 256.0 / static_cast<double>(depth),
                                    static_cast<double>(before_x), static_cast<double>(before_y)};
        rows.push_back(sample);
        u.push_back(sample.u);
        v.push_back(sample.v);
        w.push_back(sample.w);
        tx.push_back(sample.tx);
        ty.push_back(sample.ty);
    }
};

enum class Kind { turn, flat, crowd, edge };

/* Faces of three corners moved by a turn and a step of the camera; some faces are someone else's, some corners sit on
 * the view axis (u or v exactly zero), and a flat scene leaves the equations without a solution. */
Scene make_scene(std::mt19937 &random, Kind kind, int faces, double outliers) {
    Scene scene;
    std::uniform_real_distribution<float> screen_x(0.0f, 320.0f), screen_y(0.0f, 240.0f), depth(80.0f, 6000.0f);
    std::uniform_real_distribution<double> unit(0.0, 1.0), small(-1.0, 1.0);
    double *m = scene.truth;
    m[0] = 320.0 + 12.0 * small(random); m[1] = 4.0 * small(random); m[2] = 160.0 + 20.0 * small(random);
    m[3] = 6.0 * small(random); m[4] = 3.0 * small(random); m[5] = 240.0 + 9.0 * small(random);
    m[6] = 120.0 + 15.0 * small(random); m[7] = 6.0 * small(random);
    m[8] = 0.05 * small(random); m[9] = 0.03 * small(random); m[10] = 0.02 * small(random);
    const float plane_depth = depth(random);
    for (int face = 0; face < faces; ++face) {
        const bool someone_else = unit(random) < outliers;
        const double off_x = 8.0 + 40.0 * unit(random), off_y = 8.0 + 40.0 * unit(random);
        for (int corner = 0; corner < 3; ++corner) {
            float x = screen_x(random), y = screen_y(random);
            float z = kind == Kind::flat ? plane_depth : depth(random);
            if (kind == Kind::edge && corner == face % 3) x = 160.0f;
            if (kind == Kind::edge && corner == (face + 1) % 3) y = 120.0f;
            if (kind == Kind::crowd && face % 5 == 0) z = 65534.0f - static_cast<float>(face);
            const double cu = gpu_temporal_camera_u(x), cv = gpu_temporal_camera_v(y), cw = 256.0 / z;
            const double denominator = m[8] * cu + m[9] * cv + 1.0 + m[10] * cw;
            double px = (m[0] * cu + m[1] * cv + m[2] + m[3] * cw) / denominator;
            double py = (m[4] * cu + m[5] * cv + m[6] + m[7] * cw) / denominator;
            px += 0.2 * small(random);
            py += 0.2 * small(random);
            if (someone_else) { px += off_x; py -= off_y; }
            scene.add(x, y, z, static_cast<float>(px), static_cast<float>(py));
        }
    }
    return scene;
}

bool same_camera(int result_a, const GpuTemporalCamera &a, int result_b, const GpuTemporalCamera &b) {
    return result_a == result_b && a.inliers == b.inliers && std::memcmp(a.m, b.m, sizeof(a.m)) == 0;
}

void searches_match_the_whole_row_search() {
    std::mt19937 random(20261105u);
    int fitted = 0, refused = 0, mismatched = 0, scalar_mismatched = 0;
    const Kind kinds[] = {Kind::turn, Kind::flat, Kind::crowd, Kind::edge};
    for (int trial = 0; trial < 480; ++trial) {
        const Kind kind = kinds[trial % 4];
        const int faces = 8 + static_cast<int>(random() % 900);
        const double outliers = 0.1 * static_cast<double>(trial % 9);
        Scene scene = make_scene(random, kind, faces, outliers);
        const int count = static_cast<int>(scene.rows.size()) - static_cast<int>(trial % 3 == 2);
        int voters = trial % 4 == 1 ? std::max(36, (count / 3 / 2) * 3) : count;
        if (voters > count) voters = count;
        GpuTemporalCamera prior{};
        const GpuTemporalCamera *given = nullptr;
        if (trial % 5 == 1) { std::memcpy(prior.m, scene.truth, sizeof(prior.m)); prior.inliers = 30; given = &prior; }
        if (trial % 5 == 2) { std::memcpy(prior.m, scene.truth, sizeof(prior.m)); prior.m[2] += 9.0; prior.inliers = 30; given = &prior; }
        if (trial % 5 == 3) { std::memcpy(prior.m, scene.truth, sizeof(prior.m)); prior.inliers = 0; given = &prior; }
        GpuTemporalCamera expected{}, found{}, scalar{};
        const GpuTemporalCameraSamples fields = scene.fields();
        const int want = oracle::search(scene.rows.data(), count, voters, given, &expected);
        const int got = gpu_temporal_camera_search(&fields, count, voters, given, &found);
        const int got_scalar = scalar_camera_search(&fields, count, voters, given, &scalar);
        mismatched += !same_camera(want, expected, got, found);
        scalar_mismatched += !same_camera(want, expected, got_scalar, scalar);
        fitted += want;
        refused += !want;
    }
    expect(fitted > 100 && refused > 50, "the scenes both fit cameras and refuse them");
    expect(mismatched == 0, "the pairwise search returns the whole-row search's camera to the bit");
    expect(scalar_mismatched == 0, "without SSE2 the search returns the same camera to the bit");
    expect(scalar_camera_vector_path() == 0, "the second build of the header takes the scalar path");
}

void distinct_sums_rebuild_the_normal_equations() {
    std::mt19937 random(7u);
    int differing = 0;
    for (int trial = 0; trial < 200; ++trial) {
        Scene scene = make_scene(random, trial % 2 ? Kind::edge : Kind::turn, 1 + trial % 40, 0.3);
        const int count = static_cast<int>(scene.rows.size());
        if (trial % 4 == 0) for (int index = 0; index < count; index += 2) scene.tx[index] = scene.rows[index].tx = 0.0;
        Normal whole, pairwise, scalar;
        GpuTemporalCameraSums sums{};
        std::memset(whole, 0, sizeof(whole));
        const GpuTemporalCameraSamples fields = scene.fields();
        for (int index = 0; index < count; ++index) {
            oracle::accumulate(whole, &scene.rows[index]);
            gpu_temporal_camera_sums_add(&sums, &fields, index);
        }
        gpu_temporal_camera_normal(&sums, pairwise);
        scalar_camera_normal(&fields, count, scalar);
        differing += std::memcmp(whole, pairwise, sizeof(whole)) != 0 || std::memcmp(whole, scalar, sizeof(whole)) != 0;
    }
    expect(differing == 0, "the distinct sums give every entry of the normal equations to the bit");
}

void a_count_above_its_floor_is_exact() {
    std::mt19937 random(11u);
    int wrong = 0;
    for (int trial = 0; trial < 300; ++trial) {
        Scene scene = make_scene(random, Kind::turn, 5 + trial, 0.05 * (trial % 10));
        const int count = static_cast<int>(scene.rows.size()) - trial % 2;
        const GpuTemporalCameraSamples fields = scene.fields();
        const int exact = oracle::count(scene.truth, scene.rows.data(), count, 1.0);
        const int floors[] = {-1, exact - 1, exact, exact + 1, count / 2, count};
        for (const int floor : floors) {
            const int pairwise = gpu_temporal_camera_count(scene.truth, &fields, count, 1.0, floor);
            const int scalar = scalar_camera_count(scene.truth, &fields, count, 1.0, floor);
            for (const int got : {pairwise, scalar})
                wrong += exact > floor ? got != exact : got > floor;
        }
    }
    expect(wrong == 0, "a count is exact above its floor and never above the floor below it");
}

/* A limit whose square is a sample's own squared distance puts it on the boundary, where one rounding decides. */
void the_pair_test_decides_as_the_scalar_test_on_the_boundary() {
    std::mt19937 random(5u);
    int boundaries = 0, wrong = 0;
    for (int trial = 0; trial < 20000; ++trial) {
        Scene scene = make_scene(random, Kind::turn, 1, 0.5);
        double *m = scene.truth;
        m[8] *= 10.0; m[9] *= 10.0; m[10] *= 10.0;
        const oracle::Sample &sample = scene.rows[0];
        const double denominator = m[8] * sample.u + m[9] * sample.v + 1.0 + m[10] * sample.w;
        if (!(denominator > 0.25 && denominator < 4.0)) continue;
        const double dx = (m[0] * sample.u + m[1] * sample.v + m[2] + m[3] * sample.w) / denominator - sample.tx;
        const double dy = (m[4] * sample.u + m[5] * sample.v + m[6] + m[7] * sample.w) / denominator - sample.ty;
        const double distance2 = dx * dx + dy * dy, root = std::sqrt(distance2);
        double limit = -1.0;
        for (const double candidate : {root, std::nextafter(root, 0.0), std::nextafter(root, 1e300)})
            if (candidate * candidate == distance2) limit = candidate;
        if (limit < 0.0) continue;
        ++boundaries;
        Scene pair;
        for (int copy = 0; copy < 2; ++copy) {
            pair.rows.push_back(sample);
            pair.u.push_back(sample.u); pair.v.push_back(sample.v); pair.w.push_back(sample.w);
            pair.tx.push_back(sample.tx); pair.ty.push_back(sample.ty);
        }
        const GpuTemporalCameraSamples fields = pair.fields();
        for (const double edge : {limit, std::nextafter(limit, 0.0)}) {
            const int want = oracle::count(m, pair.rows.data(), 2, edge);
            wrong += gpu_temporal_camera_count(m, &fields, 2, edge, -1) != want;
            wrong += scalar_camera_count(m, &fields, 2, edge, -1) != want;
        }
    }
    expect(boundaries > 5000, "most samples give a limit exactly on their boundary");
    expect(wrong == 0, "on the boundary the pair test and the scalar test decide alike");
}

void the_select_gives_the_sorted_middle() {
    std::mt19937 random(3u);
    int wrong = 0;
    for (int trial = 0; trial < 3000; ++trial) {
        const int count = 1 + static_cast<int>(random() % 500);
        const int spread = 1 + static_cast<int>(random() % 50);
        std::vector<double> values(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index)
            values[static_cast<size_t>(index)] = trial % 3 == 0 ? 1.0 + index * 1e-3
                                                 : trial % 3 == 1 ? 1.0 - index * 1e-3
                                                 : 0.5 + static_cast<double>(random() % static_cast<unsigned>(spread)) / 7.0;
        std::vector<double> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        const double middle = gpu_temporal_select_double(values.data(), count, count / 2);
        wrong += std::memcmp(&middle, &sorted[static_cast<size_t>(count / 2)], sizeof(middle)) != 0;
    }
    expect(wrong == 0, "the select returns the value a sort leaves in the middle");
}

}  // namespace

int main() {
    searches_match_the_whole_row_search();
    distinct_sums_rebuild_the_normal_equations();
    a_count_above_its_floor_is_exact();
    the_pair_test_decides_as_the_scalar_test_on_the_boundary();
    the_select_gives_the_sorted_middle();
    if (failures) return 1;
    std::cout << "GPU temporal camera search tests passed\n";
    return 0;
}
