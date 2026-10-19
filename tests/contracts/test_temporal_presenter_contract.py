#!/usr/bin/env python3
"""Source contract for the in-between presenter: departed faces, world sprites and the capture lock."""

import re
from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    depth = 0
    for position in range(source.index("{", start), len(source)):
        if source[position] == "{":
            depth += 1
        elif source[position] == "}":
            depth -= 1
            if depth == 0:
                return source[start : position + 1]
    raise AssertionError(f"unterminated function: {signature}")


def shader(source: str, name: str) -> str:
    match = re.search(rf"static const char \*{name} =\n((?:    \".*\"\n?)+?);", source)
    require(match is not None, f"shader {name} not found")
    return "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1)))


cmake = read("CMakeLists.txt")
temporal = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
gpu = read("psxrecomp/runtime/src/gpu.c")
billboards = read("src/game/render/billboard_aspect.cpp")

require(
    "    disruptor_contract(temporal_presenter)\n" in cmake
    and (read("game.toml") + read("game-widescreen.toml")).count(
        'callback = "disruptor_sprite_depth_instruction_hook"\n'
    )
    == 2,
    "the presenter contract and the sprite depth hook for code that runs from RAM must remain registered",
)

underlay = function_body(temporal, "static void temporal_underlay(float alpha,int covered)")
require(
    "const int opaque=c->semi<0 || (t && c->semi==4);" in underlay
    and "if (c->mode!=GL_TRIANGLES || c->check || (!opaque && !covered)) continue;" in underlay,
    "the underlay must draw the textured batches the renderer keys 4, and translucent batches only in its "
    "second pass",
)
require(
    "else if (show) show=opaque && !(t && s_temp_under[j*TEXV+18]!=0.0f) && (!covered || (was->modelled && show==1));"
    in underlay,
    "a departed face that is no sprite must be opaque, and only a GTE face with all its depths may go over the "
    "scene: a grown copy has no depth of its own and a held corner has none",
)
require(
    "if (show && was->sprite) show=covered && !temporal_sprite_replaced(s,&p->gone_to[ti]);" in underlay,
    "a departed sprite must be drawn only over the scene and only when no current sprite stands in its place",
)
replaced = function_body(temporal, "static int temporal_sprite_replaced(const TemporalScene *s,const GpuTemporalTriangle *to)")
require(
    "if (!s->triangles[i].sprite) continue;" in replaced
    and "if (now[0]<=gone[1] && gone[0]<=now[1] && now[2]<=gone[3] && gone[2]<=now[3]) return 1;" in replaced,
    "only a current sprite whose box meets the departed one's replaces it",
)
require(
    "gpu_temporal_lerp(&p->start[ti],&p->gone_to[ti],alpha,&out);" in underlay
    and "show=gpu_temporal_mesh_facing(&p->triangles[ti],&out);" in underlay,
    "a departed face must move between the two ends prepared for it and hide when it turns away",
)
require(
    "glDepthFunc(covered ? GL_LESS : GL_ALWAYS); glDepthMask(covered ? GL_FALSE : GL_TRUE);" in underlay
    and "v[t?23:9]=show?temporal_order(gpu_temporal_lerp_depth(&p->gone_to[ti],&out,k)*(covered?TEMP_ORDER_CLEARANCE:1.0f)):TEMP_ORDER_FAR;"
    in underlay
    and "#define TEMP_ORDER_CLEARANCE 1.03f" in temporal
    and "glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);" in underlay,
    "the second pass must win only against current faces that lie farther away, and leave depth alone",
)

present = function_body(temporal, "static int temporal_present(float alpha,int lx,int ly,int lw,int lh)")
require(
    "glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE); glClearDepth(1.0);" in present
    and "glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);" in present
    and "glDisable(GL_DEPTH_TEST);" in present,
    "every presented frame must start from a cleared depth buffer and leave depth testing off",
)
require(
    "now->world && now->depth[k]>0.0f ? temporal_order(now->depth[k])\n"
    "                    : ti<first_world ? TEMP_ORDER_FAR : TEMP_ORDER_NEAR;" in present,
    "a current face must carry its camera depth, the backdrop the far plane and everything else the near plane",
)
require(
    "            gpu_temporal_lerp(&s->from[ti],now,alpha,&out);\n" in present
    and "                if (now->depth[k]>0.0f) v[t?23:9]=temporal_order(gpu_temporal_lerp_depth(now,&out,k));\n" in present,
    "a face between two frames must be ordered by the depth it has there, or a wall and its pieces part",
)
require(
    "            temporal_submit(s,c,s->height,s_temp_scratch,split);\n"
    "            temporal_underlay(alpha,0);\n"
    "            underlaid=1;\n"
    "            temporal_submit(s,c,s->height,s_temp_scratch+split*TEXV,c->count-split);" in present
    and "    if (underlaid) temporal_underlay(alpha,1);" in present,
    "departed faces must go under the first world face and once more over what covered them",
)
require(
    "return depth>16.0f ? 1.0f-32.0f/depth : TEMP_ORDER_NEAR;" in temporal,
    "depth order must grow with camera depth",
)

for name in ("GEO_VS", "TEX_VS"):
    require(
        shader(renderer, name).endswith("0.0, 1.0); }\\n"),
        f"{name} must end the way the presenter's ordered copy expects",
    )
require(
    "layout(location=4)" not in shader(renderer, "GEO_VS") and "layout(location=12)" not in shader(renderer, "TEX_VS"),
    "the attribute slots of the presenter's depth order must stay free in the canonical shaders",
)
require(
    "char *geo_vs=temporal_ordered_shader(GEO_VS,4), *tex_vs=temporal_ordered_shader(TEX_VS,12);" in temporal
    and "const int offsets[13]={0,2,4,8,10,12,13,14,18,19,20,22,23};" in temporal
    and "const int gs[5]={2,4,1,2,1}, go[5]={0,2,6,7,9};" in temporal,
    "the presenter must build its own shaders and feed them the depth slot of each vertex",
)

capture = function_body(renderer, "static int interp_capture(GLuint fbo, int x, int y, int w, int h,")
require(
    "temporal_prepare(fbo, y, w, h);" in capture
    and capture.index("    } else if (s_interp_blend_mode == 2 && !under_lock) {") < capture.index("    SDL_LockMutex(s_interp_mutex);"),
    "a capture must match its scene before it takes the presenter's lock",
)
publish = function_body(temporal, "static void temporal_publish(GLuint fbo, int y, int h)")
require(
    "gpu_temporal_match(" not in publish and "gpu_temporal_mesh_prepare_scene(" not in publish,
    "under the lock a scene is only rotated in",
)
matching = function_body(temporal, "static void temporal_match_scene(TemporalScene *s, const TemporalAgainst *a)")
against = function_body(temporal, "static void temporal_against(const TemporalScene *s, int w, int h, TemporalAgainst *a)")
require(
    "&s->mesh_stats,a->start,a->gone_to,a->gone," in matching
    and "s_temp_prev" not in matching
    and "s_temp_curr" not in matching
    and "a->start=s_temp_curr.start; a->gone_to=s_temp_curr.gone_to; a->gone=s_temp_curr.gone;" in against
    and "        memset(&s->camera,0,sizeof(s->camera));\n        memset(&s->onward,0,sizeof(s->onward));\n" in matching,
    "matching may write only the builder and the current scene's departure arrays, which the presenter does not read, "
    "and reads the current scene only through what the capture took of it",
)
job = function_body(renderer, "static void interp_match_job(const InterpMatchJob *job)")
post = function_body(renderer, "static void interp_match_post(TemporalScene *scene, GLuint fbo, int y, int w, int h, int dst, int linear,")
release = function_body(renderer, "static void interp_match_release(void)")
builder = function_body(temporal, "static TemporalScene *temporal_builder(GLuint fbo, int y, int h)")
reset = function_body(temporal, "static void temporal_reset(void)")
begin = function_body(temporal, "static void temporal_begin(GLuint fbo, int y, int h, uint16_t color)")
require(
    job.index("    temporal_match_scene(job->scene, &job->against);") < job.index("    SDL_LockMutex(s_interp_mutex);")
    and "    if (job->generation == s_temp_generation) {\n" in job
    and "    temporal_against(scene, w, h, &job.against);\n    job.generation = s_temp_generation;\n" in post
    and "    s_temp_held[slot] = 1;\n    s_match_retired = slot;\n" in post
    and capture.index("    if (s_match_thread) interp_match_wait();") < capture.index("    if (apart) {\n        scene = temporal_prepare_scene")
    < capture.index("    SDL_LockMutex(s_interp_mutex);")
    and capture.count("ready = s_interp_valid >= 2;") == 2
    and capture.index("        s_interp_draw_fence = NULL;\n    }\n    interp_match_release();\n") > 0
    and "    s_temp_held[s_match_retired] = 0;\n" in release
    and "if (!s_temp_held[i] && fbo && s->capture_fbo==fbo && s->active &&" in builder
    and "        if (!s_temp_held[i]) { s_temp_build[i].active = 0; s_temp_build[i].capture_fbo=0; }\n" in reset
    and "    ++s_temp_generation;\n" in reset
    and begin.count("!s_temp_held[i]") == 3
    and "    else if (s_interp_valid >= 2) dst = 3 - s_interp_prev - s_interp_cur;\n" in capture,
    "the matching thread matches unlocked and publishes under the lock unless a reset came between, a capture in any mode "
    "waits for it before preparing and tells a pair ready only once published, a slot it holds is left alone by the builder and by a reset until a capture after the "
    "presenter's last draw fence, and a picture goes to the one slot the presenter is not drawing from",
)
reset_wrapper = function_body(renderer, "static void interp_reset_history(void) {")
suspend = function_body(renderer, "static void interp_set_suspended(int *reason, int suspended) {")
setter = function_body(renderer, "void gl_renderer_set_interpolation(int enabled, double host_hz, double target_hz,")
loop = function_body(renderer, "static int interp_thread_main(void *opaque) {")
require(
    all(body.index("    interp_match_wait();\n") < body.index("SDL_LockMutex(s_interp_mutex);") for body in (reset_wrapper, suspend, setter))
    and "        const int diag = s_interp_diag;\n        SDL_UnlockMutex(s_interp_mutex);\n" in loop
    and "            if (diag && !s_interp_settle)\n" in loop,
    "a history reset waits for the matching thread before it takes the present mutex, so a captured frame is published "
    "before the reset clears it, and the presenter reads the diagnostics switch under the mutex",
)
stop = function_body(renderer, "void gl_renderer_shutdown(void)")
require(
    stop.index("    interp_match_stop();\n") < stop.index("        temporal_release();\n")
    and stop.index("        SDL_WaitThread(s_interp_thread, NULL);\n") < stop.index("    interp_match_stop();\n"),
    "the matching thread stops before the scenes it may hold are freed",
)

quad_draw = function_body(renderer, "static float interp_draw_quad(float alpha, int lx, int ly, int lw, int lh) {")
require(
    "temporal_dump_alpha(" not in present
    and quad_draw.index("temporal_verify_endpoint(lx, ly, lw, lh);") < quad_draw.index("alpha = temporal_dump_alpha(alpha);"),
    "the debug alpha pin must not reach the endpoint check, which renders alpha 1 through temporal_present",
)
require(
    "if (alpha < 1.0f && temporal_present(alpha, lx, ly, lw, lh)) return alpha;\n"
    "        alpha = 1.0f;" in quad_draw
    and quad_draw.rstrip().endswith("    return alpha;\n}"),
    "a present must report the alpha of the picture it drew, 1 after a fallback to the current frame",
)
presented = function_body(renderer, "static int interp_present(void) {")
require(
    "const float shown = interp_draw_quad((float)a, lx, ly, lw, lh);" in presented
    and "const double phase = (double)s_interp_captures + shown;" in presented,
    "the pacing report must take its phase from the alpha that was drawn, not from the alpha asked for",
)

release = function_body(temporal, "static void temporal_release(void)")
require(
    "free(s->start); free(s->gone_to); free(s->gone);" in release and "s_temp_ready=NULL;" in release,
    "release must free the departure arrays and forget the prepared scene",
)
require(
    "    SDL_LockMutex(s_interp_mutex);\n    const uint64_t locked = SDL_GetPerformanceCounter();\n" in capture,
    "the reported lock time must start where the lock is taken",
)

draw = function_body(temporal, "static void temporal_draw(const float *vertices, const uint64_t *identities, int n,")
require(
    "const int sprite = identities && identities[i]==TEMPORAL_SPRITE_IDENTITY &&" in draw
    and "t->sprite = sprite;" in draw
    and "t->modelled = !sprite && !partial && !cut && identities && identities[i] && identities[i+1] && identities[i+2] &&" in draw
    and "if (v[textured ? 19 : 6] <= 0.5f && !sprite && !partial) t->world=0;" in draw,
    "a sprite with a camera depth must be a world face without model coordinates",
)
require(
    "const int partial = identities && identities[i]==TEMPORAL_PARTIAL_IDENTITY &&" in draw
    and "identities[i+1]==TEMPORAL_PARTIAL_IDENTITY && identities[i+2]==TEMPORAL_PARTIAL_IDENTITY;" in draw,
    "a world polygon with a corner off its projection must stay a world face without model coordinates",
)
take = function_body(renderer, "static int take_visual_triangle(const int *xs, const int *ys,")
require(
    "if (pz) pz[i] = (exact && s_next_temporal_depth) || partial ? s_next_z[i] : s_sprite_depth;" in take
    and ": s_sprite_depth > 0.0f ? TEMPORAL_SPRITE_IDENTITY : 0u;" in take,
    "a rectangle's sprite depth must reach the recorded vertex",
)
partly = function_body(gpu, "static int resolve_precise_vertices(const int *indices, int count,")
queued = function_body(gpu, "static void queue_precise_triangle(int exact,")
depths = function_body(renderer, "static void glb_set_temporal_depth_triangle(int enabled,")
require(
    "const int partial = !exact && s_next_temporal_depth == 2;" in take
    and ": partial ? TEMPORAL_PARTIAL_IDENTITY" in take
    and partly.count("s_precise_vertex_depth[i] = psx_pinned_corner_depth(unpin, pinned[i], z[i]);") == 2
    and partly.index("                    s_precise_vertex_depth[i] = psx_pinned_corner_depth(unpin, pinned[i], z[i]);") < partly.index("    if (resolved != count) {")
    and "            s_precise_vertex_partial = 1;\n#endif\n            psx_pinned_places(count, unpin, pinned," in partly
    and "    s_precise_vertex_partial = 0;\n#endif\n    s_pinned_places.unpinned = 0;\n    if (!gte_geometry_correction_enabled())" in partly
    and "        if (geometry_enabled && s_precise_vertex_partial)\n            gr_set_temporal_depth_triangle(2," in queued
    and queued.index("gr_set_precise_triangle(0, 0,0, 0,0, 0,0);") < queued.index("gr_set_temporal_depth_triangle(2,")
    and "if (enabled == 2 ? z0 < 0.0f || z1 < 0.0f || z2 < 0.0f || z0 + z1 + z2 <= 0.0f" in depths
    and "s_next_temporal_depth = enabled == 2 ? 2 : 1;" in depths,
    "the corners of a partly resolved world polygon must keep their depth for the in-between frames only",
)
rect = function_body(renderer, "static void gpu_textured_rect(int x,int y,int w,int h,")
require(
    "    s_sprite_depth = s_next_sprite_depth;\n    s_next_sprite_depth = 0.0f;\n" in rect
    and rect.rstrip().endswith("    s_sprite_depth = 0.0f;\n    memset(s_flat_sides, 0, sizeof(s_flat_sides));\n}"),
    "a sprite depth is for one rectangle and must not leak into the next primitive",
)
quad = function_body(gpu, "static void gp0_exec_textured_quad(void)")
require(
    quad.count("gr_set_temporal_sprite(sprite_depth, sides);") == 1
    and quad.index("gr_set_temporal_sprite(sprite_depth, sides);") < quad.index("gr_draw_textured_rect(")
    and "                x - draw_offset_x, y - draw_offset_y, w, h, raw_w, raw_h, squashed, sides);" in quad
    and quad.index("const int raw_w = abs(vx[1] - vx[0]) + 1, raw_h = abs(vy[2] - vy[0]) + 1;") < quad.index("if (ws_tagged_anchor(&ws_ax)) {"),
    "the depth and the sides must be announced right before the rectangle they belong to, from the packet's size before the squash",
)
require(
    "int gpu_shadow_shape(void) { return psx_ws_x_margin() > 0; }\n" in gpu
    and "gpu_set_shadow_shape" not in gpu
    and "gpu_set_shadow_shape" not in read("psxrecomp/runtime/include/gpu.h")
    and "IMPROVED_SHADOWS" not in read("src/game/host.cpp")
    and "improved_shadows" not in menu_text(ROOT)
    and "improved_shadows" not in read("src/game/settings/options.cpp")
    and "improved_shadows" not in read("src/game/settings/saved.cpp")
    and "improved_shadows" not in read("src/launcher/launcher_settings.cpp"),
    "a shadow keeps the game's shape at 4:3 and lies flat on the floor in a wider picture: no setting, no switch "
    "and no environment variable chooses",
)
host_loop = read("psxrecomp/runtime/src/main.cpp")
require(
    host_loop.count("psx_game_configure(") == host_loop.count("    gpu_init();\n") == 1
    and host_loop.index("int main(int argc, char** argv) {")
    < host_loop.index("psx_game_configure(")
    < host_loop.index("    gpu_init();\n")
    < host_loop.index("    sdl_window = SDL_CreateWindow("),
    "the game is configured before the GPU starts and a window opens: nothing can ask for the shadow switch sooner",
)
sides = function_body(gpu, "static float temporal_sprite_sides(int x, int y, int w, int h, int raw_w, int raw_h, int squashed, float sides[4])")
sizer = function_body(gpu, "void gpu_temporal_size_sprite(uint32_t primitive_addr, int32_t row16, int32_t width16, int32_t height16,")
sprite_header = read("psxrecomp/runtime/include/gpu_temporal_sprite.h")
require(
    "    if (!t) return 0.0f;" in sides
    and "    const GpuTemporalSprite sprite = gpu_temporal_sprite_from_fixed(\n        t->placed, t->sized, t->shadow, t->whole, t->centre_x, t->offset_y, t->row, t->width, t->height);" in sides
    and "    gpu_temporal_sprite_sides(&sprite, (float)x, (float)y, w, h, raw_w, raw_h,\n"
    "                              squashed ? (float)ws_xnum / (float)ws_xden : 1.0f, gpu_shadow_shape(), sides);" in sides
    and '#include "gpu_temporal_sprite.h"' in gpu
    and "static inline void gpu_temporal_sprite_sides(const GpuTemporalSprite *sprite, float left, float top, int w, int h," in sprite_header
    and "tests/unit/gpu_temporal_sprite_test.cpp" in read("CMakeLists.txt")
    and "+runtime/include/gpu_temporal_sprite.h" in read("framework/FILES.txt")
    and "    if (!t || t->stamp != (uint32_t)s_frame_count || !t->placed || width16 <= 0 || height16 <= 0) return;" in sizer
    and "    t->placed = t->sized = t->shadow = t->whole = 0;" in gpu,
    "the renderer must hand a sprite's own numbers, the squash and the shadow setting to the tested side arithmetic",
)
gte = read("psxrecomp/runtime/src/gte.cpp")
runtime_main = read("psxrecomp/runtime/src/main.cpp")
audit = read("tools/audit_codegen.py")
scratch_store = function_body(gte, "extern \"C\" void gte_precision_scratch_store_pc_word(")
band = function_body(gte, "static uint32_t precision_scratch_route_clamp(")
require(
    "if (!route->clamps || precision_scratch_route_clamp(*route, projection.packed) != packed) {" in scratch_store
    and "pinned.packed = packed;\n        pinned.clamped = 1;\n        precision_store_projection(physical, pinned);" in scratch_store
    and "if (x < route.low_x) x = route.low_x;" in band and "if (x > route.high_x) x = route.high_x;" in band
    and "if (y < route.low_y) y = route.low_y;" in band and "if (y > route.high_y) y = route.high_y;" in band
    and "0x80047A0Cu, 0xACAA0004u, -0x100, 0x240, -0x100, 0x1F0)) {" in read("src/game/render/precision_routes.cpp")
    and '"cpu->gpr[11] = -256;  /* 0x8004798C: 0x200BFF00 */", "cpu->gpr[11] = 576;  /* 0x800479A8: 0x200B0240 */",' in audit
    and '"cpu->gpr[12] = -256;  /* 0x800479C4: 0x200CFF00 */", "cpu->gpr[12] = 496;  /* 0x800479E0: 0x200C01F0 */"):' in audit,
    "a corner the game pins to its guard band must keep its projection only when the stored word is that projection pinned to the retail band",
)
require(
    "                pinned[i] = gte_precision_word_clamped(addr, word);\n"
    "                if ((fixed16_integer_floor(precise_x[i]) != raw_x[i] ||\n"
    "                     fixed16_integer_floor(precise_y[i]) != raw_y[i]) &&\n"
    "                    !pinned[i]) {\n"
    "                    reject = GPU_GEOMETRY_REJECT_INTEGER_MISMATCH;" in partly,
    "a projection off its packet word must be refused unless the game pinned that word",
)
lookup = function_body(gpu, "static const WsTag *temporal_sprite_tag(void)")
require(
    lookup.index("!= PSX_WS_TAG_MATCH)") < lookup.index("return NULL;\n    return t;") and "    if (!t) return 0.0f;" in sides,
    "a sprite's place must be given only with its depth",
)
place = function_body(gpu, "void gpu_temporal_place_sprite(uint32_t primitive_addr, int32_t centre_x16, int32_t offset_y16)")
note = function_body(gpu, "void gpu_temporal_note_sprite(CPUState *cpu, uint32_t primitive_addr,")
exact = function_body(gpu, "int32_t psx_ws_project_x16(int x, int32_t fraction16)")
sprite_setter = function_body(renderer, "static void glb_set_temporal_sprite(float depth, const float sides[4])")
require(
    "if (!t || t->stamp != (uint32_t)s_frame_count) return;" in place
    and "t->placed = t->sized" in note
    and "if (ws_mode != 1 || !ws_configured()) return (int32_t)exact;" in exact
    and "return (int32_t)(cx + (exact - cx) * ws_xnum / ws_xden);" in exact
    and "    for (int side = 0; placed && side < 4; ++side) placed = isfinite(sides[side]) && fabsf(sides[side]) < 64.0f;\n#ifndef PSX_NO_DEBUG_TOOLS\n" in sprite_setter
    and "    if (whole) placed = 0;\n#endif\n    for (int side = 0; side < 4; ++side) s_next_sprite_sides[side] = placed ? sides[side] : 0.0f;" in sprite_setter
    and "const int sprite = !world && s_geometry_correction && s_sprite_depth > 0.0f;" in take
    and "px[i] = (float)xs[i] + (sprite ? s_sprite_sides[xs[i] == s_sprite_x ? 0 : 2] : s_flat_sides[xs[i] == s_sprite_x ? 0 : 1]);" in take
    and "py[i] = (float)ys[i] + (sprite ? s_sprite_sides[ys[i] == s_sprite_y ? 1 : 3] : 0.0f);" in take
    and "return (world || sprite || unpinned ? 1 : 0) | (exact ? 2 : 0) | (filler ? 4 : 0);" in take
    and "    memcpy(s_sprite_sides, s_next_sprite_sides, sizeof(s_sprite_sides));\n    memset(s_next_sprite_sides, 0, sizeof(s_next_sprite_sides));\n    memcpy(s_flat_sides, s_next_flat_sides, sizeof(s_flat_sides));\n    memset(s_next_flat_sides, 0, sizeof(s_next_flat_sides));\n    s_sprite_x = x; s_sprite_y = y;\n" in rect,
    "a sprite's unrounded place must reach the presentation of its own rectangle only, and only with geometry correction",
)
require(
    "psx_ws_tag_match_result((uint32_t)s_frame_count, t->stamp, 1," in lookup and "!= PSX_WS_TAG_MATCH)" in lookup,
    "a depth must be given only to the packet whose contents it was recorded for",
)
require(
    "    if (!site) return;\n"
    "    disruptor_sprite_depth_packet(cpu, pc, cpu->gpr[site->packet_gpr]);" in billboards,
    "every audited billboard packet seam must hand its depth over, whatever the aspect mask says",
)

print("Disruptor in-between presenter source contract: PASS")
