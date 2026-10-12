/* Perspective weights of a triangle whose picture the presentation turns by part of a game step. */
#ifndef PSXRECOMP_GPU_TURNED_PERSPECTIVE_H
#define PSXRECOMP_GPU_TURNED_PERSPECTIVE_H

#include <math.h>
#include <string.h>

/* A fractional yaw turns the view about the eye: a corner's depth along the new axis is its old depth times the turn's
 * denominator, so its q is divided by it. The whole triangle, or none of it when a corner lies too far aside to trust:
 * one q of 0 among three would corrupt the texture across the triangle. */
static inline void psx_turned_perspective(const float px[3], float pq[3], float yaw_sin, float yaw_cos, float center_x,
                                          float focal_x) {
    float turned[3];
    if (fabsf(yaw_sin) <= 0.0000001f || focal_x <= 0.0f) return;
    for (int i = 0; i < 3; ++i) {
        const float ux = (px[i] - center_x) / focal_x;
        const float den = yaw_cos - ux * yaw_sin;
        int trusted = pq[i] > 0.0f && pq[i] <= 1.0f && den >= 0.5f && den <= 2.0f;
        if (trusted) {
            turned[i] = pq[i] / den;
            trusted = turned[i] > 0.0f; /* the smallest weights round to 0 when divided */
        }
        if (!trusted) {
            pq[0] = pq[1] = pq[2] = 0.0f;
            return;
        }
    }
    memcpy(pq, turned, sizeof(turned));
}

#endif
