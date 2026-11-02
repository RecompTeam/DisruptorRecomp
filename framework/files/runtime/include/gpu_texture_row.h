#ifndef PSXRECOMP_GPU_TEXTURE_ROW_H
#define PSXRECOMP_GPU_TEXTURE_ROW_H

#include <stdint.h>
#include <string.h>

/* A wall texture's first texel row ships far lighter than the rest, so where two faces meet on it a light line runs down the wall. */
enum {
    PSX_TEXTURE_ROW_X = 448 + 0x60 / 2, /* page 0x97, 8-bit, the tile at u 0x60..0x7F and v 0xE0..0xFF, in words */
    PSX_TEXTURE_ROW_Y = 256 + 0xE0,
    PSX_TEXTURE_ROW_WORDS = 16,
};

static const uint16_t psx_texture_row_shipped[2][PSX_TEXTURE_ROW_WORDS] = {
    {0x032D, 0x8988, 0x1746, 0xBC17, 0x2D2D, 0xBC2D, 0x17BC, 0x8365, 0x8317, 0x0365, 0x8303, 0x6517, 0x6565, 0x89AC, 0x0765, 0x1783},
    {0x864F, 0x5B4F, 0x5718, 0x5757, 0x2EAE, 0x572E, 0x572E, 0x1818, 0x2E4F, 0x1811, 0x2E4F, 0x4F57, 0x0918, 0x094F, 0x4909, 0x0B4C},
};

/* After `vram` (1024 by 512 words) took the rectangle at (x, y), w by h: the light row takes the next row's texels when both are as shipped. */
static inline int psx_texture_row_fix(uint16_t *vram, int x, int y, int w, int h) {
    if (x >= PSX_TEXTURE_ROW_X + PSX_TEXTURE_ROW_WORDS || x + w <= PSX_TEXTURE_ROW_X || y > PSX_TEXTURE_ROW_Y + 1 ||
        y + h <= PSX_TEXTURE_ROW_Y)
        return 0;
    uint16_t *light = &vram[PSX_TEXTURE_ROW_Y * 1024 + PSX_TEXTURE_ROW_X];
    const uint16_t *next = light + 1024;
    if (memcmp(light, psx_texture_row_shipped[0], sizeof psx_texture_row_shipped[0]) ||
        memcmp(next, psx_texture_row_shipped[1], sizeof psx_texture_row_shipped[1]))
        return 0;
    memcpy(light, next, sizeof psx_texture_row_shipped[1]);
    return 1;
}

#endif
