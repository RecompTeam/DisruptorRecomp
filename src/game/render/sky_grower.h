#pragma once

#include "game/render/sky_panorama.h"

#include <cstdint>

namespace disruptor::sky_grower {

/* Once a VBlank while the sky is drawn, on the emulation thread: a new revision of the panorama (its indices and base
 * palette at VRAM `clut_x`, `clut_y`) grows its extension on a worker, and a grown one is handed to the renderer.
 * `changed`: the cell table was told anew. */
void look(const sky_panorama::Cells &cells, uint32_t clut_x, uint32_t clut_y, bool changed, uint32_t vblanks);

/* Developer builds: PSX_SKY_DUMP=<folder> writes each new panorama there, see sky_dump.cpp. */
void dump(const sky_panorama::Cells &cells, uint32_t clut_x, uint32_t clut_y);

}  // namespace disruptor::sky_grower
