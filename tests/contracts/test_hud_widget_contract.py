#!/usr/bin/env python3
"""Source contract for widescreen HUD widgets: the weapon list keeps one pivot."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start : source.index("\n}\n", start)]


cmake = read("CMakeLists.txt")
manifest = read("framework/FILES.txt")
gpu = read("psxrecomp/runtime/src/gpu.c")
header = read("psxrecomp/runtime/include/gpu.h")
module = read("src/game/render/hud_widgets.cpp")

require(
    "+runtime/include/gpu_ws_hud_widget.h" in manifest.splitlines(),
    "overlay manifest must list the HUD widget arithmetic",
)
require(
    "    disruptor_unit_test(disruptor_gpu_ws_hud_widget tests/unit/gpu_ws_hud_widget_test.cpp RUNTIME_HEADERS)\n"
    in cmake
    and "    disruptor_contract(hud_widget)\n" in cmake
    and '"${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/hud_widgets.cpp"' in cmake,
    "the widget unit test, its contract and the game's widget list must remain in the build",
)
require(
    "void gpu_ws_set_hud_widgets(uint32_t table_pointer, int layers, const int32_t *boxes, int count);" in header,
    "the widget API must stay public",
)

lookup = body(gpu, "static const PsxWsHudWidget *ws_hud_widget(")
require(
    "    if (!ws_active() || psx_ws_prim_is_tagged()) return NULL;\n"
    "    return psx_ws_hud_widgets_take(&ws_hud, x0, y0, x1, y1, rectangle);" in lookup,
    "only an untagged primitive in widescreen may ask for a widget, and the tested rule alone answers",
)
setter = body(gpu, "void gpu_ws_set_hud_widgets(")
require(
    "    ws_hud_table_pointer = table_pointer;\n    memset(&ws_hud, 0, sizeof(ws_hud));\n    ws_hud.layers = layers;\n"
    in setter
    and "        const PsxWsHudWidget widget = { box[0], box[1], box[2], box[3], box[4], box[5] };\n"
    "        ws_hud.widgets[ws_hud.count++] = widget;"
    in setter
    and "i < count && i < PSX_WS_HUD_WIDGET_MAX" in setter,
    "a game's widgets must replace the earlier ones whole, in the order of their six numbers, and never past the table",
)
begin = body(gpu, "void gpu_ws_begin_linked_list(void)")
node = body(gpu, "void gpu_set_gp0_linked_list_node(")
end = body(gpu, "void gpu_ws_end_linked_list(void)")
area = body(gpu, "static void gp0_exec_draw_area_tl(void)")
require(
    "    psx_ws_hud_widgets_list(&ws_hud, ws_hud.count && ws_active() ? psx_read_word(ws_hud_table_pointer) : 0u);"
    in begin
    and "    psx_ws_hud_widgets_list_end(&ws_hud);" in end,
    "every linked list must be begun with what the game's table word names now, or nothing outside widescreen, and ended",
)
require(
    "    if (word_count == 0) {\n" in node and "        psx_ws_hud_widgets_node(&ws_hud, addr);\n    }" in node,
    "an empty node, and nothing else, must open a layer",
)
require(
    "    psx_ws_hud_widgets_frame(&ws_hud);" in area,
    "a new drawing area must start a new frame for the widgets",
)
require(
    gpu.count("psx_ws_hud_widgets_list(") == 1
    and gpu.count("psx_ws_hud_widgets_list_end(") == 1
    and gpu.count("psx_ws_hud_widgets_node(") == 1
    and gpu.count("psx_ws_hud_widgets_frame(") == 1
    and gpu.count("psx_ws_hud_widgets_take(") == 1,
    "nothing else in the renderer may move the widgets' state",
)

rect = body(gpu, "static int ws_hud_widget_rect(")
quad = body(gpu, "static void ws_hud_widget_quad(")
require(
    rect == "static int ws_hud_widget_rect(int32_t *x, int32_t *y, int32_t *w, int32_t *h) {\n"
    "    const PsxWsHudWidget *widget = ws_hud_widget(*x, *y, *x + *w, *y + *h, 1);\n"
    "    if (!widget) return 0;\n"
    "    psx_ws_hud_widget_rect(widget, x, y, w, h, ws_xnum, ws_xden, ws_hud_scale_percent);\n"
    "    return 1;",
    "a widget's rectangle must be looked up by its own bounds and mapped in place, with the squash and the HUD size, and nothing more",
)
require(
    "    if (!ws_hud.in_hud_layer || !ws_axis_aligned_quad(vx, vy)) return;" in quad
    and "    const PsxWsHudWidget *widget = ws_hud_widget(min_x, min_y, max_x, max_y, 0);\n    if (!widget) return;"
    in quad
    and "        psx_ws_hud_widget_corner(widget, &vx[i], &vy[i], ws_xnum, ws_xden, ws_hud_scale_percent);" in quad,
    "only an axis-aligned quad wholly inside a widget may be mapped, corner by corner",
)

held = body(gpu, "static int ws_hud_held_rect(int32_t *x, int w) {")
scale_header = read("psxrecomp/runtime/include/gpu_ws_hud_scale.h")
require(
    "    if (!ws_hud.in_hud_layer || w <= 0 || ws_hud_held_page_x < 0 || texpage_x != (uint32_t)ws_hud_held_page_x ||\n"
    "        texpage_y != (uint32_t)ws_hud_held_page_y)\n        return 0;\n"
    in held
    and "    if (!ws_hud_held_seen) {\n        ws_hud_held_pivot = psx_ws_hud_held_pivot(*x, w, ws_disp_w());\n"
    "        ws_hud_held_seen = 1;\n    }\n"
    in held
    and "    PsxWsHeldPlace place;\n    ws_hud_held_sided = psx_ws_hud_held_place(*x, w, ws_hud_held_pivot, ws_xnum, ws_xden, &place);\n"
    "    ws_hud_held_sides[0] = place.sides[0];\n    ws_hud_held_sides[1] = place.sides[1];\n    *x = place.left;\n    return place.width;"
    in held
    and "ws_hud_user_scale" not in held
    and "ws_hud_scale_percent" not in held
    and "    gp0_ot_rank = 0xFFFFu;\n    ws_hud_held_seen = 0;\n" in body(gpu, "void gpu_ws_begin_linked_list(void) {")
    and gpu.count("ws_hud_held_seen = 0;") == 1
    and "static int ws_hud_held_page_x = -1, ws_hud_held_page_y = -1;" in gpu
    and "void gpu_ws_set_hud_held_page(int page_x, int page_y);" in header,
    "a sprite of the held weapon's texture page in a HUD layer keeps its size whatever the HUD size, and all of the "
    "weapon in one list is squashed about the column its first sprite gives",
)
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
temporal = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
rect = body(renderer, "static void gpu_textured_rect(int x,int y,int w,int h,")
textured = gpu[gpu.index("static void gp0_exec_textured_rect(void) {") :]
require(
    "static void ws_hud_held_sides_send(void) {\n    if (ws_hud_held_sided) gr_set_flat_sides(ws_hud_held_sides[0], ws_hud_held_sides[1]);\n"
    "    ws_hud_held_sided = 0;\n}\n"
    in gpu
    and gpu.count("    ws_hud_held_sides_send();\n") == 3
    and gpu.count("gr_set_flat_sides(") == 1
    and "    PsxWsPlacedTile placed = {0};\n    ws_hud_held_sided = 0;\n" in textured
    and "    *out_h = 0;\n    ws_hud_held_sided = 0;\n"
    in body(gpu, "static int ws_sprt_fixed_transform(int32_t *x0, int32_t *y0, int w, int *out_h) {")
    and textured.index("    if (draw_area_out_rect(x0, y0, dw, dh)) return;\n")
    < textured.index("    ws_hud_held_sides_send();\n")
    < textured.index("        gr_draw_textured_rect_scaled(x0, y0, dw, dh, u0, v0, u0 + w, v0 + h,")
    and "    if (whole[1] > whole[0]) return 1;\n    place->sides[0] = place->sides[1] = 0.0f;\n    return 0;\n"
    in scale_header
    and "static void glb_set_flat_sides(float left, float right) {\n"
    "    const int placed = s_raster_ok && isfinite(left + right) && fabsf(left) <= 1.0f && fabsf(right) <= 1.0f;\n"
    "    s_next_flat_sides[0] = placed ? left : 0.0f;\n    s_next_flat_sides[1] = placed ? right : 0.0f;\n}\n"
    in renderer
    and "    memcpy(s_flat_sides, s_next_flat_sides, sizeof(s_flat_sides));\n    memset(s_next_flat_sides, 0, sizeof(s_next_flat_sides));\n"
    in rect
    and "    if (w <= 0 || h <= 0) { s_sprite_depth = 0.0f; memset(s_flat_sides, 0, sizeof(s_flat_sides)); return; }\n"
    in rect
    and rect.rstrip().endswith("    memset(s_flat_sides, 0, sizeof(s_flat_sides));")
    and ": s_flat_sides[xs[i] == s_sprite_x ? 0 : 1]);" in renderer
    and "vp[20] = visual_flags || s_flat_sides[0] != 0.0f || s_flat_sides[1] != 0.0f ? px[cut_corners >= 0 ? 0 : i] : (float)xs[i];"
    in renderer
    and '    "  if (u_visual > 0.5 && a_visual_flags < -0.5 && a_visual_flags > -1.5) p.x = a_visual_pos.x;\\n"\n'
    in renderer
    and "    int flat = s_geometry_correction && textured && v[flags_at] < -0.5f && v[flags_at] > -1.5f;\n"
    "    float x = corrected || placed || flat ? v[pos_at] : v[0], y = corrected ? v[pos_at+1] : v[1];\n"
    in temporal
    and "void gr_set_flat_sides(float left, float right) {\n    if (g_b->set_flat_sides) g_b->set_flat_sides(left, right);\n}\n"
    in read("psxrecomp/runtime/src/gpu_render.c")
    and "    void (*set_flat_sides)(float left, float right);\n" in read("psxrecomp/runtime/include/gpu_render.h"),
    "a held weapon sprite's fractions reach the renderer right before its own draw and never another rectangle, and the "
    "mirror and the in-between frames, never the canonical picture, put its corners at their unrounded places",
)
require(
    "    if (w <= 0 || screen_w <= 0 || w >= screen_w) return screen_w / 2;\n" in scale_header
    and "    mirrored = centre < screen_w;\n" in scale_header
    and "    start = 12 * (int64_t)w <= 5 * (int64_t)screen_w ? 4 * (int64_t)screen_w : 3 * (int64_t)screen_w;"
    in scale_header
    and "    gone = 3 * centre - start;\n    if (gone <= 0) return screen_w / 2;\n" in scale_header
    and "    reach = 3 * (2 * (int64_t)screen_w - w) - start;\n    if (gone > reach) gone = reach;\n" in scale_header
    and "    centre = ((int64_t)screen_w * (reach + gone) + reach) / (2 * reach);\n" in scale_header
    and "    return (int32_t)(mirrored ? screen_w - centre : centre);\n" in scale_header,
    "the weapon's column is the centre in the middle third and slides to the edge the sprite comes to touch",
)
require(
    "constexpr int kHeldPage[2] = {15, 0};" in module
    and "    gpu_ws_set_hud_held_page(kHeldPage[0], kHeldPage[1]);\n" in module
    and module.count("gpu_ws_set_hud_held_page(") == 1,
    "the game must say that it keeps the held weapon's picture at the top of texture page 15",
)
fixed = body(gpu, "static int ws_sprt_fixed_transform(")
textured_rect = body(gpu, "static void gp0_exec_textured_rect(void)")
flat_quad = body(gpu, "static void gp0_exec_mono_quad(void)")
textured_quad = body(gpu, "static void gp0_exec_textured_quad(void)")
require(
    "    int32_t widget_w = w, widget_h = w;\n"
    "    const int held_w = ws_hud_held_rect(x0, w);\n"
    "    if (held_w) return held_w;\n"
    "    if (ws_hud_widget_rect(x0, y0, &widget_w, &widget_h)) {\n"
    "        *out_h = (int)widget_h;\n"
    "        return (int)widget_w;\n"
    "    }\n"
    "    int auto_w = w;"
    in fixed
    and fixed.index("ws_tagged_anchor(&ax)") < fixed.index("ws_hud_widget_rect("),
    "a fixed-size sprite must be tried as the held weapon and then as a widget, after the tag and before every other HUD rule",
)
require(
    "            int32_t widget_w = w, widget_h = h;\n"
    "            const int held_w = ws_hud_held_rect(&x0, w);\n"
    "            if (held_w) {\n"
    "                ws_w = held_w;\n"
    "            } else if (ws_hud_widget_rect(&x0, &y0, &widget_w, &widget_h)) {\n"
    "                ws_w = (int)widget_w;\n"
    "                ws_h = (int)widget_h;\n"
    "            } else if (ws_auto_ui_transform_rect(&x0, y0, &corrected_w, h))" in textured_rect,
    "a textured rectangle must be tried as the held weapon and then as a widget, before every other HUD rule",
)
require(
    flat_quad.count("ws_hud_widget_quad(vx, vy);") == 1
    and flat_quad.index("ws_expand_fullscreen_rect(")
    < flat_quad.index("ws_hud_widget_quad(vx, vy);")
    < flat_quad.index("vx[i] += draw_offset_x;"),
    "a flat quad must be mapped after the full-screen case and before the draw offset",
)
require(
    textured_quad.count("ws_hud_widget_quad(vx, vy);") == 1
    and textured_quad.index("ws_tagged_anchor(&ws_ax)")
    < textured_quad.index("ws_hud_widget_quad(vx, vy);")
    < textured_quad.index("ws_auto_ui_transform_quad(vx, vy);"),
    "a textured quad must be mapped after the tag and before the automatic UI pass",
)
require(
    gpu.count("ws_hud_widget_quad(") == 3
    and gpu.count("ws_hud_widget_rect(") == 4
    and gpu.count("psx_ws_hud_widget_rect(") == 1,
    "no other primitive may be mapped as part of a widget",
)

require(
    "constexpr uint32_t kTablePointer = 0x80071490u;" in module
    and "constexpr int kHudLayers = 1;" in module
    and "constexpr int32_t kPanels[2][6] = {{16, 10, 128, 196, 0, 0}, {232, 10, 298, 170, 320, 0}};" in module
    and "    gpu_ws_set_hud_widgets(kTablePointer, kHudLayers, kPanels[0], 2);" in module
    and module.count("gpu_ws_set_hud_widgets(") == 1
    and "constexpr int kTextStrip = 128;" in module
    and "    gpu_ws_set_hud_text_strip(kTextStrip);\n" in module
    and module.count("gpu_ws_set_hud_text_strip(") == 1,
    "the game must declare where its table is, its HUD node, the weapon list's box with a pivot in the top left corner, "
    "the psionics list's with one in the top right, and how wide its printer makes a line of text",
)

print("Disruptor HUD widget source contract: PASS")
