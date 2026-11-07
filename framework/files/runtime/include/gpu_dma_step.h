#pragma once

#include "mod_memory.h"

#include <stdint.h>

/* The next word of a block or burst GPU transfer: it stays in the allocated GPU packet aperture or in main RAM's
 * mirror, whichever its word is in. Folded into RAM, a palette row stored in a moved arena overwrote 0x0 or 0x20000. */
static inline uint32_t psx_gpu_dma_next_for(uint32_t address, int32_t step, uint32_t used) {
    const uint32_t next = address + (uint32_t)step;
    if (psx_mod_gpu_dma_aperture_offset_for(address, 4u, used, (uint32_t *)0))
        return psx_mod_gpu_dma_resolve_address_for(next, used);
    return psx_ram_canonical_offset(next) & ~3u;
}
