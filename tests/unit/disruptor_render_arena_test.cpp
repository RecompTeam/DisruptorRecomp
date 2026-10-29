#include "cpu_state.h"
#include "mod_memory.h"
#include "mod_plugins.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
std::array<uint8_t, 2u * 1024u * 1024u> ram{};
std::array<uint8_t, PSX_MOD_GPU_DMA_APERTURE_SIZE> aperture{};
uint32_t allocated = 0;
int margin = 267;
bool allocation_fails = false;
bool netplay = false;
int told = 0;
bool margin_forced = false;
int preset = 0;
int edge_margin = 0;
uint32_t drawn_far = 1280u;
int32_t drawn_fade = 512;
float ssao_from = 0.0f, ssao_to = 0.0f, palette_start = 0.0f, palette_step = 0.0f;
uint8_t *bytes(uint32_t address) {
    uint32_t offset;
    if (psx_mod_gpu_dma_aperture_offset_for(address, 4u, allocated, &offset))
        return aperture.data() + offset;
    return ram.data() + (address & 0x1FFFFFu);
}
void require(bool condition, const char *message) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
}
extern "C" {
int g_ls_mode = 0;
int g_ls_replay_active = 0;
uint32_t psx_mod_gpu_dma_memory_bytes(void) { return allocated; }
int psx_mod_game_started(void) { return 1; }
int psx_netplay_active(void) { return netplay; }
/* As in the runtime: no margin for a frame the game has just called flat, unless it is forced or the frame is native-wide. */
int32_t psx_mod_widescreen_x_margin(void) { return told && !margin_forced ? 0 : margin; }
void gpu_ws_tell_frame_kind(int flat) { told = flat; }
void gl_renderer_set_ssao_fade(float from, float to) { ssao_from = from; ssao_to = to; }
void gl_renderer_set_palette_fade(int, float start, float step, float) { palette_start = start; palette_step = step; }
int disruptor_far_rendering_get_preset(void) { return preset; }
int disruptor_far_rendering_edge_margin(void) { return edge_margin; }
int portal_reach = 0;
int disruptor_far_rendering_portal_reach(void) { return portal_reach; }
int32_t ramp_start = 512, ramp_length = 768;
int ramp_power = 1, published_power = 0;
void disruptor_far_rendering_fog_ramp(int32_t *start, int32_t *length, int *power) {
    *start = ramp_start;
    *length = ramp_length;
    *power = ramp_power;
}
void gl_renderer_set_palette_fade_power(int power) { published_power = power; }
void gl_renderer_set_palette_blocks(const int *, int) {}
int sky_behind = 0, edge_sky = -1;
void gl_renderer_set_edge_sky(int sky) { edge_sky = sky; }
float edge_depth = -1.0f;
void gl_renderer_set_edge_depth(float depth) { edge_depth = depth; }
int disruptor_backdrop_tiles_sky_behind(uint32_t) { return sky_behind; }
void disruptor_far_rendering_drawn_distances(uint32_t *far_distance, int32_t *fade_start) {
    *far_distance = drawn_far;
    *fade_start = drawn_fade;
}
uint8_t psx_mod_read_byte(uint32_t address) { return *bytes(address); }
uint32_t psx_mod_read_word(uint32_t address) {
    uint32_t value; std::memcpy(&value, bytes(address), 4); return value;
}
void psx_mod_write_word(uint32_t address, uint32_t value) {
    std::memcpy(bytes(address), &value, 4);
}
uint32_t psx_mod_alloc_gpu_dma_memory(uint32_t size, uint32_t) {
    if (allocation_fails || allocated || size > aperture.size()) return 0;
    allocated = size; return PSX_MOD_GPU_DMA_GUEST_BASE;
}
int psx_mod_register_function_entry_plugin(
        const char *, uint32_t, PSXModFunctionEntryCallback) { return 1; }
}

#include "../../src/game/render/render_arena.cpp"

int main() {
    CPUState cpu{};
    const uint32_t original[2] = {0x8008D320u, 0x8009E320u};
    for (unsigned i = 0; i < 2u; ++i) {
        psx_mod_write_word(kOriginalRoots[i], original[i]);
        psx_mod_write_word(kContexts[i] + kArenaOffset, original[i]);
        psx_mod_write_word(original[i], 0x12345678u + i);
    }
    cpu.gpr[4] = kContexts[0];
    margin = 0;
    renderer_entry(&cpu, kRendererEntry);
    require(!allocated, "4:3 allocated an enhancement arena");
    margin = 267;
    allocation_fails = true;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[0] + kArenaOffset) == original[0],
            "failed allocation changed the retail root");
    allocation_fails = false;
    renderer_entry(&cpu, kRendererEntry);
    cpu.gpr[4] = kContexts[1];
    renderer_entry(&cpu, kRendererEntry);
    const uint32_t first = psx_mod_read_word(kContexts[0] + kArenaOffset);
    const uint32_t second = psx_mod_read_word(kContexts[1] + kArenaOffset);
    require(first == PSX_MOD_GPU_DMA_GUEST_BASE && second == first + kArenaOffsets[1],
            "back buffers did not receive separate DMA arenas");
    require(psx_mod_read_word(first) == 0x12345678u &&
            psx_mod_read_word(second) == 0x12345679u,
            "retail packet templates were lost");

    /* The captured failing view emitted 1579 GT4s. Recreate a linked list
     * larger than the retail arena, with a canary at its texture-data boundary. */
    constexpr uint32_t packets = 1579u;
    constexpr uint32_t stride = 52u;
    const uint32_t texture_data = original[1] + kRetailBytes;
    psx_mod_write_word(texture_data, 0xCAFE1234u);
    for (uint32_t i = 0; i < packets; ++i) {
        const uint32_t packet = second + i * stride;
        const uint32_t next = i + 1u == packets ? 0xFFFFFFu :
                             (packet + stride) & 0xFFFFFFu;
        psx_mod_write_word(packet, (12u << 24) | next);
        for (uint32_t word = 1; word <= 12; ++word)
            psx_mod_write_word(packet + word * 4u, i ^ word);
    }
    require(psx_mod_read_word(texture_data) == 0xCAFE1234u,
            "dense-frame packet writes corrupted adjacent texture data");
    uint32_t address = second;
    for (uint32_t i = 0; i < packets; ++i) {
        address = psx_mod_gpu_dma_resolve_address_for(address, allocated);
        require(address == (second + i * stride) - 0x80000000u,
                "24-bit DMA link folded into retail RAM");
        const uint32_t tag = psx_mod_read_word(address);
        require(tag >> 24 == 12u, "packet payload length changed");
        address = tag & 0xFFFFFFu;
    }
    require(address == 0xFFFFFFu, "dense-frame DMA list did not terminate");
    g_arena_base = 0; // host cache in a fresh process after snapshot restore
    renderer_entry(&cpu, kRendererEntry);
    require(g_arena_base == PSX_MOD_GPU_DMA_GUEST_BASE &&
            psx_mod_read_word(second) >> 24 == 12u,
            "restored arena roots were not adopted without clearing packets");
    margin = 0;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "returning to 4:3 did not restore the retail root");
    margin = 267;
    renderer_entry(&cpu, kRendererEntry);
    g_ls_mode = 1;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "comparison mode kept the enhanced arena");
    g_ls_mode = 0;
    renderer_entry(&cpu, kRendererEntry);
    netplay = true;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "netplay kept the enhanced arena");
    netplay = false;
    allocated = 16u; // a different snapshot containing a smaller allocation
    renderer_entry(&cpu, kRendererEntry);
    require(!g_arena_base &&
            psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "a partial restored allocation reused the previous arena cache");
    allocated = 0;
    renderer_entry(&cpu, kRendererEntry);
    const uint32_t moved = psx_mod_read_word(kContexts[1] + kArenaOffset);
    require(moved != original[1], "widescreen did not move the arena again");
    constexpr uint32_t screen = 0x8007145Cu;
    *bytes(screen) = 1u;
    renderer_entry(&cpu, kRendererEntry);
    require(told == 1 && psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "the first frame of the map was not told flat before its arena was chosen");
    *bytes(screen) = 0u;
    renderer_entry(&cpu, kRendererEntry);
    margin_forced = true;
    *bytes(screen) = 1u;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "a margin that does not follow the kind of the frame moved the map's arena");
    *bytes(screen) = 0u;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == moved,
            "the world did not get the moved arena back under such a margin");
    margin_forced = false;
    for (const uint8_t value : {uint8_t{0}, uint8_t{2}, uint8_t{3}, uint8_t{4}}) {
        *bytes(screen) = 1u;
        renderer_entry(&cpu, kRendererEntry);
        *bytes(screen) = value;
        renderer_entry(&cpu, kRendererEntry);
        require(told == 0 && psx_mod_read_word(kContexts[1] + kArenaOffset) == moved,
                "the first frame of a world was not told so before its arena was chosen");
    }

    margin = 0;
    preset = DISRUPTOR_FAR_RENDERING_FAR;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == moved,
            "a longer draw distance at 4:3 gave the arena back to the retail root");
    preset = DISRUPTOR_FAR_RENDERING_RETAIL;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1],
            "the original draw distance at 4:3 kept the enhanced arena");
    edge_margin = 1;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == moved,
            "meshes kept past the fog's end at 4:3 left the arena at the retail root");
    edge_margin = 0;
    preset = DISRUPTOR_FAR_RENDERING_EXTENDED;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == moved,
            "a longer draw distance at 4:3 did not move the arena");
    require(disruptor_render_arena_relocated() == 1, "a frame drawn into the moved arena says so");
    preset = DISRUPTOR_FAR_RENDERING_RETAIL;
    renderer_entry(&cpu, kRendererEntry);
    require(disruptor_render_arena_relocated() == 0, "a frame drawn into the retail arena says so");
    portal_reach = 1;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == moved && disruptor_render_arena_relocated() == 1,
            "rooms drawn beside their portals at 4:3 left the arena at the retail root");
    netplay = true;
    renderer_entry(&cpu, kRendererEntry);
    require(psx_mod_read_word(kContexts[1] + kArenaOffset) == original[1] && disruptor_render_arena_relocated() == 0,
            "netplay keeps the retail arena and says so");
    netplay = false;
    portal_reach = 0;

    sky_behind = 1;
    renderer_entry(&cpu, kRendererEntry);
    require(edge_sky == 1, "the renderer is told the sky stands behind the world");
    sky_behind = 0;
    renderer_entry(&cpu, kRendererEntry);
    require(edge_sky == 0, "and when it no longer does");

    drawn_far = 1600u;
    drawn_fade = 832;
    ramp_start = 832;
    renderer_entry(&cpu, kRendererEntry);
    require(palette_start == 8.0f * 832.0f && ssao_from == 4.0f * 832.0f && ssao_to == 4.0f * 1600.0f,
            "the fades did not follow the distances the renderer draws with");
    require(edge_depth == 8.0f * (1600.0f + 160.0f), "the world dissolves short of where the game drops polygons beyond the drawn far");
    drawn_fade = -64;
    ramp_start = -64;
    renderer_entry(&cpu, kRendererEntry);
    require(palette_start == -512.0f && ssao_from == -256.0f, "a fade start before the camera lost its sign");
    ramp_start = 256;
    ramp_length = 1280;
    renderer_entry(&cpu, kRendererEntry);
    ramp_power = 3;
    renderer_entry(&cpu, kRendererEntry);
    require(palette_start == 8.0f * 256.0f && palette_step == 8.0f * 1280.0f / 12.0f && published_power == 3,
            "the palette fade spans the fog the far rendering hands over, with its power");
    std::puts("PASS: dense ultrawide packets preserve textures and 24-bit DMA links");
}
