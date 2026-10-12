#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Registers the game's projection routes for the disc with this serial of `length` characters. 0 when a route was refused. */
int disruptor_precision_routes_configure(const char *game_id, size_t length);

#ifdef __cplusplus
}
#endif
