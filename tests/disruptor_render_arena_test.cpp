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
void gl_renderer_set_ssao_fade(float, float) {}
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

#include "../src/disruptor_render_arena.cpp"

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
    require(first == PSX_MOD_GPU_DMA_GUEST_BASE && second == first + kArenaBytes,
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
    std::puts("PASS: dense ultrawide packets preserve textures and 24-bit DMA links");
}
