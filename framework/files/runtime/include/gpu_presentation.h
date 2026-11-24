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
    uint64_t micros;   /* the host's time since the log began, right before its swap */
} PsxShownPicture;

typedef struct {
    PsxShownPicture ring[PSX_SHOWN_LOG_SIZE];
    uint32_t head, tail, sequence, lost;
    int on;
} PsxShownLog;

typedef struct {
    uint64_t capture, native_capture, interval, source_interval, remainder;
    unsigned slots, extra;
} PsxLivePresentation;

static inline float psx_live_presentation_sample(PsxLivePresentation *plan, uint64_t capture,
                                                  uint64_t source_period, uint64_t display_period, int room) {
    if (!display_period) return 1.0f;
    if (plan->capture != capture || plan->interval != display_period) {
        if (plan->interval != display_period || (plan->capture && capture - plan->capture != 1u)) {
            plan->remainder = display_period / 2u;
            plan->source_interval = source_period;
        } else {
            plan->source_interval = (plan->source_interval * 3u + source_period + 2u) / 4u;
        }
        const uint64_t budget = plan->source_interval + plan->remainder;
        plan->slots = (unsigned)(budget / display_period);
        plan->remainder = budget % display_period;
        plan->extra = 0;
        plan->capture = capture;
        plan->interval = display_period;
    }
    if (!room || source_period <= display_period || plan->native_capture == capture ||
        (plan->native_capture && capture - plan->native_capture > 1u) ||
        plan->extra + 1u >= plan->slots) return 1.0f;
    return (float)++plan->extra / (float)plan->slots;
}

static inline void psx_live_presentation_shown(PsxLivePresentation *plan, uint64_t capture, float alpha) {
    if (alpha >= 1.0f) plan->native_capture = capture;
}

static inline int psx_live_presentation_previous_due(const PsxLivePresentation *plan, uint64_t capture,
                                                      uint64_t source_period, uint64_t display_period) {
    return capture > 1u && plan->native_capture < capture - 1u &&
           (source_period > display_period || (plan->capture == capture - 1u && plan->extra));
}

static inline uint64_t psx_live_presentation_idle_slot(const PsxLivePresentation *plan, uint64_t slot,
                                                       uint64_t last_source, uint64_t ready_age,
                                                       uint64_t reserve, uint64_t grace) {
    const uint64_t display = plan->interval, source = plan->source_interval;
    if (!display || source < 2u * display - display / 8u || source > 2u * display + display / 8u ||
        source <= reserve + grace) return 0;
    const uint64_t next_source = last_source + source;
    const uint64_t latest = source - reserve - grace;
    const uint64_t age = ready_age + grace < latest ? ready_age + grace : latest;
    const uint64_t target = next_source + age;
    return slot >= next_source && target > slot + grace && target < slot + display ? target : 0;
}

/* The VBlank since `first` that the emulated clock `cycle` falls in, 0 for a clock before it or no period. */
static inline uint32_t psx_shown_frame_of(uint64_t cycle, uint64_t first, uint64_t period) {
    return period != 0u && cycle > first ? (uint32_t)((cycle - first) / period) : 0u;
}

static inline void psx_shown_log_begin(PsxShownLog *log) {
    memset(log, 0, sizeof *log);
    log->on = 1;
}

/* Returns the picture's number. One that finds the ring full is counted lost and keeps its number. */
/* A host counter's reading as microseconds since egan, 0 for one before it or no frequency. */
static inline uint64_t psx_shown_micros(uint64_t now, uint64_t began, uint64_t frequency) {
    return frequency != 0u && now > began ? (now - began) / frequency * 1000000u + (now - began) % frequency * 1000000u / frequency : 0u;
}

static inline uint32_t psx_shown_log_add(PsxShownLog *log, uint32_t frame, int kind, float alpha, int width, int height, uint64_t micros) {
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
    picture->micros = micros;
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

static inline void psx_shown_script_missed(PsxShownScript *s) {
    s->next++;
    s->missed++;
    if (s->shown) s->late++;
}

/* An endpoint needs one held frame. A picture between frames needs both. */
static inline int psx_shown_script_step(PsxShownScript *s, uint32_t frame, int held, float *alpha) {
    while (s->next < s->count) {
        if (!held) return PSX_SHOWN_WAIT;
        const PsxShownPicture *picture = &s->pictures[s->next];
        if (picture->frame > frame) return PSX_SHOWN_WAIT;
        const float place = picture->kind ? picture->alpha : 1.0f;
        if (picture->frame < frame || (place < 1.0f && held < 2)) {
            psx_shown_script_missed(s);
            continue;
        }
        *alpha = place;
        return PSX_SHOWN_PRESENT;
    }
    return PSX_SHOWN_DONE;
}

static inline void psx_shown_script_presented(PsxShownScript *s) {
    s->next++;
    s->shown++;
}

static inline int psx_shown_script_native_matches(const PsxShownScript *s, uint32_t frame) {
    return s->next < s->count && s->pictures[s->next].frame == frame && !s->pictures[s->next].kind;
}

static inline int psx_shown_script_native_presented(PsxShownScript *s, uint32_t frame) {
    if (!psx_shown_script_native_matches(s, frame)) return 0;
    psx_shown_script_presented(s);
    return 1;
}

/* Nothing is left of the pictures placed before game frame `frame` or an earlier one: the next may be handed over. */
static inline int psx_shown_script_drained(const PsxShownScript *s, uint32_t frame) {
    return s->next >= s->count || s->pictures[s->next].frame > frame;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_PRESENTATION_H */
