#!/usr/bin/env python3
"""Source contract for telling a full-2D screen from play per frame buffer."""

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
manifest = read("framework/FILES.txt")
gpu = read("psxrecomp/runtime/src/gpu.c")
header = read("psxrecomp/runtime/include/gpu.h")
kinds = read("psxrecomp/runtime/include/gpu_ws_frame_kind.h")
runtime = read("psxrecomp/runtime/src/main.cpp")

require(
    "+runtime/include/gpu_ws_frame_kind.h" in manifest.splitlines(),
    "overlay manifest must list the frame kind arithmetic",
)
CONTRACTS = ["if(BUILD_TESTING)"]
UNIT_TESTS = ["if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)"]
REGISTERED = {
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/render_arena.cpp"\n': [],
    "    disruptor_contract(frame_kind)\n": CONTRACTS,
    "    disruptor_unit_test(disruptor_gpu_ws_frame_kind tests/unit/gpu_ws_frame_kind_test.cpp RUNTIME_HEADERS)\n": UNIT_TESTS,
    "    disruptor_unit_test(disruptor_render_arena tests/unit/disruptor_render_arena_test.cpp RUNTIME_HEADERS)\n": UNIT_TESTS,
}
for block, conditions in REGISTERED.items():
    require(
        cmake.count(block) == 1 and enclosing(cmake, block) == conditions,
        "the plugin, the two unit tests and this contract must stay in the build as written, under the conditions every test of the "
        f"repository is under: {block.strip().splitlines()[0]}",
    )
require(
    "int  gpu_ws_present_native_43(void);" in header and "int  gpu_ws_displayed_native_43(void);" in header,
    "both questions must stay public: the frame being drawn and the frame on display",
)
require(
    "int gpu_ws_displayed_native_43(void) { return ws_native_43(1); }\n"
    "void gpu_ws_tell_frame_kind(int flat) { psx_ws_frame_kinds_tell(&ws_frame_kinds, flat); }"
    in gpu
    and gpu.count("psx_ws_frame_kinds_tell(") == 1
    and "void gpu_ws_tell_frame_kind(int flat);" in header,
    "a game must be able to tell the kind of the frame it begins, as it tells it, and nothing else may",
)
require(
    "    if (!ws_active()) return 0;\n    return (160 * (ws_xden - ws_xnum)" in body(gpu, "int psx_ws_x_margin(void)")
    and "static int ws_active(void) { return ws_configured() && !gpu_ws_present_native_43(); }" in gpu,
    "unless it is forced or native-wide, the cull's margin must follow the kind of the frame being built: a world's first frame gets its moved arena by it",
)
require(
    "\n    psx_ws_frame_kinds_list(&ws_frame_kinds, 1);\n    if (word_count == 0) {"
    in body(gpu, "void gpu_set_gp0_linked_list_node(")
    and "    psx_ws_frame_kinds_list(&ws_frame_kinds, 0);" in body(gpu, "void gpu_ws_end_linked_list(void)")
    and "psx_ws_frame_kinds_list(" not in body(gpu, "void gpu_ws_begin_linked_list(void)")
    and gpu.count("psx_ws_frame_kinds_list(") == 2,
    "a display list must ask about the buffer drawn last from its first node to its end, and not while it is only begun",
)

projected = body(gpu, "void psx_ws_note_gte_project(int nverts)")
require(
    "    psx_ws_frame_kinds_project(&ws_frame_kinds, (uint32_t)nverts);" in projected,
    "every projected vertex must count for the frame being built",
)
area = body(gpu, "static void gp0_exec_draw_area_tl(void)")
require(
    "    psx_ws_frame_kinds_area(&ws_frame_kinds, draw_area_left, draw_area_top, WS_GTE_GAME_MODE_MIN_VERTS);" in area
    and max(area.index("draw_area_left = param & 0x3FF;"), area.index("draw_area_top  = (param >> 10) & 0x3FF;"))
    < area.index("psx_ws_frame_kinds_area("),
    "a new drawing area must settle the kind of the frame drawn there, at its own origin",
)
mode = body(gpu, "static int ws_game_mode_of(int displayed)")
require(
    "    if (ws_gte_game_mode_cfg) {\n"
    "        const int flat = displayed\n"
    "            ? psx_ws_frame_kinds_displayed(&ws_frame_kinds, display_area_x, display_area_y)\n"
    "            : psx_ws_frame_kinds_drawing(&ws_frame_kinds, WS_GTE_GAME_MODE_MIN_VERTS);\n"
    "        if (flat >= 0) return !flat;\n"
    "        if ((uint32_t)s_frame_count - ws_last_gte_stamp <= WS_GTE_GAME_MODE_HYSTERESIS) return 1;\n"
    "    }"
    in mode
    and mode.index("ws_gameplay_state_matches()")
    < mode.index("ws_full_2d_mode()")
    < mode.index("if (ws_gte_game_mode_cfg) {"),
    "a GTE-detected game's frame must be told by its buffer, the grace only where the buffer is not known, after the older gates",
)
require(
    "static int ws_game_mode(void) { return ws_game_mode_of(0); }" in gpu,
    "every draw-time question must be about the frame being drawn",
)
native = body(gpu, "static int ws_native_43(int displayed)")
require(
    "    if (!ws_engaged()) return 0;\n    if (displayed ? gpu_ws_displayed_menu() : ws_menu_frame()) return 0;\n"
    "    if (!ws_game_mode_of(displayed)) return 1;" in native,
    "the native 4:3 answer must follow the frame it is asked about",
)
require(
    "int gpu_ws_present_native_43(void) { return ws_native_43(0); }\n"
    "int gpu_ws_displayed_native_43(void) { return ws_native_43(1); }" in gpu,
    "the two public questions must differ in nothing but the frame",
)
require(
    gpu.count("psx_ws_frame_kinds_project(") == 1
    and gpu.count("psx_ws_frame_kinds_area(") == 1
    and gpu.count("psx_ws_frame_kinds_drawing(") == 1
    and gpu.count("psx_ws_frame_kinds_displayed(") == 2,
    "nothing else in the renderer may move or ask the frame kinds",
)
upload = body(gpu, "static void gp0_exec_cpu_to_vram(void)")
require(
    "static void ws_frame_pictured(int x, int y, int w, int h) {\n"
    "    psx_ws_frame_kinds_pictured(&ws_frame_kinds, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h,\n"
    "                                (uint32_t)ws_disp_w(), (uint32_t)ws_disp_h());\n}"
    in gpu
    and gpu.count("psx_ws_frame_kinds_pictured(") == 1
    and gpu.count("ws_frame_pictured(") == 2,
    "a picture is measured against buffers the size of the display, and only an upload reports one",
)
require(
    "\n    ws_frame_pictured(vram_write_x, vram_write_y, vram_write_w, vram_write_h);\n    /* A full history retains old uploads; later transfers must not append\n"
    in upload
    and upload.index("vram_write_h = (h == 0) ? 0x200 : (uint16_t)h;") < upload.index("ws_frame_pictured("),
    "every upload must report where it lands, whatever the front end makes of it",
)
require(
    "            gr_copy_rect(src_x, src_y, dst_x, dst_y, w, h);\n"
    "            const int copied = psx_ws_frame_kinds_copied(&ws_frame_kinds, (uint32_t)src_x, (uint32_t)src_y, (uint32_t)dst_x, (uint32_t)dst_y,\n"
    "                                                         (uint32_t)w, (uint32_t)h, (uint32_t)ws_disp_w(), (uint32_t)ws_disp_h());\n"
    "            if (copied == 1 && src_x + w <= 0x400 && dst_x + w <= 0x400 && src_y + h <= 0x200 && dst_y + h <= 0x200)\n"
    "                gr_wide_copy_frame(src_x, src_y, dst_x, dst_y, h);\n"
    "            if (copied == 2)\n"
    "                gr_wide_copy_rows((int)psx_ws_frame_kinds_holder(&ws_frame_kinds, (uint32_t)dst_x, (uint32_t)dst_y, (uint32_t)w, (uint32_t)h,\n"
    "                                                                  (uint32_t)ws_disp_w(), (uint32_t)ws_disp_h())->x,\n"
    "                                  src_x, src_y, dst_x, dst_y, w, h);\n            break;\n"
    in gpu
    and gpu.count("psx_ws_frame_kinds_copied(") == 1
    and gpu.count("gr_wide_copy_frame(") == 1
    and gpu.count("gr_wide_copy_rows(") == 1,
    "every copy inside video memory must report where it comes from and where it lands: one buffer copied whole onto the "
    "other keeps its kind, and the wide picture of its rows goes with it; rows moved inside a world's frame keep it, and "
    "the wide surface of that buffer moves them too",
)
render = read("psxrecomp/runtime/src/gpu_render.c")
gl = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
wide_copy = body(gl, "static void glb_wide_copy_frame(")
require(
    "    if (g_b->wide_copy_frame) g_b->wide_copy_frame(src_base_x, src_y, dst_base_x, dst_y, h);\n" in render
    and "    .wide_copy_frame = glb_wide_copy_frame,\n" in gl
    and "!s_raster_ok || g_wide_w <= 0 || s_mask_check || s_mask_set || h <= 0" in wide_copy
    and "(from == to && src_y < dst_y + h && dst_y < src_y + h)) return;" in wide_copy
    and "        if (s_wide_fbo[i] && s_wide_base[i] == src_base_x) from = s_wide_fbo[i];\n" in wide_copy
    and "wide_fbo_for(src_base_x)" not in wide_copy
    and wide_copy.index("temporal_invalidate_surface(to);")
    < wide_copy.index("    const GLuint target = wide_drawn(to);")
    < wide_copy.index("    wide_resolved(from, src_y, h);\n")
    < wide_copy.index(
        "    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, from);\n    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER, target);\n"
        "    glDisable(GL_SCISSOR_TEST);\n"
        "    p_glBlitFramebuffer(0, src_y * s_scale, w, (src_y + h) * s_scale, 0, dst_y * s_scale, w, (dst_y + h) * s_scale,\n"
    )
    and wide_copy.count("p_glBindFramebuffer(") == 3
    and wide_copy.count("wide_drawn(") == 1
    and "GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);" in wide_copy,
    "a frame copied whole takes the wide surface's rows with it: read where the source's twin was resolved, written through "
    "the destination's twin, colour, depth and mask bits, never over its own rows, never under a mask test, and the "
    "in-between presenter drops what it built from the rows drawn before",
)
wide_rows = body(gl, "static void glb_wide_copy_rows(")
require(
    "    if (g_b->wide_copy_rows) g_b->wide_copy_rows(base_x, src_x, src_y, dst_x, dst_y, w, h);\n" in render
    and "    .wide_copy_rows = glb_wide_copy_rows,\n" in gl
    and "!s_raster_ok || g_wide_w <= 0 || s_mask_check || s_mask_set || w <= 0 || h <= 0" in wide_rows
    and "    const int from = src_x - base_x + g_wide_off, to = dst_x - base_x + g_wide_off;\n"
    "    if (!fbo || from + w > g_wide_w || to + w > g_wide_w) return;\n"
    in wide_rows
    and wide_rows.index("    temporal_invalidate_surface(fbo);\n")
    < wide_rows.index("    wide_drawn(fbo);")
    < wide_rows.index("    wide_resolved(fbo, src_y, h);\n")
    < wide_rows.index("    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER, s_scratch_fbo);\n")
    < wide_rows.index("    const GLuint target = wide_drawn(fbo);")
    < wide_rows.index("    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER, target);\n")
    and wide_rows.count("wide_drawn(") == 2
    and wide_rows.count("GL_COLOR_BUFFER_BIT, GL_NEAREST);") == 2
    and "    p_glBlitFramebuffer(from * S, src_y * S, (from + w) * S, (src_y + h) * S, from * S, src_y * S, (from + w) * S, "
    "(src_y + h) * S,\n"
    in wide_rows
    and "    p_glBlitFramebuffer(from * S, src_y * S, (from + w) * S, (src_y + h) * S, to * S, dst_y * S, (to + w) * S, "
    "(dst_y + h) * S,\n"
    in wide_rows
    and "DEPTH" not in wide_rows,
    "rows moved inside a frame move the wide surface's colour by the same columns and rows: read where its twin was "
    "resolved, through the scratch surface, written through the twin, never under a mask test, and the in-between "
    "presenter drops what it built from the rows drawn before",
)
require(
    "    if (opcode >= 0x20 && opcode <= 0x7F) {\n"
    "        psx_ws_frame_kinds_drawn(&ws_frame_kinds, psx_ws_frame_kinds_blend(opcode));\n"
    in gpu
    and "        gp0_state = shaded ? GP0_POLYLINE_SHADED : GP0_POLYLINE_MONO;\n"
    "        psx_ws_frame_kinds_drawn(&ws_frame_kinds, psx_ws_frame_kinds_blend(opcode));\n"
    in gpu
    and gpu.count("psx_ws_frame_kinds_drawn(") == 2
    and gpu.index(
        "        psx_ws_frame_kinds_drawn(&ws_frame_kinds, psx_ws_frame_kinds_blend(opcode));\n        int32_t cvx, cvy;"
    )
    < gpu.index("    switch (opcode) {\n        case 0x00:"),
    "every drawing command, a polyline too, must say before it is drawn whether it is a blend: the first that is none ends a veil",
)
require(
    "    psx_ws_frame_kinds_filled(&ws_frame_kinds, dst_x, dst_y, width, height, (uint32_t)ws_disp_w(), (uint32_t)ws_disp_h());\n"
    "    ws_menu_yield((int)dst_x, (int)dst_y, (int)width, (int)height, 1);\n"
    "    gr_fill_rect((int)dst_x, (int)dst_y, (int)width, (int)height, color16);\n"
    in gpu
    and gpu.count("psx_ws_frame_kinds_filled(") == 1,
    "a fill, which no drawing area bounds, must report the rectangle it clears before it clears it: one that covers a veiled buffer ends its veil",
)
require(
    "    const int held = kinds->buffers[slot].known && kinds->buffers[slot].x == x && kinds->buffers[slot].y == y && !kinds->buffers[slot].flat;\n"
    "    const int flat = kinds->told ? kinds->told_flat : kinds->projected < least;\n"
    in kinds
    and "    kinds->buffers[slot].veiled = flat && !kinds->told && held;\n"
    "    kinds->buffers[slot].flat = flat && !kinds->buffers[slot].veiled;\n"
    in kinds
    and "    if (blend || !buffer->veiled) return;\n    buffer->veiled = 0;\n    buffer->flat = 1;\n" in kinds
    and "    return (opcode & 2u) != 0 && (lines || (opcode & 4u) == 0);\n" in kinds,
    "a frame of too few vertices set on a buffer that holds a world's frame is that frame under a veil, as when a level's end "
    "fades it out, until something but an untextured blend is drawn over it: taken for a flat frame it was shown in 4:3 on a wide screen",
)
require(
    "int gpu_ws_displayed_flat(void) {\n"
    "    return ws_gte_game_mode_cfg && !gpu_ws_displayed_menu() &&\n"
    "           psx_ws_frame_kinds_displayed(&ws_frame_kinds, display_area_x, display_area_y) == 1;\n}"
    in gpu
    and "int  gpu_ws_displayed_flat(void);" in header,
    "the present must be able to ask whether the frame on display is a flat one that is not the front end's",
)
require(
    "        bool wide_present = (!fmv_frame && !di.depth24 && g_ws_engaged &&\n"
    "                             gr_wide_supported() && !gpu_ws_displayed_flat() &&\n"
    "                             (ws_native_wide_active() || corrected_present));\n"
    in runtime
    and runtime.count("wide_present = true") == 0
    and runtime.count("gpu_ws_displayed_flat()") == 1,
    "a flat frame must be presented from video memory whatever would send it to the mirror: uploads and copies do not reach it",
)
require(
    "        fmv_frame = di.depth24 || !g_ws_engaged || gpu_ws_displayed_native_43() != 0;" in runtime
    and runtime.count("gpu_ws_displayed_native_43()") == 1,
    "the present must ask about the frame on display",
)

print("Disruptor frame kind source contract: PASS")
