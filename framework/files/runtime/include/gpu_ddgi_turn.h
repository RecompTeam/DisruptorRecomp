/* The camera's turn for probe lighting: camera to world by the true inverse, and the smooth turn's part of a step. */
#ifndef PSXRECOMP_GPU_DDGI_TURN_H
#define PSXRECOMP_GPU_DDGI_TURN_H

#include <math.h>
#include <stdint.h>

/* The inverse of a turn given by its rows, a column each: world = sum of back[k] * camera[k]. The game's turn has rows up to
 * 0.44% long, so its transpose put far points tens of units inside their surface. A singular turn keeps the transpose. */
static inline void psx_ddgi_back_turn(const float rows[3][3], float back[3][3]) {
    const float *a = rows[0], *b = rows[1], *c = rows[2];
    const float cross[3][3] = {{b[1] * c[2] - b[2] * c[1], b[2] * c[0] - b[0] * c[2], b[0] * c[1] - b[1] * c[0]},
                               {c[1] * a[2] - c[2] * a[1], c[2] * a[0] - c[0] * a[2], c[0] * a[1] - c[1] * a[0]},
                               {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}};
    const float det = a[0] * cross[0][0] + a[1] * cross[0][1] + a[2] * cross[0][2];
    for (int k = 0; k < 3; ++k)
        for (int i = 0; i < 3; ++i) back[k][i] = fabsf(det) > 1e-6f ? cross[k][i] / det : rows[k][i];
}

/* The same for the GTE's rotation, 4096 a unit. */
static inline void psx_ddgi_pose_back(const int16_t rotation[9], float back[3][3]) {
    float rows[3][3];
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) rows[r][k] = (float)rotation[r * 3 + k] / 4096.0f;
    psx_ddgi_back_turn(rows, back);
}

/* Rows and shift of a camera turned about its eye by the smooth turn's angle: the picture was drawn so, its readers are too. */
static inline void psx_ddgi_turn_rows(float rows[3][4], float turn) {
    const float turn_sin = sinf(turn), turn_cos = cosf(turn);
    for (int k = 0; k < 4; ++k) {
        const float x = rows[0][k], z = rows[2][k];
        rows[0][k] = turn_cos * x + turn_sin * z;
        rows[2][k] = turn_cos * z - turn_sin * x;
    }
}

/* A camera's inverse turn and its eye in the world, from its rows (a turn and a shift) and its offset. */
static inline void psx_ddgi_camera_back(const float rows[3][4], const float offset[3], float back[3][3], float eye[3]) {
    const float turn[3][3] = {{rows[0][0], rows[0][1], rows[0][2]}, {rows[1][0], rows[1][1], rows[1][2]},
                              {rows[2][0], rows[2][1], rows[2][2]}};
    psx_ddgi_back_turn(turn, back);
    for (int a = 0; a < 3; ++a) eye[a] = offset[a] - (back[0][a] * rows[0][3] + back[1][a] * rows[1][3] + back[2][a] * rows[2][3]);
}

/* The camera of an in-between frame, keep of the way from after back to before: rows and offset taken between, the
 * inverse and the eye of those rows, not the mean of two inverses. */
static inline void psx_ddgi_camera_between(const float before[3][4], const float before_offset[3], const float after[3][4],
                                           const float after_offset[3], float keep, float rows[3][4], float offset[3],
                                           float back[3][3], float eye[3]) {
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 4; ++k) rows[r][k] = before[r][k] * keep + after[r][k] * (1.0f - keep);
    for (int a = 0; a < 3; ++a) offset[a] = before_offset[a] * keep + after_offset[a] * (1.0f - keep);
    psx_ddgi_camera_back(rows, offset, back, eye);
}

#endif
