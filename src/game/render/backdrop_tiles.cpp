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
 *
 * Each opening is painted with the fog palette of its room's depth, and the
 * renderer is told the step and the tiles so the clearest sky owns an overlap.
 */

#include "cpu_state.h"
#include "game/render/sky_grower.h"
#include "game/render/sky_panorama.h"
#include "gpu.h"

#include <algorithm>
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
constexpr int kStackRegister = 29;   /* $sp */
constexpr uint32_t kFadeStepSlot = 80u;  /* the stack word holding the opening's fog step */
constexpr int32_t kFrameRight = 320;
constexpr int32_t kSkyMargin = 4;  /* frame pixels: a window's frames reach past the opening the game measures, the clear colour showed between */

int32_t g_span_left = 0;
int32_t g_own_left = 0;
int32_t g_margin = 0;
bool g_sky_seen = false;
uint32_t g_sky_at = 0u;
uint32_t g_cells_read_at = ~0u;
disruptor::sky_panorama::Cells g_cells_told;

/* The level's panorama table, read once a frame and told to the renderer when it changes. */
void tell_panorama(const CPUState *cpu) {
    namespace sky = disruptor::sky_panorama;
    const uint32_t now = cpu->read_word(kVBlanks);
    if (now == g_cells_read_at) return;
    g_cells_read_at = now;
    const auto word = [cpu](uint32_t address) { return cpu->read_word(address); };
    const uint32_t descriptor = cpu->read_word(sky::kDescriptorPointer);
    const sky::Cells cells = sky::read(word, descriptor);
    const bool changed = !(cells == g_cells_told);
    if (changed) {
        g_cells_told = cells;
        gpu_sky_panorama(cells.rows, cells.columns, cells.tall, sky::kPeriod, sky::kFocal, cells.xy.data(), cells.irregular ? 1 : 0);
    }
    if (cells.rows == 0) return;
    const uint32_t clut_x = cpu->read_word(descriptor + 1280u), clut_y = cpu->read_word(descriptor + 1284u);
    disruptor::sky_grower::look(cells, clut_x, clut_y, changed, now);
#if DISRUPTOR_DEV_TOOLS
    disruptor::sky_grower::dump(cells, clut_x, clut_y);
#endif
}

/* An edge inside the frame moves out by the margin; one that reaches the frame's edge becomes it. */
int32_t opened(int32_t edge, bool right_edge, int32_t margin) {
    if (edge <= 0 || edge >= kFrameRight) return edge;
    return right_edge ? std::min(edge + margin, kFrameRight) : std::max(edge - margin, 0);
}

uint32_t widened(uint32_t edge, bool right_edge) {
    return static_cast<uint32_t>(
        gpu_ws_widen_x(opened(static_cast<int32_t>(edge), right_edge, g_margin), right_edge ? 1 : 0));
}

}  // namespace

extern "C" void disruptor_backdrop_tiles_instruction_hook(
        CPUState *cpu, uint32_t address, uint32_t instruction, int phase) {
    if (!cpu || phase != 1) return;
    switch (address) {
        case kLoadLeftEdge:
        case kReloadLeftEdge:
            if (instruction != kLoadLeftEdgeWord) return;
            g_own_left = static_cast<int32_t>(cpu->gpr[kEdgeRegister]);
            /* a fogged opening is far, where walls dissolve over whatever lies behind them: only clear sky reaches past */
            if (address == kLoadLeftEdge)
                g_margin = cpu->read_word && cpu->read_word(cpu->gpr[kStackRegister] + kFadeStepSlot) == 0u ? kSkyMargin : 0;
            cpu->gpr[kEdgeRegister] = widened(cpu->gpr[kEdgeRegister], false);
            g_span_left = static_cast<int32_t>(cpu->gpr[kEdgeRegister]);
            if (address == kLoadLeftEdge && cpu->read_word) {
                gpu_sky_fog_opening(static_cast<int32_t>(cpu->read_word(cpu->gpr[kStackRegister] + kFadeStepSlot)));
                tell_panorama(cpu);
            }
            return;
        case kLoadRightEdge:
            if (instruction != kLoadRightEdgeWord) return;
            gpu_sky_fog_span(g_own_left, static_cast<int32_t>(cpu->gpr[kEdgeRegister]));
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
            gpu_sky_fog_tile(cpu, cpu->gpr[kPacketRegister]);
            return;
        default:
            return;
    }
}

/* Whether the world stands against the sky: indoors the void behind it is the clear colour, which nothing should show. */
extern "C" int disruptor_backdrop_tiles_sky_behind(uint32_t vblanks) {
    return g_sky_seen && vblanks - g_sky_at <= kSkyVBlanks ? 1 : 0;
}
