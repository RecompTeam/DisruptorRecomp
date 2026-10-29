#pragma once

/* Built only without PSX_GAME_VERSION (DISRUPTOR_DEV_TOOLS): a release carries none of it. */

#ifdef __cplusplus
extern "C" {
#endif

enum { DISRUPTOR_FIRST_MISSION = 1, DISRUPTOR_LAST_MISSION = 13 };
enum { DISRUPTOR_DIFFICULTY_REGULAR = 2, DISRUPTOR_DIFFICULTY_HARD = 4 };

int disruptor_mission_select_ready(void);
int disruptor_mission_select_start(int mission, int difficulty);

#ifdef __cplusplus
}
#endif
