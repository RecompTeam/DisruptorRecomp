#ifndef DISRUPTOR_INTRO_SKIP_H
#define DISRUPTOR_INTRO_SKIP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Cuts the three logo screens short and ends the title movie on its second frame. */
int disruptor_intro_skip_enabled(void);
void disruptor_intro_skip_set_enabled(int enabled);
/* The skip key: a press is taken for half a second by the movie on screen, or by the logo on screen or next up. */
void disruptor_intro_skip_request(void);
/* The frontend asks once per VBlank: non-zero while the boot is to run unpaced and unseen. Over at the main menu, in play, or after 1500 such VBlanks. */
int disruptor_intro_skip_fast_forward(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* DISRUPTOR_INTRO_SKIP_H */
