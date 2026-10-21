/* Exact GTE projection provenance shared by the GTE and GPU paths. SWC2 records the projection of a guest RAM word.
 * Games that read SXY through MFC2 and store with SW are admitted only through reviewed MFC2 and store PCs registered by
 * the game runtime, a bounded audit of known paths. The result codes tell an absent store from an address collision or
 * stale packet data. */

#ifndef PSXRECOMP_GTE_PRECISION_H
#define PSXRECOMP_GTE_PRECISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum GtePrecisionLookupResult {
    GTE_PRECISION_LOOKUP_ACCEPTED = 0,
    GTE_PRECISION_LOOKUP_TRACKING_DISABLED,
    GTE_PRECISION_LOOKUP_SPECULATIVE,
    GTE_PRECISION_LOOKUP_SOURCE_NOT_RAM,
    GTE_PRECISION_LOOKUP_SOURCE_UNALIGNED,
    GTE_PRECISION_LOOKUP_STORE_MISS,
    GTE_PRECISION_LOOKUP_STORE_COLLISION,
    GTE_PRECISION_LOOKUP_PROJECTION_INVALID,
    GTE_PRECISION_LOOKUP_PACKED_MISMATCH,
    GTE_PRECISION_LOOKUP_ZERO_DEPTH,
    GTE_PRECISION_LOOKUP_SATURATED,
    GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID,
    GTE_PRECISION_LOOKUP_RESULT_COUNT
} GtePrecisionLookupResult;

GtePrecisionLookupResult gte_precision_load_word_ex(
    uint32_t addr, uint32_t packed,
    int32_t *x16, int32_t *y16, uint16_t *z);

/* Texture interpolation needs an unclamped projective depth on top of the packet identity, so the two lookups stay separate. */
GtePrecisionLookupResult gte_precision_load_perspective_word_ex(
    uint32_t addr, uint32_t packed, uint16_t *z);

/* The GTE input vertex behind a projection: bit 63 set, then VZ, VY, VX as
 * 16-bit fields. Zero means unknown. */
GtePrecisionLookupResult gte_precision_load_identity(
    uint32_t addr, uint32_t packed, uint64_t *identity);

/* A corner before the divide, behind the viewer too: its place is (centre_x + hx / w, centre_y + hy / w) while w is not 0. */
typedef struct GtePrecisionView {
    double hx, hy, centre_x, centre_y;
    int32_t w;
    uint16_t focal;
    uint64_t identity; /* as gte_precision_load_identity gives it */
} GtePrecisionView;
int gte_precision_load_view_word(uint32_t addr, uint32_t packed, GtePrecisionView *view);

/* Retained boolean ABI used by older runtime code and out-of-tree modules. */
int gte_precision_load_word(uint32_t addr, uint32_t packed,
                            int32_t *x16, int32_t *y16, uint16_t *z);

/* memory.c owns the write result: every 32-bit write attempt clears the token, a completed main-RAM or scratchpad write
 * arms it, and projection callbacks consume it once before provenance enters the cache. */
typedef struct GtePrecisionWordCommitToken {
    uint32_t physical;
    uint8_t domain;  /* gte.cpp's PrecisionWordDomain: 0 none, 1 main RAM */
    uint8_t pending;
} GtePrecisionWordCommitToken;
/* Inline: memory.c runs these two on every 32-bit store. */
extern GtePrecisionWordCommitToken g_gte_precision_word_commit;
static inline void gte_precision_word_write_begin(void) {
    g_gte_precision_word_commit.domain = 0u;
    g_gte_precision_word_commit.pending = 0u;
}
/* memory.c passes the folded address after all four bytes are written: no unaligned host write may become an aligned token. */
static inline void gte_precision_main_ram_word_committed(uint32_t physical) {
    if (physical >= 0x00200000u || (physical & 3u) != 0u) {
        gte_precision_word_write_begin();
        return;
    }
    g_gte_precision_word_commit.physical = physical;
    g_gte_precision_word_commit.domain = 1u;
    g_gte_precision_word_commit.pending = 1u;
}
void gte_precision_scratch_word_committed(uint32_t physical);
/* Allocated enhancement packet memory uses a distinct physical-address domain. */
void gte_precision_gpu_dma_word_committed(uint32_t physical);
void gte_precision_store_word(uint32_t addr, uint8_t reg);
void gte_precision_invalidate_word(uint32_t addr);
/* A bit a 4 KB page that may hold provenance this generation, per domain: a store elsewhere skips the call. */
extern uint32_t g_gte_precision_ram_pages[16];
extern uint32_t g_gte_precision_dma_pages[8];
extern uint32_t g_gte_precision_scratch_live;
static inline int gte_precision_ram_live(uint32_t addr) {
    const uint32_t page = (addr & 0x1FFFFFu) >> 12;
    return (int)((g_gte_precision_ram_pages[page >> 5] >> (page & 31u)) & 1u);
}
static inline int gte_precision_dma_live(uint32_t phys) {
    const uint32_t page = ((phys - 0x00F00000u) >> 12) & 255u;
    return (int)((g_gte_precision_dma_pages[page >> 5] >> (page & 31u)) & 1u);
}

/* Register reviewed MFC2 and ordinary-SW identities. The MFC2 callback snapshots the projection read, the post-write
 * callback fires after the matching SW and the memory token confirms the commit. Both need the registered raw
 * instruction word, so changed code fails closed. Registration is idempotent, reset is for configuration, not per frame. */
void gte_precision_store_pc_routes_reset(void);
int gte_precision_mfc2_pc_route_add(uint32_t mfc2_pc, uint32_t instruction,
                                    uint8_t gte_reg);
int gte_precision_store_pc_route_add(uint32_t store_pc, uint32_t instruction,
                                     uint8_t gte_reg);
uint32_t gte_precision_mfc2_pc_read(uint32_t mfc2_pc, uint32_t instruction,
                                    uint8_t gte_reg, uint32_t packed);
void gte_precision_store_pc_word(uint32_t store_pc, uint32_t instruction,
                                 uint32_t addr, uint32_t packed);

/* Register a reviewed SW route into a finite set of aligned scratchpad words named by the game configuration. Post-write,
 * so it also needs the scratchpad commit token. */
int gte_precision_scratch_store_pc_route_add(
    uint32_t store_pc, uint32_t instruction, uint8_t gte_reg,
    uint32_t scratch_first, uint32_t scratch_stride,
    uint32_t scratch_count);
/* The route's stores may pin a coordinate into [low, high]: such a word keeps the projection it was pinned from. */
int gte_precision_scratch_store_pc_route_clamp(
    uint32_t store_pc, uint32_t instruction,
    int16_t low_x, int16_t high_x, int16_t low_y, int16_t high_y);
int gte_precision_word_clamped(uint32_t addr, uint32_t packed);
/* Whether a corner the game pinned is drawn where it projects when the GTE saturated it or its polygon falls back. */
int gte_precision_unpin_enabled(void);
void gte_precision_unpin_set(int enabled);
void gte_precision_scratch_store_pc_word(
    uint32_t store_pc, uint32_t instruction,
    uint32_t addr, uint32_t packed);

/* Register reviewed projected-word copies into GP0 packets: one exact LW, one exact SW, one unchanged GPR. The destination
 * main-RAM commit publishes the source snapshot. */
int gte_precision_copy_pc_route_add(
    uint32_t load_pc, uint32_t load_instruction,
    uint32_t store_pc, uint32_t store_instruction, uint8_t gpr);
uint32_t gte_precision_copy_pc_read(
    uint32_t load_pc, uint32_t instruction, uint8_t gpr,
    uint32_t addr, uint32_t packed);
void gte_precision_copy_pc_word(
    uint32_t store_pc, uint32_t instruction, uint8_t gpr,
    uint32_t addr, uint32_t packed);

typedef struct GtePrecisionDiagnostics {
    uint64_t store_collisions;
    uint64_t store_evictions;
    uint64_t lookup_collisions;
    uint64_t store_uncommitted_rejections;
    uint64_t registered_store_attempts;
    uint64_t registered_store_accepts;
    uint64_t registered_store_packed_rejections;
    uint64_t copy_load_attempts;
    uint64_t copy_load_accepts;
    uint64_t copy_store_attempts;
    uint64_t copy_store_accepts;
    uint64_t copy_store_packed_rejections;
} GtePrecisionDiagnostics;

void gte_precision_diagnostics(GtePrecisionDiagnostics *out);
void gte_precision_diagnostics_reset(void);

/* Keeps diagnostic interpreter replays from replacing authoritative provenance. */
int gte_replay_side_effects_begin(void);
void gte_replay_side_effects_end(void);

#ifdef __cplusplus
}
#endif

#endif /* PSXRECOMP_GTE_PRECISION_H */
