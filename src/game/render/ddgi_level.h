#pragma once

namespace disruptor::ddgi_level {

/* Hands the renderer every face of the level's rooms, lit as the game lights them near, when they changed and lighting is on. */
void publish_if_changed();

}  // namespace disruptor::ddgi_level
