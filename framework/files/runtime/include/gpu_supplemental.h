/* Packets of the faces only a widened portal walk admits, by frame: the game's own culling would have dropped them, so
 * they may fill what nothing else covers but never cover what the game drew. Free of renderer state for its test. */
#ifndef GPU_SUPPLEMENTAL_H
#define GPU_SUPPLEMENTAL_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_SUPPLEMENTAL_RANGES 8192u
#define PSX_SUPPLEMENTAL_FAR (1 << 20) /* a band's side with no edge, past any column */

/* A face is the widened walk's alone outside [left, right]: left above right for the whole face. */
typedef struct {
    uint32_t start, end;
    int32_t left, right;
} PsxSupplementalRange;

typedef struct {
    PsxSupplementalRange ranges[2][PSX_SUPPLEMENTAL_RANGES];
    uint32_t count[2];
    uint32_t frame;
    uint64_t writes; /* grows with every change of the table and never goes back */
} PsxSupplemental;

static inline void psx_supplemental_reset(PsxSupplemental *s) {
    const uint64_t writes = s->writes + 1u;
    memset(s, 0, sizeof *s);
    s->writes = writes;
}

/* A frame begins building its packets: the frame before keeps its ranges until its own are drawn. */
static inline void psx_supplemental_frame(PsxSupplemental *s) {
    s->frame++;
    s->writes++;
    s->count[s->frame & 1u] = 0u;
}

/* The bytes [start, end) of this frame's packets. Packets come in the order the game allocates them; one that does
 * not is dropped rather than break the order the lookup searches. */
static inline void psx_supplemental_band(PsxSupplemental *s, uint32_t start, uint32_t end, int32_t left, int32_t right) {
    PsxSupplementalRange *ranges = s->ranges[s->frame & 1u];
    uint32_t *count = &s->count[s->frame & 1u];
    if (end <= start) return;
    s->writes++;
    if (*count != 0u && ranges[*count - 1u].end == start && ranges[*count - 1u].left == left && ranges[*count - 1u].right == right) {
        ranges[*count - 1u].end = end;
        return;
    }
    if ((*count != 0u && start < ranges[*count - 1u].end) || *count >= PSX_SUPPLEMENTAL_RANGES) return;
    ranges[*count].start = start;
    ranges[*count].end = end;
    ranges[*count].left = left;
    ranges[*count].right = right;
    (*count)++;
}

static inline void psx_supplemental_add(PsxSupplemental *s, uint32_t start, uint32_t end) {
    psx_supplemental_band(s, start, end, PSX_SUPPLEMENTAL_FAR, -PSX_SUPPLEMENTAL_FAR);
}

static inline const PsxSupplementalRange *psx_supplemental_find(const PsxSupplemental *s, uint32_t address) {
    for (uint32_t side = 0u; side < 2u; side++) {
        const PsxSupplementalRange *first = s->ranges[side];
        uint32_t left = s->count[side];
        if (left == 0u || address < first->start || address >= first[left - 1u].end) continue;
        /* A step adds instead of branching: which way a packet's search turns is as good as random. */
        while (left > 1u) {
            const uint32_t half = left / 2u;
            first += first[half - 1u].end <= address ? half : 0u;
            left -= half;
        }
        if (first->start <= address) return first;
    }
    return NULL;
}

/* The last lookup's answer: a packet is asked about up to three times in a row, and the search is 13 steps a side. */
typedef struct {
    uint32_t address;
    uint64_t writes; /* the table's count at the lookup plus one, so 0 is no lookup yet */
    const PsxSupplementalRange *range;
} PsxSupplementalLast;

static inline const PsxSupplementalRange *psx_supplemental_find_again(const PsxSupplemental *s, PsxSupplementalLast *last, uint32_t address) {
    if (last->writes != s->writes + 1u || last->address != address) {
        last->range = psx_supplemental_find(s, address);
        last->address = address;
        last->writes = s->writes + 1u;
    }
    return last->range;
}

static inline int psx_supplemental_has(const PsxSupplemental *s, uint32_t address) { return psx_supplemental_find(s, address) != NULL; }

#ifdef __cplusplus
}
#endif

#endif /* GPU_SUPPLEMENTAL_H */
