/* When an interrupt check polls for a waiting save or load of a state. */
#ifndef PSXRECOMP_SAVE_POINT_H
#define PSXRECOMP_SAVE_POINT_H

#ifdef __cplusplus
extern "C" {
#endif

int savestate_pending(void);

/* 1 when this check must poll. A periodic poll that meets a request at a check with no address of its own is owed to the next that has one. */
static inline int psx_save_poll_due(int periodic, int save_point, int *owed) {
    if (!save_point) {
        if (periodic) *owed = savestate_pending();
        return 0;
    }
    if (!periodic && !*owed) return 0;
    *owed = 0;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif
