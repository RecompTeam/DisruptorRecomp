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
#define GPU_TEMPORAL_SPRITE_MIDDLE 160.0f /* the column the game projects the view's middle to */
#define GPU_SPRITE_CARD_FAR 0.15f  /* half width over depth a card turns all the way up to: a capsule's pickups and far enemies */
#define GPU_SPRITE_CARD_NEAR 0.35f /* from here it stays flat: the same capsule's body at the player's feet stood at 0.35 to 0.65 */

/* A packet spans its size cut to whole pixels, less one unless the game rebuilt it from a sorted record. The size is
 * the packet's own only when it is cut to the packet's whole pixels: then its width, height and centre row. */
static inline int gpu_temporal_sprite_own_size(const GpuTemporalSprite *sprite, int raw_w, int raw_h, float scale,
                                               float *width, float *height, float *centre_y) {
    const float cut = sprite->whole ? 0.5f : 1.5f, lean = sprite->whole ? 0.25f : -0.25f;
    if (!sprite->sized || fabsf(sprite->width - cut - (float)(raw_w - 1)) > 1.0f ||
        fabsf(sprite->height - cut - (float)(raw_h - 1)) > 1.0f)
        return 0;
    *width = (sprite->width - cut) * scale;
    *height = sprite->height - cut;
    *centre_y = sprite->row + lean;
    return 1;
}

/* How far the presentation moves the left, top, right and bottom side of a sprite's rectangle. left, top, w and h are
 * the rectangle on screen, raw_w and raw_h the packet's own size, scale the classic-wide squash. With `flat` a shadow
 * takes the height of a disc on the floor, and its top and bottom may then move by up to raw_h instead of the limit. */
static inline void gpu_temporal_sprite_sides(const GpuTemporalSprite *sprite, float left, float top, int w, int h,
                                             int raw_w, int raw_h, float scale, int flat, float sides[4]) {
    sides[0] = sides[1] = sides[2] = sides[3] = 0.0f;
    if (!sprite || !sprite->placed) return;
    const float cut = sprite->whole ? 0.5f : 1.5f, lean = sprite->whole ? 0.25f : -0.25f;
    float width = (float)w, height = (float)h, limit = GPU_TEMPORAL_SPRITE_SIDE_LIMIT;
    float centre_y = top + height * 0.5f + sprite->offset_y;
    if (gpu_temporal_sprite_own_size(sprite, raw_w, raw_h, scale, &width, &height, &centre_y)) {
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

/* A sprite turned about its upright axis to face the camera's point instead of lying in the screen's plane: how far
 * the presentation moves the x of its left and right side and the y of its top left, top right, bottom left and bottom
 * right corner, and the texture weight of each side, its depth's inverse as a share of the middle's. */
typedef struct {
    float x[2], y[4], q[2];
} GpuSpriteCard;

/* How far a card turns, as a share of the middle's angle off the view's, by its half width over its depth. */
static inline float gpu_sprite_card_share(float half) {
    const float share = (GPU_SPRITE_CARD_NEAR - half) / (GPU_SPRITE_CARD_NEAR - GPU_SPRITE_CARD_FAR);
    return share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
}

/* As gpu_temporal_sprite_sides, the rows turned about the horizon's. A shadow, a sprite of no own size or under a
 * pixel, one whose point stands more than the limit from its rectangle's middle, or one too near to turn stays flat: 0. */
static inline int gpu_sprite_card(const GpuTemporalSprite *sprite, float left, float top, int w, int h, int raw_w,
                                  int raw_h, float scale, float horizon, GpuSpriteCard *card) {
    float width, height, centre_y;
    if (!sprite || !sprite->placed || sprite->shadow || !(scale > 0.0f) ||
        !gpu_temporal_sprite_own_size(sprite, raw_w, raw_h, scale, &width, &height, &centre_y) || width < 1.0f ||
        height < 1.0f)
        return 0;
    const float centre_x = sprite->centre_x + (sprite->whole ? 0.25f : -0.25f) * scale;
    if (fabsf(centre_x - (left + (float)w * 0.5f)) > GPU_TEMPORAL_SPRITE_SIDE_LIMIT ||
        fabsf(centre_y - (top + (float)h * 0.5f)) > GPU_TEMPORAL_SPRITE_SIDE_LIMIT)
        return 0;
    const float focal = GPU_TEMPORAL_SPRITE_FOCAL * scale;
    const float aside = (centre_x - GPU_TEMPORAL_SPRITE_MIDDLE) / focal;  /* the tangent of the middle's angle off the view's */
    const float half = width * 0.5f / focal;                               /* the half width over the middle's depth */
    const float turn = atanf(aside) * gpu_sprite_card_share(half);
    if (turn == 0.0f) return 0;
    for (int side = 0; side < 2; ++side) {
        const float along = side ? half : -half, depth = 1.0f - along * sinf(turn);
        card->x[side] = GPU_TEMPORAL_SPRITE_MIDDLE + focal * (aside + along * cosf(turn)) / depth - (side ? left + (float)w : left);
        card->y[side] = horizon + (centre_y - height * 0.5f - horizon) / depth - top;
        card->y[2 + side] = horizon + (centre_y + height * 0.5f - horizon) / depth - (top + (float)h);
        card->q[side] = 1.0f / depth;
    }
    return 1;
}

/* The camera-space x and z of a card's half width when its middle stands at x, z, turned as gpu_sprite_card turns it. */
static inline void gpu_sprite_card_across(double x, double z, double half, double across[2]) {
    const double turn = z > 0.0 ? atan(x / z) * gpu_sprite_card_share((float)(half / z)) : 0.0;
    across[0] = half * cos(turn);
    across[1] = -half * sin(turn);
}

#endif
