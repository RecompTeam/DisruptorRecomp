#ifndef DISRUPTOR_LANGUAGE_H
#define DISRUPTOR_LANGUAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Takes a language pack once the game runs. Returns how many of its strings stand in, 0 when it is refused. */
int disruptor_language_load_pack(const uint8_t *data, uint32_t size);
int disruptor_language_active(void);
/* The pack's words for `text` as the small font draws it, into `words` of `size` bytes. 0 when it has none. */
int disruptor_language_words(const char *text, char *words, uint32_t size);
/* Names the image of another region's disc. Asked before the drive first reads the game disc. */
void disruptor_language_set_disc(const char *path);
/* The strings of the language disc laid over the game disc, once it is. */
int disruptor_language_disc_pack(const uint8_t **data, uint32_t *size);
/* That disc's language in English, as a launcher lists it: empty while none is laid over. */
const char *disruptor_language_disc_name(void);
/* Whether that disc's hints are twice as tall as the US ones. */
int disruptor_language_disc_tall_hints(void);
/* Lays the named language disc over the disc in the drive now and not at the drive's first read: its save states have a folder of their own. */
void disruptor_language_settle(void);
/* The stream frame number the language disc's player keys movie `movie`'s sound on at, 0 when it does so at the first. */
int disruptor_language_disc_sound_start(uint32_t movie);
/* The VBlanks the language disc's player lets mission `mission`'s briefing play before the mission starts, -1 while the US wait stands. */
int disruptor_language_disc_briefing_wait(uint32_t mission);
/* Nonzero while a movie's sound start this run holds: a save then waits, an older build could not let the hold go. */
int disruptor_language_movie_sound_save_deferred(void);
/* The run's own hold is not the loaded state's. */
void disruptor_language_movie_sound_state_loaded(void);

#ifdef __cplusplus
}
#endif

#endif
