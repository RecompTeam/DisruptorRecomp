#ifndef DISRUPTOR_RESTART_H
#define DISRUPTOR_RESTART_H

#ifdef __cplusplus
extern "C" {
#endif

/* Makes this program start again, with the command line it was started with, when it exits. 1 when armed, 0 where
 * a program cannot start itself again. The caller then closes the game the way a window close does. */
int disruptor_restart_arm(void);

/* 1 in a program that a restart started. It waited for the one before it to end before anything else ran. */
int disruptor_restart_started_this(void);

#ifdef __cplusplus
}
#endif

#endif
