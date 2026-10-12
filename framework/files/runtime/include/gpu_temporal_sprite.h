#ifndef PSXRECOMP_GPU_TEMPORAL_SPRITE_H
#define PSXRECOMP_GPU_TEMPORAL_SPRITE_H

#include <math.h>
#include <stdint.h>

/* What the game's seams hand over for one sprite, in pixels. */
typedef struct {
    int placed, sized, shadow, whole;
    float centre_x, offset_y; /* unrounded centre column, and how far the point stands from its stored row */
    float row, width, height; /* unrounded centre row and sides */
} GpuTemporalSprite;

/* The seams' 16.16 numbers as pixels. */
static inline GpuTemporalSprite gpu_temporal_sprite_from_fixed(int placed, int sized, int shadow, int whole,
                                                               int32_t centre_x, int32_t offset_y,
                                                               int32_t row, int32_t width, int32_t height) {
    const GpuTemporalSprite sprite = {
        placed != 0, sized != 0, shadow != 0, whole != 0,
        (float)centre_x / 65536.0f, (float)offset_y / 65536.0f,
        (float)row / 65536.0f, (float)width / 65536.0f, (float)height / 65536.0f,
    };
    return sprite;
}

#define GPU_TEMPORAL_SPRITE_SIDE_LIMIT 3.0f /* px a side may move: a fraction, the squash's rounding and a size cut to whole pixels */
#define GPU_TEMPORAL_SPRITE_HORIZON 120.0f
#define GPU_TEMPORAL_SPRITE_FOCAL 160.0f

/* How far the presentation moves the left, top, right and bottom side of a sprite's rectangle. left, top, w and h are
 * the rectangle on screen, raw_w and raw_h the packet's own size, scale the classic-wide squash. With `flat` a shadow
 * takes the height of a disc on the floor, and its top and bottom may then move by up to raw_h instead of the limit. */
static inline void gpu_temporal_sprite_sides(const GpuTemporalSprite *sprite, float left, float top, int w, int h,
                                             int raw_w, int raw_h, float scale, int flat, float sides[4]) {
    sides[0] = sides[1] = sides[2] = sides[3] = 0.0f;
    if (!sprite || !sprite->placed) return;
    /* A packet spans its size cut to whole pixels, less one unless the game rebuilt it from a sorted record. */
    const float cut = sprite->whole ? 0.5f : 1.5f, lean = sprite->whole ? 0.25f : -0.25f;
    float width = (float)w, height = (float)h, limit = GPU_TEMPORAL_SPRITE_SIDE_LIMIT;
    float centre_y = top + height * 0.5f + sprite->offset_y;
    /* The size is the packet's own only when it is cut to the packet's whole pixels. */
    if (sprite->sized && fabsf(sprite->width - cut - (float)(raw_w - 1)) <= 1.0f &&
        fabsf(sprite->height - cut - (float)(raw_h - 1)) <= 1.0f) {
        width = (sprite->width - cut) * scale;
        height = sprite->height - cut;
        centre_y = sprite->row + lean;
        if (sprite->shadow && flat && sprite->row > GPU_TEMPORAL_SPRITE_HORIZON) {
            /* A disc on the floor is as tall as it is wide times its row under the horizon over the focal length. */
            height = (sprite->width - cut) * (sprite->row - GPU_TEMPORAL_SPRITE_HORIZON) / GPU_TEMPORAL_SPRITE_FOCAL;
            limit = (float)raw_h;
        }
        if (width < 1.0f) width = 1.0f;
        if (height < 1.0f) height = 1.0f;
    }
    /* Halving an odd size downwards and dropping the last column or not leaves the middle a quarter pixel off on average. */
    const float centre_x = sprite->centre_x + lean * scale;
    sides[0] = centre_x - width * 0.5f - left;
    sides[2] = centre_x + width * 0.5f - (left + (float)w);
    sides[1] = centre_y - height * 0.5f - top;
    sides[3] = centre_y + height * 0.5f - (top + (float)h);
    if (fabsf(sides[0]) > GPU_TEMPORAL_SPRITE_SIDE_LIMIT || fabsf(sides[2]) > GPU_TEMPORAL_SPRITE_SIDE_LIMIT ||
        fabsf(sides[1]) > limit || fabsf(sides[3]) > limit)
        sides[0] = sides[1] = sides[2] = sides[3] = 0.0f;
}

#endif
