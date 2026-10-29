#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CPUState;

/* Draw-distance presets, saved as [disruptor] draw_distance. */
enum DisruptorFarRenderingPreset {
    DISRUPTOR_FAR_RENDERING_RETAIL = 0,
    DISRUPTOR_FAR_RENDERING_EXTENDED = 1, /* 1.25x */
    DISRUPTOR_FAR_RENDERING_FAR = 2,      /* 1.50x */
    DISRUPTOR_FAR_RENDERING_DOUBLE = 3,   /* 2x */
    DISRUPTOR_FAR_RENDERING_PRESET_COUNT = 4,
};

/* Independent, session-only control for the game's depth-fade ramp.  The
 * disabled mode moves the beginning of the ramp to the effective far plane,
 * leaving a zero-width fade interval without changing the clip distance. */
enum DisruptorFarRenderingDepthFadeMode {
    DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL = 0,
    DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED = 1,
    DISRUPTOR_FAR_RENDERING_DEPTH_FADE_MODE_COUNT = 2,
};

enum DisruptorFarRenderingResult {
    DISRUPTOR_FAR_RENDERING_OK = 1,
    DISRUPTOR_FAR_RENDERING_INVALID_PRESET = 0,
    DISRUPTOR_FAR_RENDERING_NETPLAY_BLOCKED = -1,
};

enum DisruptorFarRenderingInstructionPhase {
    DISRUPTOR_FAR_RENDERING_BEFORE_INSTRUCTION = 0,
    DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION = 1,
};

enum { DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION = 4 };

typedef struct DisruptorFarRenderingDiagnostics {
    uint32_t abi_version;
    uint32_t struct_size;

    int32_t preset;
    /* True only while the entry-to-epilogue diagnostics window is active.
     * Exact load substitution itself is intentionally stateless so it also
     * works after a savestate resumes in the middle of the renderer. */
    int32_t active;
    int32_t gameplay_ready;
    int32_t netplay_blocked;

    uint64_t renderer_entries;
    uint64_t completed_frames;
    uint64_t substituted_loads;
    uint64_t rejected_entries;
    uint64_t nested_entries;
    uint64_t missed_epilogues;
    uint64_t invalid_globals;
    uint64_t primitive_samples;

    uint32_t source_far_distance;
    int32_t source_fade_start;
    uint32_t effective_far_distance;
    int32_t effective_fade_start;

    uint32_t primitive_start;
    uint32_t primitive_end;
    uint32_t primitive_last_delta;
    uint32_t primitive_high_water;

    /* Wall-clock renderer span, not thread CPU time. */
    double render_wall_us_p50;
    double render_wall_us_p95;
    double render_wall_us_max;

    /* ABI v2 append-only fields.  substituted_loads above remains the total
     * for compatibility with v1 readers. */
    int32_t depth_fade_mode;
    uint32_t reserved_v2;
    uint64_t far_load_substitutions;
    uint64_t fade_load_substitutions;

    /* ABI v3 append-only fields.  These are passive renderer observations:
     * they never recreate guest visibility decisions or write guest state.
     * A portal shortcut relaxation is an earlier far-test result, whereas a
     * portal final-decision flip reaches the helper's final distance test. */
    uint64_t traversal_recursion_entries;
    uint64_t traversal_cap_hits;
    uint64_t portal_far_tests;
    uint64_t portal_shortcut_relaxations;
    uint64_t portal_final_decisions;
    uint64_t portal_final_effective_rejections;
    uint64_t portal_final_decision_flips;
    uint64_t object_far_decisions;
    uint64_t object_effective_rejections;
    uint64_t object_decision_flips;
    uint64_t observer_sequence_errors;
    uint64_t visibility_samples;
    uint64_t packet_stage_samples;

    uint32_t traversal_rooms_last;
    uint32_t traversal_rooms_high_water;
    uint32_t submitted_spans_last;
    uint32_t submitted_spans_high_water;
    uint32_t traversal_max_depth_last;
    uint32_t traversal_max_depth_high_water;

    /* Last complete frame's primitive-arena growth at four renderer stages.
     * A sample is published only when every boundary is seen in order and all
     * five pointers (including entry and epilogue) are valid and monotonic. */
    uint32_t packet_entry_to_traversal_end_last;
    uint32_t packet_traversal_to_visible_start_last;
    uint32_t packet_visible_loop_last;
    uint32_t packet_post_visible_loop_last;

    /* ABI v4: walks turned back from a sector out of memory past the game's own distance, and the times the copy of the
     * game's portal test did not come out as the game's did. */
    uint64_t sector_requests_held;
    uint64_t portal_model_mismatches;
} DisruptorFarRenderingDiagnostics;

int disruptor_far_rendering_set_preset(int preset);
/* PSX_DISRUPTOR_DRAW_DISTANCE=125, 150 or 200 sets that preset for a developer's measure, past what the menu offers. */
void disruptor_far_rendering_force_from_environment(void);
int disruptor_far_rendering_get_preset(void);
int disruptor_far_rendering_set_depth_fade_mode(int mode);
int disruptor_far_rendering_get_depth_fade_mode(void);
int disruptor_far_rendering_gameplay_ready(void);
int disruptor_far_rendering_netplay_blocked(void);
/* The far plane and fade start the renderer draws with now: the globals, which hold the preset's inside a written frame. */
void disruptor_far_rendering_drawn_distances(uint32_t *far_distance, int32_t *fade_start);

/* Writes only the caller-provided capacity and returns one on success.
 * Timing/primitive comparison fields are scoped to the current distance/fade
 * combination and reset when either setting changes; safety counters remain
 * session-wide. */
int disruptor_far_rendering_get_diagnostics(
    DisruptorFarRenderingDiagnostics *out, uint32_t capacity);

/* Clears an in-flight diagnostics window before resetting all session state. */
void disruptor_far_rendering_reset_session(void);

/* Abandons an in-flight entry-to-epilogue window after a savestate load: the
 * restored RAM holds the game's own distances, so nothing is written back. */
void disruptor_far_rendering_abandon_metrics(void);

/* Puts the game's own distances back before a savestate is taken. */
void disruptor_far_rendering_state_saving(void);

/* Where the fog of the twelve palette rows begins, how deep it is and the power it grows with: under the smooth fade from
 * the game's own start to the far plane drawn with, else the game's own 768 units from the start drawn with. */
void disruptor_far_rendering_fog_ramp(int32_t *start, int32_t *length, int *exponent);
/* Nonzero while the meshes are drawn past the fog's end, for the renderer to dissolve them: the arena must hold them. */
int disruptor_far_rendering_edge_margin(void);
/* 1 grows the fog evenly, 2 and 3 keep it thinner near the viewer. Applies under the smooth fade. */
void disruptor_far_rendering_set_fog_exponent(int exponent);
int disruptor_far_rendering_get_fog_exponent(void);
/* The share of the next palette row, in 256ths, that the sprite whose packet sits at `resolved_packet` blends in; -1 for none.
 * The words first drawn there own the share: other words at that address later are another primitive. */
int disruptor_far_rendering_packet_fraction(uint32_t resolved_packet, uint32_t signature);
/* Nonzero when the frame's fog shaped the fade of the list body in `packet`: `rows` from range `from` to `to` by that fog. */
int disruptor_far_rendering_list_fade_rows(uint32_t packet, int32_t from, int32_t to, int32_t *rows);

/* Nonzero inside a frame drawn at the preset's distances or with the rooms' spans opened: a savestate waits for its epilogue. */
int disruptor_far_rendering_save_deferred(void);
/* Nonzero unless PSX_DISRUPTOR_PORTAL_REACH=0: rooms seen beside their portals are drawn, the arena must hold them. */
int disruptor_far_rendering_portal_reach(void);

/* Version-pinned runtime seams for SLUS-00224. */
void disruptor_far_rendering_renderer_entry(struct CPUState *cpu,
                                            uint32_t address);
void disruptor_far_rendering_sector_entry(struct CPUState *cpu, uint32_t address);
void disruptor_far_rendering_instruction_hook(
    struct CPUState *cpu, uint32_t pc, uint32_t instruction, int phase);

#ifdef __cplusplus
}  /* extern "C" */
#endif
