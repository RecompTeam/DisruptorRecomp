#include "cpu_state.h"
#include "game/render/ddgi_level.h"
#include "gpu_ddgi.h"
#include "gte_precision.h"
#include "mod_plugins.h"

#include <cstdint>
#include <cstdio>

namespace {

constexpr uint32_t kDrawWorld = 0x80040E68u;
constexpr uint32_t kVSync = 0x8004B3D4u;
constexpr uint32_t kFrameWait = 0x80043E84u; /* where the frame loop's wait after its last draw returns to */
constexpr uint32_t kCameraX = 0x800775CCu;     /* a vertex the GTE gets is world x plus this */
constexpr uint32_t kCameraHeight = 0x800775D0u; /* in eighths of the vertices' unit */
constexpr uint32_t kCameraZ = 0x800775C8u;     /* a vertex the GTE gets is world z less this */
constexpr uint32_t kMission = 0x80071354u;     /* gp+0x208 */

void camera_offset(int32_t offset[3]) {
    /* The game's words wrap as 32-bit integers: negate and scale them the same way, without signed overflow. */
    offset[0] = static_cast<int32_t>(0u - psx_mod_read_word(kCameraX));
    offset[1] = static_cast<int32_t>(0u - 8u * psx_mod_read_word(kCameraHeight));
    offset[2] = static_cast<int32_t>(psx_mod_read_word(kCameraZ));
}

void world_draw(CPUState *, uint32_t) {
    gte_pose_begin();
    gl_renderer_set_ddgi_mission(psx_mod_read_byte(kMission));
    disruptor::ddgi_level::publish_if_changed();
}

void frame_waits(CPUState *cpu, uint32_t) {
    if (cpu && cpu->gpr[31] == kFrameWait) gpu_ddgi_draw_probes();
}

PSX_MOD_CONSTRUCTOR(register_disruptor_ddgi_view) {
    gte_set_pose_offset_reader(camera_offset);
    if (!psx_mod_register_function_entry_plugin("disruptor.ddgi.world", kDrawWorld, world_draw) ||
        !psx_mod_register_function_entry_plugin("disruptor.ddgi.vsync", kVSync, frame_waits))
        std::fprintf(stderr, "disruptor: failed to register the probe lighting hooks\n");
}

} // namespace
