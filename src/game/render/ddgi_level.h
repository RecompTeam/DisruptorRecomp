#pragma once

#include <cstddef>

struct PsxNightVertex;

namespace disruptor::ddgi_level {

/* Hands the renderer every face of the level's rooms, lit as the game lights them near, when they changed and lighting is on. */
void publish_if_changed();
int night_area(std::size_t queued_frames = 0);
const PsxNightVertex *night_geometry(int area, int *vertices);
void invalidate_night_area();

}  // namespace disruptor::ddgi_level
