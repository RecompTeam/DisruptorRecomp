/* A triangle cut to the columns of the picture that a turn by part of a game step can place. */
#ifndef PSXRECOMP_GPU_TURN_CLIP_H
#define PSXRECOMP_GPU_TURN_CLIP_H

#include "gpu_turned_perspective.h"

#include <math.h>

#define PSX_TURN_CLIP_MAX 5
/* Colours and unweighted textures are stretched by as much as the denominators of a triangle's corners differ: a tenth. */
#define PSX_TURN_CLIP_NEAR 0.9f
#define PSX_TURN_CLIP_FAR 1.1f
#define PSX_TURN_CLIP_PICTURE 3.0f /* focal lengths either side that are never cut: wider than a 32:9 picture */

typedef struct PsxTurnCorner {
    float x, y;
    float share[3]; /* of the triangle's own corners, adding up to 1 */
} PsxTurnCorner;

/* The columns where the turn's denominator stays in bounds: past 0 a corner lands on the picture's other side. 0: no turn. */
static inline int psx_turn_band(float yaw_sin, float yaw_cos, float center_x, float focal_x, float *low, float *high) {
    float near_edge, far_edge;
    if (!(fabsf(yaw_sin) > 0.0000001f) || !(focal_x > 0.0f)) return 0;
    near_edge = center_x + focal_x * (yaw_cos - PSX_TURN_CLIP_NEAR) / yaw_sin;
    far_edge = center_x + focal_x * (yaw_cos - PSX_TURN_CLIP_FAR) / yaw_sin;
    *low = fminf(fminf(near_edge, far_edge), center_x - focal_x * PSX_TURN_CLIP_PICTURE);
    *high = fmaxf(fmaxf(near_edge, far_edge), center_x + focal_x * PSX_TURN_CLIP_PICTURE);
    return 1;
}

static inline float psx_turn_mix(const PsxTurnCorner *corner, const float value[3]) {
    return corner->share[0] * value[0] + corner->share[1] * value[1] + corner->share[2] * value[2];
}

/* The triangle's own corner that a corner left by psx_turn_clip is, or -1 for a cut. */
static inline int psx_turn_own(const PsxTurnCorner *corner) {
    for (int i = 0; i < 3; ++i)
        if (corner->share[i] == 1.0f) return i;
    return -1;
}

/* A cut's depth, from the corners it has a share of: reciprocals mix. One without depth leaves none, a negative one a negative. */
static inline float psx_turn_depth(const PsxTurnCorner *corner, const float depth[3]) {
    const int own = psx_turn_own(corner);
    float inverse = 0.0f;
    int mirror_only = 0;
    if (own >= 0) return depth[own];
    for (int i = 0; i < 3; ++i) {
        if (corner->share[i] == 0.0f) continue;
        if (!(fabsf(depth[i]) > PSX_TURNED_NO_DEPTH)) return 0.0f;
        inverse += corner->share[i] / fabsf(depth[i]);
        mirror_only |= depth[i] < 0.0f;
    }
    return inverse > 0.0f ? (mirror_only ? -1.0f : 1.0f) / inverse : 0.0f;
}

/* What is left of a triangle between two columns, corners in order. 0: nothing. A corner that stays keeps a share of 1. */
static inline int psx_turn_clip(const float x[3], const float y[3], float low, float high,
                                PsxTurnCorner out[PSX_TURN_CLIP_MAX]) {
    PsxTurnCorner kept[PSX_TURN_CLIP_MAX];
    int count = 3;
    for (int i = 0; i < 3; ++i) {
        if (!isfinite(x[i]) || !isfinite(y[i])) return 0;
        out[i].x = x[i];
        out[i].y = y[i];
        for (int k = 0; k < 3; ++k) out[i].share[k] = k == i ? 1.0f : 0.0f;
    }
    for (int side = 0; side < 2; ++side) {
        const float bound = side ? high : low;
        int next = 0;
        for (int i = 0; i < count; ++i) {
            const PsxTurnCorner *from = &out[i], *to = &out[(i + 1) % count];
            const int from_in = side ? from->x <= bound : from->x >= bound;
            const int to_in = side ? to->x <= bound : to->x >= bound;
            if (from_in) kept[next++] = *from;
            if (from_in != to_in) {
                const float along = (bound - from->x) / (to->x - from->x);
                PsxTurnCorner *cut = &kept[next++];
                cut->x = bound;
                cut->y = from->y + (to->y - from->y) * along;
                for (int k = 0; k < 3; ++k) cut->share[k] = from->share[k] + (to->share[k] - from->share[k]) * along;
            }
        }
        if (next < 3) return 0;
        count = next;
        for (int i = 0; i < count; ++i) out[i] = kept[i];
    }
    return count;
}

#endif
