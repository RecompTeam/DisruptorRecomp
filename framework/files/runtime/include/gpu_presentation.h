/* What reached the screen, picture by picture: a log a capsule keeps while it records, and a script its replay presents
 * again, each picture between the same two game frames at the same place. A game frame is named by the VBlank in which
 * the game presented it, which a recording and its replay share. Free of renderer state for its test. */
#ifndef GPU_PRESENTATION_H
#define GPU_PRESENTATION_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_SHOWN_LOG_SIZE 4096u

typedef struct {
    uint32_t sequence; /* its number among the pictures since the log began */
    uint32_t frame;    /* the later of the two game frames it is placed between */
    float alpha;       /* its place between them, 1 the later one */
    uint16_t width, height;
    uint8_t kind;      /* 0 a game frame's own swap, 1 the in-between presenter's */
} PsxShownPicture;

typedef struct {
    PsxShownPicture ring[PSX_SHOWN_LOG_SIZE];
    uint32_t head, tail, sequence, lost;
    int on;
} PsxShownLog;

/* The VBlank since `first` that the emulated clock `cycle` falls in, 0 for a clock before it or no period. */
static inline uint32_t psx_shown_frame_of(uint64_t cycle, uint64_t first, uint64_t period) {
    return period != 0u && cycle > first ? (uint32_t)((cycle - first) / period) : 0u;
}

static inline void psx_shown_log_begin(PsxShownLog *log) {
    memset(log, 0, sizeof *log);
    log->on = 1;
}

/* Returns the picture's number. One that finds the ring full is counted lost and keeps its number. */
static inline uint32_t psx_shown_log_add(PsxShownLog *log, uint32_t frame, int kind, float alpha, int width, int height) {
    const uint32_t sequence = log->sequence++;
    if (log->head - log->tail >= PSX_SHOWN_LOG_SIZE) {
        log->lost++;
        return sequence;
    }
    PsxShownPicture *picture = &log->ring[log->head % PSX_SHOWN_LOG_SIZE];
    picture->sequence = sequence;
    picture->frame = frame;
    picture->alpha = alpha;
    picture->width = (uint16_t)(width > 0 && width < 65536 ? width : 0);
    picture->height = (uint16_t)(height > 0 && height < 65536 ? height : 0);
    picture->kind = (uint8_t)(kind ? 1 : 0);
    log->head++;
    return sequence;
}

static inline uint32_t psx_shown_log_take(PsxShownLog *log, PsxShownPicture *out, uint32_t room) {
    uint32_t taken = 0u;
    while (taken < room && log->tail != log->head) out[taken++] = log->ring[log->tail++ % PSX_SHOWN_LOG_SIZE];
    return taken;
}

typedef struct {
    PsxShownPicture *pictures;
    uint32_t count, next, shown, missed;
    uint32_t late;     /* missed after the first was shown: the replay fell behind, not the presenter's start */
    uint32_t timeouts; /* game frames handed over before their pictures were shown, the wait given up */
    uint64_t first, period; /* the replay's clock at the recording's first VBlank, and a VBlank's cycles */
} PsxShownScript;

enum { PSX_SHOWN_WAIT, PSX_SHOWN_PRESENT, PSX_SHOWN_DONE };

/* The presenter holding game frame `frame` as its later one, `ready` when it holds the two a picture is placed between. A
 * picture of an earlier frame, or of this one while not ready, can no longer be drawn: it is counted missed. A game
 * frame's own swap is presented as the later frame. */
static inline int psx_shown_script_step(PsxShownScript *s, uint32_t frame, int ready, float *alpha) {
    while (s->next < s->count) {
        const PsxShownPicture *picture = &s->pictures[s->next];
        if (picture->frame > frame) return PSX_SHOWN_WAIT;
        if (picture->frame < frame || !ready) {
            s->next++;
            s->missed++;
            if (s->shown) s->late++;
            continue;
        }
        *alpha = picture->kind ? picture->alpha : 1.0f;
        return PSX_SHOWN_PRESENT;
    }
    return PSX_SHOWN_DONE;
}

static inline void psx_shown_script_presented(PsxShownScript *s) {
    s->next++;
    s->shown++;
}

/* Nothing is left of the pictures placed before game frame `frame` or an earlier one: the next may be handed over. */
static inline int psx_shown_script_drained(const PsxShownScript *s, uint32_t frame) {
    return s->next >= s->count || s->pictures[s->next].frame > frame;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_PRESENTATION_H */
