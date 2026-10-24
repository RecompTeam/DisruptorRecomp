/* A world sprite's silhouette for the shadow it throws: the texels that block light, taken from VRAM as the sprite is drawn. */
#ifndef PSXRECOMP_GPU_DDGI_MASK_H
#define PSXRECOMP_GPU_DDGI_MASK_H

#include "gpu_ddgi.h"

#include <math.h>
#include <string.h>

/* The texel's 15-bit colour with its mask bit through the page's depth and CLUT, 0 where it is transparent. vram: 1024 by 512. */
static inline uint16_t psx_ddgi_texel(const uint16_t *vram, int u, int v, uint16_t texpage, uint16_t clut) {
    u &= 255;
    v &= 255;
    const int tx = (texpage & 15) * 64, ty = ((texpage >> 4) & 1) * 256, depth = (texpage >> 7) & 3;
    const int cx = (clut & 63) * 16, cy = (clut >> 6) & 511, row = ((ty + v) & 511) * 1024;
    if (depth == 0) {
        const uint16_t px = vram[row + ((tx + (u >> 2)) & 1023)];
        return vram[cy * 1024 + ((cx + ((px >> ((u & 3) * 4)) & 15)) & 1023)];
    }
    if (depth == 1) {
        const uint16_t px = vram[row + ((tx + (u >> 1)) & 1023)];
        return vram[cy * 1024 + ((cx + ((px >> ((u & 1) * 8)) & 255)) & 1023)];
    }
    return vram[row + ((tx + u) & 1023)];
}

/* A texel blocks light unless it is transparent, or it blends: a semi-transparent sprite's texel with the mask bit. */
static inline int psx_ddgi_blocks(uint16_t texel, int semi) { return texel && !(semi && (texel & 0x8000u)); }

/* What a card's picture is as drawn, from VRAM now: by the next present the game has put the next frames of its animations
 * where these were. A 16 by 16 grid gives the share of texels not clear (cover), the share of those with the mask bit
 * (blended), their mean colour, and the box of the texels that block light; with `masked`, 32 by 32 bits over that box padded
 * by a cell and a half, and the bounds of the bits set. Without, the bounds are the grid's texels not clear. */
static inline void psx_ddgi_card_look(const uint16_t *vram, GpuDdgiCard *card, int semi, int masked) {
    memset(card->mask, 0, sizeof card->mask);
    memset(card->box, 0, sizeof card->box);
    memset(card->bounds, 0, sizeof card->bounds);
    memset(card->texels, 0, sizeof card->texels);
    card->cover = card->blended = 0.0f;
    const float du = (float)(card->uv[2] - card->uv[0]), dv = (float)(card->uv[3] - card->uv[1]);
    float lo[2] = {1.0f, 1.0f}, hi[2] = {0.0f, 0.0f}, seen_lo[2] = {1.0f, 1.0f}, seen_hi[2] = {0.0f, 0.0f}, sum[3] = {0.0f, 0.0f, 0.0f};
    int opaque = 0, marked = 0;
    for (int j = 0; j < 16; ++j)
        for (int i = 0; i < 16; ++i) {
            const float fx = ((float)i + 0.5f) / 16.0f, fy = ((float)j + 0.5f) / 16.0f;
            const uint16_t texel = psx_ddgi_texel(vram, card->uv[0] + (int)floorf(fx * du), card->uv[1] + (int)floorf(fy * dv),
                                                  card->texpage, card->clut);
            if (!texel) continue;
            ++opaque;
            marked += (texel & 0x8000u) != 0;
            sum[0] += (float)(texel & 31) / 31.0f;
            sum[1] += (float)((texel >> 5) & 31) / 31.0f;
            sum[2] += (float)((texel >> 10) & 31) / 31.0f;
            seen_lo[0] = fminf(seen_lo[0], fx - 0.5f / 16.0f);
            seen_lo[1] = fminf(seen_lo[1], fy - 0.5f / 16.0f);
            seen_hi[0] = fmaxf(seen_hi[0], fx + 0.5f / 16.0f);
            seen_hi[1] = fmaxf(seen_hi[1], fy + 0.5f / 16.0f);
            if (!psx_ddgi_blocks(texel, semi)) continue;
            lo[0] = fminf(lo[0], fx - 1.5f / 16.0f);
            lo[1] = fminf(lo[1], fy - 1.5f / 16.0f);
            hi[0] = fmaxf(hi[0], fx + 1.5f / 16.0f);
            hi[1] = fmaxf(hi[1], fy + 1.5f / 16.0f);
        }
    if (!opaque) return;
    card->cover = (float)opaque / 256.0f;
    card->blended = (float)marked / (float)opaque;
    for (int k = 0; k < 3; ++k) card->texels[k] = sum[k] / (float)opaque;
    if (!masked) {
        card->bounds[0] = seen_lo[0];
        card->bounds[1] = seen_lo[1];
        card->bounds[2] = seen_hi[0];
        card->bounds[3] = seen_hi[1];
        return;
    }
    if (hi[0] <= lo[0] || hi[1] <= lo[1]) return;
    for (int a = 0; a < 2; ++a) {
        lo[a] = fmaxf(lo[a], 0.0f);
        hi[a] = fminf(hi[a], 1.0f);
    }
    int us[32], first_column = 32, last_column = -1, first_row = GPU_DDGI_MASK_ROWS, last_row = -1;
    for (int column = 0; column < 32; ++column)
        us[column] = card->uv[0] + (int)floorf((lo[0] + ((float)column + 0.5f) / 32.0f * (hi[0] - lo[0])) * du);
    for (int row = 0; row < GPU_DDGI_MASK_ROWS; ++row) {
        const float fy = lo[1] + ((float)row + 0.5f) / (float)GPU_DDGI_MASK_ROWS * (hi[1] - lo[1]);
        const int v = card->uv[1] + (int)floorf(fy * dv);
        uint32_t bits = 0;
        for (int column = 0; column < 32; ++column)
            if (psx_ddgi_blocks(psx_ddgi_texel(vram, us[column], v, card->texpage, card->clut), semi)) {
                bits |= 1u << column;
                first_column = column < first_column ? column : first_column;
                last_column = column > last_column ? column : last_column;
            }
        card->mask[row] = bits;
        if (bits) {
            first_row = row < first_row ? row : first_row;
            last_row = row;
        }
    }
    card->box[0] = lo[0];
    card->box[1] = lo[1];
    card->box[2] = hi[0];
    card->box[3] = hi[1];
    if (last_row < 0) return;
    card->bounds[0] = lo[0] + (float)first_column / 32.0f * (hi[0] - lo[0]);
    card->bounds[1] = lo[1] + (float)first_row / (float)GPU_DDGI_MASK_ROWS * (hi[1] - lo[1]);
    card->bounds[2] = lo[0] + (float)(last_column + 1) / 32.0f * (hi[0] - lo[0]);
    card->bounds[3] = lo[1] + (float)(last_row + 1) / (float)GPU_DDGI_MASK_ROWS * (hi[1] - lo[1]);
}

/* The share of a card's texels that block light: a semi-transparent sprite's with the mask bit blend. An enemy with
 * translucent parts blends more than half of them in some frames of its animation and still throws a shadow. */
static inline float psx_ddgi_card_blocking(const GpuDdgiCard *card) {
    return card->cover * ((card->flags & 1) ? 1.0f - card->blended : 1.0f);
}

/* Where a caster's picture stands: the centre of the card's box and its half sizes, the card's own halves wide and tall. */
static inline void psx_ddgi_box_picture(const GpuDdgiCard *card, float wide, float tall, float picture[3], float *half_width,
                                        float *half_height) {
    const float bx = card->box[0] + card->box[2] - 1.0f, by = card->box[1] + card->box[3] - 1.0f;
    for (int a = 0; a < 3; ++a) picture[a] = card->centre[a] + card->across[a] * bx + card->down[a] * by;
    *half_width = wide * (card->box[2] - card->box[0]);
    *half_height = tall * (card->box[3] - card->box[1]);
}

#endif
