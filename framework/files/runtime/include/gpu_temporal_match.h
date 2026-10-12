#ifndef PSXRECOMP_GPU_TEMPORAL_MATCH_H
#define PSXRECOMP_GPU_TEMPORAL_MATCH_H

/* Conservative correspondence for retained, ordered render triangles. This
 * does not establish guest object identity: a material key must describe the
 * complete ordered UV/material topology, not a transient command address.
 * Ambiguous, discontinuous, or excessively dense geometry stays current.
 * Coordinates are in native display pixels after normalizing buffer offsets.
 * q is the per-polygon normalized reciprocal-depth shader attribute; three
 * zeros mean affine. depth is independent GTE camera depth (zero=unknown). */
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct GpuTemporalTriangle {
    uint64_t material;
    float x[3], y[3], q[3];
    int world;
    float depth[3];
    /* The GTE input vertex of each corner when `modelled`: the same point of
     * the level gives the same value up to one shift shared by a whole frame. */
    int16_t model[3][3];
    int modelled;
    int sprite; /* a CPU-projected billboard: one depth, no model */
    /* Nonzero: x and y of that corner are numerators over this weight, the
     * form a projection produces. Zero means a plain screen position. */
    float w[3];
} GpuTemporalTriangle;

#define GPU_TEMPORAL_MAX_TRIANGLES 8192
#define GPU_TEMPORAL_MAX_PROBES 256
#define GPU_TEMPORAL_MAX_MOTION 32.0
#define GPU_TEMPORAL_MIN_AREA 0.5

typedef struct GpuTemporalIndex {
    const GpuTemporalTriangle *triangles;
    int *heads, *next, *cell_x, *cell_y;
    unsigned mask;
} GpuTemporalIndex;

static inline int gpu_temporal_finite(float value) {
    return value == value && value >= -FLT_MAX && value <= FLT_MAX;
}

static inline double gpu_temporal_cross(double ax, double ay,
                                         double bx, double by) {
    return ax * by - ay * bx;
}

static inline double gpu_temporal_area(const GpuTemporalTriangle *triangle) {
    return gpu_temporal_cross(
        (double)triangle->x[1] - triangle->x[0],
        (double)triangle->y[1] - triangle->y[0],
        (double)triangle->x[2] - triangle->x[0],
        (double)triangle->y[2] - triangle->y[0]);
}

static inline int gpu_temporal_valid(const GpuTemporalTriangle *triangle,
                                      int *cell_x, int *cell_y) {
    double cx, cy, area;
    int positive_depths = 0;
    if (!triangle || !triangle->world) return 0;
    for (int vertex = 0; vertex < 3; ++vertex) {
        if (!gpu_temporal_finite(triangle->x[vertex]) ||
            !gpu_temporal_finite(triangle->y[vertex]) ||
            !gpu_temporal_finite(triangle->q[vertex]) ||
            triangle->q[vertex] < 0.0f) return 0;
        positive_depths += triangle->q[vertex] > 0.0f;
    }
    if (positive_depths != 0 && positive_depths != 3) return 0;
    area = gpu_temporal_area(triangle);
    if (fabs(area) < GPU_TEMPORAL_MIN_AREA) return 0;
    cx = floor(((double)triangle->x[0] + triangle->x[1] + triangle->x[2]) /
               (3.0 * GPU_TEMPORAL_MAX_MOTION));
    cy = floor(((double)triangle->y[0] + triangle->y[1] + triangle->y[2]) /
               (3.0 * GPU_TEMPORAL_MAX_MOTION));
    /* Leave room for both neighboring cells without an integer overflow. */
    if (cx < (double)INT_MIN + 1.0 || cx > (double)INT_MAX - 1.0 ||
        cy < (double)INT_MIN + 1.0 || cy > (double)INT_MAX - 1.0) return 0;
    if (cell_x) *cell_x = (int)cx;
    if (cell_y) *cell_y = (int)cy;
    return 1;
}

static inline unsigned gpu_temporal_hash(uint64_t material, int x, int y,
                                          unsigned mask) {
    uint64_t hash = material ^ ((uint64_t)(uint32_t)x << 32) ^ (uint32_t)y;
    hash ^= hash >> 30;
    hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27;
    hash *= UINT64_C(0x94d049bb133111eb);
    hash ^= hash >> 31;
    return (unsigned)hash & mask;
}

static inline int gpu_temporal_compatible(const GpuTemporalTriangle *a,
                                           const GpuTemporalTriangle *b) {
    double area_a, area_b, ratio;
    double ex, ey, fx, fy, dex, dey, dfx, dfy, linear, quadratic;
    if (a->material != b->material) return 0;
    area_a = gpu_temporal_area(a);
    area_b = gpu_temporal_area(b);
    if ((area_a < 0.0) != (area_b < 0.0)) return 0;
    ratio = fabs(area_b / area_a);
    if (ratio < 0.5 || ratio > 2.0) return 0;
    for (int vertex = 0; vertex < 3; ++vertex) {
        const double dx = (double)a->x[vertex] - b->x[vertex];
        const double dy = (double)a->y[vertex] - b->y[vertex];
        if (dx * dx + dy * dy >
            GPU_TEMPORAL_MAX_MOTION * GPU_TEMPORAL_MAX_MOTION) return 0;
        if ((a->q[vertex] == 0.0f) != (b->q[vertex] == 0.0f)) return 0;
        if (a->q[vertex] > 0.0f) {
            const double depth_ratio = (double)b->q[vertex] / a->q[vertex];
            if (depth_ratio < 0.5 || depth_ratio > 2.0) return 0;
        }
    }

    /* Equal endpoint winding alone permits a collapsing halfway triangle
     * (for example a 180-degree rotation). Check the quadratic area minimum
     * across the entire interval, not just a chosen interpolation sample. */
    ex = (double)a->x[1] - a->x[0];
    ey = (double)a->y[1] - a->y[0];
    fx = (double)a->x[2] - a->x[0];
    fy = (double)a->y[2] - a->y[0];
    dex = ((double)b->x[1] - b->x[0]) - ex;
    dey = ((double)b->y[1] - b->y[0]) - ey;
    dfx = ((double)b->x[2] - b->x[0]) - fx;
    dfy = ((double)b->y[2] - b->y[0]) - fy;
    linear = gpu_temporal_cross(dex, dey, fx, fy) +
             gpu_temporal_cross(ex, ey, dfx, dfy);
    quadratic = gpu_temporal_cross(dex, dey, dfx, dfy);
    if (quadratic != 0.0) {
        const double t = -linear / (2.0 * quadratic);
        if (t > 0.0 && t < 1.0) {
            const double middle_area = area_a + t * (linear + t * quadratic);
            if ((middle_area < 0.0) != (area_a < 0.0) ||
                fabs(middle_area) < GPU_TEMPORAL_MIN_AREA) return 0;
        }
    }
    return 1;
}

static inline void gpu_temporal_index_build(GpuTemporalIndex *index, int count) {
    for (unsigned bucket = 0; bucket <= index->mask; ++bucket)
        index->heads[bucket] = -1;
    for (int triangle = 0; triangle < count; ++triangle) {
        unsigned bucket;
        if (!gpu_temporal_valid(&index->triangles[triangle],
                                 &index->cell_x[triangle],
                                 &index->cell_y[triangle])) continue;
        bucket = gpu_temporal_hash(index->triangles[triangle].material,
                                    index->cell_x[triangle],
                                    index->cell_y[triangle], index->mask);
        index->next[triangle] = index->heads[bucket];
        index->heads[bucket] = triangle;
    }
}

/* Find exactly one candidate. A probe limit fails closed on pathological
 * hash collisions or dense repeated material, bounding frame-time work. */
static inline int gpu_temporal_unique(const GpuTemporalIndex *index,
                                       const GpuTemporalTriangle *triangle) {
    int cell_x, cell_y, match = -1, probes = 0;
    if (!gpu_temporal_valid(triangle, &cell_x, &cell_y)) return -1;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = cell_x + dx, y = cell_y + dy;
            const unsigned bucket = gpu_temporal_hash(
                triangle->material, x, y, index->mask);
            for (int candidate = index->heads[bucket]; candidate >= 0;
                 candidate = index->next[candidate]) {
                if (++probes > GPU_TEMPORAL_MAX_PROBES) return -1;
                if (index->cell_x[candidate] != x ||
                    index->cell_y[candidate] != y ||
                    !gpu_temporal_compatible(triangle,
                                               &index->triangles[candidate]))
                    continue;
                if (match >= 0) return -1;
                match = candidate;
            }
        }
    }
    return match;
}

/* Every output begins as -1, including allocation failure and invalid sizes.
 * A match must be unique in BOTH directions; draw order never breaks ties.
 * Expected linear work for spatially distributed geometry, with a strict
 * per-query probe cap even when every material and centroid is identical. */
static inline void gpu_temporal_match(const GpuTemporalTriangle *prev,
                                       int prev_n,
                                       const GpuTemporalTriangle *curr,
                                       int curr_n, int *matches) {
    GpuTemporalIndex previous, current;
    unsigned buckets = 1;
    int *storage, *cursor, *reverse;
    size_t integers;
    if (!matches || curr_n <= 0) return;
    for (int triangle = 0; triangle < curr_n; ++triangle) matches[triangle] = -1;
    if (!prev || !curr || prev_n <= 0 ||
        prev_n > GPU_TEMPORAL_MAX_TRIANGLES ||
        curr_n > GPU_TEMPORAL_MAX_TRIANGLES) return;
    while (buckets < (unsigned)(prev_n > curr_n ? prev_n : curr_n) * 2u)
        buckets <<= 1;
    integers = 2u * buckets + 4u * (size_t)prev_n + 3u * (size_t)curr_n;
    storage = (int *)malloc(integers * sizeof(int));
    if (!storage) return;
    cursor = storage;
    previous.triangles = prev;
    previous.mask = buckets - 1;
    previous.heads = cursor; cursor += buckets;
    previous.next = cursor; cursor += prev_n;
    previous.cell_x = cursor; cursor += prev_n;
    previous.cell_y = cursor; cursor += prev_n;
    current.triangles = curr;
    current.mask = buckets - 1;
    current.heads = cursor; cursor += buckets;
    current.next = cursor; cursor += curr_n;
    current.cell_x = cursor; cursor += curr_n;
    current.cell_y = cursor; cursor += curr_n;
    reverse = cursor;
    for (int triangle = 0; triangle < prev_n; ++triangle) reverse[triangle] = -2;
    gpu_temporal_index_build(&previous, prev_n);
    gpu_temporal_index_build(&current, curr_n);
    for (int triangle = 0; triangle < curr_n; ++triangle) {
        const int candidate = gpu_temporal_unique(&previous, &curr[triangle]);
        if (candidate < 0) continue;
        if (reverse[candidate] == -2)
            reverse[candidate] = gpu_temporal_unique(&current, &prev[candidate]);
        if (reverse[candidate] == triangle) matches[triangle] = candidate;
    }
    free(storage);
}

/* Interpolate only a pair already accepted by gpu_temporal_match. XY is
 * projected-space motion; q remains reciprocal depth throughout and is
 * interpolated as the shader's perspective attribute, never as camera Z.
 * This preserves positive depth and the all-zero affine sentinel. It does
 * not claim linear motion in world space. UV/material data stays current. */
static inline void gpu_temporal_lerp(const GpuTemporalTriangle *prev,
                                      const GpuTemporalTriangle *curr,
                                      float alpha,
                                      GpuTemporalTriangle *out) {
    GpuTemporalTriangle result;
    if (!out) return;
    if (!curr) { if (prev) *out = *prev; return; }
    if (!prev || !gpu_temporal_finite(alpha) || alpha >= 1.0f) {
        /* A weighted end still has to be divided by its weight. */
        if (curr->w[0] == 0.0f && curr->w[1] == 0.0f && curr->w[2] == 0.0f) { *out = *curr; return; }
        if (!prev) prev = curr;
        alpha = 1.0f;
    }
    if (alpha <= 0.0f) {
        /* A weighted corner still has to be divided by its weight. */
        if (prev->w[0] == 0.0f && prev->w[1] == 0.0f && prev->w[2] == 0.0f) { *out = *prev; return; }
        alpha = 0.0f;
    }
    result = *curr;
    for (int vertex = 0; vertex < 3; ++vertex) {
        if (prev->w[vertex] != 0.0f || curr->w[vertex] != 0.0f) {
            /* A weighted corner moves along the line its projection moved
             * it: straight in space, so collinear corners stay collinear
             * for any turn. The result keeps its weight; at or below zero
             * the corner is not in front of the viewer yet. */
            const double before = prev->w[vertex] != 0.0f ? prev->w[vertex] : 1.0;
            const double after = curr->w[vertex] != 0.0f ? curr->w[vertex] : 1.0;
            const double weight = before + (after - before) * alpha;
            const double scale = weight > 1e-6 ? 1.0 / weight : 0.0;
            result.x[vertex] = (float)(((double)prev->x[vertex] +
                ((double)curr->x[vertex] - prev->x[vertex]) * alpha) * scale);
            result.y[vertex] = (float)(((double)prev->y[vertex] +
                ((double)curr->y[vertex] - prev->y[vertex]) * alpha) * scale);
            result.w[vertex] = (float)weight;
            result.q[vertex] = (float)((double)prev->q[vertex] +
                ((double)curr->q[vertex] - prev->q[vertex]) * alpha);
            continue;
        }
        result.x[vertex] = (float)((double)prev->x[vertex] +
            ((double)curr->x[vertex] - prev->x[vertex]) * alpha);
        result.y[vertex] = (float)((double)prev->y[vertex] +
            ((double)curr->y[vertex] - prev->y[vertex]) * alpha);
        result.q[vertex] = (float)((double)prev->q[vertex] +
            ((double)curr->q[vertex] - prev->q[vertex]) * alpha);
    }
    *out = result;
}

#endif /* PSXRECOMP_GPU_TEMPORAL_MATCH_H */
