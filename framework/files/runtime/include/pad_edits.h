#pragma once

/* What the game did to the pads in a frame, kept so the late low-latency re-sample of the devices is edited the same
 * way: the frame hooks run before it, and without this every press they made was lost. Pad words are active-low. */

#include <stdint.h>
#include <string.h>

typedef struct {
    uint16_t word[2];
    uint16_t pressed[2];
    uint16_t released[2];
    uint8_t sticks[2][4];
    uint8_t has_word[2];
    uint8_t has_sticks[2];
} PsxPadEdits;

static inline void psx_pad_edits_clear(PsxPadEdits *edits) { memset(edits, 0, sizeof *edits); }

/* A whole word replaces the device's: presses and releases before it are gone, later ones apply over it. */
static inline void psx_pad_edits_word(PsxPadEdits *edits, int slot, uint16_t word) {
    edits->word[slot] = word;
    edits->has_word[slot] = 1;
    edits->pressed[slot] = 0;
    edits->released[slot] = 0;
}

static inline void psx_pad_edits_press(PsxPadEdits *edits, int slot, uint16_t mask) {
    edits->pressed[slot] = (uint16_t)(edits->pressed[slot] | mask);
    edits->released[slot] = (uint16_t)(edits->released[slot] & ~mask);
}

static inline void psx_pad_edits_release(PsxPadEdits *edits, int slot, uint16_t mask) {
    edits->released[slot] = (uint16_t)(edits->released[slot] | mask);
    edits->pressed[slot] = (uint16_t)(edits->pressed[slot] & ~mask);
}

static inline void psx_pad_edits_sticks(PsxPadEdits *edits, int slot, const uint8_t sticks[4]) {
    memcpy(edits->sticks[slot], sticks, 4);
    edits->has_sticks[slot] = 1;
}

static inline int psx_pad_edits_any(const PsxPadEdits *edits, int slot) {
    return edits->has_word[slot] || edits->pressed[slot] || edits->released[slot];
}

/* The pad word the game reads after a re-sample read `sampled` from the devices. */
static inline uint16_t psx_pad_edits_apply(const PsxPadEdits *edits, int slot, uint16_t sampled) {
    const uint16_t base = edits->has_word[slot] ? edits->word[slot] : sampled;
    return (uint16_t)((base & ~edits->pressed[slot]) | edits->released[slot]);
}
