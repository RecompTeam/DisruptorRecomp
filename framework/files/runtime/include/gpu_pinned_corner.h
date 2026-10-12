/* What the presentation is told of a polygon's corners when the game pinned some of them to its guard band. */
#ifndef PSXRECOMP_GPU_PINNED_CORNER_H
#define PSXRECOMP_GPU_PINNED_CORNER_H

#include <stdint.h>

typedef struct PsxPinnedPlaces {
    int32_t x16[4], y16[4];
    int unpinned; /* some corner is placed off the game's pixel */
} PsxPinnedPlaces;

/* A polygon that falls back: a pinned corner goes where it projects, as in an exact polygon, the rest stay on the game's pixels. */
static inline void psx_pinned_places(int count, int unpin, const int *pinned,
                                     const int32_t *precise_x, const int32_t *precise_y,
                                     const int32_t *raw_x, const int32_t *raw_y,
                                     const int32_t *drawn_x, const int32_t *drawn_y, PsxPinnedPlaces *places) {
    places->unpinned = 0;
    for (int i = 0; i < count; ++i) {
        const int moved = unpin && pinned[i];
        places->x16[i] = moved ? (int32_t)((int64_t)precise_x[i] + (int64_t)(drawn_x[i] - raw_x[i]) * 65536) : drawn_x[i] * 65536;
        places->y16[i] = moved ? (int32_t)((int64_t)precise_y[i] + (int64_t)(drawn_y[i] - raw_y[i]) * 65536) : drawn_y[i] * 65536;
        places->unpinned |= moved;
    }
}

/* A pinned corner is a few units from the viewer's plane: carried by the camera it crosses it and its face is dropped. */
static inline uint16_t psx_pinned_corner_depth(int unpin, int pinned, uint16_t depth) {
    return unpin && pinned ? 0 : depth;
}

/* Mode 1 drops all three depths when one of them is 0. */
static inline int psx_pinned_depth_mode(uint16_t a, uint16_t b, uint16_t c) {
    return a && b && c ? 1 : 2;
}

#endif
