/* A world face with a corner at or behind the viewer's plane: the mirror takes it with a real w and the rasterizer clips it. */
#ifndef PSXRECOMP_GPU_NEAR_FACE_H
#define PSXRECOMP_GPU_NEAR_FACE_H

#include "gte_precision.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

/* On a level point that only the view gave, for a corner the GTE's own lookup refuses: the presenter's mesh pairs no face by it. */
#define PSX_NEAR_IDENTITY UINT64_C(0x0800000000000000)

/* The GTE's divide stops at such a depth, the viewer's plane and what lies behind it included. */
static inline int psx_near_depth(int32_t w, uint16_t focal) { return (int64_t)w * 2 <= (int64_t)focal; }

/* All corners come from one projection, and one of them lies that near or was pinned by the game: neither has a pixel of its own. */
static inline int psx_near_polygon(const GtePrecisionView *corner, const int *pinned, int count) {
    int near_one = 0;
    for (int i = 0; i < count; ++i) {
        if (corner[i].focal == 0 || corner[i].focal != corner[0].focal || corner[i].centre_x != corner[0].centre_x ||
            corner[i].centre_y != corner[0].centre_y)
            return 0;
        near_one |= pinned[i] || psx_near_depth(corner[i].w, corner[i].focal);
    }
    return near_one;
}

/* Where a corner projects, through the centre when behind, and its w. On the viewer's plane it counts as a quarter behind: a face wholly there is not drawn. */
static inline void psx_near_place(const GtePrecisionView *corner, double shift_x, double shift_y, float place[3]) {
    const double w = corner->w != 0 ? (double)corner->w : -0.25;
    place[0] = (float)(corner->centre_x + shift_x + corner->hx / w);
    place[1] = (float)(corner->centre_y + shift_y + corner->hy / w);
    place[2] = (float)w;
}

/* A corner the camera carried from (nx, ny) over weight, in the terms of its place and w now: a share of the way on. */
static inline void psx_near_between(double nx, double ny, double weight, double share, float place[3]) {
    const double x = nx + ((double)place[0] - nx) * share, y = ny + ((double)place[1] - ny) * share;
    double w = weight + (1.0 - weight) * share;
    if (w == 0.0) w = -1e-6;
    place[0] = (float)(x / w);
    place[1] = (float)(y / w);
    place[2] = (float)((double)place[2] * w);
}

/* Whether a corner found in the other frame stands where the camera, (nx, ny) over weight, expects this one of depth w. */
static inline int psx_near_agrees(const float found[3], double nx, double ny, double weight, double w) {
    const double x = nx / weight, y = ny / weight, depth = weight * w;
    if (weight == 0.0 || !isfinite(x + y + depth)) return 0;
    return (found[2] > 0.0f) == (depth > 0.0) && fabs((double)found[2] - depth) <= 0.2 * fabs(depth) + 8.0 &&
           fabs((double)found[0] - x) + fabs((double)found[1] - y) <= 0.2 * (fabs(x - 160.0) + fabs(y - 120.0)) + 16.0;
}

/* The same towards the corner's own place and w in the other frame. */
static inline void psx_near_towards(const float other[3], double share, float place[3]) {
    const double ratio = (double)other[2] / (double)place[2];
    psx_near_between((double)other[0] * ratio, (double)other[1] * ratio, ratio, share, place);
}

/* A corner of a near face that is not drawn: one place and one w for all three, since unlike w at one place are no point to the rasterizer. */
static inline void psx_near_hide(float place[3]) {
    place[0] = place[1] = 0.0f;
    place[2] = 1.0f;
}

/* The corner handed over that much farther: a larger w at the same projected place. */
static inline void psx_near_farther(float place[3], float clearance) { place[2] *= clearance; }

/* A near corner is turned at any angle: where the turn's denominator passes zero it keeps a hundredth and its sign, and the w takes it. */
static inline float psx_near_turn(float den) { return fabsf(den) < 0.01f ? (den < 0.0f ? -0.01f : 0.01f) : den; }

/* How far a corner that agrees stands from where the camera expects it, in pixels and units of depth together. */
static inline double psx_near_miss(const float found[3], double nx, double ny, double weight, double w) {
    return fabs((double)found[0] - nx / weight) + fabs((double)found[1] - ny / weight) + fabs((double)found[2] - weight * w);
}

typedef struct PsxNearCorner {
    int vertex;       /* its number in the frame, rising through the table */
    int16_t model[3]; /* its point of the level */
    float place[3];
} PsxNearCorner;

/* The other frame's corner at this vertex's level point, its model a camera's step away: rooms repeat models, so the nearest that agrees. */
static inline int psx_near_find(const PsxNearCorner *own, int own_count, int vertex, const PsxNearCorner *other, int other_count,
                                const int step[3], int sign, double nx, double ny, double weight, double w, float place[3]) {
    const int16_t *model = NULL;
    double least = 0.0;
    int best = -1;
    for (int low = 0, high = own_count - 1; low <= high && !model;) {
        const int middle = low + (high - low) / 2;
        if (own[middle].vertex == vertex) model = own[middle].model;
        else if (own[middle].vertex < vertex) low = middle + 1;
        else high = middle - 1;
    }
    for (int i = 0; model && i < other_count; ++i) {
        const int16_t *there = other[i].model;
        double miss;
        if (there[0] != model[0] + sign * step[0] || there[1] != model[1] + sign * step[1] || there[2] != model[2] + sign * step[2] ||
            !psx_near_agrees(other[i].place, nx, ny, weight, w))
            continue;
        miss = psx_near_miss(other[i].place, nx, ny, weight, w);
        if (best < 0 || miss < least) {
            best = i;
            least = miss;
        }
    }
    if (best < 0) return 0;
    memcpy(place, other[best].place, sizeof(other[best].place));
    return 1;
}

#endif
