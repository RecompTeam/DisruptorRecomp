#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The camera of one world draw: a game marks its start, the first projection after it fills the rest. A world point is
 * input + offset, the input the GTE's vertex. */
typedef struct GteCameraPose {
    int16_t rotation[9];
    int32_t translation[3];
    int32_t offset[3];
    int32_t centre_x16, centre_y16;
    int32_t squash_num, squash_den;
    uint16_t focal;
    uint32_t sequence; /* 0: no pose */
} GteCameraPose;
void gte_pose_begin(void);
void gte_set_pose_offset_reader(void (*reader)(int32_t offset[3]));
int gte_pose_get(uint32_t sequence, GteCameraPose *pose);

#ifdef __cplusplus
}
#endif
