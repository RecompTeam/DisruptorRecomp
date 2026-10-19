/*
 * Sky and skyline backdrop tiles for Disruptor (SLUS-00224) in classic
 * widescreen.
 *
 * func_8003AF68 fills the visible opening with opaque SPRT tiles placed edge
 * to edge, one panorama texel per frame pixel, after clamping the opening to
 * the 320-pixel frame. The renderer's HUD sprite squash would narrow each tile
 * around the pivot of its screen third and leave bands of the clear colour.
 *
 * Instruction seams widen the opening's edges before the tile loop reads them,
 * so the loop emits the extra columns a wider view shows, and mark each tile
 * once its packet is complete. The renderer squashes marked tiles about the
 * display centre. Guest RAM is written only by the game's own code.
 */

#include "cpu_state.h"
#include "gpu.h"

#include <cstdint>

namespace {

constexpr uint32_t kVBlanks = 0x8005B920u;  /* libetc's VBlank count */
constexpr uint32_t kSkyVBlanks = 300u;      /* five seconds: the sky still counts as behind the world this long after a tile */

constexpr uint32_t kLoadLeftEdge = 0x8003B328u;
constexpr uint32_t kComputeScroll = 0x8003B338u;
constexpr uint32_t kLoadRightEdge = 0x8003B348u;
constexpr uint32_t kTileQueued = 0x8003B5A8u;  /* first AddPrim has returned */
constexpr uint32_t kReloadLeftEdge = 0x8003B638u;
constexpr uint32_t kLoadLeftEdgeWord = 0x8FB80040u;
constexpr uint32_t kComputeScrollWord = 0x00621821u;
constexpr uint32_t kLoadRightEdgeWord = 0x8FB80028u;
constexpr uint32_t kTileQueuedWord = 0x8F8205B8u;
constexpr uint32_t kPanoramaPeriod = 0x500u;
constexpr int32_t kPanoramaColumn = 64;  /* a tile is cut from one such column of the panorama */
constexpr int kScrollRegister = 3;   /* $v1 */
constexpr int kPacketRegister = 17;  /* $s1 */
constexpr int kEdgeRegister = 24;    /* $t8 */

int32_t g_span_left = 0;
bool g_sky_seen = false;
uint32_t g_sky_at = 0u;

uint32_t widened(uint32_t edge, bool right_edge) {
    return static_cast<uint32_t>(
        gpu_ws_widen_x(static_cast<int32_t>(edge), right_edge ? 1 : 0));
}

}  // namespace

extern "C" void disruptor_backdrop_tiles_instruction_hook(
        CPUState *cpu, uint32_t address, uint32_t instruction, int phase) {
    if (!cpu || phase != 1) return;
    switch (address) {
        case kLoadLeftEdge:
        case kReloadLeftEdge:
            if (instruction != kLoadLeftEdgeWord) return;
            cpu->gpr[kEdgeRegister] = widened(cpu->gpr[kEdgeRegister], false);
            g_span_left = static_cast<int32_t>(cpu->gpr[kEdgeRegister]);
            return;
        case kLoadRightEdge:
            if (instruction != kLoadRightEdgeWord) return;
            cpu->gpr[kEdgeRegister] = widened(cpu->gpr[kEdgeRegister], true);
            gpu_ws_backdrop_span(g_span_left, static_cast<int32_t>(cpu->gpr[kEdgeRegister]));
            return;
        case kComputeScroll:
            if (instruction != kComputeScrollWord) return;
            /* The column search assumes a panorama X of zero or more. */
            if (static_cast<int32_t>(cpu->gpr[kEdgeRegister] +
                                     cpu->gpr[kScrollRegister]) < 0)
                cpu->gpr[kScrollRegister] += kPanoramaPeriod;
            gpu_ws_backdrop_scroll(static_cast<int32_t>(cpu->gpr[kScrollRegister]), static_cast<int32_t>(kPanoramaPeriod), kPanoramaColumn);
            return;
        case kTileQueued:
            if (instruction != kTileQueuedWord) return;
            g_sky_seen = cpu->read_word != nullptr;
            if (g_sky_seen) g_sky_at = cpu->read_word(kVBlanks);
            gpu_ws_tag_screen_tile(cpu, cpu->gpr[kPacketRegister]);
            return;
        default:
            return;
    }
}

/* Whether the world stands against the sky: indoors the void behind it is the clear colour, which nothing should show. */
extern "C" int disruptor_backdrop_tiles_sky_behind(uint32_t vblanks) {
    return g_sky_seen && vblanks - g_sky_at <= kSkyVBlanks ? 1 : 0;
}
