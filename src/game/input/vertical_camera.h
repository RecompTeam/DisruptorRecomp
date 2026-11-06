#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct CPUState;

/*
 * Disruptor has no retail pitch variable.  The host extension expresses pitch
 * in the same 256-units-per-turn domain as the game's yaw and projects it as a
 * vertical horizon offset.  The request survives temporarily unsupported
 * presentation states; the effective values are neutral while suspended.
 */
void disruptor_vertical_camera_set_requested_pitch(double pitch_units);
void disruptor_vertical_camera_recenter(void);
/* Shots go where the view looks even when the game picked a target to raise them to. */
void disruptor_vertical_camera_set_free_aim(int free_aim);

double disruptor_vertical_camera_requested_pitch(void);
double disruptor_vertical_camera_effective_pitch(void);
int disruptor_vertical_camera_effective_center_y(void);
double disruptor_vertical_camera_effective_slope(void);
int disruptor_vertical_camera_active(void);
/* True only while new mouse-Y input may update the requested pitch: a suspension outside live play keeps the pitch. */
int disruptor_vertical_camera_input_allowed(void);
/* Frames the game's renderer drew in live play: a loading screen draws none. */
uint32_t disruptor_vertical_camera_live_frames(void);
/* The horizon row the game's projection literals take at this moment: 120 unless a tilted frame is drawn in live play. */
int disruptor_vertical_camera_projection_row(void);
/* The same horizon in 16.16 with its fraction, as GTE OFY holds it. */
int32_t disruptor_vertical_camera_projection_row16(void);

enum DisruptorVerticalCameraInstructionPhase {
    DISRUPTOR_VERTICAL_CAMERA_BEFORE_INSTRUCTION = 0,
    DISRUPTOR_VERTICAL_CAMERA_AFTER_INSTRUCTION = 1,
};

/*
 * Version-pinned exact-instruction dispatcher for SLUS-00224.  Callers must
 * pass the original instruction word.  A PC/opcode/phase mismatch is an exact
 * no-op, which keeps regenerated or differently-versioned code fail-closed.
 */
void disruptor_vertical_camera_instruction_hook(
    struct CPUState *cpu, uint32_t pc, uint32_t instruction, int phase);

#ifdef __cplusplus
}  /* extern "C" */
#endif
