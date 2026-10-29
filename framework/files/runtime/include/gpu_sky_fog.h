/* Sky openings of a frame and the fog step each is painted with: where openings meet, the clearest sky shows.
 * Free of renderer state so tests run the same code the renderer does. */
#ifndef GPU_SKY_FOG_H
#define GPU_SKY_FOG_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_SKY_FOG_OPENINGS 32u
#define PSX_SKY_FOG_TILES 1024u /* power of two */
#define PSX_SKY_FOG_PROBES 8u
#define PSX_SKY_FOG_WORDS 4u /* SPRT: colour+code, xy, uv+clut, wh */
#define PSX_SKY_FOG_OPCODE 0x64u

typedef struct {
    int32_t left, top, right, bottom;
} PsxSkyRect;

typedef struct {
    PsxSkyRect rect;
    int32_t step;
    uint16_t clut;
} PsxSkyOpening;

typedef struct {
    PsxSkyOpening openings[PSX_SKY_FOG_OPENINGS];
    uint32_t count, frame;
} PsxSkyFrame;

typedef struct {
    uint32_t key, frame;
    uint32_t words[PSX_SKY_FOG_WORDS];
    uint32_t opening;
} PsxSkyTile;

typedef struct {
    PsxSkyFrame frames[2];
    PsxSkyTile tiles[PSX_SKY_FOG_TILES];
    uint32_t frame;
    int drawn;
} PsxSkyFog;

static inline PsxSkyRect psx_sky_fog_tile_rect(const uint32_t *words) {
    const int32_t x = (int16_t)(words[1] & 0xFFFFu), y = (int16_t)(words[1] >> 16);
    const PsxSkyRect rect = {x, y, x + (int32_t)(words[3] & 0x3FFu), y + (int32_t)((words[3] >> 16) & 0x1FFu)};
    return rect;
}

static inline PsxSkyFrame *psx_sky_fog_current(PsxSkyFog *fog) { return &fog->frames[fog->frame & 1u]; }

/* A reset or a loaded state: no record may meet a packet of the new timeline. */
static inline void psx_sky_fog_reset(PsxSkyFog *fog) { memset(fog, 0, sizeof *fog); }

/* An opening the game begins to fill with `step`. The first one after a frame's sky was drawn begins the next frame. */
static inline void psx_sky_fog_open(PsxSkyFog *fog, int32_t step) {
    if (fog->drawn) {
        fog->drawn = 0;
        fog->frame++;
    }
    PsxSkyFrame *frame = psx_sky_fog_current(fog);
    if (frame->frame != fog->frame) {
        frame->frame = fog->frame;
        frame->count = 0u;
    }
    if (frame->count >= PSX_SKY_FOG_OPENINGS) {
        frame->count = PSX_SKY_FOG_OPENINGS + 1u; /* past the table: the frame's tiles are drawn as the game made them */
        return;
    }
    const PsxSkyOpening opening = {{INT32_MAX, INT32_MAX, INT32_MIN, INT32_MIN}, step, 0u};
    frame->openings[frame->count++] = opening;
}

static inline PsxSkyTile *psx_sky_fog_slot(PsxSkyFog *fog, uint32_t key, uint32_t probe) {
    return &fog->tiles[((key >> 2) + probe) & (PSX_SKY_FOG_TILES - 1u)];
}

/* A tile of the opening begun last, its packet complete at `key`. */
static inline void psx_sky_fog_tile(PsxSkyFog *fog, uint32_t key, const uint32_t *words) {
    PsxSkyFrame *frame = psx_sky_fog_current(fog);
    if (!key || (words[0] >> 24) != PSX_SKY_FOG_OPCODE || frame->frame != fog->frame || frame->count == 0u ||
        frame->count > PSX_SKY_FOG_OPENINGS)
        return;
    PsxSkyOpening *opening = &frame->openings[frame->count - 1u];
    const PsxSkyRect rect = psx_sky_fog_tile_rect(words);
    if (rect.right <= rect.left || rect.bottom <= rect.top) return;
    if (rect.left < opening->rect.left) opening->rect.left = rect.left;
    if (rect.top < opening->rect.top) opening->rect.top = rect.top;
    if (rect.right > opening->rect.right) opening->rect.right = rect.right;
    if (rect.bottom > opening->rect.bottom) opening->rect.bottom = rect.bottom;
    opening->clut = (uint16_t)(words[2] >> 16);
    PsxSkyTile *target = 0;
    for (uint32_t probe = 0; probe < PSX_SKY_FOG_PROBES && !target; probe++)
        if (psx_sky_fog_slot(fog, key, probe)->key == key) target = psx_sky_fog_slot(fog, key, probe);
    for (uint32_t probe = 0; probe < PSX_SKY_FOG_PROBES && !target; probe++) {
        PsxSkyTile *tile = psx_sky_fog_slot(fog, key, probe);
        if (tile->key == 0u || fog->frame - tile->frame > 1u) target = tile;
    }
    if (!target) target = psx_sky_fog_slot(fog, key, 0u); /* all eight live: the first one's tile keeps the game's fog */
    target->key = key;
    target->frame = fog->frame;
    target->opening = frame->count - 1u;
    for (uint32_t i = 0; i < PSX_SKY_FOG_WORDS; i++) target->words[i] = words[i];
}

/* Whether two openings overlap or share an edge: the sky runs on from one into the other. */
static inline int psx_sky_fog_meet(const PsxSkyRect *a, const PsxSkyRect *b) {
    return a->left <= b->right && b->left <= a->right && a->top <= b->bottom && b->top <= a->bottom;
}

/* Whether the tile drawn from `key` with `words` takes `clut`, the palette of the clearest opening its own one reaches
 * through openings that meet, so sky running on across openings shows one fog. A tile is drawn once: its record goes
 * with the draw. */
static inline int psx_sky_fog_take(PsxSkyFog *fog, uint32_t key, const uint32_t *words, uint16_t *clut) {
    if (!key) return 0;
    PsxSkyTile *found = 0;
    for (uint32_t probe = 0; probe < PSX_SKY_FOG_PROBES && !found; probe++) {
        PsxSkyTile *tile = psx_sky_fog_slot(fog, key, probe);
        if (tile->key == key) found = tile;
    }
    if (!found) return 0;
    int same = 1;
    for (uint32_t i = 0; i < PSX_SKY_FOG_WORDS; i++) same &= found->words[i] == words[i];
    found->key = 0u;
    if (found->frame == fog->frame) fog->drawn = 1;
    const PsxSkyFrame *frame = &fog->frames[found->frame & 1u];
    if (!same || frame->frame != found->frame || frame->count > PSX_SKY_FOG_OPENINGS) return 0;
    uint32_t reached[PSX_SKY_FOG_OPENINGS], count = 1u, best = found->opening;
    uint8_t seen[PSX_SKY_FOG_OPENINGS] = {0};
    reached[0] = found->opening;
    seen[found->opening] = 1u;
    for (uint32_t at = 0; at < count; at++)
        for (uint32_t i = 0; i < frame->count; i++) {
            if (seen[i] || !psx_sky_fog_meet(&frame->openings[reached[at]].rect, &frame->openings[i].rect)) continue;
            seen[i] = 1u;
            reached[count++] = i;
            if (frame->openings[i].step < frame->openings[best].step) best = i;
        }
    if (best == found->opening) return 0;
    *clut = frame->openings[best].clut;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_SKY_FOG_H */
