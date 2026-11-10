#ifndef PSX_GPU_RAW_CHANGES_H
#define PSX_GPU_RAW_CHANGES_H

#include <stdint.h>

/* Where a picture changed lately, so a copy of it taken at an earlier generation is brought up to date by its changed places alone. */

#define PSX_RAW_CHANGE_RECTS 64

typedef struct {
    uint16_t x, y, w, h;
} PsxRawRect;

typedef struct {
    uint64_t generation;                     /* one up a change, never 0 once begun */
    uint64_t forgotten;                      /* the newest change the table no longer holds */
    uint64_t changed[PSX_RAW_CHANGE_RECTS];  /* the newest change of each place, 0 for a free row */
    PsxRawRect rects[PSX_RAW_CHANGE_RECTS];
} PsxRawChanges;

static inline void psx_raw_changes_begin(PsxRawChanges *c) {
    c->forgotten = ++c->generation;
}

static inline void psx_raw_changes_note(PsxRawChanges *c, int x, int y, int w, int h) {
    int row = 0;
    ++c->generation;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > 0xFFFF || y + h > 0xFFFF) {
        psx_raw_changes_begin(c);
        return;
    }
    for (int other = 0; other < PSX_RAW_CHANGE_RECTS; ++other) {
        const PsxRawRect *rect = &c->rects[other];
        if (c->changed[other] && rect->x == x && rect->y == y && rect->w == w && rect->h == h) {
            c->changed[other] = c->generation;
            return;
        }
        if (c->changed[other] < c->changed[row]) row = other;
    }
    if (c->changed[row] > c->forgotten) c->forgotten = c->changed[row];
    c->rects[row].x = (uint16_t)x;
    c->rects[row].y = (uint16_t)y;
    c->rects[row].w = (uint16_t)w;
    c->rects[row].h = (uint16_t)h;
    c->changed[row] = c->generation;
}

/* The places to copy into a copy taken at `held`, or -1 when it takes the whole picture (`most` texels count as the whole). */
static inline int psx_raw_changes_since(const PsxRawChanges *c, uint64_t held, uint64_t most,
                                        PsxRawRect out[PSX_RAW_CHANGE_RECTS]) {
    uint64_t area = 0;
    int count = 0;
    if (!held || held > c->generation || held < c->forgotten) return -1;
    for (int row = 0; row < PSX_RAW_CHANGE_RECTS; ++row) {
        if (c->changed[row] <= held) continue;
        area += (uint64_t)c->rects[row].w * c->rects[row].h;
        if (area >= most) return -1;
        out[count++] = c->rects[row];
    }
    return count;
}

#endif
