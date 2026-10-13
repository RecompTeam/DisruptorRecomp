#!/usr/bin/env python3
"""Source contract for a world face's texture on the mirror: the framework's whole-pixel rule is taken back there."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def between(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    return source[start : source.index(closing, start)]


cmake = read("CMakeLists.txt")
overlay = read("framework/FILES.txt")
header = read("psxrecomp/runtime/include/gpu_world_texel.h")
rule = read("psxrecomp/runtime/src/gpu_uv.h")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")

require(
    overlay.count("+runtime/include/gpu_world_texel.h\n") == 1
    and '#include "gpu_world_texel.h"\n' in renderer
    and "    disruptor_unit_test(disruptor_gpu_world_texel tests/unit/gpu_world_texel_test.cpp RUNTIME_HEADERS\n"
    '        INCLUDES "${PSXRECOMP_ROOT}/runtime/src")\n' in cmake
    and "    disruptor_contract(world_texel)\n" in cmake,
    "the header is applied to the framework, and its test, which runs the framework's own rule, and this contract are registered",
)

require(
    "static inline void psx_uv_tri_dirs(const int *xs, const int *ys,\n" in rule
    and "    if (area2 == 0) { *du = 0; *dv = 0; return; }\n" in rule
    and "    *du = dudx == 0 ? dudy : (dudy == 0 ? dudx : 0);\n    *dv = dvdx == 0 ? dvdy : (dvdy == 0 ? dvdx : 0);\n" in rule
    and "    if (dir > 0 && hi > lo) hi--;\n    if (dir < 0 && hi > lo) lo++;\n" in rule
    and "    if (du < 0) { us[0]++; us[1]++; us[2]++; }\n    if (dv < 0) { vs[0]++; vs[1]++; vs[2]++; }\n" in rule
    and "gpu_uv.h" not in overlay,
    "the framework's rule is as this change found it: it decides by whole-pixel corners, moves a texture a texel and "
    "tightens a bound by one, and a triangle without an area runs neither way",
)

require(
    "        bounds[i] = (float)ruled[i] + (ruled[i] == plain[i] + (i < 2 ? 1 : -1) ? 0.25f : 0.0f);\n" in header
    and "        const float whole = floorf(bounds[i] + 0.5f);\n"
    "        const int marked = take && bounds[i] - whole > 0.125f;\n"
    "        if (marked && i < 2) uv[i] -= 1.0f;\n"
    "        bounds[i] = whole + (marked ? (i < 2 ? -1.0f : 1.0f) : 0.0f);\n" in header,
    "a bound the rule tightened by one carries a quarter, and settling a corner rounds it off or takes the rule back",
)

triangle = between(renderer, "static void gpu_textured_triangle(const int *xs, const int *ys,", "\n}\n")
require(
    "    int plain_lim[4], marked_lim = 0;\n    if (!lim) {\n" in triangle
    and "        psx_uv_tri_limits(xs, ys, mu, mv, lim_buf);\n"
    "        psx_uv_tri_mirror_offset(xs, ys, mu, mv);\n"
    "        if (visual_flags) {\n"
    "            static const int nowhere[3] = {0, 0, 0};\n"
    "            psx_uv_tri_limits(nowhere, nowhere, us, vs, plain_lim);  /* without an area a mapping runs neither way */\n"
    "            marked_lim = 1;\n"
    "        }\n"
    "        us = mu; vs = mv;\n" in triangle
    and "            vp[14] = (float)lim[0];  vp[15] = (float)lim[1];        /* a_limits */\n"
    "            vp[16] = (float)lim[2];  vp[17] = (float)lim[3];\n"
    "            if (marked_lim) psx_world_texel_mark(lim, plain_lim, &vp[14]);\n" in triangle
    and renderer.count("psx_world_texel_mark(") == 1
    and renderer.count("marked_lim = 1;") == 1,
    "only a polygon's triangle the mirror places itself is marked, from the game's own texture corners, and a rectangle "
    "keeps the rule as it came",
)

require(
    '    "  v_limits = ivec4(floor(a_limits + 0.5));\\n"\n' in renderer
    and '    "  bool corrected = u_visual > 0.5 && a_visual_flags > 0.5;\\n"\n' in renderer
    and '    "  vec4 took = corrected ? step(0.125, a_limits - floor(a_limits + 0.5)) : vec4(0.0);\\n"\n'
    '    "  v_uv -= took.xy; v_limits += ivec4(vec4(-took.xy, took.zw));\\n"\n' in renderer
    and '    "  v_uvq = vec3(a_uv*q, q); v_perspective = q > 0.0 ? 1 : 0;\\n"\n    "  v_uvq.xy -= took.xy*q;\\n"\n' in renderer
    and renderer.index('"  vec4 took = corrected ?') > renderer.index('"  v_limits = ivec4(floor(a_limits + 0.5));')
    and renderer.count("took.xy") == 3
    and renderer.count("took.zw") == 1,
    "the canonical picture rounds a mark off, and the mirror takes the rule back for a face it places itself: the texel "
    "off the texture, with and without perspective, and the bound to where it was",
)

position = between(presenter, "static void temporal_position(float *v, int textured, int gate, int direct) {", "\n}\n")
require(
    "    int corrected = !direct && s_geometry_correction && v[flags_at] > 0.5f;\n" in position
    and "    if (textured) psx_world_texel_settle(corrected, &v[2], &v[14]);\n" in position
    and presenter.count("psx_world_texel_settle(") == 1,
    "a recorded corner is settled once as the mirror shows it, so the presenter's shader finds nothing marked",
)

print("Disruptor world texel source contract: PASS")
