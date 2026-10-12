/* Perspective weights of a triangle whose picture the presentation turns by part of a game step. */
#ifndef PSXRECOMP_GPU_TURNED_PERSPECTIVE_H
#define PSXRECOMP_GPU_TURNED_PERSPECTIVE_H

#include <math.h>
#include <string.h>

/* A fractional yaw turns the view about the eye: a corner's depth along the new axis is its old depth times the turn's
 * denominator, so its q is divided by it, and a triangle without q takes 1 for each: its texture is turned as a picture.
 * All three corners or none when one lies too far aside to trust: one q of 0 among three would corrupt the texture. */
static inline void psx_turned_perspective(const float px[3], float pq[3], float yaw_sin, float yaw_cos, float center_x,
                                          float focal_x) {
    float turned[3];
    const int flat = pq[0] == 0.0f && pq[1] == 0.0f && pq[2] == 0.0f;
    if (fabsf(yaw_sin) <= 0.0000001f || focal_x <= 0.0f) return;
    for (int i = 0; i < 3; ++i) {
        const float ux = (px[i] - center_x) / focal_x;
        const float den = yaw_cos - ux * yaw_sin;
        const float own = flat ? 1.0f : pq[i];
        int trusted = own > 0.0f && own <= 1.0f && den >= 0.5f && den <= 2.0f;
        if (trusted) {
            turned[i] = own / den;
            trusted = turned[i] > 0.0f; /* the smallest weights round to 0 when divided */
        }
        if (!trusted) {
            pq[0] = pq[1] = pq[2] = 0.0f;
            return;
        }
    }
    memcpy(pq, turned, sizeof(turned));
}

#define PSX_TURNED_NO_DEPTH 16.0f /* the display mirror takes a depth this small, of either sign, as none */

/* A corner's depth along the turned view: turned depths lie in one plane where the unturned did. None stays none, one stays one. */
static inline float psx_turned_depth(float x, float depth, float yaw_sin, float yaw_cos, float center_x, float focal_x) {
    float den, turned;
    if (fabsf(yaw_sin) <= 0.0000001f || focal_x <= 0.0f || !(fabsf(depth) > PSX_TURNED_NO_DEPTH)) return depth;
    den = yaw_cos - (x - center_x) / focal_x * yaw_sin;
    if (!(den >= 0.5f && den <= 2.0f)) return depth;
    turned = fabsf(depth) * den;
    if (!(turned > PSX_TURNED_NO_DEPTH)) turned = nextafterf(PSX_TURNED_NO_DEPTH, 2.0f * PSX_TURNED_NO_DEPTH);
    return depth < 0.0f ? -turned : turned;
}

#endif
