/* Classic-widescreen backdrop tiles: edge mapping and the packet mark table.
 * Free of renderer state so tests run the same code the renderer does. */
#ifndef GPU_WS_SCREEN_TILE_H
#define GPU_WS_SCREEN_TILE_H

#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_WS_SCREEN_TILE_SLOTS 1024u /* power of two */
#define PSX_WS_SCREEN_TILE_PROBES 8u
#define PSX_WS_SCREEN_TILE_WORDS 4u    /* SPRT: colour+code, xy, uv+clut, wh */
#define PSX_WS_SCREEN_TILE_OPCODE 0x64u
#define PSX_WS_SCREEN_TILE_MAX_AGE 60u /* frames before an undrawn mark's slot is reused */

typedef struct {
    uint32_t key;
    uint32_t stamp;
    uint32_t words[PSX_WS_SCREEN_TILE_WORDS];
    uint32_t pins; /* bit 0: its left edge is the left end of the span the game fills, bit 1: its right the right end */
} PsxWsScreenTile;

/* A backdrop tile as the display's picture places it between the game's whole pixels, see gpu_ws_backdrop_scroll. */
typedef struct PsxWsPlacedTile {
    float left, right;    /* how far the unrounded edges stand off the whole pixels the frame draws them at */
    float moved;          /* how far the panorama stands off the game's whole-pixel scroll, in display pixels */
    float scale;          /* display pixels a texel */
    float column;         /* texels in a column of the panorama: a tile is cut from one */
    float origin, period; /* the panorama's, in display pixels */
    uint32_t pins;
} PsxWsPlacedTile;

typedef struct {
    PsxWsScreenTile slots[PSX_WS_SCREEN_TILE_SLOTS];
} PsxWsScreenTiles;

static inline int32_t psx_ws_squash_about(int32_t x, int32_t centre,
                                          int32_t numerator,
                                          int32_t denominator) {
    const int32_t offset = x - centre;
    const int32_t half = offset >= 0 ? denominator / 2 : -denominator / 2;
    return centre + (offset * numerator + half) / denominator;
}

/* Inverse of the squash, rounded outward: squashing the result never lands
 * inside the edge it was widened from. */
static inline int32_t psx_ws_widen_about(int32_t x, int32_t centre,
                                         int32_t numerator,
                                         int32_t denominator, int round_up) {
    const int32_t scaled = (x - centre) * denominator;
    int32_t quotient = scaled / numerator;
    const int32_t remainder = scaled % numerator;
    if (remainder != 0 && (remainder > 0) == (round_up != 0))
        quotient += round_up ? 1 : -1;
    return centre + quotient;
}

static inline PsxWsScreenTile *psx_ws_screen_tile_slot(PsxWsScreenTiles *tiles,
                                                       uint32_t key,
                                                       uint32_t probe) {
    return &tiles->slots[((key >> 2) + probe) &
                         (PSX_WS_SCREEN_TILE_SLOTS - 1u)];
}

/* Packets 0x1000 bytes apart share a home slot, so the whole probe run is
 * searched for the key before a free slot is taken. */
static inline void psx_ws_screen_tile_mark(PsxWsScreenTiles *tiles,
                                           uint32_t key, const uint32_t *words,
                                           uint32_t now, uint32_t pins) {
    if (!key || (words[0] >> 24) != PSX_WS_SCREEN_TILE_OPCODE) return;
    PsxWsScreenTile *target = 0;
    for (uint32_t probe = 0; probe < PSX_WS_SCREEN_TILE_PROBES; probe++) {
        PsxWsScreenTile *tile = psx_ws_screen_tile_slot(tiles, key, probe);
        if (tile->key == key) {
            target = tile;
            break;
        }
        const int reusable = tile->key == 0u ||
                             now - tile->stamp > PSX_WS_SCREEN_TILE_MAX_AGE;
        if (!target && reusable) target = tile;
    }
    if (!target) target = psx_ws_screen_tile_slot(tiles, key, 0u);
    target->key = key;
    target->stamp = now;
    target->pins = pins;
    for (uint32_t i = 0; i < PSX_WS_SCREEN_TILE_WORDS; i++)
        target->words[i] = words[i];
}

/* A mark buys one draw of a packet with the same address and words; any other
 * rectangle drawn from that address retires it. `pins` gets the mark's. */
static inline int psx_ws_screen_tile_take(PsxWsScreenTiles *tiles,
                                          uint32_t key, const uint32_t *words,
                                          uint32_t now, uint32_t *pins) {
    if (!key) return 0;
    for (uint32_t probe = 0; probe < PSX_WS_SCREEN_TILE_PROBES; probe++) {
        PsxWsScreenTile *tile = psx_ws_screen_tile_slot(tiles, key, probe);
        if (tile->key != key) continue;
        int match = now - tile->stamp <= PSX_WS_SCREEN_TILE_MAX_AGE;
        for (uint32_t i = 0; i < PSX_WS_SCREEN_TILE_WORDS; i++)
            match &= tile->words[i] == words[i];
        tile->key = 0u;
        *pins = match ? tile->pins : 0u;
        return match;
    }
    return 0;
}

/* Which edges of a tile at frame x are the ends of the span the game fills. An end stays on its whole pixel when the
 * panorama is placed between two of them: the tiles end there, and a moved end would bare the clear colour. */
static inline uint32_t psx_ws_screen_tile_pins(int32_t x, int32_t w, int32_t span_left, int32_t span_right) {
    return (x <= span_left ? 1u : 0u) | (x + w >= span_right ? 2u : 0u);
}

/* Where a tile's edge at frame x stands in the display's picture: the squash about the centre, not rounded. */
static inline double psx_ws_screen_tile_edge(int32_t x, int32_t centre, int32_t numerator, int32_t denominator) {
    return centre + ((double)x - centre) * numerator / denominator;
}

/* The texels of its column a tile has beyond the end at texture coordinate u: `end` 0 is its left, 1 its right. */
static inline float psx_ws_screen_tile_room(float u, float column, int end) {
    if (!(column > 0.0f)) return 0.0f;
    return end ? ceilf(u / column) * column - u : u - floorf(u / column) * column;
}

/* How far the texture slides at an end that stays on its whole pixel while the panorama stands `moved` display pixels
 * off it (`offset` is the end's own rounding, see PsxWsPlacedTile): the end shows the texel that lies there. Bounded by
 * the column's texels outward and by the tile's own inward. */
static inline float psx_ws_screen_tile_slide(float offset, float moved, float scale, int end,
                                             float room_out, float room_in) {
    if (!(scale > 0.0f) || !isfinite(offset + moved)) return 0.0f;
    const float slide = -(offset + moved) / scale;
    const float low = end ? -room_in : -room_out, high = end ? room_out : room_in;
    return slide < low ? low : slide > high ? high : slide;
}

/* That slide before the column and the tile bound it: the presenter adds its own way to it and bounds the sum. */
static inline float psx_ws_screen_tile_want(float offset, float moved, float scale) {
    return scale > 0.0f && isfinite(offset + moved) ? -(offset + moved) / scale : 0.0f;
}

/* How far back the panorama stood a frame earlier, in the display's pixels: the short way round its period, and no
 * more than `most`, past which a frame's own tiles cannot stand in for the frame before. */
static inline float psx_ws_backdrop_back(float before, float now, float period, float most) {
    if (!(period > 0.0f) || !isfinite(before) || !isfinite(now)) return 0.0f;
    float back = before - now;
    back -= period * floorf(back / period + 0.5f);
    return back > most ? most : back < -most ? -most : back;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_WS_SCREEN_TILE_H */
