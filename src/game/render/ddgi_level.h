#pragma once

namespace disruptor::ddgi_level {

/* Hands the renderer every face of the level's rooms when they changed, while the sun is on a mission that has one. */
void publish_if_changed();

}  // namespace disruptor::ddgi_level
