#pragma once

#include <stdint.h>

extern "C" {
// The HUD read its mode: a list it opens prints its name a frame before its panel is drawn.
void disruptor_hud_widgets_mode(uint32_t mode);
}
