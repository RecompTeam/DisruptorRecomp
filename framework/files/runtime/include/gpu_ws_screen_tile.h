/* Classic-widescreen backdrop tiles: edge mapping and the packet mark table.
 * Free of renderer state so tests run the same code the renderer does. */
#ifndef GPU_WS_SCREEN_TILE_H
#define GPU_WS_SCREEN_TILE_H

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
} PsxWsScreenTile;

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
                                           uint32_t now) {
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
    for (uint32_t i = 0; i < PSX_WS_SCREEN_TILE_WORDS; i++)
        target->words[i] = words[i];
}

/* A mark buys one draw of a packet with the same address and words; any other
 * rectangle drawn from that address retires it. */
static inline int psx_ws_screen_tile_take(PsxWsScreenTiles *tiles,
                                          uint32_t key, const uint32_t *words,
                                          uint32_t now) {
    if (!key) return 0;
    for (uint32_t probe = 0; probe < PSX_WS_SCREEN_TILE_PROBES; probe++) {
        PsxWsScreenTile *tile = psx_ws_screen_tile_slot(tiles, key, probe);
        if (tile->key != key) continue;
        int match = now - tile->stamp <= PSX_WS_SCREEN_TILE_MAX_AGE;
        for (uint32_t i = 0; i < PSX_WS_SCREEN_TILE_WORDS; i++)
            match &= tile->words[i] == words[i];
        tile->key = 0u;
        return match;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_WS_SCREEN_TILE_H */
