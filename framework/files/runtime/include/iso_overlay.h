#ifndef PSXRECOMP_ISO_OVERLAY_H
#define PSXRECOMP_ISO_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A trusted module may stand in front of every image opened with iso_open.
 * `read` is asked for each raw sector first: 1 when it filled the 2352 bytes,
 * 0 to let the image answer, -1 when the sector cannot be had. `sectors` gives
 * the disc's length, or 0 to leave the image's own. `closed` is told when such
 * an image is closed: its handle may be given to another image afterwards.
 */
typedef struct PsxIsoOverlay {
    int (*read)(void *handle, uint32_t lba, uint8_t *raw);
    uint32_t (*sectors)(void *handle);
    void (*closed)(void *handle);
} PsxIsoOverlay;

void iso_set_overlay(const PsxIsoOverlay *overlay);
/* An image the overlay does not stand in front of, and the same for one read of any image. */
void *iso_open_plain(const char *path);
int iso_read_raw_sector_plain(void *handle, uint32_t lba, uint8_t *raw);
uint32_t iso_sector_count_plain(void *handle);
/* The image last opened with iso_open and still open: the disc in the drive. Null when there is none. */
void *iso_game_disc(void);

#ifdef __cplusplus
}
#endif

#endif
