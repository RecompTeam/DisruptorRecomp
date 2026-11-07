#!/usr/bin/env python3
"""Source contract for probe lighting: the tested headers are the code that runs, camera to world by the true inverse,
silhouettes taken as a sprite is drawn, lamp pass buffers of each presenting thread's own, and the render thread fed what
the game's thread took."""

import codecs
import re
from pathlib import Path

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


cmake = read("CMakeLists.txt")
listed = read("framework/FILES.txt")
lighting = read("psxrecomp/runtime/src/gpu_gl_ddgi.c.inc")
capture = read("psxrecomp/runtime/src/gpu_ddgi.c.inc")
scene = read("psxrecomp/runtime/src/gpu_ddgi_scene.c.inc")
header = read("psxrecomp/runtime/include/gpu_ddgi.h")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
render_thread = read("psxrecomp/runtime/src/gpu_gl_render_thread.c.inc")

require("    disruptor_contract(probe_lighting)\n" in cmake, "this contract is registered")

require(
    all(f"+runtime/include/gpu_ddgi_{name}.h\n" in listed for name in ("bvh", "fit", "mask", "turn"))
    and all(
        f"disruptor_unit_test(disruptor_gpu_ddgi_{name} tests/unit/gpu_ddgi_{name}_test.cpp RUNTIME_HEADERS)" in cmake
        for name in ("turn", "bvh", "mask", "fit")
    )
    and "disruptor_unit_test(disruptor_ddgi_level tests/unit/disruptor_ddgi_level_test.cpp RUNTIME_HEADERS INCLUDES src)"
    in cmake,
    "the four headers are the framework's own files and each has its unit test, the level reader too",
)

build = function_body(
    lighting, "static void ddgi_build(const GpuDdgiTriangle *tris, int count, int bvh, const float eye[3])"
)
level = function_body(lighting, "static void ddgi_level_make(int count) {")
require(
    '#include "gpu_ddgi_bvh.h"\n#include "gpu_ddgi_fit.h"\n#include "gpu_ddgi_turn.h"\n' in lighting
    and "    psx_ddgi_corner_normals(&corners, tris, count, eye);\n" in build
    and "    psx_ddgi_median_split(&build, 0, 0, count);\n" in build
    and "    psx_ddgi_sah_split(&build, 0, 0, kept, 0);\n" in level
    and not re.search(
        r"static (void|int|float|uint16_t|uint64_t|int64_t) ddgi_(split|level_split|select|face_of|oct16|corner_key|corner_normals|round|back_turn)\(",
        lighting,
    ),
    "the frame's and the level's BVH, the corner normals and their packing are the tested headers' and have no copy here",
)

update = function_body(lighting, "static void ddgi_update(float cy)")
probes = function_body(lighting, "static void ddgi_probe_update(const DdgiFrame *now)")
require(
    update.index("        psx_ddgi_turn_rows(now->rows, shot->turn);\n")
    < update.index("        psx_ddgi_camera_back(now->rows, now->offset, now->back, now->eye);\n"),
    "the lighting's camera is turned by the smooth turn's part of a step first, then taken back to the world by its inverse",
)

note = function_body(capture, "static int ddgi_note_card(")
dynamic = function_body(scene, "static void ddgi_scene_dynamic_lamps(")
whole = function_body(scene, "int gpu_ddgi_scene(uint32_t pose, GpuDdgiScene *scene)")
require(
    "    psx_ddgi_pose_back(camera.rotation, back);\n" in note
    and "v += back[row][axis] * (c[row] - camera.translation[row]);" in note
    and "        psx_ddgi_pose_back(camera->rotation, back);\n" in dynamic
    and "v += back[r][a] * (c[r] - camera->translation[r]);" in dynamic
    and "    psx_ddgi_pose_back(camera.rotation, back);\n" in whole
    and "v -= back[r][a] * camera.translation[r];" in whole
    and "rotation[" not in note + dynamic + whole,
    "a sprite card, the muzzle glow and the scene's eye go from camera to world by the true inverse: the game's turn is not "
    "orthonormal and its transpose put them off",
)

casters = function_body(
    scene, "static void ddgi_scene_casters(const DdgiList *list, const float eye[3], GpuDdgiScene *scene)"
)
picture = function_body(
    lighting, '    "float picture_hides(vec3 p, vec3 l, float spread, float reach, int i, bool own){\\n"'
)
require(
    '#include "gpu_ddgi_mask.h"\n' in capture
    and "    psx_ddgi_card_look(vram, card, semi, !(card->flags & 20));\n" in note
    and "ddgi_look" not in capture + scene
    and "psx_ddgi_card_blocking(card) < 0.02f || card->bounds[2] <= card->bounds[0]" in casters
    and "blended > 0.5f" not in casters
    and "    uint32_t mask[GPU_DDGI_MASK_ROWS];" in function_body(header, "typedef struct GpuDdgiCaster")
    and "        memcpy(c->mask, card->mask, sizeof c->mask);\n" in casters
    and "        psx_ddgi_box_picture(card, wide, tall, c->picture, &c->half_width, &c->half_height);\n" in casters
    and '    "uniform usampler2D u_mask;\\n"\n' in lighting
    and "mask_at(i, f)" in picture
    and "texel(" not in picture,
    "a sprite's look and silhouette are taken from VRAM as it is drawn and travel with its caster: by the next present the "
    "game has streamed the next animation frames into the texels they came from",
)

lamp = function_body(
    lighting, "static void ddgi_lamp_pass(int thread, GLuint color, GLuint depth, float focal_x, float focal_y,"
)
mix = function_body(
    lighting, "static int ddgi_mix_begin(int thread, GLuint color, GLuint depth, float focal_x, float focal_y,"
)
between = function_body(
    lighting, "static void ddgi_present_between(const DdgiFrame *before, const DdgiFrame *after, float alpha)"
)
mission = function_body(lighting, "void gl_renderer_set_ddgi_mission(int mission)")
sun_set = function_body(lighting, "void gl_renderer_ddgi_sun_set_mission(int mission, const float sun[4])")
require(
    "    if (ctx) ctx->lamp_ready = 0;\n"
    "    if (!ctx || !ctx->frame.valid || !(s_ddgi_shadows || s_ddgi_lamps || ddgi_sun_active() || ddgi_probes_live())) return;\n"
    in lamp
    and "    if (!ctx->lamp_ready) return 0;\n" in mix,
    "lamp light switched off is not mixed in from the last pass that ran",
)
require(
    "before->pose == after->pose) return;" in between
    and "scene.pose == after->scene.pose" not in between
    and "    psx_ddgi_camera_between(before->rows, before->offset, after->rows, after->offset, keep, shown->rows, shown->offset,\n"
    in between
    and "back[k][a] = before->back" not in between,
    "an in-between frame moves the camera whenever the camera moved, and takes the inverse and the eye of the rows it shows",
)
require(
    update.index("        if (s_interp_draw_fence) gz_wait(0, s_interp_draw_fence);\n")
    < update.index("        const int bvh = s_ddgi_bvh_next;\n")
    < update.index("        ddgi_build(tris, count, bvh, now->eye);\n")
    < update.index("        ddgi_level_update();\n"),
    "a BVH or level slot is written only after the presenter's last draw that may read it",
)
probe_writes = [m.start() for m in re.finditer(r"ddgi_draw\(s_ddgi_(state|irr|dist)_fbo", probes)]
unlocks = [m.start() for m in re.finditer(r"SDL_UnlockMutex\(s_interp_mutex\)", probes)]
held = [
    (m.start(), min((u for u in unlocks if u > m.start()), default=-1))
    for m in re.finditer(r"SDL_LockMutex\(s_interp_mutex\)", probes)
]
probe_fence = probes.rfind("    s_ddgi_probe_fence = p_glFenceSync(PSXGL_SYNC_GPU_COMMANDS_COMPLETE, 0);\n")
require(
    len(probe_writes) == 3
    and any(
        lock < probes.find("gz_wait(0, s_interp_draw_fence);", lock) < min(probe_writes)
        and max(probe_writes) < probe_fence < probes.find("    glFlush();\n", probe_fence) < unlock
        for lock, unlock in held
    )
    and lamp.index("    if (thread && s_ddgi_probe_fence) gz_wait(1, s_ddgi_probe_fence);")
    < lamp.index("    ddgi_bind_probes(color, depth);\n")
    and "ddgi_bind_probes" not in mix[: mix.index("    if (!ctx->lamp_ready) return 0;\n")],
    "the probes' state, irradiance and distances are written under the presenter's lock after its last draw, with a fence "
    "made and flushed before the lock is let go, and the lamp pass, which samples them, waits for that fence first",
)
units = [int(n) for n in re.findall(r"PSXGL_TEXTURE0 \+ (\d+)", update + probes)]
require(
    mission.index("        ddgi_level_clear();\n        gpu_ddgi_forget_level();\n")
    < mission.index("    if (rth_rec_call(RTH_DDGI_MISSION, v, 1)) return;\n")
    < mission.index("    s_ddgi_probe_reset = 1;\n")
    and "ssao_mode();" in sun_set
    and f"bound[{max(units) + 1}]" in update
    and update.count(f"for (int i = 0; i < {max(units) + 1}; ++i)") == 2,
    "a new mission forgets the level's hand-overs and the scene's caches on the game's thread before the call is recorded, "
    "the probes' light where the call runs; a sun setting recomputes the shading path, and the update puts back every "
    "texture unit it and the probes bind",
)
reset = probes[probes.index("    if (s_ddgi_probe_reset) {\n") : probes.index("        s_ddgi_probe_reset = 0;\n")]
require(
    reset.index("SDL_LockMutex(s_interp_mutex)")
    < reset.index("gz_wait(0, s_interp_draw_fence)")
    < reset.index("glClear(")
    < reset.index("s_ddgi_probe_fence = p_glFenceSync(")
    < reset.index("glFlush();")
    < reset.index("SDL_UnlockMutex(s_interp_mutex)"),
    "the probes' reset is written under the presenter's lock after its last draw and fenced before the lock is let go",
)
record = function_body(lighting, "static void ddgi_record_shot(float cy)")
for present, op in (
    ("void gl_renderer_present_vram(", "RTH_PRESENT_VRAM"),
    ("void gl_renderer_present_wide_or_vram(", "RTH_PRESENT_WIDE"),
):
    body = function_body(renderer, present)
    require(
        body.index("ddgi_record_shot(") < body.index(f"rth_rec_call({op}, "),
        "a present recorded for the render thread carries the lighting's shot of the frame just before it",
    )
require(
    "ddgi_shot_take(&s_ddgi_shots[slot], cy, 1)" in record
    and "    rth_rec_call(RTH_DDGI_SHOT, v, 1);\n" in record
    and "    case RTH_DDGI_SHOT: s_rthx_ddgi_shot = v[0]; break;\n" in render_thread
    and update.index("        s_rthx_ddgi_shot = -1;\n") < update.index("    if ((!probes && !lamps)")
    and "gpu_ddgi_shown(" not in update
    and "gpu_ddgi_scene(" not in update
    and "s_ddgi" not in function_body(render_thread, "static int gl_rth_eligible(void)"),
    "the render thread draws the lighting from a copy the game's thread took at the present, never from the game's lists "
    "that the next frames overwrite, and every shot is used once",
)
require(
    "    float casters[GPU_DDGI_CASTERS * 2 * 4], pictures[GPU_DDGI_CASTERS * 2 * 4], lamps[GPU_DDGI_LAMPS * 3 * 4];\n"
    "    uint32_t masks[GPU_DDGI_CASTERS * GPU_DDGI_MASK_ROWS];\n"
    in lamp
    and "static float casters" not in lighting
    and "static uint32_t masks" not in lighting,
    "the lamp pass's uploads are on each presenting thread's own stack: both threads come there",
)


def fields(struct: str) -> list[str]:
    body = lighting[
        lighting.rindex("typedef struct {", 0, lighting.index(f"}} {struct};")) : lighting.index(f"}} {struct};")
    ]
    return re.findall(r"\w+", body.split("GLint", 1)[1].replace(";", ""))


def looked_up(start: str, program: str) -> list[str]:
    body = lighting[lighting.index(start) : lighting.index("};", lighting.index(start))]
    return re.findall(rf'ddgi_uniform\({program}, "u_(\w+)"\)', body)


LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def macro(name: str) -> str:
    """A GLSL macro's text: the string literals of its #define, line continuations followed."""
    start = lighting.index(f"#define {name} ")
    end = start
    while lighting[lighting.index("\n", end) - 1] == "\\":
        end = lighting.index("\n", end) + 1
    return glsl_of(lighting[start + len(f"#define {name} ") : lighting.index("\n", end)])


def glsl_of(code: str) -> str:
    """The GLSL a run of C code puts together: its string literals and the DDGI_GLSL_ macros it names, in order."""
    parts = []
    for match in re.finditer(r'"(?:[^"\\]|\\.)*"|\bDDGI_GLSL_\w+\b', code):
        token = match.group(0)
        parts.append(
            macro(token)
            if token.startswith("DDGI_GLSL_")
            else codecs.decode(LITERAL.fullmatch(token).group(1), "unicode_escape")
        )
    return "".join(parts)


def program_source(name: str) -> str:
    start = lighting.index(f"static const char *{name} =")
    return glsl_of(lighting[start : lighting.index(";\n", start)])


def declared(source: str) -> set[str]:
    names = set()
    for declaration in re.findall(r"\buniform\s+\w+\s+([^;]+);", source):
        names |= {
            re.sub(r"\[.*", "", part).strip()[2:] for part in declaration.split(",") if part.strip().startswith("u_")
        }
    return names


lamp_names, apply_names = fields("DdgiLampUniforms"), fields("DdgiApplyUniforms")
for struct, start, program, names in (
    ("DdgiLampUniforms", "    ctx->la = (DdgiLampUniforms){", "l", lamp_names),
    ("DdgiApplyUniforms", "    return (DdgiApplyUniforms){", "m", apply_names),
):
    require(
        names == looked_up(start, program),
        f"{struct}: every field is looked up by its own uniform's name in the field's place: a location of -1 makes "
        "glUniform do nothing without an error",
    )

mix_names = re.findall(r'ctx->mix_\w+ = ddgi_uniform\(ctx->mix_prog, "u_(\w+)"\);', lighting)
programs = {
    "DDGI_MIX_FS": set(apply_names) | set(mix_names),
    "DDGI_LAMP_FS": set(apply_names) | set(lamp_names),
}
for name, looked in programs.items():
    source = program_source(name)
    require(
        source.startswith("#version 330\n") and declared(source) <= looked,
        f"{name}: every uniform the program declares is looked up for it, so none is left at its default unseen",
    )
require(
    set(lamp_names) <= declared(program_source("DDGI_LAMP_FS"))
    and set(mix_names) <= declared(program_source("DDGI_MIX_FS"))
    and set(apply_names) <= set().union(*(declared(program_source(name)) for name in programs)),
    "every uniform looked up is declared by the program it is looked up for, or by one of the programs the shared list "
    "serves",
)

apply_set = function_body(lighting, "static void ddgi_apply_set(const DdgiApplyUniforms *u, const DdgiFrame *f,")
require(
    "    if (f->focal > 0.0f) centre_y += (f->centre_y - 120.0f) * focal_y / f->focal;\n" in apply_set
    and "        now->centre_y = (float)cam.centre_y16 / 65536.0f;\n" in update
    and "    shown->centre_y = before->centre_y * keep + after->centre_y * (1.0f - keep);\n" in between,
    "a world place taken back from the picture uses the projection's own centre row: the vertical look moves the horizon "
    "off the picture's middle",
)

print("Disruptor probe lighting source contract: PASS")
