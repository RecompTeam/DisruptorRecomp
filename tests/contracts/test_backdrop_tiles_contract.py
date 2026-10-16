#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's widescreen backdrop tiles."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800
BACKDROP_BEGIN = 0x8003AF68
BACKDROP_END = 0x8003B6E0
SET_SPRT_CALL = 0x0C0131F3

TILE_SITES = {
    0x8003B328: 0x8FB80040,
    0x8003B338: 0x00621821,
    0x8003B348: 0x8FB80028,
    0x8003B5A8: 0x8F8205B8,
    0x8003B638: 0x8FB80040,
}

RETAIL_INSTRUCTIONS = {
    **TILE_SITES,
    0x8003AF68: 0x27BDFF60,
    0x8003B33C: 0xAFB80038,
    0x8003B340: 0x0303C021,
    0x8003B344: 0xAFB80018,
    0x8003B350: 0x0303C021,
    0x8003B354: 0xAFB80028,
    0x8003B360: 0x2B020500,
    0x8003B514: 0x8F910390,
    0x8003B538: SET_SPRT_CALL,
    0x8003B564: 0xA6380008,
    0x8003B570: 0xA628000A,
    0x8003B57C: 0xA6370010,
    0x8003B580: 0xA63E0012,
    0x8003B5A0: 0x0C013180,
    0x8003B5A4: 0xA622000E,
    0x8003B5C8: 0x26310014,
    0x8003B640: 0xAFB80038,
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/game/render/backdrop_tiles.cpp")
gpu = read("psxrecomp/runtime/src/gpu.c")
gpu_header = read("psxrecomp/runtime/include/gpu.h")
codegen = read("psxrecomp/recompiler/src/code_generator.cpp")
configs = read("game.toml") + read("game-widescreen.toml")
audit = read("tools/audit_codegen.py")

require(
    "src/game/render/backdrop_tiles.cpp" in cmake
    and "    disruptor_contract(backdrop_tiles RETAIL)\n" in cmake
    and "    disruptor_unit_test(disruptor_backdrop_tiles " in cmake,
    "backdrop tile source, its unit test and its contract test must remain registered",
)
require(
    "    disruptor_contract(backdrop_tiles RETAIL)\n" in cmake,
    "the build gate must run this contract with its inputs required",
)
require(
    "+runtime/include/gpu_ws_screen_tile.h" in read("framework/FILES.txt").splitlines(),
    "the tile header must be applied to the framework checkout",
)

for text, name in (
    (source, "host hook"),
    (codegen, "code generator"),
    (configs, "game configuration"),
    (audit, "codegen audit"),
):
    upper = text.upper()
    require(
        all(f"0X{address:08X}" in upper and f"0X{word:08X}" in upper for address, word in TILE_SITES.items()),
        f"the {name} must carry the five audited seams",
    )
require(
    "disruptor_backdrop_tiles_instruction_hook(cpu, " in codegen
    and configs.count('callback = "disruptor_backdrop_tiles_instruction_hook"\n') == 2,
    "compiled and interpreted code must reach the same host hook",
)
require(
    "gpu_ws_tag_screen_tile(cpu, cpu->gpr[kPacketRegister]);" in source and "kPacketRegister = 17;" in source,
    "tiles must be marked from the packet pointer the routine holds in $s1",
)
require("psx_mod_write" not in source, "the backdrop hooks must not write guest memory")
require("mod_plugins.h" not in source, "the backdrop hooks must not need a function-entry plugin")

require(
    "void gpu_ws_tag_screen_tile(CPUState *cpu, uint32_t primitive_addr);" in gpu_header
    and "int32_t gpu_ws_widen_x(int32_t x, int round_up);" in gpu_header,
    "the renderer must export the backdrop tile seams",
)
tagging = gpu[gpu.index("void gpu_ws_tag_screen_tile") : gpu.index("int32_t gpu_ws_widen_x")]
widening = gpu[gpu.index("int32_t gpu_ws_widen_x") : gpu.index("static int ws_screen_tile_take")]
taking = gpu[gpu.index("static int ws_screen_tile_take") : gpu.index("void psx_ws_sprite_tag")]
require(
    "if ((!ws_active() && !ws_backdrop_placed()) || !cpu || !cpu->read_word) return;" in tagging
    and "cpu->read_word(primitive_addr + 4u + i * 4u)" in tagging
    and "    psx_ws_screen_tile_mark(&ws_screen_tiles,\n"
    "                            psx_mod_gpu_dma_resolve_address(primitive_addr),\n"
    "                            words, (uint32_t)s_frame_count,\n"
    "                            ws_backdrop_spanned\n"
    "                                ? psx_ws_screen_tile_pins((int16_t)(words[1] & 0xFFFFu), (int16_t)(words[3] & 0xFFFFu),\n"
    "                                                          ws_backdrop_left, ws_backdrop_right)\n"
    "                                : 3u);\n"
    in tagging
    and "static int ws_backdrop_placed(void) { return ws_backdrop_period > 0 && gte_geometry_correction_enabled(); }\n"
    in gpu,
    "tiles must be marked with their packet words and with which of their edges end the game's span, in classic "
    "widescreen and wherever the display places the panorama itself",
)
require(
    "if (!ws_active()) return x;" in widening
    and "return psx_ws_widen_about(x, ws_disp_w() / 2, ws_xnum, ws_xden, round_up);" in widening,
    "edges must stay untouched outside classic widescreen",
)
require(
    "    return psx_ws_screen_tile_take(\n"
    "        &ws_screen_tiles,\n"
    "        psx_mod_gpu_dma_resolve_address(gp0_cmd_source_addr - 4u),\n"
    "        gp0_cmd_buf, (uint32_t)s_frame_count, pins);\n"
    in taking
    and "static int ws_screen_tile_take(uint32_t *pins) {\n    *pins = 0u;\n" in taking,
    "a drawn packet must be matched by its tag address against the words it was marked with",
)
require(
    "if (ws_screen_tile_take(&tile_pins)) {" in gpu
    and "psx_ws_squash_about(x0 + w, centre, ws_xnum, ws_xden);" in gpu
    and "x0 = psx_ws_squash_about(x0, centre, ws_xnum, ws_xden);" in gpu,
    "marked tiles must squash about the display centre through shared edges",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()

    def word(address: int) -> int:
        return struct.unpack_from("<I", image, ROM_TEXT_OFFSET + address - LOAD_ADDRESS)[0]

    for address, instruction in RETAIL_INSTRUCTIONS.items():
        require(word(address) == instruction, f"retail backdrop identity changed at 0x{address:08X}")
    calls = [address for address in range(BACKDROP_BEGIN, BACKDROP_END, 4) if word(address) == SET_SPRT_CALL]
    require(calls == [0x8003B538], "the backdrop routine must build tiles through one SetSprt call")

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    emitted = re.findall(
        r"disruptor_backdrop_tiles_instruction_hook\(cpu, 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, 1\)", text
    )
    require(
        sorted((int(address, 16), int(instruction, 16)) for address, instruction in emitted)
        == sorted(TILE_SITES.items()),
        "generated guest code must call the hook at exactly the five audited seams",
    )

renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
tile_header = read("psxrecomp/runtime/include/gpu_ws_screen_tile.h")
dispatch = read("psxrecomp/runtime/src/gpu_render.c")
module = read("src/game/render/backdrop_tiles.cpp")
require(
    "            g_span_left = static_cast<int32_t>(cpu->gpr[kEdgeRegister]);\n" in module
    and "            gpu_ws_backdrop_span(g_span_left, static_cast<int32_t>(cpu->gpr[kEdgeRegister]));\n" in module
    and "                cpu->gpr[kScrollRegister] += kPanoramaPeriod;\n"
    "            gpu_ws_backdrop_scroll(static_cast<int32_t>(cpu->gpr[kScrollRegister]), static_cast<int32_t>(kPanoramaPeriod), kPanoramaColumn);\n"
    in module
    and "constexpr int32_t kPanoramaColumn = 64;" in module
    and "void gpu_ws_backdrop_scroll(int32_t scroll, int32_t period, int32_t column);\nvoid gpu_ws_backdrop_span(int32_t left, int32_t right);\n"
    in gpu_header
    and "    ws_backdrop_now = scroll;\n    ws_backdrop_period = period > 0 ? period : 0;\n    ws_backdrop_column = column > 0 ? column : 0;\n"
    in gpu
    and "    ws_backdrop_left = left;\n    ws_backdrop_right = right;\n    ws_backdrop_spanned = right > left;\n"
    in gpu,
    "the game's module tells the panorama's scroll, period and column width and the widened span, after its own seams have run, "
    "and writes nothing more to the guest",
)
require(
    "    } else if (w > 0 && (ws_active() || ws_backdrop_placed())) {\n" in gpu
    and "            if (ws_backdrop_placed()) {\n" in gpu
    and "                const double scale = (double)ws_xnum / (double)ws_xden;\n"
    "                const double turn = isfinite(s_geometry_camera_yaw_residual) ? s_geometry_camera_yaw_residual : 0.0;\n"
    "                placed.moved = (float)(turn * (double)ws_backdrop_period / 256.0 * scale);\n"
    "                placed.left = (float)(psx_ws_screen_tile_edge(frame_x, centre, ws_xnum, ws_xden) - (double)x0);\n"
    "                placed.right = (float)(psx_ws_screen_tile_edge(frame_x + w, centre, ws_xnum, ws_xden) - (double)right);\n"
    "                placed.scale = (float)scale;\n"
    "                placed.column = (float)ws_backdrop_column;\n"
    "                placed.origin = (float)(-(double)ws_backdrop_now * scale) + placed.moved;\n"
    "                placed.period = (float)((double)ws_backdrop_period * scale);\n"
    "                placed.pins = tile_pins;\n"
    "                tile_placed = 1;\n"
    in gpu
    and "    setup_textured_draw(color24, semi_trans, raw_texture);\n    if (tile_placed) gr_set_screen_tile(&placed);\n    ws_hud_held_sides_send();\n    if (dw != w || dh != h)\n"
    in gpu
    and gpu.index(
        "    if (draw_area_out_rect(x0, y0, dw, dh)) return;\n    setup_textured_draw(color24, semi_trans, raw_texture);\n    if (tile_placed)"
    )
    > gpu.index("                tile_placed = 1;\n")
    and "void gr_set_screen_tile(const struct PsxWsPlacedTile *tile) {\n    if (g_b->set_screen_tile) g_b->set_screen_tile(tile);\n}\n"
    in dispatch
    and "    .set_screen_tile = glb_set_screen_tile,\n    .set_flat_sides = glb_set_flat_sides,\n#ifndef PSX_DISABLE_FRAME_INTERPOLATION\n"
    in renderer,
    "a marked tile is handed to the renderer with the remainder of the turn as a move of the panorama, each edge's rounding, "
    "the panorama's origin and period, only for a rectangle that is drawn, and in a build without in-between frames too",
)
require(
    "    if (!s_raster_ok || !tile || !isfinite(tile->left + tile->right + tile->moved) || !(tile->scale > 0.0f) ||\n"
    "        fabsf(tile->left) > 1.0f || fabsf(tile->right) > 1.0f || fabsf(tile->moved) > 64.0f) return;\n"
    "    s_next_tile = *tile;\n    s_next_tile_set = 1;\n"
    in renderer
    and "    s_tile_set = s_next_tile_set && w > 0 && h > 0;  /* as a sprite's depth: for this rectangle alone */\n"
    "    s_next_tile_set = 0;\n    s_tile = s_next_tile;\n"
    in renderer
    and "    gpu_textured_triangle(xs2,ys2,us2,vs2,col,tp,clut_x,clut_y,s_mod_raw,semi,lim);\n    s_tile_set = 0;\n    s_sprite_depth = 0.0f;\n    memset(s_flat_sides, 0, sizeof(s_flat_sides));\n}\n"
    in renderer
    and "static int tile_placed(void) { return s_geometry_correction && s_tile_set; }\n" in renderer
    and "    if (s_tile.pins & (end ? 2u : 1u)) {\n        *place = (float)x;\n        *mark = end ? -4.0f : -3.0f;\n"
    "        *slide = psx_ws_screen_tile_slide(offset, s_tile.moved, s_tile.scale, end,\n"
    "                                          psx_ws_screen_tile_room(u, s_tile.column, end), across > 1.0f ? across - 1.0f : 0.0f);\n"
    "        *want = psx_ws_screen_tile_want(offset, s_tile.moved, s_tile.scale);\n"
    "    } else {\n        *place = (float)x + offset + s_tile.moved;\n        *mark = -2.0f;\n        *slide = *want = 0.0f;\n    }\n"
    in renderer
    and "            if (!visual_flags && tile_placed()) {\n" in renderer
    and "                tile_corner(xs[i], (float)us[i], (float)(high - low), &vp[20], &vp[19], &vp[22], &vp[21]);  /* y's slot: the mirror takes a_pos.y */\n"
    in renderer,
    "a tile's placement is for its own rectangle and only with geometry correction: a free edge moves with the panorama, an "
    "end of the span stays on its whole pixel and slides its texture within its column and its own width",
)
require(
    '    "  bool placed = u_visual > 0.5 && a_visual_flags < -1.5;\\n"\n' in renderer
    and '    "  vec2 p = corrected ? a_visual_pos : a_pos;\\n"\n'
    '    "  if (u_visual > 0.5 && a_visual_flags < -0.5 && a_visual_flags > -1.5) p.x = a_visual_pos.x;\\n"\n'
    '    "  if (placed) { p = vec2(a_visual_pos.x, a_pos.y); v_uv.x += a_q; v_limits.x -= 64; v_limits.z += 64; }\\n"\n'
    in renderer
    and renderer.count("a_visual_flags < -1.5") == 1
    and '"  v_bill = u_visual > 0.5 ? (a_visual_flags < -0.5 && a_visual_flags > -1.5 ? 0.75 : ' in renderer
    and '    "  float q = (u_visual > 0.5 && a_visual_flags > 1.5 && a_q > 0.0) ? a_q : 0.0;\\n"\n' in renderer,
    "only the mirror places a tile: it is put where it stands and never turned, its texture slides and may read its column "
    "past its own ends, it is no flat picture over the world, and its slide is no perspective weight",
)
require(
    "    int placed = !direct && s_geometry_correction && textured && v[flags_at] < -1.5f;  /* a backdrop tile, see tile_corner */\n"
    "    int flat = s_geometry_correction && textured && v[flags_at] < -0.5f && v[flags_at] > -1.5f;\n"
    "    float x = corrected || placed || flat ? v[pos_at] : v[0], y = corrected ? v[pos_at+1] : v[1];\n"
    "    const float want = placed ? v[pos_at+1] : 0.0f;  /* a tile's y slot: the slide its end wants, see tile_corner */\n"
    in presenter
    and "    if (textured && (!corrected || v[flags_at] <= 1.5f))\n        v[22] = 0;\n    if (placed) v[22] = want;\n"
    in presenter
    and presenter.index("    const float want = placed ? v[pos_at+1] : 0.0f;")
    < presenter.index("    v[0] = v[pos_at] = x; v[1] = v[pos_at+1] = y;\n")
    and "    if (placed) v[2] +=" not in presenter
    and "        if (textured && v[19] < -1.5f) {\n"
    "            s->backdrop=1; s->backdrop_origin=s_tile.origin; s->backdrop_period=s_tile.period;\n"
    "            s->backdrop_scale=s_tile.scale; s->backdrop_column=s_tile.column;\n        }\n"
    in presenter
    and "    s->active = 1; s->bad = 0; s->nv = s->nt = s->nc = 0;\n    s->backdrop = 0; s->backdrop_back = 0.0f;\n"
    in presenter
    and "    s->origin_y=y; s->height=h; s->matched=s->world=s->moving=0;\n    s->backdrop_back=0.0f;\n" in presenter
    and "        if (s->backdrop && s_temp_curr.backdrop)\n"
    "            s->backdrop_back=psx_ws_backdrop_back(s_temp_curr.backdrop_origin,s->backdrop_origin,s->backdrop_period,TEMP_BACKDROP_MOST);\n"
    in presenter
    and "#define TEMP_BACKDROP_MOST 12.0f" in presenter
    and "#define TEMP_BACKDROP_END_CARRY 2.0f" in presenter,
    "a recorded frame keeps the panorama's numbers, takes a tile where the mirror put it with the slide folded into its "
    "texture, and knows how far back the panorama stood in the frame before, bounded",
)
require(
    "        if (t && c->mode==GL_TRIANGLES && s->backdrop) for (int j=0;j+2<c->count;j+=3) {\n"
    "            const float moved=(1.0f-alpha)*s->backdrop_back;\n"
    in presenter
    and "            if (!(v[19]<-1.5f)) continue;\n" in presenter
    and "                if (corner[19]>-2.5f) { corner[0]=corner[20]=corner[0]+moved; continue; }\n"
    "                const int end=corner[19]<-3.5f;\n"
    "                const float room=psx_ws_screen_tile_room(corner[2],s->backdrop_column,end);\n"
    "                float carried=0.0f;\n"
    "                if (end ? moved>0.0f : moved<0.0f) {\n"
    "                    /* Past the texels its column has beyond the end the tile's texture would stretch. */\n"
    "                    const float spare=(room-(end ? corner[22] : -corner[22]))*s->backdrop_scale;\n"
    "                    const float extra=(TEMP_BACKDROP_END_CARRY-1.0f)*fabsf(moved);\n"
    "                    carried=moved+(moved>0.0f ? 1.0f : -1.0f)*(spare>0.0f ? fminf(extra,spare) : 0.0f);\n"
    "                }\n"
    "                corner[0]=corner[20]=corner[0]+carried;\n"
    "                corner[2]+=psx_ws_screen_tile_slide(0.0f,moved-carried-corner[22]*s->backdrop_scale,s->backdrop_scale,end,\n"
    "                                                    room,high-low>1.0f ? high-low-1.0f : 0.0f);\n"
    "                corner[22]=0.0f;\n"
    in presenter
    and '"  v_bill = a_visual_flags < -0.5 && a_visual_flags > -1.5 ? 0.75 : (a_visual_flags < 1.5 && a_order > -0.9999'
    in presenter
    and presenter.index("        if (t && c->mode==GL_TRIANGLES && s->backdrop)")
    > presenter.index("        memcpy(s_temp_scratch,s->vertices+c->first*TEXV,c->count*TEXV*sizeof(float));\n"),
    "an in-between frame shows the panorama on its way from the frame before: free edges move by the share of the way left, "
    "an end it carries outward goes further along, the other ends slide their texture by it",
)
require(
    "    return (x <= span_left ? 1u : 0u) | (x + w >= span_right ? 2u : 0u);\n" in tile_header
    and "    return centre + ((double)x - centre) * numerator / denominator;\n" in tile_header
    and "    return end ? ceilf(u / column) * column - u : u - floorf(u / column) * column;\n" in tile_header
    and "    const float slide = -(offset + moved) / scale;\n"
    "    const float low = end ? -room_in : -room_out, high = end ? room_out : room_in;\n"
    "    return slide < low ? low : slide > high ? high : slide;\n"
    in tile_header
    and "    back -= period * floorf(back / period + 0.5f);\n    return back > most ? most : back < -most ? -most : back;\n"
    in tile_header
    and "    return scale > 0.0f && isfinite(offset + moved) ? -(offset + moved) / scale : 0.0f;\n" in tile_header
    and "        *pins = match ? tile->pins : 0u;\n" in tile_header
    and "    target->pins = pins;\n" in tile_header,
    "the tile header holds the arithmetic the unit test runs: the pins, an unrounded edge, a column's room, the slide and "
    "the way back round the period",
)

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"Disruptor backdrop tile source/codegen contract: PASS ({checked})")
