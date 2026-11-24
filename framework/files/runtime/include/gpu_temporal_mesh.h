#ifndef PSXRECOMP_GPU_TEMPORAL_MESH_H
#define PSXRECOMP_GPU_TEMPORAL_MESH_H

#include "gpu_temporal_match.h"
#include "gpu_temporal_sprite.h"
#include <string.h>
#if !defined(GPU_TEMPORAL_CAMERA_SCALAR) && (defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64))
#define GPU_TEMPORAL_CAMERA_SSE2 1
#include <emmintrin.h>
#else
#define GPU_TEMPORAL_CAMERA_SSE2 0
#endif

/* Welds need exact current XY and GTE depth, or a complete oppositely traversed shared edge. Texture q is never a key. */
typedef struct GpuTemporalMeshStats {
    int seeded_vertices;
    int shared_vertices;
    int propagated_vertices;
    int conflicting_vertices;
    int held_vertices;
    int anchored_junctions;
    int camera_samples;
    int camera_inliers;
    int reprojected_vertices;
    int departed_faces;
    int welded_corners;
    int twin_faces;
} GpuTemporalMeshStats;

#define GPU_TEMPORAL_MESH_VOTE_EPSILON (1.0 / 64.0)

typedef struct GpuTemporalMeshNode {
    int parent, size, first, next, votes, conflict, held, pending, pending_next;
    int link, statics, camera;
    float depth;
    double min_dx, max_dx, min_dy, max_dy, dx, dy;
    double nx, ny, weight; /* a camera corner's previous position, homogeneous */
} GpuTemporalMeshNode;

typedef struct GpuTemporalMeshWorkspace {
    GpuTemporalMeshNode *nodes;
    int node_capacity;
} GpuTemporalMeshWorkspace;

/* Rigid camera motion maps a current corner to its previous screen position by a ratio of linear forms in (x, y, 1, 1/depth). */
#define GPU_TEMPORAL_CAMERA_TERMS 11
#define GPU_TEMPORAL_CAMERA_MIN_INLIERS 24
#define GPU_TEMPORAL_CAMERA_TOLERANCE 0.75 /* px; matched motion this close to the model is static world */

typedef struct GpuTemporalCamera {
    double m[GPU_TEMPORAL_CAMERA_TERMS];
    int samples, inliers;
    int shift[3]; /* camera step in level units, see gpu_temporal_mesh_pairs */
    double scale; /* previous depth / current depth = denominator * scale */
} GpuTemporalCamera;

/* Zero is the view axis, so the denominator stays positive up to a right-angle turn (from the corner it changed sign at 37 degrees). */
static inline double gpu_temporal_camera_u(float x) { return ((double)x - 160.0) / 320.0; }
static inline double gpu_temporal_camera_v(float y) { return ((double)y - 120.0) / 240.0; }

static inline int gpu_temporal_camera_project(const GpuTemporalCamera *camera,
                                              float x, float y, float depth,
                                              double *px, double *py) {
    const double u = gpu_temporal_camera_u(x), v = gpu_temporal_camera_v(y);
    const double w = 256.0 / (double)depth;
    const double *m = camera->m;
    const double denominator = m[8] * u + m[9] * v + 1.0 + m[10] * w;
    if (!(denominator > 0.25 && denominator < 4.0)) return 0;
    *px = (m[0] * u + m[1] * v + m[2] + m[3] * w) / denominator;
    *py = (m[4] * u + m[5] * v + m[6] + m[7] * w) / denominator;
    return gpu_temporal_finite((float)*px) && gpu_temporal_finite((float)*py);
}

static inline int gpu_temporal_camera_project_h(const GpuTemporalCamera *camera,
                                                float x, float y, float depth,
                                                double *nx, double *ny, double *weight) {
    const double u = gpu_temporal_camera_u(x), v = gpu_temporal_camera_v(y);
    const double w = 256.0 / (double)depth;
    const double *m = camera->m;
    *weight = (m[8] * u + m[9] * v + 1.0 + m[10] * w) * camera->scale;
    *nx = (m[0] * u + m[1] * v + m[2] + m[3] * w) * camera->scale;
    *ny = (m[4] * u + m[5] * v + m[6] + m[7] * w) * camera->scale;
    return gpu_temporal_finite((float)*nx) && gpu_temporal_finite((float)*ny) &&
           gpu_temporal_finite((float)*weight);
}

/* The exact inverse of the fitted camera, so faces placed by either meet along shared edges. */
static inline int gpu_temporal_camera_invert(const GpuTemporalCamera *camera,
                                             GpuTemporalCamera *inverse) {
    const double *m = camera->m;
    const double a[3][3] = {
        {(m[0] - 160.0 * m[8]) / 320.0, (m[1] - 160.0 * m[9]) / 320.0, (m[2] - 160.0) / 320.0},
        {(m[4] - 120.0 * m[8]) / 240.0, (m[5] - 120.0 * m[9]) / 240.0, (m[6] - 120.0) / 240.0},
        {m[8], m[9], 1.0}};
    const double t[3] = {(m[3] - 160.0 * m[10]) / 320.0, (m[7] - 120.0 * m[10]) / 240.0, m[10]};
    const double pixels[3] = {320.0, 240.0, 0.0}, centre[3] = {160.0, 120.0, 1.0};
    double b[3][3], shift[3], row[3][4];
    const double det = a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) -
                       a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0]) +
                       a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]);
    if (!(fabs(det) > 1e-9) || !(camera->scale > 0.0)) return 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const int r0 = (j + 1) % 3, r1 = (j + 2) % 3, c0 = (i + 1) % 3, c1 = (i + 2) % 3;
            b[i][j] = (a[r0][c0] * a[r1][c1] - a[r0][c1] * a[r1][c0]) / det;
        }
    if (!(fabs(b[2][2]) > 1e-9)) return 0;
    for (int i = 0; i < 3; ++i)
        shift[i] = -camera->scale * (b[i][0] * t[0] + b[i][1] * t[1] + b[i][2] * t[2]);
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j)
            row[i][j] = (pixels[i] * b[i][j] + centre[i] * b[2][j]) / b[2][2];
        row[i][3] = (pixels[i] * shift[i] + centre[i] * shift[2]) / b[2][2];
    }
    *inverse = *camera;
    memcpy(&inverse->m[0], row[0], 4 * sizeof(double));
    memcpy(&inverse->m[4], row[1], 4 * sizeof(double));
    inverse->m[8] = row[2][0]; inverse->m[9] = row[2][1]; inverse->m[10] = row[2][3];
    inverse->scale = b[2][2] / camera->scale;
    for (int term = 0; term < GPU_TEMPORAL_CAMERA_TERMS; ++term)
        if (!gpu_temporal_finite((float)inverse->m[term])) return 0;
    return inverse->scale > 0.0;
}

static inline int gpu_temporal_camera_solve(double (*normal)[GPU_TEMPORAL_CAMERA_TERMS + 1],
                                            double *solution) {
    enum { N = GPU_TEMPORAL_CAMERA_TERMS };
    double scale = 0.0;
    for (int row = 0; row < N; ++row)
        if (fabs(normal[row][row]) > scale) scale = fabs(normal[row][row]);
    if (scale <= 0.0) return 0;
    for (int column = 0; column < N; ++column) {
        int pivot = column;
        for (int row = column + 1; row < N; ++row)
            if (fabs(normal[row][column]) > fabs(normal[pivot][column])) pivot = row;
        /* One visible plane makes 1/depth an affine function of x and y. */
        if (fabs(normal[pivot][column]) < scale * 1e-10) return 0;
        if (pivot != column)
            for (int k = column; k <= N; ++k) {
                const double swap = normal[column][k];
                normal[column][k] = normal[pivot][k];
                normal[pivot][k] = swap;
            }
        for (int row = column + 1; row < N; ++row) {
            const double factor = normal[row][column] / normal[column][column];
            for (int k = column; k <= N; ++k) normal[row][k] -= factor * normal[column][k];
        }
    }
    for (int row = N - 1; row >= 0; --row) {
        double value = normal[row][N];
        for (int k = row + 1; k < N; ++k) value -= normal[row][k] * solution[k];
        solution[row] = value / normal[row][row];
    }
    return 1;
}

static inline int gpu_temporal_mesh_known_depth(float depth);

/* One array a field, so two samples take the same arithmetic at once. */
typedef struct GpuTemporalCameraSamples {
    double *u, *v, *w, *tx, *ty;
} GpuTemporalCameraSamples;

static inline int gpu_temporal_camera_pair_valid(const GpuTemporalTriangle *before,
                                                 const GpuTemporalTriangle *now) {
    double area_before, area_now, ratio;
    if (!gpu_temporal_valid(before, NULL, NULL) || !gpu_temporal_valid(now, NULL, NULL)) return 0;
    area_before = gpu_temporal_area(before);
    area_now = gpu_temporal_area(now);
    if ((area_before < 0.0) != (area_now < 0.0)) return 0;
    ratio = fabs(area_now / area_before);
    return ratio >= 0.25 && ratio <= 4.0;
}

static inline int gpu_temporal_camera_agrees(const double *m, const GpuTemporalCameraSamples *samples, int index,
                                             double limit) {
    const double u = samples->u[index], v = samples->v[index], w = samples->w[index];
    const double denominator = m[8] * u + m[9] * v + 1.0 + m[10] * w;
    double dx, dy;
    if (!(denominator > 0.25 && denominator < 4.0)) return 0;
    dx = (m[0] * u + m[1] * v + m[2] + m[3] * w) / denominator - samples->tx[index];
    dy = (m[4] * u + m[5] * v + m[6] + m[7] * w) / denominator - samples->ty[index];
    return dx * dx + dy * dy <= limit * limit;
}

#if GPU_TEMPORAL_CAMERA_SSE2
/* Bits 0 and 1 for samples `index` and `index + 1`: the scalar test's operations in its order, so the same answer. */
static inline int gpu_temporal_camera_agrees2(const __m128d *m, const GpuTemporalCameraSamples *samples, int index,
                                              __m128d limit2) {
    const __m128d u = _mm_loadu_pd(samples->u + index), v = _mm_loadu_pd(samples->v + index);
    const __m128d w = _mm_loadu_pd(samples->w + index);
    const __m128d denominator = _mm_add_pd(
        _mm_add_pd(_mm_add_pd(_mm_mul_pd(m[8], u), _mm_mul_pd(m[9], v)), _mm_set1_pd(1.0)), _mm_mul_pd(m[10], w));
    const __m128d valid = _mm_and_pd(_mm_cmpgt_pd(denominator, _mm_set1_pd(0.25)),
                                     _mm_cmplt_pd(denominator, _mm_set1_pd(4.0)));
    const __m128d dx = _mm_sub_pd(
        _mm_div_pd(_mm_add_pd(_mm_add_pd(_mm_add_pd(_mm_mul_pd(m[0], u), _mm_mul_pd(m[1], v)), m[2]),
                              _mm_mul_pd(m[3], w)), denominator),
        _mm_loadu_pd(samples->tx + index));
    const __m128d dy = _mm_sub_pd(
        _mm_div_pd(_mm_add_pd(_mm_add_pd(_mm_add_pd(_mm_mul_pd(m[4], u), _mm_mul_pd(m[5], v)), m[6]),
                              _mm_mul_pd(m[7], w)), denominator),
        _mm_loadu_pd(samples->ty + index));
    const __m128d within = _mm_cmple_pd(_mm_add_pd(_mm_mul_pd(dx, dx), _mm_mul_pd(dy, dy)), limit2);
    return _mm_movemask_pd(_mm_and_pd(valid, within));
}
#endif

/* Samples within `limit`, or any number not above `floor` once the rest could no longer lift the count above it. */
static inline int gpu_temporal_camera_count(const double *m, const GpuTemporalCameraSamples *samples, int count,
                                            double limit, int floor) {
    int agreeing = 0, index = 0;
#if GPU_TEMPORAL_CAMERA_SSE2
    __m128d terms[GPU_TEMPORAL_CAMERA_TERMS];
    const __m128d limit2 = _mm_set1_pd(limit * limit);
    for (int term = 0; term < GPU_TEMPORAL_CAMERA_TERMS; ++term) terms[term] = _mm_set1_pd(m[term]);
    for (; index + 2 <= count; index += 2) {
        int mask;
        if (!(index & 63) && agreeing + (count - index) <= floor) return agreeing;
        mask = gpu_temporal_camera_agrees2(terms, samples, index, limit2);
        agreeing += (mask & 1) + (mask >> 1);
    }
#endif
    for (; index < count; ++index) {
        if (!(index & 63) && agreeing + (count - index) <= floor) return agreeing;
        agreeing += gpu_temporal_camera_agrees(m, samples, index, limit);
    }
    return agreeing;
}

/* The normal equations' distinct sums, each lane summed in sample order, so every entry is the full sum's bits:
 * 0..9 the (u, v, 1, w) products, 10..25 and 26..41 those times the x and y row ends, 42..50 the two rows' shared terms. */
#define GPU_TEMPORAL_CAMERA_SUMS 52
typedef struct GpuTemporalCameraSums {
    double sum[GPU_TEMPORAL_CAMERA_SUMS];
} GpuTemporalCameraSums;

static const unsigned char gpu_temporal_camera_plain_left[10] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 3};
static const unsigned char gpu_temporal_camera_plain_right[10] = {0, 1, 2, 3, 1, 2, 3, 2, 3, 3};
static const unsigned char gpu_temporal_camera_shared_left[9] = {0, 0, 0, 1, 1, 2, 0, 1, 2};
static const unsigned char gpu_temporal_camera_shared_right[9] = {0, 1, 2, 1, 2, 2, 3, 3, 3};

static inline void gpu_temporal_camera_sums_add(GpuTemporalCameraSums *sums, const GpuTemporalCameraSamples *samples,
                                                int index) {
    const double u = samples->u[index], v = samples->v[index], w = samples->w[index];
    const double tx = samples->tx[index], ty = samples->ty[index];
    const double a[4] = {u, v, 1.0, w};
    const double c[4] = {-tx * u, -tx * v, -tx * w, tx}, d[4] = {-ty * u, -ty * v, -ty * w, ty};
    double *s = sums->sum;
#if GPU_TEMPORAL_CAMERA_SSE2
#define GPU_TEMPORAL_CAMERA_LANES(at, x, y) \
    _mm_storeu_pd(s + (at), _mm_add_pd(_mm_loadu_pd(s + (at)), _mm_mul_pd((x), (y))))
#define GPU_TEMPORAL_CAMERA_SHARED(at, cx, cy, dx, dy) \
    _mm_storeu_pd(s + (at), _mm_add_pd(_mm_add_pd(_mm_loadu_pd(s + (at)), _mm_mul_pd((cx), (cy))), _mm_mul_pd((dx), (dy))))
    const __m128d c01 = _mm_set_pd(c[1], c[0]), c2x = _mm_set_pd(tx, c[2]);
    const __m128d d01 = _mm_set_pd(d[1], d[0]), d2y = _mm_set_pd(ty, d[2]);
    GPU_TEMPORAL_CAMERA_LANES(0, _mm_set1_pd(u), _mm_set_pd(v, u));
    GPU_TEMPORAL_CAMERA_LANES(2, _mm_set1_pd(u), _mm_set_pd(w, 1.0));
    GPU_TEMPORAL_CAMERA_LANES(4, _mm_set1_pd(v), _mm_set_pd(1.0, v));
    GPU_TEMPORAL_CAMERA_LANES(6, _mm_set_pd(1.0, v), _mm_set_pd(1.0, w));
    GPU_TEMPORAL_CAMERA_LANES(8, _mm_set_pd(w, 1.0), _mm_set1_pd(w));
    for (int i = 0; i < 4; ++i) {
        const __m128d ai = _mm_set1_pd(a[i]);
        GPU_TEMPORAL_CAMERA_LANES(10 + 4 * i, ai, c01);
        GPU_TEMPORAL_CAMERA_LANES(12 + 4 * i, ai, c2x);
        GPU_TEMPORAL_CAMERA_LANES(26 + 4 * i, ai, d01);
        GPU_TEMPORAL_CAMERA_LANES(28 + 4 * i, ai, d2y);
    }
    GPU_TEMPORAL_CAMERA_SHARED(42, _mm_set1_pd(c[0]), c01, _mm_set1_pd(d[0]), d01);
    GPU_TEMPORAL_CAMERA_SHARED(44, c01, _mm_set_pd(c[1], c[2]), d01, _mm_set_pd(d[1], d[2]));
    GPU_TEMPORAL_CAMERA_SHARED(46, _mm_set_pd(c[2], c[1]), _mm_set1_pd(c[2]), _mm_set_pd(d[2], d[1]),
                               _mm_set1_pd(d[2]));
    GPU_TEMPORAL_CAMERA_SHARED(48, c01, _mm_set1_pd(tx), d01, _mm_set1_pd(ty));
    s[50] = s[50] + c[2] * tx + d[2] * ty;
#undef GPU_TEMPORAL_CAMERA_LANES
#undef GPU_TEMPORAL_CAMERA_SHARED
#else
    for (int lane = 0; lane < 10; ++lane)
        s[lane] += a[gpu_temporal_camera_plain_left[lane]] * a[gpu_temporal_camera_plain_right[lane]];
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) {
            s[10 + 4 * i + k] += a[i] * c[k];
            s[26 + 4 * i + k] += a[i] * d[k];
        }
    for (int lane = 0; lane < 9; ++lane) {
        const int i = gpu_temporal_camera_shared_left[lane], k = gpu_temporal_camera_shared_right[lane];
        s[42 + lane] = s[42 + lane] + c[i] * c[k] + d[i] * d[k];
    }
#endif
}

/* Rows (u, v, 1, w, 0, 0, 0, 0, -tx u, -tx v, -tx w | tx) and (0, 0, 0, 0, u, v, 1, w, -ty u, -ty v, -ty w | ty). */
static inline void gpu_temporal_camera_normal(const GpuTemporalCameraSums *sums,
                                              double (*normal)[GPU_TEMPORAL_CAMERA_TERMS + 1]) {
    const double *s = sums->sum;
    memset(normal, 0, sizeof(double) * GPU_TEMPORAL_CAMERA_TERMS * (GPU_TEMPORAL_CAMERA_TERMS + 1));
    for (int lane = 0; lane < 10; ++lane) {
        const int i = gpu_temporal_camera_plain_left[lane], k = gpu_temporal_camera_plain_right[lane];
        normal[i][k] = s[lane];
        normal[k][i] = s[lane];
        normal[4 + i][4 + k] = s[lane];
        normal[4 + k][4 + i] = s[lane];
    }
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) {
            normal[i][8 + k] = s[10 + 4 * i + k];
            normal[4 + i][8 + k] = s[26 + 4 * i + k];
            if (k < 3) {
                normal[8 + k][i] = s[10 + 4 * i + k];
                normal[8 + k][4 + i] = s[26 + 4 * i + k];
            }
        }
    for (int lane = 0; lane < 9; ++lane) {
        const int i = 8 + gpu_temporal_camera_shared_left[lane], k = 8 + gpu_temporal_camera_shared_right[lane];
        normal[i][k] = s[42 + lane];
        if (k < GPU_TEMPORAL_CAMERA_TERMS) normal[k][i] = s[42 + lane];
    }
}

/* `agreeing`, when given, gets how many samples `reference` puts within `limit`. */
static inline int gpu_temporal_camera_refit(const double *reference, double limit,
                                            const GpuTemporalCameraSamples *samples, int count, double *out,
                                            int *agreeing) {
    GpuTemporalCameraSums sums;
    double normal[GPU_TEMPORAL_CAMERA_TERMS][GPU_TEMPORAL_CAMERA_TERMS + 1];
    int used = 0, index = 0;
    memset(&sums, 0, sizeof(sums));
    if (!reference) {
        for (; index < count; ++index) gpu_temporal_camera_sums_add(&sums, samples, index);
        used = count;
    } else {
#if GPU_TEMPORAL_CAMERA_SSE2
        __m128d terms[GPU_TEMPORAL_CAMERA_TERMS];
        const __m128d limit2 = _mm_set1_pd(limit * limit);
        for (int term = 0; term < GPU_TEMPORAL_CAMERA_TERMS; ++term) terms[term] = _mm_set1_pd(reference[term]);
        for (; index + 2 <= count; index += 2) {
            const int mask = gpu_temporal_camera_agrees2(terms, samples, index, limit2);
            if (mask & 1) { gpu_temporal_camera_sums_add(&sums, samples, index); ++used; }
            if (mask & 2) { gpu_temporal_camera_sums_add(&sums, samples, index + 1); ++used; }
        }
#endif
        for (; index < count; ++index)
            if (gpu_temporal_camera_agrees(reference, samples, index, limit)) {
                gpu_temporal_camera_sums_add(&sums, samples, index);
                ++used;
            }
    }
    if (agreeing) *agreeing = used;
    if (used < GPU_TEMPORAL_CAMERA_MIN_INLIERS) return 0;
    gpu_temporal_camera_normal(&sums, normal);
    return gpu_temporal_camera_solve(normal, out);
}

#define GPU_TEMPORAL_CAMERA_DRAWS 64
#define GPU_TEMPORAL_CAMERA_TRUSTED_CORNERS 36

/* RANSAC over the first `voters` samples, then refits over all `count`. */
static inline int gpu_temporal_camera_search(const GpuTemporalCameraSamples *samples, int count, int voters,
                                             const GpuTemporalCamera *prior, GpuTemporalCamera *camera) {
    static const double limits[4] = {2.0, 1.0, GPU_TEMPORAL_CAMERA_TOLERANCE,
                                     GPU_TEMPORAL_CAMERA_TOLERANCE};
    double best[GPU_TEMPORAL_CAMERA_TERMS], trial[GPU_TEMPORAL_CAMERA_TERMS];
    int faces = voters / 3, best_count = -1, agreeing;
    unsigned random = 0x9E3779B9u;
    if (prior && prior->inliers > 0) {
        memcpy(best, prior->m, sizeof(best));
        best_count = gpu_temporal_camera_count(best, samples, voters, 1.0, -1);
    }
    if (gpu_temporal_camera_refit(NULL, 0.0, samples, voters, trial, NULL)) {
        agreeing = gpu_temporal_camera_count(trial, samples, voters, 1.0, best_count);
        if (agreeing > best_count) { best_count = agreeing; memcpy(best, trial, sizeof(best)); }
    }
    for (int draw = 0; draw < GPU_TEMPORAL_CAMERA_DRAWS && faces >= 3 && best_count < voters; ++draw) {
        GpuTemporalCameraSums sums;
        double normal[GPU_TEMPORAL_CAMERA_TERMS][GPU_TEMPORAL_CAMERA_TERMS + 1];
        int picked[3];
        for (int slot = 0; slot < 3; ++slot) {
            random = random * 1664525u + 1013904223u;
            picked[slot] = (int)((random >> 8) % (unsigned)faces);
        }
        if (picked[0] == picked[1] || picked[0] == picked[2] || picked[1] == picked[2]) continue;
        memset(&sums, 0, sizeof(sums));
        for (int slot = 0; slot < 3; ++slot)
            for (int vertex = 0; vertex < 3; ++vertex)
                gpu_temporal_camera_sums_add(&sums, samples, picked[slot] * 3 + vertex);
        gpu_temporal_camera_normal(&sums, normal);
        if (!gpu_temporal_camera_solve(normal, trial)) continue;
        agreeing = gpu_temporal_camera_count(trial, samples, voters, 1.0, best_count);
        if (agreeing > best_count) { best_count = agreeing; memcpy(best, trial, sizeof(best)); }
    }
    if (best_count < GPU_TEMPORAL_CAMERA_MIN_INLIERS || best_count * 3 < voters) return 0;
    for (int pass = 0; pass < 4; ++pass) {
        int held;
        if (gpu_temporal_camera_refit(best, limits[pass], samples, count, trial, &held) &&
            gpu_temporal_camera_count(trial, samples, count, limits[pass], held - 1) >= held)
            memcpy(best, trial, sizeof(best));
    }
    memcpy(camera->m, best, sizeof(best));
    camera->inliers = gpu_temporal_camera_count(best, samples, count, GPU_TEMPORAL_CAMERA_TOLERANCE, -1);
    return camera->inliers >= GPU_TEMPORAL_CAMERA_MIN_INLIERS;
}

/* Repeated textures on a fast turn make a wall of wrong picks agree with itself, so `trusted` pairs vote when there are enough. */
static inline int gpu_temporal_camera_fit(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n, const int *pairs,
        const int *trusted, const GpuTemporalCamera *prior,
        GpuTemporalCamera *camera) {
    GpuTemporalCameraSamples samples;
    double *store;
    const size_t room = (size_t)(curr_n > 0 ? curr_n : 0) * 3;
    int count = 0, voters = 0, found;
    memset(camera, 0, sizeof(*camera));
    camera->scale = 1.0;
    if (curr_n <= 0) return 0;
    store = (double *)malloc(room * 5 * sizeof(*store));
    if (!store) return 0;
    samples.u = store;
    samples.v = store + room;
    samples.w = store + room * 2;
    samples.tx = store + room * 3;
    samples.ty = store + room * 4;
    for (int pass = 0; pass < 2; ++pass)
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const int previous = pairs[triangle];
        if (trusted && trusted[triangle] >= 2) continue; /* a moving object says nothing about the camera */
        if ((trusted && trusted[triangle] == 1) != (pass == 0)) continue;
        if (previous < 0 || previous >= prev_n ||
            !gpu_temporal_camera_pair_valid(&prev[previous], &curr[triangle]) ||
            !gpu_temporal_mesh_known_depth(curr[triangle].depth[0]) ||
            !gpu_temporal_mesh_known_depth(curr[triangle].depth[1]) ||
            !gpu_temporal_mesh_known_depth(curr[triangle].depth[2])) continue;
        for (int vertex = 0; vertex < 3; ++vertex, ++count) {
            samples.u[count] = gpu_temporal_camera_u(curr[triangle].x[vertex]);
            samples.v[count] = gpu_temporal_camera_v(curr[triangle].y[vertex]);
            samples.w[count] = 256.0 / (double)curr[triangle].depth[vertex];
            samples.tx[count] = prev[previous].x[vertex];
            samples.ty[count] = prev[previous].y[vertex];
        }
        if (pass == 0) voters = count;
    }
    camera->samples = count;
    if (count < GPU_TEMPORAL_CAMERA_MIN_INLIERS) { free(store); return 0; }
    if (voters < GPU_TEMPORAL_CAMERA_TRUSTED_CORNERS) voters = count;
    found = gpu_temporal_camera_search(&samples, count, voters, prior, camera);
    free(store);
    return found;
}

#define GPU_TEMPORAL_CAMERA_SCALE_CORNERS 12

/* The value a sort would leave at `rank`; the rest end in no particular order. */
static inline double gpu_temporal_select_double(double *values, int count, int rank) {
    int low = 0, high = count - 1;
    while (low < high) {
        const double pivot = values[low + (high - low) / 2];
        int i = low, j = high;
        while (i <= j) {
            while (values[i] < pivot) ++i;
            while (values[j] > pivot) --j;
            if (i <= j) {
                const double swap = values[i];
                values[i++] = values[j];
                values[j--] = swap;
            }
        }
        if (rank <= j) high = j;
        else if (rank >= i) low = i;
        else break;
    }
    return values[rank];
}

/* Median of the depth ratios identical faces give, 0 when too few. */
static inline double gpu_temporal_camera_depth_scale(
        const GpuTemporalCamera *camera, const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n, const int *pairs, const int *trusted) {
    double *ratios = (double *)malloc((size_t)curr_n * 3 * sizeof(*ratios));
    double scale = 0.0;
    int count = 0;
    if (!ratios) return 0.0;
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const int previous = pairs[triangle];
        if (trusted[triangle] != 1 || previous < 0 || previous >= prev_n) continue;
        for (int vertex = 0; vertex < 3; ++vertex) {
            const float before = prev[previous].depth[vertex], now = curr[triangle].depth[vertex];
            double denominator;
            if (!gpu_temporal_mesh_known_depth(before) || !gpu_temporal_mesh_known_depth(now)) continue;
            denominator = camera->m[8] * gpu_temporal_camera_u(curr[triangle].x[vertex]) +
                          camera->m[9] * gpu_temporal_camera_v(curr[triangle].y[vertex]) + 1.0 +
                          camera->m[10] * 256.0 / now;
            if (denominator > 0.0) ratios[count++] = (double)before / ((double)now * denominator);
        }
    }
    if (count >= GPU_TEMPORAL_CAMERA_SCALE_CORNERS) {
        scale = gpu_temporal_select_double(ratios, count, count / 2);
    }
    free(ratios);
    return gpu_temporal_finite((float)scale) && scale > 0.0 ? scale : 0.0;
}

typedef struct GpuTemporalCornerEntry {
    uint64_t key;
    int corner;
} GpuTemporalCornerEntry;

static inline uint64_t gpu_temporal_corner_key(const int16_t *model, const int *shift) {
    uint64_t key = 1;
    for (int axis = 0; axis < 3; ++axis)
        key = (key << 21) | ((uint64_t)(uint32_t)(model[axis] - shift[axis]) & 0x1FFFFFu);
    return key;
}

static inline GpuTemporalCornerEntry *gpu_temporal_corner_find(GpuTemporalCornerEntry *table,
                                                               unsigned mask, uint64_t key) {
    uint64_t hash = key * UINT64_C(0x9E3779B97F4A7C15);
    unsigned slot = (unsigned)(hash >> 32) & mask;
    while (table[slot].key && table[slot].key != key) slot = (slot + 1) & mask;
    return &table[slot];
}

/* A static face keeps its three input vertices up to one integer shift per frame, so it has at most one twin. */
#define GPU_TEMPORAL_TWIN_CANDIDATES 32
#define GPU_TEMPORAL_TWIN_TRIALS 8
#define GPU_TEMPORAL_TWIN_MINIMUM 8
#define GPU_TEMPORAL_TWIN_OBJECT_MINIMUM 4
#define GPU_TEMPORAL_TWIN_OBJECT_REACH 24 /* level units a moving object may gain on the camera per frame */

typedef struct GpuTemporalTwinEntry {
    uint64_t key;
    int index, count, claim, spent;
} GpuTemporalTwinEntry;

static inline uint64_t gpu_temporal_twin_key(const GpuTemporalTriangle *triangle, const int *shift) {
    uint64_t key = triangle->material ^ UINT64_C(0x9E3779B97F4A7C15);
    for (int vertex = 0; vertex < 3; ++vertex)
        for (int axis = 0; axis < 3; ++axis) {
            key ^= (uint64_t)(uint32_t)(triangle->model[vertex][axis] + shift[axis]);
            key *= UINT64_C(0x100000001B3);
            key ^= key >> 29;
        }
    return key ? key : 1;
}

static inline int gpu_temporal_twin_same(const GpuTemporalTriangle *before,
                                         const GpuTemporalTriangle *now, const int *shift) {
    if (before->material != now->material) return 0;
    for (int vertex = 0; vertex < 3; ++vertex)
        for (int axis = 0; axis < 3; ++axis)
            if (before->model[vertex][axis] != now->model[vertex][axis] + shift[axis]) return 0;
    return 1;
}

static inline GpuTemporalTwinEntry *gpu_temporal_twin_find(GpuTemporalTwinEntry *table, unsigned mask,
                                                           uint64_t key) {
    unsigned slot = (unsigned)(key ^ (key >> 32)) & mask;
    while (table[slot].key && table[slot].key != key) slot = (slot + 1) & mask;
    return &table[slot];
}

/* Each pass uses a fresh claim number, followed by one for multiple claimants. */
static inline int gpu_temporal_twin_count(GpuTemporalTwinEntry *table, unsigned mask,
                                          const GpuTemporalTriangle *prev,
                                          const GpuTemporalTriangle *curr, int curr_n,
                                          const int *shift, const int *taken, int *slots, int pass) {
    const int claim = 2 * pass;
    int found = 0;
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        if (slots) slots[triangle] = -1;
        if (!curr[triangle].world || !curr[triangle].modelled) continue;
        if (taken && taken[triangle] >= 0) continue;
        GpuTemporalTwinEntry *entry = gpu_temporal_twin_find(table, mask, gpu_temporal_twin_key(&curr[triangle], shift));
        if (entry->count != 1 || entry->spent ||
            !gpu_temporal_twin_same(&prev[entry->index], &curr[triangle], shift)) continue;
        if (entry->claim < claim) {
            entry->claim = claim;
            ++found;
            if (slots) slots[triangle] = (int)(entry - table);
        } else if (entry->claim == claim) {
            entry->claim = claim + 1;
            --found;
        }
    }
    if (slots)
        for (int triangle = 0; triangle < curr_n; ++triangle)
            if (slots[triangle] >= 0 && table[slots[triangle]].claim != claim) slots[triangle] = -1;
    return found;
}

/* trusted: 1 may vote for the camera, 2 a twin under a second shift (moving object), 3 a rider. With twins, a modelled
 * face without one has no previous self. `shift` carries the last interval's in and this one's out. */
static inline int gpu_temporal_mesh_pairs(const GpuTemporalTriangle *prev, int prev_n,
                                          const GpuTemporalTriangle *curr, int curr_n,
                                          const int *matches, int *pairs, int *trusted,
                                          int *shift) {
    typedef struct { uint64_t material; int before, now, index, used; } Entry;
    typedef struct { int shift[3], votes; } Candidate;
    static const int zero[3] = {0, 0, 0};
    Entry *table;
    GpuTemporalTwinEntry *faces;
    Candidate candidates[GPU_TEMPORAL_TWIN_CANDIDATES];
    int candidate_count = 0, best_twins = 0, best_shift[3] = {0, 0, 0}, claim_pass = 0;
    unsigned mask = 1, face_mask = 1;
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        pairs[triangle] = matches[triangle];
        trusted[triangle] = 0;
    }
    while (mask < (unsigned)(prev_n + curr_n) * 2u) mask <<= 1;
    while (face_mask < (unsigned)prev_n * 2u) face_mask <<= 1;
    table = (Entry *)calloc((size_t)mask, sizeof(*table));
    faces = (GpuTemporalTwinEntry *)calloc((size_t)face_mask, sizeof(*faces));
    if (!table || !faces) { free(table); free(faces); return 0; }
    --mask;
    --face_mask;
    for (int pass = 0; pass < 3; ++pass) {
        const GpuTemporalTriangle *list = pass == 0 ? prev : curr;
        const int count = pass == 0 ? prev_n : curr_n;
        for (int triangle = 0; triangle < count; ++triangle) {
            uint64_t hash = list[triangle].material;
            unsigned slot;
            if (!list[triangle].world) continue;
            hash ^= hash >> 30; hash *= UINT64_C(0xbf58476d1ce4e5b9); hash ^= hash >> 27;
            slot = (unsigned)hash & mask;
            while (table[slot].used && table[slot].material != list[triangle].material)
                slot = (slot + 1) & mask;
            if (pass == 2) {
                if (table[slot].before == 1 && table[slot].now == 1) {
                    pairs[triangle] = table[slot].index;
                    trusted[triangle] = 1;
                }
                continue;
            }
            table[slot].used = 1;
            table[slot].material = list[triangle].material;
            if (pass == 0) { ++table[slot].before; table[slot].index = triangle; }
            else ++table[slot].now;
        }
    }
    free(table);

    for (int triangle = 0; triangle < prev_n; ++triangle) {
        GpuTemporalTwinEntry *entry;
        if (!prev[triangle].world || !prev[triangle].modelled) continue;
        const uint64_t key = gpu_temporal_twin_key(&prev[triangle], zero);
        entry = gpu_temporal_twin_find(faces, face_mask, key);
        if (entry->key && !gpu_temporal_twin_same(&prev[entry->index], &prev[triangle], zero)) {
            entry->count = 2; /* two different faces under one key: neither can be a twin */
            continue;
        }
        entry->key = key;
        entry->index = triangle;
        ++entry->count;
    }
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const int previous = pairs[triangle];
        int proposed[3], consistent = 1, known = -1;
        if (previous < 0 || previous >= prev_n || !curr[triangle].modelled || !prev[previous].modelled) continue;
        for (int axis = 0; axis < 3; ++axis)
            proposed[axis] = prev[previous].model[0][axis] - curr[triangle].model[0][axis];
        consistent = gpu_temporal_twin_same(&prev[previous], &curr[triangle], proposed);
        if (!consistent) continue;
        for (int index = 0; index < candidate_count && known < 0; ++index)
            if (candidates[index].shift[0] == proposed[0] && candidates[index].shift[1] == proposed[1] &&
                candidates[index].shift[2] == proposed[2]) known = index;
        if (known < 0 && candidate_count < GPU_TEMPORAL_TWIN_CANDIDATES) {
            known = candidate_count++;
            memcpy(candidates[known].shift, proposed, sizeof(proposed));
            candidates[known].votes = 0;
        }
        if (known >= 0) ++candidates[known].votes;
    }
    /* No pair proposed a shift (fast turn, repeated textures): ask a few faces against each look-alike. */
    for (int triangle = 0, asked = 0; !candidate_count && triangle < curr_n && asked < 8; ++triangle) {
        if (!curr[triangle].world || !curr[triangle].modelled) continue;
        ++asked;
        for (int previous = 0; previous < prev_n && candidate_count < GPU_TEMPORAL_TWIN_CANDIDATES; ++previous) {
            int proposed[3], known = 0;
            if (!prev[previous].world || !prev[previous].modelled ||
                prev[previous].material != curr[triangle].material) continue;
            for (int axis = 0; axis < 3; ++axis)
                proposed[axis] = prev[previous].model[0][axis] - curr[triangle].model[0][axis];
            if (!gpu_temporal_twin_same(&prev[previous], &curr[triangle], proposed)) continue;
            for (int index = 0; index < candidate_count; ++index)
                known |= !memcmp(candidates[index].shift, proposed, sizeof(proposed));
            if (known) continue;
            memcpy(candidates[candidate_count].shift, proposed, sizeof(proposed));
            candidates[candidate_count++].votes = 1;
        }
    }
    for (int trial = -2; trial < GPU_TEMPORAL_TWIN_TRIALS; ++trial) {
        const int *proposed = zero;
        int twins;
        if (trial == -1) proposed = shift;
        if (trial >= 0) {
            int top = -1;
            for (int index = 0; index < candidate_count; ++index)
                if (candidates[index].votes > 0 && (top < 0 || candidates[index].votes > candidates[top].votes))
                    top = index;
            if (top < 0) break;
            candidates[top].votes = -candidates[top].votes;
            proposed = candidates[top].shift;
        }
        if ((trial > -2 && !memcmp(proposed, zero, sizeof(zero))) ||
            (trial >= 0 && !memcmp(proposed, shift, sizeof(zero)))) continue;
        twins = gpu_temporal_twin_count(faces, face_mask, prev, curr, curr_n, proposed, NULL, NULL, ++claim_pass);
        if (twins > best_twins) { best_twins = twins; memcpy(best_shift, proposed, sizeof(best_shift)); }
    }
    if (best_twins >= GPU_TEMPORAL_TWIN_MINIMUM) {
        int *world_slots = (int *)malloc((size_t)curr_n * 2 * sizeof(*world_slots));
        if (world_slots) {
            int *object_slots = world_slots + curr_n;
            gpu_temporal_twin_count(faces, face_mask, prev, curr, curr_n, best_shift, NULL, world_slots, ++claim_pass);
            for (int triangle = 0; triangle < curr_n; ++triangle) {
                if (curr[triangle].modelled) pairs[triangle] = -1, trusted[triangle] = 0;
                if (world_slots[triangle] < 0) continue;
                GpuTemporalTwinEntry *entry = &faces[world_slots[triangle]];
                pairs[triangle] = entry->index;
                trusted[triangle] = 1;
                entry->spent = 1;
            }
            /* A vertically carried group may claim faces left by the ordinary object pass. */
            for (int lift_pass = 0; lift_pass < 2; ++lift_pass) {
                for (int index = 0; index <= candidate_count; ++index) {
                    const int *proposed = index < candidate_count ? candidates[index].shift : zero;
                    const int zero_shift = !proposed[0] && !proposed[1] && !proposed[2];
                    const int same_horizontal = proposed[0] == best_shift[0] && proposed[2] == best_shift[2];
                    const int follows_lift = !zero_shift && same_horizontal &&
                        abs(proposed[1]) <= GPU_TEMPORAL_TWIN_OBJECT_REACH && abs(proposed[1]) < abs(best_shift[1]) &&
                        abs(proposed[1] - best_shift[1]) > GPU_TEMPORAL_TWIN_OBJECT_REACH;
                    if (follows_lift != lift_pass) continue;
                    const int rides = zero_shift || follows_lift;
                    int near_camera = 1;
                    for (int axis = 0; axis < 3; ++axis)
                        near_camera &= rides || abs(proposed[axis] - best_shift[axis]) <= GPU_TEMPORAL_TWIN_OBJECT_REACH;
                    if (!near_camera || !memcmp(proposed, best_shift, sizeof(best_shift)) ||
                        gpu_temporal_twin_count(faces, face_mask, prev, curr, curr_n, proposed, pairs,
                                                object_slots, ++claim_pass) <
                            GPU_TEMPORAL_TWIN_OBJECT_MINIMUM) continue;
                    for (int triangle = 0; triangle < curr_n; ++triangle) {
                        if (object_slots[triangle] < 0) continue;
                        GpuTemporalTwinEntry *entry = &faces[object_slots[triangle]];
                        pairs[triangle] = entry->index;
                        trusted[triangle] = rides ? 3 : 2;
                        entry->spent = 1;
                    }
                }
            }
            free(world_slots);
        }
        memcpy(shift, best_shift, sizeof(best_shift));
    } else {
        best_twins = 0;
        shift[0] = shift[1] = shift[2] = 0;
    }
    free(faces);
    return best_twins;
}

#define GPU_TEMPORAL_SPRITE_DEPTH_RATIO 0.75

typedef struct GpuTemporalSpriteBox {
    double left, top, right, bottom;
} GpuTemporalSpriteBox;

/* A billboard's rectangle from its upper half {x, x+w, x},{y, y, y+h} and lower half {x+w, x, x+w},{y, y+h, y+h}. */
static inline int gpu_temporal_sprite_box(const GpuTemporalTriangle *faces, int count, int first,
                                          GpuTemporalSpriteBox *box) {
    const GpuTemporalTriangle *upper = &faces[first], *lower = upper + 1;
    if (first + 1 >= count || !upper->sprite || upper->sprite != lower->sprite ||
        !gpu_temporal_mesh_known_depth(upper->depth[0]) || !gpu_temporal_mesh_known_depth(upper->depth[1]) ||
        upper->depth[2] != upper->depth[0] || lower->depth[1] != upper->depth[0] ||
        lower->depth[0] != upper->depth[1] || lower->depth[2] != upper->depth[1] ||
        !(upper->x[0] < upper->x[1]) || !(upper->x[2] < lower->x[2]) ||
        !(upper->y[0] < upper->y[2]) || !(lower->y[0] < lower->y[2]) ||
        lower->x[0] != upper->x[1] || lower->y[0] != upper->y[1] ||
        lower->x[1] != upper->x[2] || lower->y[1] != upper->y[2]) return 0;
    if (upper->sprite >= GPU_TEMPORAL_SPRITE_ACTOR_BASE) {
        box->left = (upper->x[0] + upper->x[2]) * 0.5;
        box->right = (upper->x[1] + lower->x[2]) * 0.5;
        box->top = (upper->y[0] + upper->y[1]) * 0.5;
        box->bottom = (upper->y[2] + lower->y[2]) * 0.5;
        return 1;
    }
    if (upper->x[2] != upper->x[0] || lower->x[2] != upper->x[1]) return 0;
    box->left = upper->x[0]; box->top = upper->y[0];
    box->right = upper->x[1]; box->bottom = upper->y[2];
    return 1;
}

typedef struct GpuTemporalSpritePair {
    double distance;
    int now, was;
} GpuTemporalSpritePair;

enum { GPU_TEMPORAL_POSE_MOTION = 4 };

static inline int gpu_temporal_compare_sprite_pair(const void *left, const void *right) {
    const GpuTemporalSpritePair *a = (const GpuTemporalSpritePair *)left, *b = (const GpuTemporalSpritePair *)right;
    if (a->distance != b->distance) return a->distance < b->distance ? -1 : 1;
    return a->now != b->now ? a->now - b->now : a->was - b->was;
}

/* Billboards are paired by place, not picture: pictures change per animation frame and look-alikes stand side by side.
 * Nearest pairs first, within its own size of where the camera puts the centre and at a like depth. */
static inline void gpu_temporal_mesh_pair_sprites(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const GpuTemporalCamera *camera, int *pairs, int *trusted, int *reverse, GpuTemporalTriangle *from) {
    GpuTemporalSpritePair *candidates;
    size_t before = 0, now_count = 0;
    int count = 0;
    for (int triangle = 0; triangle < prev_n; ++triangle) before += prev[triangle].sprite != 0;
    for (int triangle = 0; triangle < curr_n; ++triangle) now_count += curr[triangle].sprite != 0;
    if (!before || !now_count) return;
    candidates = (GpuTemporalSpritePair *)malloc(before * now_count * sizeof(*candidates));
    if (!candidates) return;
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        GpuTemporalSpriteBox now, was;
        double px = 0.0, py = 0.0, reach;
        if (!gpu_temporal_sprite_box(curr, curr_n, triangle, &now) ||
            !gpu_temporal_camera_project(camera, (float)((now.left + now.right) * 0.5),
                                         (float)((now.top + now.bottom) * 0.5), curr[triangle].depth[0],
                                         &px, &py)) continue;
        reach = now.right - now.left > now.bottom - now.top ? now.right - now.left : now.bottom - now.top;
        for (int previous = 0; previous < prev_n; ++previous) {
            double dx, dy, ratio;
            if (!gpu_temporal_sprite_box(prev, prev_n, previous, &was)) continue;
            if ((prev[previous].sprite & ~GPU_TEMPORAL_SPRITE_KEEP_ART) !=
                (curr[triangle].sprite & ~GPU_TEMPORAL_SPRITE_KEEP_ART)) continue;
            ratio = (double)prev[previous].depth[0] / curr[triangle].depth[0];
            if (ratio < GPU_TEMPORAL_SPRITE_DEPTH_RATIO || ratio > 1.0 / GPU_TEMPORAL_SPRITE_DEPTH_RATIO) continue;
            dx = (was.left + was.right) * 0.5 - px;
            dy = (was.top + was.bottom) * 0.5 - py;
            if (dx * dx + dy * dy > reach * reach) continue;
            candidates[count].distance = dx * dx + dy * dy;
            candidates[count].now = triangle;
            candidates[count++].was = previous;
        }
    }
    qsort(candidates, (size_t)count, sizeof(*candidates), gpu_temporal_compare_sprite_pair);
    for (int index = 0; index < count; ++index) {
        const GpuTemporalSpritePair *pair = &candidates[index];
        int available = 1;
        for (int half = 0; half < 2; ++half) {
            const int other = reverse[pair->was + half];
            if (trusted[pair->now + half] >= 2 || (other >= 0 && trusted[other] >= 2)) available = 0;
        }
        if (!available) continue;
        const GpuTemporalTriangle *now = &curr[pair->now], *was = &prev[pair->was];
        if (!gpu_temporal_camera_pair_valid(&was[0], &now[0]) || !gpu_temporal_camera_pair_valid(&was[1], &now[1])) continue;
        GpuTemporalSpriteBox prior_box, current_box;
        const int preserve = (now->sprite & GPU_TEMPORAL_SPRITE_KEEP_ART) &&
            (now[0].material != was[0].material || now[1].material != was[1].material) &&
            gpu_temporal_sprite_box(prev, prev_n, pair->was, &prior_box) &&
            gpu_temporal_sprite_box(curr, curr_n, pair->now, &current_box);
        for (int half = 0; half < 2; ++half) {
            const int current = pair->now + half, previous = pair->was + half;
            const int old_previous = pairs[current], old_current = reverse[previous];
            if (old_previous >= 0 && old_previous < prev_n && reverse[old_previous] == current)
                reverse[old_previous] = -1;
            if (old_current >= 0 && pairs[old_current] == previous) {
                pairs[old_current] = -1;
                trusted[old_current] = 0;
            }
        }
        for (int half = 0; half < 2; ++half) {
            pairs[pair->now + half] = pair->was + half;
            trusted[pair->now + half] = preserve ? GPU_TEMPORAL_POSE_MOTION : 2;
            reverse[pair->was + half] = pair->now + half;
            if (preserve) {
                const double scale = ((double)now->depth[0] + now->depth[1]) / ((double)was->depth[0] + was->depth[1]);
                const double x = (prior_box.left + prior_box.right - scale * (current_box.left + current_box.right)) * 0.5;
                const double y = (prior_box.top + prior_box.bottom - scale * (current_box.top + current_box.bottom)) * 0.5;
                GpuTemporalTriangle *start = &from[pair->now + half];
                for (int vertex = 0; vertex < 3; ++vertex) {
                    start->x[vertex] = (float)(x + scale * now[half].x[vertex]);
                    start->y[vertex] = (float)(y + scale * now[half].y[vertex]);
                }
            }
        }
    }
    free(candidates);
}

static inline int gpu_temporal_camera_projects(const GpuTemporalCamera *camera, const GpuTemporalTriangle *triangle,
                                               double px[3], double py[3]) {
    for (int vertex = 0; vertex < 3; ++vertex)
        if (!gpu_temporal_mesh_known_depth(triangle->depth[vertex]) ||
            !gpu_temporal_camera_project(camera, triangle->x[vertex], triangle->y[vertex],
                                         triangle->depth[vertex], &px[vertex], &py[vertex])) return 0;
    return 1;
}

static inline int gpu_temporal_camera_coincides(const GpuTemporalTriangle *candidate, uint64_t material,
                                                const double px[3], const double py[3]) {
    if (candidate->material != material || !candidate->world) return 0;
    for (int vertex = 0; vertex < 3; ++vertex) {
        const double dx = candidate->x[vertex] - px[vertex];
        const double dy = candidate->y[vertex] - py[vertex];
        if (!(dx * dx + dy * dy <= GPU_TEMPORAL_CAMERA_TOLERANCE * GPU_TEMPORAL_CAMERA_TOLERANCE)) return 0;
    }
    return 1;
}

/* A previous same-material face where the camera puts this one marks a wrong pick in the static world, not a moving object. */
static inline int gpu_temporal_camera_rematch(
        const GpuTemporalCamera *camera, const GpuTemporalTriangle *prev,
        int prev_n, const GpuTemporalTriangle *triangle) {
    double px[3], py[3];
    if (!gpu_temporal_camera_projects(camera, triangle, px, py)) return -1;
    for (int candidate = 0; candidate < prev_n; ++candidate)
        if (gpu_temporal_camera_coincides(&prev[candidate], triangle->material, px, py)) return candidate;
    return -1;
}

#define GPU_TEMPORAL_FACE_CELL 4.0  /* px: a corner within the tolerance of a point lies in at most two cells a side */
#define GPU_TEMPORAL_FACE_FAR 1.0e9 /* px: past this the index is not asked */

/* A frame's world faces by material and the cell of their first corner: the answer of gpu_temporal_camera_rematch, the
 * lowest index that coincides, without its scan of every face, which grew with the square of the faces drawn. */
typedef struct GpuTemporalFaceIndex {
    const GpuTemporalTriangle *faces;
    int count, mask, failed;
    int *head, *next;
} GpuTemporalFaceIndex;

static inline uint64_t gpu_temporal_face_bucket(uint64_t material, int64_t column, int64_t row) {
    uint64_t hash = material * 0x9E3779B97F4A7C15ull;
    hash ^= (uint64_t)column * 0xC2B2AE3D27D4EB4Full + (hash << 6) + (hash >> 2);
    hash ^= (uint64_t)row * 0x165667B19E3779F9ull + (hash << 6) + (hash >> 2);
    return hash ^ (hash >> 29);
}

static inline int gpu_temporal_face_indexable(double x, double y) {
    return fabs(x) < GPU_TEMPORAL_FACE_FAR && fabs(y) < GPU_TEMPORAL_FACE_FAR;
}

static inline void gpu_temporal_face_index_free(GpuTemporalFaceIndex *index) {
    free(index->head);
    free(index->next);
    memset(index, 0, sizeof(*index));
}

static inline int gpu_temporal_face_index_build(GpuTemporalFaceIndex *index, const GpuTemporalTriangle *faces, int count) {
    int buckets = 1;
    gpu_temporal_face_index_free(index);
    while (buckets < 2 * count) buckets <<= 1;
    index->head = (int *)malloc((size_t)buckets * sizeof(*index->head));
    index->next = (int *)malloc((size_t)(count > 0 ? count : 1) * sizeof(*index->next));
    if (!index->head || !index->next) {
        gpu_temporal_face_index_free(index);
        index->failed = 1;
        return 0;
    }
    index->faces = faces;
    index->count = count;
    index->mask = buckets - 1;
    for (int bucket = 0; bucket < buckets; ++bucket) index->head[bucket] = -1;
    for (int face = 0; face < count; ++face) {
        const GpuTemporalTriangle *triangle = &faces[face];
        const double x = triangle->x[0], y = triangle->y[0];
        if (!triangle->world || !gpu_temporal_face_indexable(x, y)) continue;
        const uint64_t bucket = gpu_temporal_face_bucket(triangle->material, (int64_t)floor(x / GPU_TEMPORAL_FACE_CELL),
                                                         (int64_t)floor(y / GPU_TEMPORAL_FACE_CELL)) & (uint64_t)index->mask;
        index->next[face] = index->head[bucket];
        index->head[bucket] = face;
    }
    return 1;
}

/* gpu_temporal_camera_rematch through `index`, made of `faces` on first use. */
static inline int gpu_temporal_camera_rematch_in(GpuTemporalFaceIndex *index, const GpuTemporalTriangle *faces, int count,
                                                 const GpuTemporalCamera *camera, const GpuTemporalTriangle *triangle) {
    double px[3], py[3];
    const double reach = GPU_TEMPORAL_CAMERA_TOLERANCE;
    int best = -1;
    if (index->faces != faces && (index->failed || !gpu_temporal_face_index_build(index, faces, count)))
        return gpu_temporal_camera_rematch(camera, faces, count, triangle);
    if (!gpu_temporal_camera_projects(camera, triangle, px, py)) return -1;
    if (!gpu_temporal_face_indexable(px[0] - reach, py[0] - reach) || !gpu_temporal_face_indexable(px[0] + reach, py[0] + reach))
        return gpu_temporal_camera_rematch(camera, faces, count, triangle);
    const int64_t first_column = (int64_t)floor((px[0] - reach) / GPU_TEMPORAL_FACE_CELL);
    const int64_t last_column = (int64_t)floor((px[0] + reach) / GPU_TEMPORAL_FACE_CELL);
    const int64_t first_row = (int64_t)floor((py[0] - reach) / GPU_TEMPORAL_FACE_CELL);
    const int64_t last_row = (int64_t)floor((py[0] + reach) / GPU_TEMPORAL_FACE_CELL);
    for (int64_t column = first_column; column <= last_column; ++column)
        for (int64_t row = first_row; row <= last_row; ++row) {
            const uint64_t bucket = gpu_temporal_face_bucket(triangle->material, column, row) & (uint64_t)index->mask;
            for (int face = index->head[bucket]; face >= 0; face = index->next[face])
                if ((best < 0 || face < best) && gpu_temporal_camera_coincides(&faces[face], triangle->material, px, py))
                    best = face;
        }
    return best;
}

typedef struct GpuTemporalMeshEdge {
    int a, b, triangle, direction;
} GpuTemporalMeshEdge;

typedef struct GpuTemporalMeshFace {
    int topology, eligible, matched, queued;
} GpuTemporalMeshFace;

typedef struct GpuTemporalMeshLink {
    int vertex, next;
} GpuTemporalMeshLink;

/* For finite values the keys order and equal as the floats do: -0 is +0. */
static inline uint32_t gpu_temporal_mesh_float_key(float value) {
    uint32_t bits;
    if (value == 0.0f) value = 0.0f;
    memcpy(&bits, &value, sizeof(bits));
    return bits & 0x80000000u ? ~bits : bits | 0x80000000u;
}

static inline uint64_t gpu_temporal_mesh_pair_key(float first, float second) {
    return (uint64_t)gpu_temporal_mesh_float_key(first) << 32 | gpu_temporal_mesh_float_key(second);
}

#define GPU_TEMPORAL_MESH_RADIX_BITS 11
#define GPU_TEMPORAL_MESH_RADIX_DIGITS ((64 + GPU_TEMPORAL_MESH_RADIX_BITS - 1) / GPU_TEMPORAL_MESH_RADIX_BITS)
#define GPU_TEMPORAL_MESH_RADIX_BUCKETS (1 << GPU_TEMPORAL_MESH_RADIX_BITS)

/* Orders `order` by keys[order[i]], equal keys keeping their order. A digit that is the same everywhere costs no pass. */
static inline void gpu_temporal_mesh_radix(const uint64_t *keys, int *order, int *spare, int count) {
    uint32_t counts[GPU_TEMPORAL_MESH_RADIX_DIGITS][GPU_TEMPORAL_MESH_RADIX_BUCKETS];
    const uint64_t mask = GPU_TEMPORAL_MESH_RADIX_BUCKETS - 1;
    int *from = order, *to = spare;
    if (count < 2) return;
    memset(counts, 0, sizeof(counts));
    for (int index = 0; index < count; ++index) {
        const uint64_t key = keys[order[index]];
        for (int digit = 0; digit < GPU_TEMPORAL_MESH_RADIX_DIGITS; ++digit)
            ++counts[digit][(key >> (digit * GPU_TEMPORAL_MESH_RADIX_BITS)) & mask];
    }
    for (int digit = 0; digit < GPU_TEMPORAL_MESH_RADIX_DIGITS; ++digit) {
        const int shift = digit * GPU_TEMPORAL_MESH_RADIX_BITS;
        uint32_t *bucket = counts[digit];
        uint32_t start = 0;
        int *swap;
        if (bucket[(keys[from[0]] >> shift) & mask] == (uint32_t)count) continue;
        for (int value = 0; value < GPU_TEMPORAL_MESH_RADIX_BUCKETS; ++value) {
            const uint32_t held = bucket[value];
            bucket[value] = start;
            start += held;
        }
        for (int index = 0; index < count; ++index) {
            const int item = from[index];
            to[bucket[(keys[item] >> shift) & mask]++] = item;
        }
        swap = from; from = to; to = swap;
    }
    if (from != order) memcpy(order, from, (size_t)count * sizeof(*order));
}

/* Orders by `major`, then by `minor`, then by the place in `order` on entry. */
static inline void gpu_temporal_mesh_sort(const uint64_t *major, const uint64_t *minor, int *order, int *spare,
                                          int count) {
    gpu_temporal_mesh_radix(minor, order, spare, count);
    gpu_temporal_mesh_radix(major, order, spare, count);
}

static inline int gpu_temporal_mesh_root(GpuTemporalMeshNode *nodes, int corner) {
    int root = corner;
    while (nodes[root].parent != root) root = nodes[root].parent;
    while (nodes[corner].parent != corner) {
        const int next = nodes[corner].parent;
        nodes[corner].parent = root;
        corner = next;
    }
    return root;
}

static inline void gpu_temporal_mesh_union(GpuTemporalMeshNode *nodes,
                                            int left, int right) {
    int a = gpu_temporal_mesh_root(nodes, left);
    int b = gpu_temporal_mesh_root(nodes, right);
    if (a == b) return;
    if (nodes[a].depth > 0.0f && nodes[b].depth > 0.0f &&
        nodes[a].depth != nodes[b].depth) return;
    if (nodes[a].size < nodes[b].size) { const int swap = a; a = b; b = swap; }
    nodes[b].parent = a;
    nodes[a].size += nodes[b].size;
    nodes[a].held |= nodes[b].held;
    nodes[a].conflict |= nodes[b].conflict;
    if (nodes[a].depth == 0.0f) nodes[a].depth = nodes[b].depth;
}

static inline int gpu_temporal_mesh_valid(const GpuTemporalTriangle *triangle) {
    return gpu_temporal_valid(triangle, NULL, NULL);
}

static inline int gpu_temporal_mesh_topology(const GpuTemporalTriangle *triangle) {
    if (!triangle->world) return 0;
    for (int vertex = 0; vertex < 3; ++vertex)
        if (!gpu_temporal_finite(triangle->x[vertex]) ||
            !gpu_temporal_finite(triangle->y[vertex])) return 0;
    return 1;
}

static inline int gpu_temporal_mesh_known_depth(float depth) {
    /* SZ=65535 may be a saturated distant projection, not a shared 3D point. */
    return gpu_temporal_finite(depth) && depth > 0.0f && depth < 65535.0f;
}

static inline int gpu_temporal_mesh_depth_compatible(
        const GpuTemporalTriangle *triangles, int a, int b) {
    const float da = triangles[a / 3].depth[a % 3];
    const float db = triangles[b / 3].depth[b % 3];
    return !gpu_temporal_mesh_known_depth(da) ||
           !gpu_temporal_mesh_known_depth(db) || da == db;
}

static inline int gpu_temporal_mesh_group_depth_compatible(
        GpuTemporalMeshNode *nodes, int a, int b) {
    const int ra = gpu_temporal_mesh_root(nodes, a);
    const int rb = gpu_temporal_mesh_root(nodes, b);
    if (nodes[ra].depth == 0.0f || nodes[rb].depth == 0.0f ||
        nodes[ra].depth == nodes[rb].depth) return 1;
    /* An unknown corner must not bridge two known-depth surfaces: keep the groups distinct and pin the endpoints. */
    nodes[ra].held = nodes[rb].held = 1;
    nodes[ra].conflict = nodes[rb].conflict = 1;
    return 0;
}

static inline int gpu_temporal_mesh_compare_connection(const void *left,
                                                       const void *right) {
    const GpuTemporalMeshEdge *a = (const GpuTemporalMeshEdge *)left;
    const GpuTemporalMeshEdge *b = (const GpuTemporalMeshEdge *)right;
    if (a->a != b->a) return a->a < b->a ? -1 : 1;
    return a->b < b->b ? -1 : a->b > b->b ? 1 : 0;
}

static inline int gpu_temporal_mesh_has_connection(
        const GpuTemporalMeshEdge *edges, int count, int a, int b) {
    int lo = 0, hi = count;
    if (a > b) { const int swap = a; a = b; b = swap; }
    while (lo < hi) {
        const int middle = lo + (hi - lo) / 2;
        const GpuTemporalMeshEdge *edge = &edges[middle];
        if (edge->a < a || (edge->a == a && edge->b < b)) lo = middle + 1;
        else hi = middle;
    }
    return lo < count && edges[lo].a == a && edges[lo].b == b;
}

/* A-M/M-B plus A-B is a T junction when collinear and depth agrees projectively: pin the three groups, since independent
 * lerps need not keep M on the long edge. Degree is bounded to 16. */
static inline void gpu_temporal_mesh_anchor_junctions(
        const GpuTemporalTriangle *curr, int vertex_count,
        GpuTemporalMeshNode *nodes, GpuTemporalMeshEdge *edges, int edge_count,
        GpuTemporalMeshLink *links, GpuTemporalMeshStats *stats) {
    int link_count = 0;
    for (int index = 0; index < edge_count; ++index) {
        int a = gpu_temporal_mesh_root(nodes, edges[index].a);
        int b = gpu_temporal_mesh_root(nodes, edges[index].b);
        if (a > b) { const int swap = a; a = b; b = swap; }
        edges[index].a = a; edges[index].b = b;
    }
    qsort(edges, (size_t)edge_count, sizeof(*edges), gpu_temporal_mesh_compare_connection);
    for (int index = 0; index < edge_count; ++index) {
        const int a = edges[index].a, b = edges[index].b;
        if (a == b || (index && a == edges[index - 1].a && b == edges[index - 1].b)) continue;
        links[link_count].vertex = b; links[link_count].next = nodes[a].link;
        nodes[a].link = link_count++;
        links[link_count].vertex = a; links[link_count].next = nodes[b].link;
        nodes[b].link = link_count++;
    }
    for (int middle = 0; middle < vertex_count; ++middle) {
        int neighbors[16], degree = 0;
        double mx, my;
        if (nodes[middle].parent != middle || nodes[middle].depth <= 0.0f) continue;
        for (int link = nodes[middle].link; link >= 0; link = links[link].next) {
            if (degree == 16) { ++degree; break; }
            neighbors[degree++] = links[link].vertex;
        }
        if (degree > 16) continue;
        mx = curr[middle / 3].x[middle % 3];
        my = curr[middle / 3].y[middle % 3];
        for (int first = 0; first < degree; ++first) for (int second = first + 1; second < degree; ++second) {
            const int a = neighbors[first], b = neighbors[second];
            double ax, ay, dx, dy, length2, parameter, cross, expected_depth;
            if (nodes[a].depth <= 0.0f || nodes[b].depth <= 0.0f ||
                !gpu_temporal_mesh_has_connection(edges, edge_count, a, b)) continue;
            ax = curr[a / 3].x[a % 3]; ay = curr[a / 3].y[a % 3];
            dx = (double)curr[b / 3].x[b % 3] - ax;
            dy = (double)curr[b / 3].y[b % 3] - ay;
            length2 = dx * dx + dy * dy;
            if (length2 <= 0.0) continue;
            parameter = ((mx - ax) * dx + (my - ay) * dy) / length2;
            if (parameter <= 0.000001 || parameter >= 0.999999) continue;
            cross = (mx - ax) * dy - (my - ay) * dx;
            if (cross * cross > length2 / (32.0 * 32.0)) continue;
            expected_depth = 1.0 / ((1.0 - parameter) / nodes[a].depth + parameter / nodes[b].depth);
            if (fabs(expected_depth - nodes[middle].depth) > 2.0) continue;
            nodes[a].held = nodes[middle].held = nodes[b].held = 1;
            if (stats) ++stats->anchored_junctions;
        }
    }
}

#define GPU_TEMPORAL_CAMERA_MAX_MOTION 1024.0

#define GPU_TEMPORAL_MIN_WEIGHT 0.02

/* Camera motion has no search window: any finite weighted corner is usable, gpu_temporal_mesh_facing skips the rest. */
static inline int gpu_temporal_mesh_camera_safe(const GpuTemporalTriangle *curr,
                                                const GpuTemporalTriangle *from) {
    (void)curr;
    for (int vertex = 0; vertex < 3; ++vertex)
        if (!gpu_temporal_finite(from->x[vertex]) || !gpu_temporal_finite(from->y[vertex]) ||
            !gpu_temporal_finite(from->w[vertex])) return 0;
    return 1;
}

/* The game culls back faces, so a lerped face with flipped winding or a corner behind the viewer was not visible yet. */
static inline int gpu_temporal_mesh_facing(const GpuTemporalTriangle *curr,
                                           const GpuTemporalTriangle *lerped) {
    const double now = gpu_temporal_area(curr), then = gpu_temporal_area(lerped);
    for (int vertex = 0; vertex < 3; ++vertex)
        if (lerped->w[vertex] != 0.0f && lerped->w[vertex] < GPU_TEMPORAL_MIN_WEIGHT) return 0;
    return now == 0.0 || (now < 0.0) == (then < 0.0);
}

static inline void gpu_temporal_mesh_write_triangle(
        const GpuTemporalTriangle *current, int triangle,
        GpuTemporalMeshNode *nodes, GpuTemporalTriangle *out) {
    for (int vertex = 0; vertex < 3; ++vertex) {
        const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
        if (nodes[root].camera && nodes[root].weight != 0.0) {
            out->x[vertex] = (float)nodes[root].nx;
            out->y[vertex] = (float)nodes[root].ny;
            out->w[vertex] = (float)nodes[root].weight;
            continue;
        }
        out->x[vertex] = (float)((double)current->x[vertex] + nodes[root].dx);
        out->y[vertex] = (float)((double)current->y[vertex] + nodes[root].dy);
        out->w[vertex] = 0.0f;
    }
}

/* Returns the number of current triangles with retained XY or texture-q motion. `from` has current indexing and starts as
 * current. Only a matched face gets its own prior q. HUD, invalid inputs and allocation failure hold. Stats count unique
 * eligible vertex groups, except propagated_vertices (unmatched corners receiving shared motion). */
/* `departed*` have previous indexing and may be NULL: a previous face the game no longer submits is flagged and given its
 * path's two ends (a shared corner takes the current face's path). The flag is 2 for a corner without depth: it stays put. */
static inline int gpu_temporal_mesh_prepare_scene(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const int *matches, GpuTemporalTriangle *from,
        GpuTemporalMeshStats *stats,
        GpuTemporalTriangle *departed_from, GpuTemporalTriangle *departed_to, int *departed,
        GpuTemporalCamera *prior, GpuTemporalMeshWorkspace *workspace) {
    GpuTemporalMeshNode *nodes = NULL;
    uint64_t *major = NULL, *minor = NULL;
    int *order = NULL, *spare = NULL;
    GpuTemporalMeshEdge *edges = NULL;
    GpuTemporalMeshFace *faces = NULL;
    GpuTemporalMeshLink *links = NULL;
    GpuTemporalCornerEntry *corners = NULL;
    int *queue = NULL, *reverse = NULL, *pairs = NULL, *trusted = NULL;
    int point_count = 0, edge_count = 0, queue_head = 0, queue_tail = 0;
    int queue_count = 0, moving_triangles = 0;
    int vertex_count, has_camera = 0, has_forward = 0, twins = 0;
    int shift[3] = {0, 0, 0};
    unsigned corner_mask = 1;
    GpuTemporalCamera camera, forward;
    GpuTemporalFaceIndex prev_faces, curr_faces;
    memset(&prev_faces, 0, sizeof(prev_faces));
    memset(&curr_faces, 0, sizeof(curr_faces));
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!departed_to || !departed_from) departed = NULL;
    if (departed && prev)
        for (int triangle = 0; triangle < prev_n; ++triangle) departed[triangle] = 0;
    if (!curr || !from || curr_n <= 0) return 0;
    for (int triangle = 0; triangle < curr_n; ++triangle) from[triangle] = curr[triangle];
    if (!prev || !matches || prev_n <= 0 ||
        curr_n > GPU_TEMPORAL_MAX_TRIANGLES ||
        prev_n > GPU_TEMPORAL_MAX_TRIANGLES) return 0;
    vertex_count = curr_n * 3;
    if (workspace) {
        if (vertex_count > workspace->node_capacity) {
            GpuTemporalMeshNode *grown = (GpuTemporalMeshNode *)malloc((size_t)vertex_count * sizeof(*grown));
            if (!grown) return 0;
            free(workspace->nodes);
            workspace->nodes = grown;
            workspace->node_capacity = vertex_count;
        }
        nodes = workspace->nodes;
        memset(nodes, 0, (size_t)vertex_count * sizeof(*nodes));
    } else {
        nodes = (GpuTemporalMeshNode *)calloc((size_t)vertex_count, sizeof(*nodes));
    }
    major = (uint64_t *)malloc((size_t)vertex_count * (2 * sizeof(*major) + 2 * sizeof(*order)));
    edges =(GpuTemporalMeshEdge *)malloc((size_t)vertex_count * sizeof(*edges));
    faces = (GpuTemporalMeshFace *)calloc((size_t)curr_n, sizeof(*faces));
    queue = (int *)malloc((size_t)curr_n * sizeof(*queue));
    links = (GpuTemporalMeshLink *)malloc((size_t)vertex_count * 2 * sizeof(*links));
    reverse = (int *)malloc((size_t)prev_n * sizeof(*reverse));
    pairs = (int *)malloc((size_t)curr_n * sizeof(*pairs));
    trusted = (int *)malloc((size_t)curr_n * sizeof(*trusted));
    if (!nodes || !major || !edges || !faces || !queue || !links || !reverse || !pairs ||
        !trusted) goto cleanup;
    minor = major + vertex_count;
    order = (int *)(minor + vertex_count);
    spare = order + vertex_count;

    for (int corner = 0; corner < vertex_count; ++corner) {
        nodes[corner].parent = corner;
        nodes[corner].size = 1;
        nodes[corner].first = -1;
        nodes[corner].link = -1;
        if (gpu_temporal_mesh_known_depth(curr[corner / 3].depth[corner % 3]))
            nodes[corner].depth = curr[corner / 3].depth[corner % 3];
    }
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const GpuTemporalTriangle *t = &curr[triangle];
        faces[triangle].topology = gpu_temporal_mesh_topology(t);
        faces[triangle].eligible = gpu_temporal_mesh_valid(t);
        if (!faces[triangle].topology) continue;
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int next = (vertex + 1) % 3;
            GpuTemporalMeshEdge *edge = &edges[edge_count++];
            int a = vertex, b = next;
            edge->direction = 1;
            if (t->x[a] > t->x[b] || (t->x[a] == t->x[b] && t->y[a] > t->y[b])) {
                a = next; b = vertex; edge->direction = -1;
            }
            edge->a = triangle * 3 + a; edge->b = triangle * 3 + b;
            edge->triangle = triangle;
            if (gpu_temporal_mesh_known_depth(t->depth[vertex])) {
                const int corner = triangle * 3 + vertex;
                major[corner] = gpu_temporal_mesh_pair_key(t->x[vertex], t->y[vertex]);
                minor[corner] = gpu_temporal_mesh_float_key(t->depth[vertex]);
                order[point_count++] = corner;
            }
        }
    }

    if (prior) memcpy(shift, prior->shift, sizeof(shift));
    twins = gpu_temporal_mesh_pairs(prev, prev_n, curr, curr_n, matches, pairs, trusted, shift);
    gpu_temporal_mesh_sort(major, minor, order, spare, point_count);
    /* The world goes by a rider: a corner of one that meets a corner of the other is not welded to it. */
    for (int point = 0, last[2] = {-1, -1}; point < point_count; ++point) {
        const int corner = order[point];
        const int rides = trusted[corner / 3] == 3;
        if (point && (major[corner] != major[order[point - 1]] || minor[corner] != minor[order[point - 1]]))
            last[0] = last[1] = -1;
        if (last[rides] >= 0) gpu_temporal_mesh_union(nodes, last[rides], corner);
        last[rides] = corner;
    }
    for (int edge = 0; edge < edge_count; ++edge) {
        const int a = edges[edge].a, b = edges[edge].b;
        major[edge] = gpu_temporal_mesh_pair_key(curr[a / 3].x[a % 3], curr[a / 3].y[a % 3]);
        minor[edge] = gpu_temporal_mesh_pair_key(curr[b / 3].x[b % 3], curr[b / 3].y[b % 3]);
        order[edge] = edge;
    }
    gpu_temporal_mesh_sort(major, minor, order, spare, edge_count);
    for (int first = 0; first < edge_count;) {
        int end = first + 1;
        while (end < edge_count && major[order[first]] == major[order[end]] && minor[order[first]] == minor[order[end]])
            ++end;
        if (end - first == 2) {
            const GpuTemporalMeshEdge *a = &edges[order[first]], *b = &edges[order[first + 1]];
            if (a->triangle != b->triangle && a->direction != b->direction &&
                (trusted[a->triangle] == 3) == (trusted[b->triangle] == 3) &&
                gpu_temporal_mesh_depth_compatible(curr, a->a, b->a) &&
                gpu_temporal_mesh_depth_compatible(curr, a->b, b->b)) {
                const int compatible_a = gpu_temporal_mesh_group_depth_compatible(nodes, a->a, b->a);
                const int compatible_b = gpu_temporal_mesh_group_depth_compatible(nodes, a->b, b->b);
                if (!compatible_a || !compatible_b) {
                    /* Reject the edge atomically: pinning one endpoint would let the other separate. */
                    const int endpoints[4] = {a->a, a->b, b->a, b->b};
                    for (int endpoint = 0; endpoint < 4; ++endpoint) {
                        const int root = gpu_temporal_mesh_root(nodes, endpoints[endpoint]);
                        nodes[root].held = nodes[root].conflict = 1;
                    }
                    first = end;
                    continue;
                }
                gpu_temporal_mesh_union(nodes, a->a, b->a);
                gpu_temporal_mesh_union(nodes, a->b, b->b);
            }
        }
        first = end;
    }

    /* Incidence lists after welding bound propagation: each group is pinned once. */
    for (int corner = 0; corner < vertex_count; ++corner) {
        int root;
        if (!faces[corner / 3].topology) continue;
        root = gpu_temporal_mesh_root(nodes, corner);
        nodes[corner].next = nodes[root].first;
        nodes[root].first = corner;
        if (!faces[corner / 3].eligible) {
            /* A collapsed rider edge cannot open while its endpoints share a node. */
            if (trusted[corner / 3] == 3) {
                const int first = corner - corner % 3;
                const int a = gpu_temporal_mesh_root(nodes, first);
                const int b = gpu_temporal_mesh_root(nodes, first + 1);
                const int c = gpu_temporal_mesh_root(nodes, first + 2);
                if (a == b || a == c || b == c) continue;
            }
            nodes[root].held = 1;
        }
    }
    has_camera = gpu_temporal_camera_fit(prev, prev_n, curr, curr_n, pairs, trusted, prior, &camera);
    memcpy(camera.shift, shift, sizeof(shift));
    if (has_camera) {
        const double scale = gpu_temporal_camera_depth_scale(&camera, prev, prev_n, curr, curr_n,
                                                             pairs, trusted);
        if (scale > 0.0) {
            camera.scale = scale;
            has_forward = gpu_temporal_camera_invert(&camera, &forward);
        }
    }
    if (prior) {
        if (has_camera) *prior = camera;
        else prior->inliers = 0;
    }
    if (stats) stats->twin_faces = twins;
    if (stats) {
        stats->camera_samples = camera.samples;
        stats->camera_inliers = has_camera ? camera.inliers : 0;
    }
    if (!has_camera) {
        gpu_temporal_mesh_anchor_junctions(curr, vertex_count, nodes, edges, edge_count, links, stats);
    } else {
        for (int triangle = 0; triangle < prev_n; ++triangle) reverse[triangle] = -1;
        for (int triangle = 0; triangle < curr_n; ++triangle)
            if (pairs[triangle] >= 0 && pairs[triangle] < prev_n) reverse[pairs[triangle]] = triangle;
        gpu_temporal_mesh_pair_sprites(prev, prev_n, curr, curr_n, &camera, pairs, trusted, reverse, from);
    }
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        int previous = has_camera ? pairs[triangle] : matches[triangle];
        int dynamic = !has_camera, aliased = 0;
        if (!faces[triangle].eligible || previous < 0 || previous >= prev_n ||
            (has_camera ? !gpu_temporal_camera_pair_valid(&prev[previous], &curr[triangle])
                        : !gpu_temporal_mesh_valid(&prev[previous]) ||
                          !gpu_temporal_compatible(&prev[previous], &curr[triangle]))) continue;
        if (has_camera && trusted[triangle] >= 2) dynamic = 1;
        for (int vertex = 0; vertex < 3 && !dynamic; ++vertex) {
            double px, py;
            if (!gpu_temporal_mesh_known_depth(curr[triangle].depth[vertex]) ||
                !gpu_temporal_camera_project(&camera, curr[triangle].x[vertex],
                                             curr[triangle].y[vertex],
                                             curr[triangle].depth[vertex], &px, &py)) {
                dynamic = 1;
                break;
            }
            px -= prev[previous].x[vertex]; py -= prev[previous].y[vertex];
            dynamic = px * px + py * py >
                      GPU_TEMPORAL_CAMERA_TOLERANCE * GPU_TEMPORAL_CAMERA_TOLERANCE;
        }
        if (has_camera && dynamic && !trusted[triangle]) {
            const int rematched = gpu_temporal_camera_rematch_in(&prev_faces, prev, prev_n, &camera, &curr[triangle]);
            if (rematched >= 0) { previous = rematched; dynamic = 0; }
        }
        if (dynamic && has_forward && !trusted[triangle]) {
            /* The past face still stands where the camera takes it: a look-alike, and the face itself is new. */
            const int stayed = gpu_temporal_camera_rematch_in(&curr_faces, curr, curr_n, &forward, &prev[previous]);
            if (stayed >= 0 && stayed != triangle) dynamic = 0, aliased = 1;
        }
        faces[triangle].matched = !aliased;
        const GpuTemporalTriangle *motion = trusted[triangle] == GPU_TEMPORAL_POSE_MOTION ? &from[triangle] : &prev[previous];
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
            GpuTemporalMeshNode *node = &nodes[root];
            const double dx = (double)motion->x[vertex] - curr[triangle].x[vertex];
            const double dy = (double)motion->y[vertex] - curr[triangle].y[vertex];
            if (!aliased) from[triangle].q[vertex] = motion->q[vertex];
            if (!dynamic) { ++node->statics; continue; }
            if (!node->votes) {
                node->min_dx = node->max_dx = dx;
                node->min_dy = node->max_dy = dy;
            } else {
                if (dx < node->min_dx) node->min_dx = dx;
                if (dx > node->max_dx) node->max_dx = dx;
                if (dy < node->min_dy) node->min_dy = dy;
                if (dy > node->max_dy) node->max_dy = dy;
            }
            ++node->votes;
        }
    }
    for (int corner = 0; corner < vertex_count; ++corner) {
        GpuTemporalMeshNode *node = &nodes[corner];
        double spread_x = 0.0, spread_y = 0.0;
        int consistent;
        if (node->parent != corner || node->first < 0) continue;
        if (node->votes) {
            spread_x = node->max_dx - node->min_dx;
            spread_y = node->max_dy - node->min_dy;
        }
        consistent = node->votes &&
            spread_x * spread_x + spread_y * spread_y <=
            GPU_TEMPORAL_MESH_VOTE_EPSILON * GPU_TEMPORAL_MESH_VOTE_EPSILON;
        /* Only a group every matched face around it moves alike, and unlike the camera, is a moving object. */
        if (has_camera && trusted[corner / 3] != 3 && node->depth > 0.0f && !(consistent && !node->statics)) {
            const float x = curr[corner / 3].x[corner % 3];
            const float y = curr[corner / 3].y[corner % 3];
            if (gpu_temporal_camera_project_h(&camera, x, y, node->depth,
                                              &node->nx, &node->ny, &node->weight) &&
                node->weight != 0.0) {
                /* dx and dy only tell the passes below that this group moves. */
                node->dx = node->nx / node->weight - x; node->dy = node->ny / node->weight - y;
                if (node->dx == 0.0 && node->dy == 0.0 && node->weight != 1.0) node->dx = 1e-9;
                node->conflict = node->votes && !consistent;
                node->held = 0; node->camera = 1;
                if (stats) ++stats->reprojected_vertices;
                continue;
            }
        }
        if (!node->votes) continue;
        if (!consistent) {
            node->conflict = node->held = 1;
        } else if (!node->held) {
            /* A bounding-box midpoint keeps consensus independent of submission order. */
            node->dx = (node->min_dx + node->max_dx) * 0.5;
            node->dy = (node->min_dy + node->max_dy) * 0.5;
        }
    }

    for (int triangle = 0; triangle < curr_n; ++triangle) {
        if (!faces[triangle].eligible) continue;
        queue[queue_tail] = triangle;
        queue_tail = (queue_tail + 1) % curr_n;
        ++queue_count;
        faces[triangle].queued = 1;
    }
    while (queue_count) {
        const int batch_count = queue_count;
        int pending_head = -1;
        /* Evaluate a whole batch before pinning: safety cannot depend on submission order. */
        for (int item = 0; item < batch_count; ++item) {
            const int triangle = queue[queue_head];
            GpuTemporalTriangle candidate = from[triangle];
            queue_head = (queue_head + 1) % curr_n;
            --queue_count;
            faces[triangle].queued = 0;
            int by_camera = has_camera;
            const int previous = pairs[triangle];
            const GpuTemporalTriangle *past = trusted[triangle] == 3 && previous >= 0 && previous < prev_n
                ? &prev[previous] : NULL;
            int exact_rider = past != NULL;
            gpu_temporal_mesh_write_triangle(&curr[triangle], triangle, nodes, &candidate);
            for (int vertex = 0; vertex < 3; ++vertex) {
                by_camera &= nodes[gpu_temporal_mesh_root(nodes, triangle * 3 + vertex)].camera;
                if (exact_rider) {
                    const double dx = (double)candidate.x[vertex] - past->x[vertex];
                    const double dy = (double)candidate.y[vertex] - past->y[vertex];
                    exact_rider = candidate.w[vertex] == 0.0f &&
                        dx * dx + dy * dy <= GPU_TEMPORAL_MESH_VOTE_EPSILON * GPU_TEMPORAL_MESH_VOTE_EPSILON;
                }
            }
            if (by_camera ? gpu_temporal_mesh_camera_safe(&curr[triangle], &candidate)
                          : gpu_temporal_mesh_valid(&candidate) &&
                            gpu_temporal_compatible_pose(&curr[triangle], &candidate, exact_rider)) continue;
            for (int vertex = 0; vertex < 3; ++vertex) {
                const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
                GpuTemporalMeshNode *node = &nodes[root];
                if (node->pending || (node->dx == 0.0 && node->dy == 0.0)) continue;
                /* A voted corner that fails must not stop the camera's. */
                if (!by_camera && node->camera) continue;
                node->pending = 1;
                node->pending_next = pending_head;
                pending_head = root;
            }
        }
        while (pending_head >= 0) {
            const int root = pending_head;
            GpuTemporalMeshNode *node = &nodes[root];
            const float x = curr[root / 3].x[root % 3], y = curr[root / 3].y[root % 3];
            pending_head = node->pending_next;
            node->pending = 0;
            if (has_camera && trusted[root / 3] != 3 && !node->camera && node->depth > 0.0f &&
                gpu_temporal_camera_project_h(&camera, x, y, node->depth,
                                              &node->nx, &node->ny, &node->weight) &&
                node->weight != 0.0) {
                node->dx = node->nx / node->weight - x; node->dy = node->ny / node->weight - y;
                if (node->dx == 0.0 && node->dy == 0.0 && node->weight != 1.0) node->dx = 1e-9;
                node->camera = 1;
                if (stats) ++stats->reprojected_vertices;
            } else {
                node->dx = node->dy = 0.0;
                node->held = 1;
            }
            for (int corner = node->first; corner >= 0; corner = nodes[corner].next) {
                const int affected = corner / 3;
                if (!faces[affected].eligible || faces[affected].queued) continue;
                queue[queue_tail] = affected;
                queue_tail = (queue_tail + 1) % curr_n;
                ++queue_count;
                faces[affected].queued = 1;
            }
        }
    }

    for (int triangle = 0; triangle < curr_n; ++triangle) {
        int moving = 0, position_motion = 0;
        /* With a camera a tiny face follows its corners instead of pinning them. */
        if (!faces[triangle].eligible && !(has_camera && faces[triangle].topology)) continue;
        gpu_temporal_mesh_write_triangle(&curr[triangle], triangle, nodes, &from[triangle]);
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
            const int changed = from[triangle].x[vertex] != curr[triangle].x[vertex] ||
                                from[triangle].y[vertex] != curr[triangle].y[vertex] ||
                                from[triangle].w[vertex] != 0.0f;
            position_motion |= changed;
            if (stats && !faces[triangle].matched && changed && nodes[root].votes)
                ++stats->propagated_vertices;
        }
        /* A completely held face keeps its current perspective mapping too. */
        if (!position_motion)
            for (int vertex = 0; vertex < 3; ++vertex)
                from[triangle].q[vertex] = curr[triangle].q[vertex];
        for (int vertex = 0; vertex < 3; ++vertex)
            moving |= from[triangle].x[vertex] != curr[triangle].x[vertex] ||
                      from[triangle].y[vertex] != curr[triangle].y[vertex] ||
                      from[triangle].w[vertex] != 0.0f ||
                      from[triangle].q[vertex] != curr[triangle].q[vertex];
        moving_triangles += moving != 0;
    }
    if (stats) for (int corner = 0; corner < vertex_count; ++corner) {
        const GpuTemporalMeshNode *node = &nodes[corner];
        if (node->parent != corner || node->first < 0) continue;
        stats->seeded_vertices += node->votes > 0;
        stats->shared_vertices += node->size > 1;
        stats->conflicting_vertices += node->conflict != 0;
        stats->held_vertices += node->held || (!node->votes && !node->camera);
    }
    if (departed && has_forward) {
        static const int zero[3] = {0, 0, 0};
        while (corner_mask < (unsigned)vertex_count * 2u) corner_mask <<= 1;
        corners = (GpuTemporalCornerEntry *)calloc((size_t)corner_mask, sizeof(*corners));
        --corner_mask;
        for (int corner = 0; corners && corner < vertex_count; ++corner) {
            const GpuTemporalTriangle *face = &curr[corner / 3];
            GpuTemporalCornerEntry *entry;
            if (!face->world || !face->modelled) continue;
            entry = gpu_temporal_corner_find(corners, corner_mask,
                                             gpu_temporal_corner_key(face->model[corner % 3], zero));
            if (entry->key) continue;
            entry->key = gpu_temporal_corner_key(face->model[corner % 3], zero);
            entry->corner = corner;
        }
    }
    if (departed && has_forward) for (int triangle = 0; triangle < prev_n; ++triangle) {
        GpuTemporalTriangle start = prev[triangle], to = prev[triangle];
        int known = prev[triangle].world && reverse[triangle] < 0;
        int carried = 0;
        for (int vertex = 0; vertex < 3 && known; ++vertex) {
            const float x = prev[triangle].x[vertex], y = prev[triangle].y[vertex];
            double nx = 0.0, ny = 0.0, weight = 0.0;
            const GpuTemporalCornerEntry *entry = NULL;
            start.w[vertex] = 0.0f;
            if (prev[triangle].depth[vertex] == 0.0f) { to.w[vertex] = 0.0f; continue; }
            known = gpu_temporal_mesh_known_depth(prev[triangle].depth[vertex]);
            if (!known) break;
            ++carried;
            if (corners && prev[triangle].modelled)
                entry = gpu_temporal_corner_find(
                    corners, corner_mask, gpu_temporal_corner_key(prev[triangle].model[vertex], shift));
            if (entry && entry->key) {
                const GpuTemporalMeshNode *node = &nodes[gpu_temporal_mesh_root(nodes, entry->corner)];
                const double dx = node->weight != 0.0 ? node->nx / node->weight - x : 0.0;
                const double dy = node->weight != 0.0 ? node->ny / node->weight - y : 0.0;
                /* The same level corner starts where this one stands. */
                if (node->camera && node->weight != 0.0 &&
                    dx * dx + dy * dy <= 4.0 * GPU_TEMPORAL_CAMERA_TOLERANCE * GPU_TEMPORAL_CAMERA_TOLERANCE) {
                    start.x[vertex] = (float)node->nx; start.y[vertex] = (float)node->ny;
                    start.w[vertex] = (float)node->weight;
                    to.x[vertex] = curr[entry->corner / 3].x[entry->corner % 3];
                    to.y[vertex] = curr[entry->corner / 3].y[entry->corner % 3];
                    to.w[vertex] = 0.0f;
                    to.depth[vertex] = curr[entry->corner / 3].depth[entry->corner % 3];
                    if (stats) ++stats->welded_corners;
                    continue;
                }
            }
            known = gpu_temporal_camera_project_h(&forward, x, y, prev[triangle].depth[vertex],
                                                  &nx, &ny, &weight) && weight != 0.0;
            to.x[vertex] = (float)nx; to.y[vertex] = (float)ny; to.w[vertex] = (float)weight;
            to.depth[vertex] = (float)(prev[triangle].depth[vertex] * weight);
        }
        /* A face still submitted has its own interpolated copy on top. */
        if (!known || !carried || !gpu_temporal_mesh_camera_safe(&prev[triangle], &to) ||
            gpu_temporal_camera_rematch_in(&curr_faces, curr, curr_n, &forward, &prev[triangle]) >= 0) continue;
        departed_from[triangle] = start;
        departed_to[triangle] = to;
        departed[triangle] = carried == 3 ? 1 : 2;
        if (stats) ++stats->departed_faces;
    }

cleanup:
    gpu_temporal_face_index_free(&prev_faces);
    gpu_temporal_face_index_free(&curr_faces);
    free(corners);
    free(trusted);
    free(pairs);
    free(reverse);
    free(links);
    free(queue);
    free(faces);
    free(edges);
    free(major);
    if (!workspace) free(nodes);
    return moving_triangles;
}

static inline int gpu_temporal_mesh_prepare(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const int *matches, GpuTemporalTriangle *from,
        GpuTemporalMeshStats *stats) {
    return gpu_temporal_mesh_prepare_scene(prev, prev_n, curr, curr_n, matches,
                                           from, stats, NULL, NULL, NULL, NULL, NULL);
}

#endif /* PSXRECOMP_GPU_TEMPORAL_MESH_H */
