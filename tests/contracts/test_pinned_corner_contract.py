#!/usr/bin/env python3
"""Source contract for the corners the game pins to its guard band: where they are drawn and how an in-between frame treats them."""

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start : source.index("\n}\n", start)]


def returns(function: str) -> list[str]:
    """Every return statement of a function body, in order: an added early one switches the function off."""
    return re.findall(r"\breturn\b[^;]*;", function)


def enclosing(source: str, block: str) -> list[str]:
    """The conditions open around a block of CMake, a branch after the first shown as such."""
    opened: list[str] = []
    for line in source[: source.index(block)].splitlines():
        command = line.strip()
        if command.startswith("if("):
            opened.append(command)
        elif command.startswith(("elseif(", "else(")):
            opened[-1] = f"{command} of {opened[-1]}"
        elif command.startswith("endif("):
            opened.pop()
    return opened


cmake = read("CMakeLists.txt")
overlay = read("framework/FILES.txt")
gte = read("psxrecomp/runtime/src/gte.cpp")
gpu = read("psxrecomp/runtime/src/gpu.c")
render_header = read("psxrecomp/runtime/include/gpu_render.h")
render = read("psxrecomp/runtime/src/gpu_render.c")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
temporal = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
mesh = read("psxrecomp/runtime/include/gpu_temporal_mesh.h")

REGISTRATION = "    disruptor_contract(pinned_corner)\n"
UNIT_TEST = (
    "    disruptor_unit_test(disruptor_gpu_pinned_corner tests/unit/gpu_pinned_corner_test.cpp RUNTIME_HEADERS)\n"
)
require(
    cmake.count(REGISTRATION) == 1 and enclosing(cmake, REGISTRATION) == ["if(BUILD_TESTING)"],
    "this contract must stay registered as written, under the condition every contract of the repository is under",
)
require(
    cmake.count(UNIT_TEST) == 1
    and enclosing(cmake, UNIT_TEST) == ["if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)"]
    and overlay.count("+runtime/include/gpu_pinned_corner.h\n") == 1,
    "the unit test of the pinned-corner arithmetic must stay registered where every built test is, and its header must reach the framework",
)

lookup = body(gte, "static GtePrecisionLookupResult precision_lookup_projection(")
require(
    "    if (stored.saturated && !(stored.clamped && stored.unbounded && precision_unpin()))\n"
    "        return GTE_PRECISION_LOOKUP_SATURATED;"
    in lookup
    and lookup.count("GTE_PRECISION_LOOKUP_SATURATED") == 1
    and lookup.index("return GTE_PRECISION_LOOKUP_ZERO_DEPTH;") < lookup.index("stored.saturated"),
    "a saturated projection must be refused unless the game pinned it and an exact divide placed it, and never without a depth",
)
load = body(gte, 'extern "C" GtePrecisionLookupResult gte_precision_load_word_ex(')
require(
    "    const bool exact = projection->clamped && projection->unbounded && precision_unpin();\n"
    "    if (x16) *x16 = exact ? projection->exact_x16 : projection->x16;\n"
    "    if (y16) *y16 = exact ? projection->exact_y16 : projection->y16;\n"
    in load
    and gte.count("exact_x16 : projection->x16") == 1,
    "only a corner the game pinned may leave the pixel the GTE gave it for the exact projection",
)
weight = body(gte, 'extern "C" GtePrecisionLookupResult gte_precision_load_perspective_word_ex(')
require(
    "    if (!projection->perspective_valid && !(projection->clamped && projection->unbounded == 2 && precision_unpin()))\n"
    "        return GTE_PRECISION_LOOKUP_PERSPECTIVE_INVALID;\n"
    in weight
    and gte.count("!projection->perspective_valid") == 1
    and "                s_precise_sxy[2].unbounded = nearer >= 1.0 && gte->H != 0 && gte->MAC3 > 0 && gte->MAC3 <= 0xFFFF ? 2 : 1;\n"
    in gte,
    "a corner drawn at its exact place is textured by the depth that place was divided by, when the place was not brought "
    "nearer and the depth is the camera's own, and no other corner the GTE gave no depth",
)
switch = body(gte, "static int precision_unpin()")
require(
    "        s_precision_unpin = 1;\n#ifndef PSX_NO_DEBUG_TOOLS\n" in switch
    and "        if (value && value[0] == '0') s_precision_unpin = 0;\n#endif\n" in switch
    and gte.count('getenv("PSX_GEOMETRY_UNPIN")') == 1,
    "the comparison switch must be on unless a build with debug tools is told otherwise",
)

resolve = body(gpu, "static int resolve_precise_vertices(const int *indices, int count,")
require(
    '#include "gpu_pinned_corner.h"\n' in gpu
    and "    int pinned[4] = {0};\n    const int unpin = gte_precision_unpin_enabled();\n" in resolve
    and "                pinned[i] = gte_precision_word_clamped(addr, word);\n"
    "                if ((fixed16_integer_floor(precise_x[i]) != raw_x[i] ||\n"
    "                     fixed16_integer_floor(precise_y[i]) != raw_y[i]) &&\n"
    "                    !pinned[i]) {\n"
    "                    reject = GPU_GEOMETRY_REJECT_INTEGER_MISMATCH;"
    in resolve
    and resolve.count("pinned[i] = ") == 1,
    "a corner counts as pinned only when its looked-up projection went through the game's clamp to this very word",
)
require(
    "    s_pinned_places.unpinned = 0;\n    if (!gte_geometry_correction_enabled())\n        return 0;\n" in resolve
    and "            s_precise_vertex_partial = 1;\n#endif\n"
    "            psx_pinned_places(count, unpin, pinned, precise_x, precise_y, raw_x, raw_y, vx, vy, &s_pinned_places);\n"
    "        }\n        return 0;\n    }\n"
    in resolve
    and resolve.count("s_pinned_places") == 2,
    "a polygon that falls back with some corner resolved must place its corners by the tested arithmetic, from this "
    "polygon's own lookups, and the mark must not outlive the polygon",
)
require(
    resolve.count("s_precise_vertex_depth[i] = psx_pinned_corner_depth(unpin, pinned[i], z[i]);") == 2
    and resolve.count("s_precise_vertex_depth[i] = ") == 2,
    "a pinned corner must reach the in-between frames without a depth, in a whole polygon and in one that falls back",
)
require(
    returns(resolve) == ["return 0;", "return 0;", "return 0;", "return 1;"]
    and resolve.index("        return 0;\n    }\n\n    int32_t precise_x[4]")
    < resolve.index("    for (int i = 0; i < count; ++i) {")
    and "    if (resolved != count) {\n        if (resolved != 0) {\n" in resolve,
    "the resolution must leave only where it did: with the correction off, without a command source, on a fallback, and at its end",
)
queue = body(gpu, "static void queue_precise_triangle(int exact,")
require(
    "        gr_set_precise_triangle(0, 0,0, 0,0, 0,0);\n"
    "        if (geometry_enabled && s_pinned_places.unpinned)\n"
    "            gr_set_unpinned_triangle(s_pinned_places.x16[a], s_pinned_places.y16[a], s_pinned_places.x16[b], s_pinned_places.y16[b],\n"
    "                                     s_pinned_places.x16[c], s_pinned_places.y16[c]);\n"
    in queue
    and queue.count("gr_set_unpinned_triangle(") == 1
    and queue.index("    if (!geometry_enabled || !exact) {\n")
    < queue.index("gr_set_unpinned_triangle(")
    < queue.index("        return;\n    }\n")
    and returns(queue) == ["return;"],
    "the pinned corners' places must be queued for a triangle without exact provenance only, corner for corner",
)
require(
    "    gr_set_temporal_depth_triangle(psx_pinned_depth_mode(s_precise_vertex_depth[a], s_precise_vertex_depth[b], s_precise_vertex_depth[c]),\n"
    "                                   (float)s_precise_vertex_depth[a],\n"
    "                                   (float)s_precise_vertex_depth[b],\n"
    "                                   (float)s_precise_vertex_depth[c]);\n" in queue,
    "an exact triangle with a corner without depth must keep the depths of the others: the all-or-nothing mode drops them",
)

require(
    "void gr_set_unpinned_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2);"
    in render_header
    and "    void (*set_unpinned_triangle)(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2);\n} GpuRenderBackend;"
    in render_header
    and "void gr_set_unpinned_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {\n"
    "    if (g_b->set_unpinned_triangle) g_b->set_unpinned_triangle(x0, y0, x1, y1, x2, y2);\n}"
    in render,
    "the backend's member must stay last, so that a table written without it leaves it null, and a null one must be skipped",
)
take = body(renderer, "static int take_visual_triangle(const int *xs, const int *ys,")
require(
    "    const int unpinned = !exact && s_geometry_correction && s_next_unpinned;\n" in take
    and "        if (exact) {\n"
    "            px[i] = (float)((double)s_next_x16[i] / 65536.0);\n"
    "            py[i] = (float)((double)s_next_y16[i] / 65536.0);\n"
    "        } else if (unpinned) {\n"
    "            px[i] = (float)((double)s_next_unpinned_x16[i] / 65536.0);\n"
    "            py[i] = (float)((double)s_next_unpinned_y16[i] / 65536.0);\n"
    "        } else {\n"
    in take
    and returns(take) == ["return (world || sprite || unpinned ? 1 : 0) | (exact ? 2 : 0) | (filler ? 4 : 0);"],
    "the queued places must be used only without exact ones and with the correction on, and the shader told to use them",
)
setter = body(
    renderer,
    "static void glb_set_unpinned_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2)",
)
require(
    "    if (!s_raster_ok) return;\n    s_next_unpinned = 1;\n"
    "    s_next_unpinned_x16[0] = x0; s_next_unpinned_y16[0] = y0;\n"
    "    s_next_unpinned_x16[1] = x1; s_next_unpinned_y16[1] = y1;\n"
    "    s_next_unpinned_x16[2] = x2; s_next_unpinned_y16[2] = y2;"
    in setter
    and "    .set_unpinned_triangle = glb_set_unpinned_triangle,\n" in renderer,
    "the GL backend must keep the three places in the order given and be the backend's member",
)
require(
    renderer.count("s_next_precise = 0;\n")
    == renderer.count("s_next_precise = 0;\n    s_next_unpinned = 0;\n")
    + renderer.count("s_next_precise = 0;\n        s_next_unpinned = 0;\n")
    + 1
    and renderer.count("static int           s_next_precise = 0;\n") == 1,
    "the queued places are for one triangle: wherever the exact ones are dropped these must be dropped too",
)

underlay = body(temporal, "static void temporal_underlay(float alpha,int covered)")
require(
    "            int show=p->gone[ti];\n" in underlay
    and "            else if (show) show=opaque && !(t && s_temp_under[j*TEXV+18]!=0.0f) && (!covered || (was->modelled && show==1));"
    in underlay,
    "a departed face with a held corner must go under the current world only: its depth there is not the face's",
)
prepare = body(mesh, "static inline int gpu_temporal_mesh_prepare_scene(")
require(
    "            start.w[vertex] = 0.0f;\n"
    "            if (prev[triangle].depth[vertex] == 0.0f) { to.w[vertex] = 0.0f; continue; }\n"
    "            known = gpu_temporal_mesh_known_depth(prev[triangle].depth[vertex]);\n"
    "            if (!known) break;\n"
    "            ++carried;\n"
    in prepare
    and "        if (!known || !carried || !gpu_temporal_mesh_camera_safe(&prev[triangle], &to) ||\n" in prepare
    and "        departed[triangle] = carried == 3 ? 1 : 2;\n" in prepare,
    "a departed face must be carried when at least one corner has a depth, a corner without one held where it was drawn",
)

print("Disruptor pinned corner source contract: PASS")
