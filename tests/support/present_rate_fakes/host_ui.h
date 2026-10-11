#ifndef DISRUPTOR_TEST_FAKE_HOST_UI_H
#define DISRUPTOR_TEST_FAKE_HOST_UI_H
void psx_host_video_get_interpolation(int *enabled, int *target_fps, int *blend);
int psx_host_video_set_interpolation(int enabled, int target_fps, int blend);
#endif
