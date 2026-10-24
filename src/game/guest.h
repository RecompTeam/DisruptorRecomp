#pragma once

#include "mod_plugins.h"

#include <cstdint>

namespace disruptor {

/* The game's memory through the mod interface, as the logic headers' Memory parameter reads and writes it. */
struct Guest {
    uint32_t word(uint32_t address) const { return psx_mod_read_word(address); }
    uint32_t half(uint32_t address) const { return psx_mod_read_half(address); }
    uint32_t byte(uint32_t address) const { return psx_mod_read_byte(address); }
    void write(uint32_t address, uint32_t value) { psx_mod_write_word(address, value); }
    void write_half(uint32_t address, uint16_t value) { psx_mod_write_half(address, value); }
    void write_byte(uint32_t address, uint8_t value) { psx_mod_write_byte(address, value); }
};

} // namespace disruptor
