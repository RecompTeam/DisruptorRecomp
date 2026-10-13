/* A world face's texture on the mirror, free of the whole-pixel rule for mirrored 2D pictures (gpu_uv.h). */
#ifndef PSXRECOMP_GPU_WORLD_TEXEL_H
#define PSXRECOMP_GPU_WORLD_TEXEL_H

#include <math.h>

/* A triangle's bounds with a quarter on each one the rule tightened by a texel: the canonical picture rounds it off. */
static inline void psx_world_texel_mark(const int ruled[4], const int plain[4], float bounds[4]) {
    for (int i = 0; i < 4; ++i)
        bounds[i] = (float)ruled[i] + (ruled[i] == plain[i] + (i < 2 ? 1 : -1) ? 0.25f : 0.0f);
}

/* A recorded corner with whole bounds: `take` as the mirror shows it, a lower bound's texel back on the texture too. */
static inline void psx_world_texel_settle(int take, float uv[2], float bounds[4]) {
    for (int i = 0; i < 4; ++i) {
        const float whole = floorf(bounds[i] + 0.5f);
        const int marked = take && bounds[i] - whole > 0.125f;
        if (marked && i < 2) uv[i] -= 1.0f;
        bounds[i] = whole + (marked ? (i < 2 ? -1.0f : 1.0f) : 0.0f);
    }
}

#endif
