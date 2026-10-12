#ifndef DISRUPTOR_LANGUAGE_H
#define DISRUPTOR_LANGUAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Takes a language pack once the game runs. Returns how many of its strings stand in, 0 when it is refused. */
int disruptor_language_load_pack(const uint8_t *data, uint32_t size);
int disruptor_language_active(void);
/* Names the image of another region's disc. Asked before the drive first reads the game disc. */
void disruptor_language_set_disc(const char *path);
/* The strings of the language disc laid over the game disc, once it is. */
int disruptor_language_disc_pack(const uint8_t **data, uint32_t *size);
/* Whether that disc's hints are twice as tall as the US ones. */
int disruptor_language_disc_tall_hints(void);
/* Lays the named language disc over the disc in the drive now and not at the drive's first read: its save states have a folder of their own. */
void disruptor_language_settle(void);

#ifdef __cplusplus
}
#endif

#endif
