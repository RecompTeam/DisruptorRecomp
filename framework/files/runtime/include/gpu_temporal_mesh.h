#ifndef PSXRECOMP_GPU_TEMPORAL_MESH_H
#define PSXRECOMP_GPU_TEMPORAL_MESH_H

#include "gpu_temporal_match.h"
#include <string.h>

/* Current-frame topology reconciles the histories found by triangle matching.
 * It never finds temporal identity from projected coordinates. Known-depth
 * corners share exact current XY and absolute GTE depth. Without depth, only
 * an unambiguous, oppositely traversed complete shared edge establishes a weld.
 * Texture q is normalized separately by each polygon and is never a weld key.
 */
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

/* Rigid camera motion maps a current corner (x, y, depth) to its previous
 * screen position as a ratio of linear forms in (x, y, 1, 1/depth). Fitted to
 * the matched corners, it moves the whole static world, matched or not, and
 * coincident corners get the same answer, so nothing has to be pinned. */
#define GPU_TEMPORAL_CAMERA_TERMS 11
#define GPU_TEMPORAL_CAMERA_MIN_INLIERS 24
#define GPU_TEMPORAL_CAMERA_TOLERANCE 0.75 /* px; matched motion this close to the model is static world */

typedef struct GpuTemporalCamera {
    double m[GPU_TEMPORAL_CAMERA_TERMS];
    int samples, inliers;
    int shift[3]; /* camera step in level units, see gpu_temporal_mesh_pairs */
    double scale; /* previous depth / current depth = denominator * scale */
} GpuTemporalCamera;

/* Screen coordinates as the model sees them: zero is the view axis, where
 * the denominator stays positive for any turn short of a right angle. From
 * the corner it changed sign at 37 degrees and the model fell apart. */
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

/* The same projection without the division, for any corner at all. */
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

/* The camera taking previous corners to current ones. It is the exact inverse,
 * not a second fit: a corner sent there and back returns where it started, so
 * faces placed by either camera meet along their shared edges. */
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

typedef struct GpuTemporalCameraSample {
    double u, v, w, tx, ty;
} GpuTemporalCameraSample;

/* The same face in two frames, without the matcher's search window. */
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

static inline int gpu_temporal_camera_agrees(const double *m, const GpuTemporalCameraSample *sample,
                                             double limit) {
    const double denominator = m[8] * sample->u + m[9] * sample->v + 1.0 + m[10] * sample->w;
    double dx, dy;
    if (!(denominator > 0.25 && denominator < 4.0)) return 0;
    dx = (m[0] * sample->u + m[1] * sample->v + m[2] + m[3] * sample->w) / denominator - sample->tx;
    dy = (m[4] * sample->u + m[5] * sample->v + m[6] + m[7] * sample->w) / denominator - sample->ty;
    return dx * dx + dy * dy <= limit * limit;
}

static inline int gpu_temporal_camera_count(const double *m, const GpuTemporalCameraSample *samples,
                                            int count, double limit) {
    int agreeing = 0;
    for (int index = 0; index < count; ++index)
        agreeing += gpu_temporal_camera_agrees(m, &samples[index], limit);
    return agreeing;
}

static inline void gpu_temporal_camera_accumulate(
        double (*normal)[GPU_TEMPORAL_CAMERA_TERMS + 1], const GpuTemporalCameraSample *sample) {
    enum { N = GPU_TEMPORAL_CAMERA_TERMS };
    double rows[2][GPU_TEMPORAL_CAMERA_TERMS + 1];
    memset(rows, 0, sizeof(rows));
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

/* Least squares over the samples that agree with `reference`, or over all. */
static inline int gpu_temporal_camera_refit(const double *reference, double limit,
                                            const GpuTemporalCameraSample *samples, int count,
                                            double *out) {
    double normal[GPU_TEMPORAL_CAMERA_TERMS][GPU_TEMPORAL_CAMERA_TERMS + 1];
    int used = 0;
    memset(normal, 0, sizeof(normal));
    for (int index = 0; index < count; ++index) {
        if (reference && !gpu_temporal_camera_agrees(reference, &samples[index], limit)) continue;
        gpu_temporal_camera_accumulate(normal, &samples[index]);
        ++used;
    }
    return used >= GPU_TEMPORAL_CAMERA_MIN_INLIERS && gpu_temporal_camera_solve(normal, out);
}

#define GPU_TEMPORAL_CAMERA_DRAWS 64
#define GPU_TEMPORAL_CAMERA_TRUSTED_CORNERS 36

/* `pairs` gives a candidate previous face per current face. Look-alike pairs
 * and moving objects are common, so the camera is the hypothesis most pairs
 * agree with: the previous interval's camera, a fit of everything, or a fit
 * of three faces drawn at random. A windowed match can pick the next tile of
 * a repeated texture on a fast turn, and a whole wall of those agrees with
 * itself, so the vote belongs to `trusted` pairs when there are enough. */
static inline int gpu_temporal_camera_fit(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n, const int *pairs,
        const int *trusted, const GpuTemporalCamera *prior,
        GpuTemporalCamera *camera) {
    static const double limits[4] = {2.0, 1.0, GPU_TEMPORAL_CAMERA_TOLERANCE,
                                     GPU_TEMPORAL_CAMERA_TOLERANCE};
    GpuTemporalCameraSample *samples;
    double best[GPU_TEMPORAL_CAMERA_TERMS], trial[GPU_TEMPORAL_CAMERA_TERMS];
    int count = 0, faces, best_count = -1, agreeing, voters;
    unsigned random = 0x9E3779B9u;
    memset(camera, 0, sizeof(*camera));
    camera->scale = 1.0;
    if (curr_n <= 0) return 0;
    samples = (GpuTemporalCameraSample *)malloc((size_t)curr_n * 3 * sizeof(*samples));
    if (!samples) return 0;
    voters = 0;
    for (int pass = 0; pass < 2; ++pass)
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const int previous = pairs[triangle];
        if (trusted && trusted[triangle] == 2) continue; /* a moving object says nothing about the camera */
        if ((trusted && trusted[triangle] == 1) != (pass == 0)) continue;
        if (previous < 0 || previous >= prev_n ||
            !gpu_temporal_camera_pair_valid(&prev[previous], &curr[triangle]) ||
            !gpu_temporal_mesh_known_depth(curr[triangle].depth[0]) ||
            !gpu_temporal_mesh_known_depth(curr[triangle].depth[1]) ||
            !gpu_temporal_mesh_known_depth(curr[triangle].depth[2])) continue;
        for (int vertex = 0; vertex < 3; ++vertex) {
            GpuTemporalCameraSample *sample = &samples[count++];
            sample->u = gpu_temporal_camera_u(curr[triangle].x[vertex]);
            sample->v = gpu_temporal_camera_v(curr[triangle].y[vertex]);
            sample->w = 256.0 / (double)curr[triangle].depth[vertex];
            sample->tx = prev[previous].x[vertex];
            sample->ty = prev[previous].y[vertex];
        }
        if (pass == 0) voters = count;
    }
    camera->samples = count;
    if (count < GPU_TEMPORAL_CAMERA_MIN_INLIERS) { free(samples); return 0; }
    if (voters < GPU_TEMPORAL_CAMERA_TRUSTED_CORNERS) voters = count;
    faces = voters / 3;
    if (prior && prior->inliers > 0) {
        memcpy(best, prior->m, sizeof(best));
        best_count = gpu_temporal_camera_count(best, samples, voters, 1.0);
    }
    if (gpu_temporal_camera_refit(NULL, 0.0, samples, voters, trial)) {
        agreeing = gpu_temporal_camera_count(trial, samples, voters, 1.0);
        if (agreeing > best_count) { best_count = agreeing; memcpy(best, trial, sizeof(best)); }
    }
    for (int draw = 0; draw < GPU_TEMPORAL_CAMERA_DRAWS && faces >= 3; ++draw) {
        double normal[GPU_TEMPORAL_CAMERA_TERMS][GPU_TEMPORAL_CAMERA_TERMS + 1];
        int picked[3];
        memset(normal, 0, sizeof(normal));
        for (int slot = 0; slot < 3; ++slot) {
            random = random * 1664525u + 1013904223u;
            picked[slot] = (int)((random >> 8) % (unsigned)faces);
        }
        if (picked[0] == picked[1] || picked[0] == picked[2] || picked[1] == picked[2]) continue;
        for (int slot = 0; slot < 3; ++slot)
            for (int vertex = 0; vertex < 3; ++vertex)
                gpu_temporal_camera_accumulate(normal, &samples[picked[slot] * 3 + vertex]);
        if (!gpu_temporal_camera_solve(normal, trial)) continue;
        agreeing = gpu_temporal_camera_count(trial, samples, voters, 1.0);
        if (agreeing > best_count) { best_count = agreeing; memcpy(best, trial, sizeof(best)); }
    }
    if (best_count < GPU_TEMPORAL_CAMERA_MIN_INLIERS || best_count * 3 < voters) { free(samples); return 0; }
    for (int pass = 0; pass < 4; ++pass)
        if (gpu_temporal_camera_refit(best, limits[pass], samples, count, trial) &&
            gpu_temporal_camera_count(trial, samples, count, limits[pass]) >=
                gpu_temporal_camera_count(best, samples, count, limits[pass]))
            memcpy(best, trial, sizeof(best));
    memcpy(camera->m, best, sizeof(best));
    camera->inliers = gpu_temporal_camera_count(best, samples, count, GPU_TEMPORAL_CAMERA_TOLERANCE);
    free(samples);
    return camera->inliers >= GPU_TEMPORAL_CAMERA_MIN_INLIERS;
}

#define GPU_TEMPORAL_CAMERA_SCALE_CORNERS 12

static inline int gpu_temporal_compare_double(const void *left, const void *right) {
    const double a = *(const double *)left, b = *(const double *)right;
    return (a > b) - (a < b);
}

/* The fitted denominator is the depth ratio up to one factor, and identical
 * faces know both depths: the median of what they say, 0 when too few do. */
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
        qsort(ratios, (size_t)count, sizeof(*ratios), gpu_temporal_compare_double);
        scale = ratios[count / 2];
    }
    free(ratios);
    return gpu_temporal_finite((float)scale) && scale > 0.0 ? scale : 0.0;
}

typedef struct GpuTemporalCornerEntry {
    uint64_t key;
    int corner;
} GpuTemporalCornerEntry;

/* A modelled corner's level position in the current frame's coordinates. */
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

/* The game hands the GTE level coordinates relative to the camera, so a static
 * face keeps its three input vertices from frame to frame up to one integer
 * shift shared by the whole frame. With that shift a face has at most one
 * twin: this is identity, not resemblance, and no search window is involved. */
#define GPU_TEMPORAL_TWIN_CANDIDATES 32
#define GPU_TEMPORAL_TWIN_TRIALS 8
#define GPU_TEMPORAL_TWIN_MINIMUM 8
#define GPU_TEMPORAL_TWIN_OBJECT_MINIMUM 4
#define GPU_TEMPORAL_TWIN_OBJECT_REACH 24 /* level units a moving object may gain on the camera per frame */

typedef struct GpuTemporalTwinEntry {
    uint64_t key;
    int index, count, claims, spent;
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

/* Number of current faces with exactly one twin under `shift`; `twins`, when
 * given, receives the previous index of each or -1. Faces with `taken` set
 * already have a twin under another shift and are left alone, and so are
 * previous faces whose entry is `spent`. */
static inline int gpu_temporal_twin_count(GpuTemporalTwinEntry *table, unsigned mask,
                                          const GpuTemporalTriangle *prev,
                                          const GpuTemporalTriangle *curr, int curr_n,
                                          const int *shift, const int *taken, int *twins) {
    int found = 0;
    for (unsigned slot = 0; slot <= mask; ++slot) table[slot].claims = 0;
    for (int pass = 0; pass < 2; ++pass)
        for (int triangle = 0; triangle < curr_n; ++triangle) {
            GpuTemporalTwinEntry *entry;
            if (pass && twins) twins[triangle] = -1;
            if (!curr[triangle].world || !curr[triangle].modelled) continue;
            if (taken && taken[triangle] >= 0) continue;
            entry = gpu_temporal_twin_find(table, mask, gpu_temporal_twin_key(&curr[triangle], shift));
            if (entry->count != 1 || entry->spent ||
                !gpu_temporal_twin_same(&prev[entry->index], &curr[triangle], shift)) continue;
            if (!pass) { ++entry->claims; continue; }
            if (entry->claims != 1) continue;
            if (twins) twins[triangle] = entry->index;
            ++found;
        }
    return found;
}

/* A candidate previous face for every current one. `trusted` is 1 for the
 * ones that may vote for the camera (twins, or without them faces whose
 * material is unique in both frames) and 2 for twins under a second shift,
 * which is a moving object. Once a frame has twins, a modelled face without
 * one has no previous self: the matcher's pick for it was a look-alike.
 * `shift` carries the previous interval's shift in and this one's out. */
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
    int candidate_count = 0, best_twins = 0, best_shift[3] = {0, 0, 0};
    unsigned mask = 1;
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        pairs[triangle] = matches[triangle];
        trusted[triangle] = 0;
    }
    while (mask < (unsigned)(prev_n + curr_n) * 2u) mask <<= 1;
    table = (Entry *)calloc((size_t)mask, sizeof(*table));
    faces = (GpuTemporalTwinEntry *)calloc((size_t)mask, sizeof(*faces));
    if (!table || !faces) { free(table); free(faces); return 0; }
    --mask;
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
        entry = gpu_temporal_twin_find(faces, mask, gpu_temporal_twin_key(&prev[triangle], zero));
        if (entry->key && !gpu_temporal_twin_same(&prev[entry->index], &prev[triangle], zero)) {
            entry->count = 2; /* two different faces under one key: neither can be a twin */
            continue;
        }
        entry->key = gpu_temporal_twin_key(&prev[triangle], zero);
        entry->index = triangle;
        ++entry->count;
    }
    /* Every pair so far proposes the shift it would need. */
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
    /* No pair proposed anything, as on a fast turn with repeated textures:
     * let a few faces propose the shift to each previous face of their look. */
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
        twins = gpu_temporal_twin_count(faces, mask, prev, curr, curr_n, proposed, NULL, NULL);
        if (twins > best_twins) { best_twins = twins; memcpy(best_shift, proposed, sizeof(best_shift)); }
    }
    if (best_twins >= GPU_TEMPORAL_TWIN_MINIMUM) {
        int *twins = (int *)malloc((size_t)curr_n * 2 * sizeof(*twins));
        if (twins) {
            int *object = twins + curr_n;
            gpu_temporal_twin_count(faces, mask, prev, curr, curr_n, best_shift, NULL, twins);
            for (int triangle = 0; triangle < curr_n; ++triangle) {
                if (curr[triangle].modelled) pairs[triangle] = -1, trusted[triangle] = 0;
                if (twins[triangle] < 0) continue;
                pairs[triangle] = twins[triangle];
                trusted[triangle] = 1;
                gpu_temporal_twin_find(faces, mask,
                    gpu_temporal_twin_key(&prev[twins[triangle]], zero))->spent = 1;
            }
            /* A group of faces that kept its shape but gained a few units on
             * the camera is an object in motion. A tile's width is not a few. */
            for (int index = 0; index < candidate_count; ++index) {
                const int *proposed = candidates[index].shift;
                int near_camera = 1;
                for (int axis = 0; axis < 3; ++axis)
                    near_camera &= abs(proposed[axis] - best_shift[axis]) <= GPU_TEMPORAL_TWIN_OBJECT_REACH;
                if (!near_camera || !memcmp(proposed, best_shift, sizeof(best_shift)) ||
                    gpu_temporal_twin_count(faces, mask, prev, curr, curr_n, proposed, pairs, object) <
                        GPU_TEMPORAL_TWIN_OBJECT_MINIMUM) continue;
                for (int triangle = 0; triangle < curr_n; ++triangle) {
                    if (object[triangle] < 0) continue;
                    pairs[triangle] = object[triangle];
                    trusted[triangle] = 2;
                    gpu_temporal_twin_find(faces, mask,
                        gpu_temporal_twin_key(&prev[object[triangle]], zero))->spent = 1;
                }
            }
            free(twins);
        }
        memcpy(shift, best_shift, sizeof(best_shift));
    } else {
        best_twins = 0;
        shift[0] = shift[1] = shift[2] = 0;
    }
    free(faces);
    return best_twins;
}

/* Which corner of its rectangle each vertex of a billboard half stands on. */
static inline unsigned gpu_temporal_sprite_half(const GpuTemporalTriangle *face) {
    float left = face->x[0], top = face->y[0];
    unsigned kind = 0;
    for (int vertex = 1; vertex < 3; ++vertex) {
        if (face->x[vertex] < left) left = face->x[vertex];
        if (face->y[vertex] < top) top = face->y[vertex];
    }
    for (int vertex = 0; vertex < 3; ++vertex)
        kind |= ((face->x[vertex] > left ? 1u : 0u) | (face->y[vertex] > top ? 2u : 0u)) << (2 * vertex);
    return kind;
}

#define GPU_TEMPORAL_SPRITE_DEPTH_RATIO 0.75
/* Corner codes of the renderer's two rectangle halves: {x, x+w, x},{y, y, y+h} and {x+w, x, x+w},{y, y+h, y+h}. */
#define GPU_TEMPORAL_SPRITE_UPPER_LEFT 0x24u
#define GPU_TEMPORAL_SPRITE_LOWER_RIGHT 0x39u

typedef struct GpuTemporalSpriteBox {
    double left, top, right, bottom;
} GpuTemporalSpriteBox;

/* The rectangle of the billboard whose upper left half is `first`, when the next face is its lower right half. */
static inline int gpu_temporal_sprite_box(const GpuTemporalTriangle *faces, int count, int first,
                                          GpuTemporalSpriteBox *box) {
    const GpuTemporalTriangle *upper = &faces[first], *lower = upper + 1;
    if (first + 1 >= count || !upper->sprite || !lower->sprite ||
        !gpu_temporal_mesh_known_depth(upper->depth[0]) || lower->depth[0] != upper->depth[0] ||
        gpu_temporal_sprite_half(upper) != GPU_TEMPORAL_SPRITE_UPPER_LEFT ||
        gpu_temporal_sprite_half(lower) != GPU_TEMPORAL_SPRITE_LOWER_RIGHT ||
        lower->x[0] != upper->x[1] || lower->y[0] != upper->y[1] ||
        lower->x[1] != upper->x[2] || lower->y[1] != upper->y[2]) return 0;
    box->left = upper->x[0]; box->top = upper->y[0];
    box->right = upper->x[1]; box->bottom = upper->y[2];
    return 1;
}

typedef struct GpuTemporalSpritePair {
    double distance;
    int now, was;
} GpuTemporalSpritePair;

static inline int gpu_temporal_compare_sprite_pair(const void *left, const void *right) {
    const GpuTemporalSpritePair *a = (const GpuTemporalSpritePair *)left, *b = (const GpuTemporalSpritePair *)right;
    if (a->distance != b->distance) return a->distance < b->distance ? -1 : 1;
    return a->now != b->now ? a->now - b->now : a->was - b->was;
}

/* Billboards are paired by place, not by picture. A picture changes with every
 * animation frame, and look-alikes (shadows, a squad in one pose) stand side by
 * side, so a pick by material loses a billboard's motion at every new picture
 * and can take a neighbour's. A whole rectangle is paired with the previous
 * rectangle nearest to where the camera puts its centre, no farther away than
 * its own size and at a like depth, the nearest pairs first. That rectangle is
 * taken for its previous self: two look-alikes that swap places inside these
 * limits are not told apart. A half without its other half is left to the matcher. */
static inline void gpu_temporal_mesh_pair_sprites(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const GpuTemporalCamera *camera, int *pairs, int *trusted, int *reverse) {
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
        for (int half = triangle; half <= triangle + 1; ++half) {
            if (pairs[half] >= 0 && pairs[half] < prev_n && reverse[pairs[half]] == half)
                reverse[pairs[half]] = -1;
            pairs[half] = -1;
            trusted[half] = 0;
        }
        reach = now.right - now.left > now.bottom - now.top ? now.right - now.left : now.bottom - now.top;
        for (int previous = 0; previous < prev_n; ++previous) {
            double dx, dy, ratio;
            if (!gpu_temporal_sprite_box(prev, prev_n, previous, &was)) continue;
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
        if (pairs[pair->now] >= 0 || reverse[pair->was] >= 0) continue;
        for (int half = 0; half < 2; ++half) {
            pairs[pair->now + half] = pair->was + half;
            trusted[pair->now + half] = 2; /* its own motion, whatever the camera says */
            reverse[pair->was + half] = pair->now + half;
        }
    }
    free(candidates);
}

/* A matched face that disagrees with the camera is either a moving object or
 * a wrong pick among repeated textures. A previous face of the same material
 * lying where the camera puts this one settles it for the static world. */
static inline int gpu_temporal_camera_rematch(
        const GpuTemporalCamera *camera, const GpuTemporalTriangle *prev,
        int prev_n, const GpuTemporalTriangle *triangle) {
    double px[3], py[3];
    for (int vertex = 0; vertex < 3; ++vertex)
        if (!gpu_temporal_mesh_known_depth(triangle->depth[vertex]) ||
            !gpu_temporal_camera_project(camera, triangle->x[vertex], triangle->y[vertex],
                                         triangle->depth[vertex], &px[vertex], &py[vertex])) return -1;
    for (int candidate = 0; candidate < prev_n; ++candidate) {
        int coincides = 1;
        if (prev[candidate].material != triangle->material || !prev[candidate].world) continue;
        for (int vertex = 0; vertex < 3 && coincides; ++vertex) {
            const double dx = prev[candidate].x[vertex] - px[vertex];
            const double dy = prev[candidate].y[vertex] - py[vertex];
            coincides = dx * dx + dy * dy <=
                        GPU_TEMPORAL_CAMERA_TOLERANCE * GPU_TEMPORAL_CAMERA_TOLERANCE;
        }
        if (coincides) return candidate;
    }
    return -1;
}

typedef struct GpuTemporalMeshPoint {
    float x, y, depth;
    int corner;
} GpuTemporalMeshPoint;

typedef struct GpuTemporalMeshEdge {
    float ax, ay, bx, by;
    int a, b, triangle, direction;
} GpuTemporalMeshEdge;

typedef struct GpuTemporalMeshFace {
    int topology, eligible, matched, queued;
} GpuTemporalMeshFace;

typedef struct GpuTemporalMeshLink {
    int vertex, next;
} GpuTemporalMeshLink;

static inline int gpu_temporal_mesh_compare_float(float a, float b) {
    return a < b ? -1 : a > b ? 1 : 0;
}

static inline int gpu_temporal_mesh_compare_point(const void *left,
                                                  const void *right) {
    const GpuTemporalMeshPoint *a = (const GpuTemporalMeshPoint *)left;
    const GpuTemporalMeshPoint *b = (const GpuTemporalMeshPoint *)right;
    int order = gpu_temporal_mesh_compare_float(a->x, b->x);
    if (!order) order = gpu_temporal_mesh_compare_float(a->y, b->y);
    if (!order) order = gpu_temporal_mesh_compare_float(a->depth, b->depth);
    return order;
}

static inline int gpu_temporal_mesh_compare_edge(const void *left,
                                                 const void *right) {
    const GpuTemporalMeshEdge *a = (const GpuTemporalMeshEdge *)left;
    const GpuTemporalMeshEdge *b = (const GpuTemporalMeshEdge *)right;
    int order = gpu_temporal_mesh_compare_float(a->ax, b->ax);
    if (!order) order = gpu_temporal_mesh_compare_float(a->ay, b->ay);
    if (!order) order = gpu_temporal_mesh_compare_float(a->bx, b->bx);
    if (!order) order = gpu_temporal_mesh_compare_float(a->by, b->by);
    return order;
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
    /* An unknown corner must not bridge two different known-depth surfaces.
     * Keep the groups distinct and pin the ambiguous coincident endpoints. */
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

/* A-M/M-B plus A-B forms a confirmed current-frame T junction when precise
 * positions are collinear and absolute depth agrees projectively. Pin those
 * three groups locally: independent screen-space lerps of A, M and B need not
 * keep M on the long edge. No position is snapped at the real-frame endpoint.
 * Degree is bounded to 16, and long-edge lookup is logarithmic. */
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

/* Camera motion has no search window to respect. Any finite weighted corner
 * is usable: gpu_temporal_mesh_facing skips the face while it is turned
 * away or still behind the viewer. */
static inline int gpu_temporal_mesh_camera_safe(const GpuTemporalTriangle *curr,
                                                const GpuTemporalTriangle *from) {
    (void)curr;
    for (int vertex = 0; vertex < 3; ++vertex)
        if (!gpu_temporal_finite(from->x[vertex]) || !gpu_temporal_finite(from->y[vertex]) ||
            !gpu_temporal_finite(from->w[vertex])) return 0;
    return 1;
}

/* The game culls back faces before it submits them, so an interpolated face
 * whose winding differs from the real frame's was not visible yet. Neither
 * was one with a corner that had not come in front of the viewer. */
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

/* Returns the number of current triangles with retained XY or texture-q
 * motion. `from` has current indexing and is always initialized from current;
 * it is safe to interpolate every output with its corresponding current face.
 * Only a matched face receives its own prior normalized q. The output depth
 * remains current metadata. HUD, invalid inputs and allocation failure hold.
 * Stats except propagated_vertices count unique eligible vertex groups;
 * propagated_vertices counts unmatched face corners receiving shared motion.
 */
/* `departed`, `departed_from` and `departed_to` have previous indexing and may
 * be NULL. A previous face the game no longer submits still covers part of
 * the screen between the two frames: it is flagged and given the two ends of
 * its path. A corner it shares with a current face takes that face's path.
 * The flag is 2 for a face with a corner that has no depth: it stays where it was drawn. */
static inline int gpu_temporal_mesh_prepare_scene(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const int *matches, GpuTemporalTriangle *from,
        GpuTemporalMeshStats *stats,
        GpuTemporalTriangle *departed_from, GpuTemporalTriangle *departed_to, int *departed,
        GpuTemporalCamera *prior) {
    GpuTemporalMeshNode *nodes = NULL;
    GpuTemporalMeshPoint *points = NULL;
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
    nodes = (GpuTemporalMeshNode *)calloc((size_t)vertex_count, sizeof(*nodes));
    points = (GpuTemporalMeshPoint *)malloc((size_t)vertex_count * sizeof(*points));
    edges = (GpuTemporalMeshEdge *)malloc((size_t)vertex_count * sizeof(*edges));
    faces = (GpuTemporalMeshFace *)calloc((size_t)curr_n, sizeof(*faces));
    queue = (int *)malloc((size_t)curr_n * sizeof(*queue));
    links = (GpuTemporalMeshLink *)malloc((size_t)vertex_count * 2 * sizeof(*links));
    reverse = (int *)malloc((size_t)prev_n * sizeof(*reverse));
    pairs = (int *)malloc((size_t)curr_n * sizeof(*pairs));
    trusted = (int *)malloc((size_t)curr_n * sizeof(*trusted));
    if (!nodes || !points || !edges || !faces || !queue || !links || !reverse || !pairs ||
        !trusted) goto cleanup;

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
            edge->ax = t->x[a]; edge->ay = t->y[a];
            edge->bx = t->x[b]; edge->by = t->y[b];
            edge->a = triangle * 3 + a; edge->b = triangle * 3 + b;
            edge->triangle = triangle;
            if (gpu_temporal_mesh_known_depth(t->depth[vertex])) {
                GpuTemporalMeshPoint *point = &points[point_count++];
                point->x = t->x[vertex]; point->y = t->y[vertex];
                point->depth = t->depth[vertex];
                point->corner = triangle * 3 + vertex;
            }
        }
    }

    qsort(points, (size_t)point_count, sizeof(*points), gpu_temporal_mesh_compare_point);
    for (int point = 1; point < point_count; ++point)
        if (!gpu_temporal_mesh_compare_point(&points[point - 1], &points[point]))
            gpu_temporal_mesh_union(nodes, points[point - 1].corner, points[point].corner);
    qsort(edges, (size_t)edge_count, sizeof(*edges), gpu_temporal_mesh_compare_edge);
    for (int first = 0; first < edge_count;) {
        int end = first + 1;
        while (end < edge_count && !gpu_temporal_mesh_compare_edge(&edges[first], &edges[end]))
            ++end;
        if (end - first == 2) {
            const GpuTemporalMeshEdge *a = &edges[first], *b = &edges[first + 1];
            if (a->triangle != b->triangle && a->direction != b->direction &&
                gpu_temporal_mesh_depth_compatible(curr, a->a, b->a) &&
                gpu_temporal_mesh_depth_compatible(curr, a->b, b->b)) {
                const int compatible_a = gpu_temporal_mesh_group_depth_compatible(nodes, a->a, b->a);
                const int compatible_b = gpu_temporal_mesh_group_depth_compatible(nodes, a->b, b->b);
                if (!compatible_a || !compatible_b) {
                    /* Reject the edge atomically, including motion: pinning
                     * only its depth-conflicting endpoint would let the other
                     * endpoint separate while these groups remain unwelded. */
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

    /* Build incidence lists after welding. They also bound safety propagation:
     * each group can be pinned once and visits each of its corners once. */
    for (int corner = 0; corner < vertex_count; ++corner) {
        int root;
        if (!faces[corner / 3].topology) continue;
        root = gpu_temporal_mesh_root(nodes, corner);
        nodes[corner].next = nodes[root].first;
        nodes[root].first = corner;
        /* A tiny/degenerate or otherwise ineligible WORLD face still shares
         * topology. It anchors all of its corners so adjacent moving faces
         * cannot detach from it. HUD faces never participate in this graph. */
        if (!faces[corner / 3].eligible) nodes[root].held = 1;
    }
    if (prior) memcpy(shift, prior->shift, sizeof(shift));
    twins = gpu_temporal_mesh_pairs(prev, prev_n, curr, curr_n, matches, pairs, trusted, shift);
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
        gpu_temporal_mesh_pair_sprites(prev, prev_n, curr, curr_n, &camera, pairs, trusted, reverse);
    }
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        int previous = has_camera ? pairs[triangle] : matches[triangle];
        int dynamic = !has_camera, aliased = 0;
        if (!faces[triangle].eligible || previous < 0 || previous >= prev_n ||
            (has_camera ? !gpu_temporal_camera_pair_valid(&prev[previous], &curr[triangle])
                        : !gpu_temporal_mesh_valid(&prev[previous]) ||
                          !gpu_temporal_compatible(&prev[previous], &curr[triangle]))) continue;
        if (has_camera && trusted[triangle] == 2) dynamic = 1;
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
            const int rematched = gpu_temporal_camera_rematch(&camera, prev, prev_n, &curr[triangle]);
            if (rematched >= 0) { previous = rematched; dynamic = 0; }
        }
        if (dynamic && has_forward && !trusted[triangle]) {
            /* The matched past face still stands where the camera takes it:
             * this pair was a look-alike, and the face itself is new. */
            const int stayed = gpu_temporal_camera_rematch(&forward, curr, curr_n, &prev[previous]);
            if (stayed >= 0 && stayed != triangle) dynamic = 0, aliased = 1;
        }
        faces[triangle].matched = !aliased;
        for (int vertex = 0; vertex < 3; ++vertex) {
            const int root = gpu_temporal_mesh_root(nodes, triangle * 3 + vertex);
            GpuTemporalMeshNode *node = &nodes[root];
            const double dx = (double)prev[previous].x[vertex] - curr[triangle].x[vertex];
            const double dy = (double)prev[previous].y[vertex] - curr[triangle].y[vertex];
            if (!aliased) from[triangle].q[vertex] = prev[previous].q[vertex];
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
        /* Only a group that every matched face around it moves alike, and
         * differently from the camera, belongs to a moving object. */
        if (has_camera && node->depth > 0.0f && !(consistent && !node->statics)) {
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
            /* A bounding-box midpoint makes consensus independent of triangle
             * submission order, unlike an accumulating floating-point mean. */
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
        /* Evaluate a whole batch before applying any pins: a face's safety
         * decision cannot depend on whether its neighbor was submitted first. */
        for (int item = 0; item < batch_count; ++item) {
            const int triangle = queue[queue_head];
            GpuTemporalTriangle candidate = from[triangle];
            queue_head = (queue_head + 1) % curr_n;
            --queue_count;
            faces[triangle].queued = 0;
            int by_camera = has_camera;
            gpu_temporal_mesh_write_triangle(&curr[triangle], triangle, nodes, &candidate);
            for (int vertex = 0; vertex < 3; ++vertex)
                by_camera &= nodes[gpu_temporal_mesh_root(nodes, triangle * 3 + vertex)].camera;
            if (by_camera ? gpu_temporal_mesh_camera_safe(&curr[triangle], &candidate)
                          : gpu_temporal_mesh_valid(&candidate) &&
                            gpu_temporal_compatible(&curr[triangle], &candidate)) continue;
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
            if (has_camera && !node->camera && node->depth > 0.0f &&
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
            gpu_temporal_camera_rematch(&forward, curr, curr_n, &prev[triangle]) >= 0) continue;
        departed_from[triangle] = start;
        departed_to[triangle] = to;
        departed[triangle] = carried == 3 ? 1 : 2;
        if (stats) ++stats->departed_faces;
    }

cleanup:
    free(corners);
    free(trusted);
    free(pairs);
    free(reverse);
    free(links);
    free(queue);
    free(faces);
    free(edges);
    free(points);
    free(nodes);
    return moving_triangles;
}

static inline int gpu_temporal_mesh_prepare(
        const GpuTemporalTriangle *prev, int prev_n,
        const GpuTemporalTriangle *curr, int curr_n,
        const int *matches, GpuTemporalTriangle *from,
        GpuTemporalMeshStats *stats) {
    return gpu_temporal_mesh_prepare_scene(prev, prev_n, curr, curr_n, matches,
                                           from, stats, NULL, NULL, NULL, NULL);
}

#endif /* PSXRECOMP_GPU_TEMPORAL_MESH_H */
