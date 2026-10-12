#ifndef PSXRECOMP_GPU_WS_TAG_MATCH_H
#define PSXRECOMP_GPU_WS_TAG_MATCH_H

#include <stdint.h>

enum PsxWsTagMatchResult {
    PSX_WS_TAG_EXPIRED = 0,
    PSX_WS_TAG_MATCH = 1,
    PSX_WS_TAG_CONTENT_MISMATCH = -1,
};

/* Fingerprint a POLY_FT4's GP0 words, independent of host byte order. CLUT and
 * page are left out: the 0x8003D488 seam tags before the game stores them. */
static inline uint32_t psx_ws_ft4_signature_words(const uint32_t *words) {
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 9; i++) {
        uint32_t word = i == 2 || i == 4 ? words[i] & 0xFFFFu : words[i];
        for (int byte = 0; byte < 4; byte++) {
            hash ^= word & 0xFFu;
            hash *= 16777619u;
            word >>= 8;
        }
    }
    return hash;
}

static inline int psx_ws_tag_match_result(
        uint32_t now, uint32_t stamp, int content_validated,
        uint32_t expected_signature, const uint32_t *current_words) {
    if (now - stamp > 2u) return PSX_WS_TAG_EXPIRED;
    if (!content_validated ||
        expected_signature == psx_ws_ft4_signature_words(current_words)) {
        return PSX_WS_TAG_MATCH;
    }
    return PSX_WS_TAG_CONTENT_MISMATCH;
}

#endif
