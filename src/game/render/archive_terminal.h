#pragma once

#include <stdint.h>

enum {
    DISRUPTOR_ARCHIVE_SCREEN_X = -17360,
    DISRUPTOR_ARCHIVE_SCREEN_TOP = -6080,
    DISRUPTOR_ARCHIVE_SCREEN_BOTTOM = -5696,
    DISRUPTOR_ARCHIVE_PANEL_LEFT_Z = 21408,
    DISRUPTOR_ARCHIVE_PANEL_MID_Z = 20976,
    DISRUPTOR_ARCHIVE_PANEL_RIGHT_Z = 20544,
    DISRUPTOR_ARCHIVE_FRAME_WIDTH = 64,
    DISRUPTOR_ARCHIVE_FRAME_HEIGHT = 40,
    DISRUPTOR_ARCHIVE_FRAME_COUNT = 8,
};

#define DISRUPTOR_ARCHIVE_GLASS_LEFT 6
#define DISRUPTOR_ARCHIVE_GLASS_TOP 6
#define DISRUPTOR_ARCHIVE_GLASS_RIGHT 34
#define DISRUPTOR_ARCHIVE_GLASS_BOTTOM 26
#define DISRUPTOR_ARCHIVE_GLASS_CORNER 1

#ifdef __cplusplus
extern "C" {
#endif

void gl_renderer_set_archive_terminal(int frame, int32_t eye_x, int32_t eye_y, int32_t eye_z);

#ifdef __cplusplus
}

namespace disruptor::archive_terminal {

void after_input();
void update(bool in_gauntlet, const int32_t (&eye)[3]);
void state_loaded();

}
#endif
