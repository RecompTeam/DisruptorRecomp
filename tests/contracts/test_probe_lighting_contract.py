#!/usr/bin/env python3
"""Source contract for probe lighting: the tested headers are the code that runs, camera to world by the true inverse,
silhouettes taken as a sprite is drawn, and lamp pass buffers of each presenting thread's own."""

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

require("    disruptor_contract(probe_lighting)\n" in cmake, "this contract is registered")

require(
    all(f"+runtime/include/gpu_ddgi_{name}.h\n" in listed for name in ("bvh", "mask", "turn"))
    and all(
        f"disruptor_unit_test(disruptor_gpu_ddgi_{name} tests/unit/gpu_ddgi_{name}_test.cpp RUNTIME_HEADERS)" in cmake
        for name in ("turn", "bvh", "mask")
    )
    and "disruptor_unit_test(disruptor_ddgi_level tests/unit/disruptor_ddgi_level_test.cpp RUNTIME_HEADERS INCLUDES src)" in cmake,
    "the three headers are the framework's own files and each has its unit test, the level reader too",
)

build = function_body(lighting, "static void ddgi_build(const GpuDdgiTriangle *tris, int count, int bvh, const float eye[3])")
level = function_body(lighting, "static void ddgi_level_make(int count) {")
require(
    '#include "gpu_ddgi_bvh.h"\n#include "gpu_ddgi_turn.h"\n' in lighting
    and "    psx_ddgi_corner_normals(&corners, tris, count, eye);\n" in build
    and "    psx_ddgi_median_split(&build, 0, 0, count);\n" in build
    and "    psx_ddgi_sah_split(&build, 0, 0, kept, 0);\n" in level
    and not re.search(r"static (void|int|float|uint16_t|uint64_t|int64_t) ddgi_(split|level_split|select|face_of|oct16|corner_key|corner_normals|round|back_turn)\(", lighting),
    "the frame's and the level's BVH, the corner normals and their packing are the tested headers' and have no copy here",
)

update = function_body(lighting, "static void ddgi_update(GLuint frame, float w, float h, float cx, float cy)")
require(
    update.index("        psx_ddgi_turn_rows(now->rows, gpu_ddgi_turn(cam.sequence));\n")
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

casters = function_body(scene, "static void ddgi_scene_casters(const DdgiList *list, const float eye[3], GpuDdgiScene *scene)")
picture = function_body(lighting, '    "float picture_hides(vec3 p, vec3 l, float spread, float reach, int i, bool own){\\n"')
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

lamp = function_body(lighting, "static void ddgi_lamp_pass(int thread, GLuint color, GLuint depth, float focal_x, float focal_y,")
mix = function_body(lighting, "static int ddgi_mix_begin(int thread, GLuint color, GLuint depth, float focal_x, float focal_y,")
between = function_body(lighting, "static void ddgi_present_between(const DdgiFrame *before, const DdgiFrame *after, float alpha)")
mission = function_body(lighting, "void gl_renderer_set_ddgi_mission(int mission)")
sun_set = function_body(lighting, "void gl_renderer_ddgi_sun_set_mission(int mission, const float sun[4])")
require(
    "    if (ctx) ctx->lamp_ready = 0;\n    if (!ctx || !ctx->frame.valid || !(s_ddgi_shadows || s_ddgi_lamps || ddgi_sun_active())) return;\n"
    in lamp
    and "lamps = ctx->lamp_ready && (s_ddgi_shadows || s_ddgi_lamps || ddgi_sun_active());" in mix,
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
    update.index("        if (s_interp_draw_fence) p_glWaitSync(s_interp_draw_fence, 0, PSXGL_TIMEOUT_IGNORED);\n")
    < update.index("        const int bvh = s_ddgi_bvh_next;\n")
    < update.index("        ddgi_build(tris, count, bvh, now->eye);\n")
    < update.index("        if (sun) ddgi_level_update();\n"),
    "a BVH or level slot is written only after the presenter's last draw that may read it",
)
probe_writes = [m.start() for m in re.finditer(r"ddgi_draw\(s_ddgi_(state|irr|dist)_fbo", update)]
unlocks = [m.start() for m in re.finditer(r"SDL_UnlockMutex\(s_interp_mutex\)", update)]
held = [(m.start(), min((u for u in unlocks if u > m.start()), default=-1)) for m in re.finditer(r"SDL_LockMutex\(s_interp_mutex\)", update)]
probe_fence = update.find("        s_ddgi_probe_fence = p_glFenceSync(PSXGL_SYNC_GPU_COMMANDS_COMPLETE, 0);\n")
require(
    len(probe_writes) == 3
    and any(
        lock < update.find("p_glWaitSync(s_interp_draw_fence, 0, PSXGL_TIMEOUT_IGNORED);", lock) < min(probe_writes)
        and max(probe_writes) < probe_fence < update.find("        glFlush();\n", probe_fence) < unlock
        for lock, unlock in held
    )
    and mix.index("    if (thread && s_ddgi_probe_fence) p_glWaitSync(s_ddgi_probe_fence, 0, PSXGL_TIMEOUT_IGNORED);\n")
    < mix.index("    ddgi_bind_probes(color, depth);\n"),
    "the probes' state, irradiance and distances are written under the presenter's lock after its last draw, with a fence "
    "made and flushed before the lock is let go, and the presenter waits for that fence before it samples them",
)
units =[int(n) for n in re.findall(r"PSXGL_TEXTURE0 \+ (\d+)", update)]
require(
    "    gpu_ddgi_forget_level();\n" in mission
    and "ssao_mode();" in sun_set
    and f"bound[{max(units) + 1}]" in update
    and update.count(f"for (int i = 0; i < {max(units) + 1}; ++i)") == 2,
    "a new mission forgets the scene's caches, a sun setting recomputes the shading path, and the update puts back every "
    "texture unit it binds",
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
    body = lighting[lighting.rindex("typedef struct {", 0, lighting.index(f"}} {struct};")) : lighting.index(f"}} {struct};")]
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
        parts.append(macro(token) if token.startswith("DDGI_GLSL_") else codecs.decode(LITERAL.fullmatch(token).group(1), "unicode_escape"))
    return "".join(parts)


def program_source(name: str) -> str:
    start = lighting.index(f"static const char *{name} =")
    return glsl_of(lighting[start : lighting.index(";\n", start)])


def declared(source: str) -> set[str]:
    names = set()
    for declaration in re.findall(r"\buniform\s+\w+\s+([^;]+);", source):
        names |= {re.sub(r"\[.*", "", part).strip()[2:] for part in declaration.split(",") if part.strip().startswith("u_")}
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
    "DDGI_SAMPLE_FS": set(apply_names),
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

print("Disruptor probe lighting source contract: PASS")
