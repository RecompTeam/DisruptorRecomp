/*
 * Disruptor's grown world quads (SLUS-00224).
 *
 * The world renderer draws a polygon whose flag bit 0 is set twice. At
 * 0x80046DA4 it writes a second POLY_GT4 0x34 bytes after the first, with the
 * first packet's corners pushed outwards by a sixteenth and stored in reverse
 * order. Those corners come from integer arithmetic on the packed positions,
 * so the GTE provenance of the first packet never reaches them: the renderer
 * drew the second packet on whole pixels and could not move it between two
 * game frames. Recomputing the arithmetic names, for each corner of the
 * second packet, the corner of the first that it came from. Guest state is
 * only read.
 */

#include "mod_plugins.h"

#include <array>
#include <cstdint>

namespace {

constexpr uint32_t kPacketBytes = 0x34u;
constexpr uint32_t kShadedTexturedQuad = 0x3Cu;
constexpr uint32_t kGuestBase = 0x80000000u;
/* Word index of each position in a POLY_GT4, counted from its command word. */
constexpr std::array<uint32_t, 4> kPositions{1u, 4u, 7u, 10u};

int32_t sra4(int32_t value) {
    return value >= 0 ? value / 16 : -((-value + 15) / 16);
}

/* One axis of 0x80046E1C..0x80046EB8: a corner moves away from its two neighbours, which may have moved already. */
void grow(std::array<int32_t, 4>& a) {
    a[0] += sra4(2 * a[0] - a[1] - a[2]);
    a[1] += sra4(2 * a[1] - a[0] - a[3]);
    a[3] += sra4(2 * a[3] - a[1] - a[2]);
    a[2] += sra4(2 * a[2] - a[0] - a[3]);
}

}  // namespace

extern "C" void disruptor_grown_quad_positions(const uint32_t first[4], uint32_t second[4]) {
    std::array<int32_t, 4> x{}, y{};
    for (int corner = 0; corner < 4; ++corner) {
        x[corner] = static_cast<int16_t>(first[corner] & 0xFFFFu);
        y[corner] = static_cast<int16_t>(first[corner] >> 16);
    }
    grow(x);
    grow(y);
    for (int corner = 0; corner < 4; ++corner) {
        second[corner] = (static_cast<uint32_t>(x[3 - corner]) & 0xFFFFu) +
                         (static_cast<uint32_t>(y[3 - corner]) << 16);
    }
}

extern "C" int disruptor_grown_quad_sources(
        uint32_t command_address, const uint32_t *packet,
        uint32_t source_address[4], uint32_t source_word[4]) {
    if (!packet || !source_address || !source_word || command_address < kPacketBytes ||
        ((packet[0] >> 24) & 0xFCu) != kShadedTexturedQuad) {
        return 0;
    }
    const uint32_t first = command_address - kPacketBytes;
    if (psx_mod_read_word(kGuestBase | first) >> 24 != packet[0] >> 24) return 0;
    uint32_t positions[4], grown[4];
    for (int corner = 0; corner < 4; ++corner) {
        positions[corner] = psx_mod_read_word(kGuestBase | (first + kPositions[corner] * 4u));
    }
    disruptor_grown_quad_positions(positions, grown);
    for (int corner = 0; corner < 4; ++corner) {
        if (grown[corner] != packet[kPositions[corner]]) return 0;
    }
    for (int corner = 0; corner < 4; ++corner) {
        source_address[corner] = first + kPositions[3 - corner] * 4u;
        source_word[corner] = positions[3 - corner];
    }
    return 1;
}
