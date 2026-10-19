/* Disruptor's widened visibility, a wide picture or a longer draw distance, can exceed its two 0x11000-byte primitive
 * arenas. The second arena is immediately followed by texture descriptors;
 * spilling packets into those descriptors corrupts subsequent frames.
 * Relocate only the audited renderer's arena roots, using the framework's
 * opt-in memory that survives the GPU's 24-bit linked-list tags.
 *
 * The same renderer draws the map, without a world, and has to draw it from
 * the retail arena: from the moved one the map's text comes out as noise. So
 * the map keeps the retail arena whatever the margin says. And the renderer
 * culls and fills its arena before it projects anything, so the widescreen
 * layer is told here what the frame is going to be. */
#include "game/render/far_rendering.h"

#include "cpu_state.h"
#include "gpu.h"
#include "lockstep.h"
#include "mod_plugins.h"
#include "mod_memory.h"
#include "psx_netplay.h"

#include <cstdint>

extern "C" void gl_renderer_set_ssao_fade(float from, float to);
extern "C" void gl_renderer_set_palette_fade(int rows, float start, float step, float side);
extern "C" void gl_renderer_set_palette_fade_power(int power);
extern "C" void gl_renderer_set_edge_sky(int sky);
extern "C" int disruptor_backdrop_tiles_sky_behind(uint32_t vblanks);

namespace {
constexpr uint32_t kRendererEntry = 0x80040E68u;
constexpr uint32_t kVBlanks = 0x8005B920u;  /* libetc's VBlank count */
constexpr uint32_t kScreen = 0x8007145Cu;  /* gp + 0x310, the renderer skips the world at 0x800410E0 when it is 1 */
constexpr uint8_t kMapScreen = 1u;
constexpr float kDepthUnit = 4.0f;  /* camera depth the renderer gets for one unit of the game's range */
constexpr uint32_t kContexts[2] = {0x80074458u, 0x80074E50u};
constexpr uint32_t kOriginalRoots[2] = {0x800776BCu, 0x800776C0u};
constexpr uint32_t kArenaOffset = 0x70u;
constexpr uint32_t kRetailBytes = 0x11000u;
constexpr uint32_t kArenaBytes = PSX_MOD_GPU_DMA_APERTURE_SIZE / 2u;
uint32_t g_arena_base = 0;
} // namespace

/* At the renderer's entry, and from far_rendering.cpp after the screen effects, which change the fade start. */
extern "C" void disruptor_render_arena_publish_fades(void) {
    uint32_t far_distance = 0u;
    int32_t fade_start = 0;
    disruptor_far_rendering_drawn_distances(&far_distance, &fade_start);
    gl_renderer_set_ssao_fade(kDepthUnit * static_cast<float>(fade_start), kDepthUnit * static_cast<float>(far_distance));
    int32_t ramp_start = 0, ramp_length = 0;
    int ramp_power = 1;
    disruptor_far_rendering_fog_ramp(&ramp_start, &ramp_length, &ramp_power);
    gl_renderer_set_palette_fade(12, 8.0f * static_cast<float>(ramp_start), 8.0f * static_cast<float>(ramp_length) / 12.0f, 0.375f);
    gl_renderer_set_palette_fade_power(ramp_power);
    gl_renderer_set_edge_sky(disruptor_backdrop_tiles_sky_behind(psx_mod_read_word(kVBlanks)));
}

namespace {

void renderer_entry(CPUState *cpu, uint32_t address) {
    if (!cpu || address != kRendererEntry || !psx_mod_game_started()) return;
    const bool map = psx_mod_read_byte(kScreen) == kMapScreen;
    gpu_ws_tell_frame_kind(map);  /* before the margin below is read */
    disruptor_render_arena_publish_fades();
    unsigned index;
    for (index = 0; index < 2u; ++index)
        if (cpu->gpr[4] == kContexts[index]) break;
    if (index == 2u) return;

    const uint32_t original = psx_mod_read_word(kOriginalRoots[index]);
    if (original < 0x80000000u || original > 0x80200000u - kRetailBytes ||
        (original & 3u) != 0u) return;
    const uint32_t root_address = kContexts[index] + kArenaOffset;
    const uint32_t current = psx_mod_read_word(root_address);
    /* Allocation state is part of the machine snapshot; this host cache is
     * re-derived when a state is loaded into another process or timeline. */
    const uint32_t allocated = psx_mod_gpu_dma_memory_bytes();
    if (allocated != 2u * kArenaBytes) g_arena_base = 0u;
    const uint32_t saved_root = PSX_MOD_GPU_DMA_GUEST_BASE + index * kArenaBytes;
    if (!g_arena_base && allocated == 2u * kArenaBytes && current == saved_root)
        g_arena_base = PSX_MOD_GPU_DMA_GUEST_BASE;
    const uint32_t relocated = g_arena_base ?
        g_arena_base + index * kArenaBytes : 0u;
    if (current != original && current != relocated) return;

    if (g_ls_mode || g_ls_replay_active || psx_netplay_active() || map ||
        (psx_mod_widescreen_x_margin() == 0 && !disruptor_far_rendering_edge_margin() &&
         disruptor_far_rendering_get_preset() == DISRUPTOR_FAR_RENDERING_RETAIL)) {
        if (current == relocated && relocated)
            psx_mod_write_word(root_address, original);
        return;
    }
    if (!g_arena_base) {
        g_arena_base = psx_mod_alloc_gpu_dma_memory(2u * kArenaBytes, 16u);
        if (!g_arena_base) return;
    }
    const uint32_t destination = g_arena_base + index * kArenaBytes;
    if (current == destination) return;

    /* Preserve any retail packet templates on activation or a level reset.
     * Each back buffer owns a separate arena; ordering tables stay in RAM. */
    for (uint32_t offset = 0; offset < kRetailBytes; offset += 4u)
        psx_mod_write_word(destination + offset,
                           psx_mod_read_word(original + offset));
    psx_mod_write_word(root_address, destination);
}

PSX_MOD_CONSTRUCTOR(register_disruptor_render_arena) {
    psx_mod_register_function_entry_plugin(
        "disruptor.render_arena", kRendererEntry, renderer_entry);
}
} // namespace
