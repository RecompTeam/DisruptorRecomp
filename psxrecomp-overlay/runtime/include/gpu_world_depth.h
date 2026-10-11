/* Which polygons the mirror draws by depth, and the depth it stores for a corner. */
#ifndef PSXRECOMP_GPU_WORLD_DEPTH_H
#define PSXRECOMP_GPU_WORLD_DEPTH_H

/* The depth the mirror stores for a camera depth beyond 16, pulled toward the eye by `pull`: 1 is the far end, -1 the eye. */
static inline float psx_world_depth_stored(float depth, float pull) {
    const float stored = 1.0f - 32.0f * (1.0f + pull) / depth;
    return stored > -1.0f ? stored : -1.0f;
}

/* Beyond 16 by the margin too: the pulled depth of such a corner stays inside the clip range. */
static inline float psx_world_depth_nearest(float margin) { return 16.0f * (1.0f + margin) + 0.01f; }

/* An exact polygon with every corner beyond the nearest depth: bit 1 of the visual flags says exact. */
static inline int psx_world_depth_class(int enabled, int visual_flags, const float depth[3], float margin) {
    const float nearest = psx_world_depth_nearest(margin);
    return enabled && (visual_flags & 2) && depth[0] > nearest && depth[1] > nearest && depth[2] > nearest;
}

#endif
