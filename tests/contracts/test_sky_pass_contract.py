#!/usr/bin/env python3
"""Source contract for the sky pass: the panorama drawn pixel by pixel from the level's cell table, as the game lays it or
turned with the world through its projection."""

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


renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
thread = read("psxrecomp/runtime/src/gpu_gl_render_thread.c.inc")
module = read("src/game/render/backdrop_tiles.cpp")
gpu = read("psxrecomp/runtime/src/gpu.c")

require(
    "    tell_panorama(cpu);\n" in module
    and "    if (now == g_cells_read_at) return;\n" in module
    and "    if (cells == g_cells_told) return;\n" in module
    and "    gpu_sky_panorama(cells.rows, cells.columns, sky::kPeriod, sky::kFocal, cells.xy.data());\n" in module
    and "    gr_set_sky_cells(rows, columns, period, focal, cells);\n" in gpu
    and "        if (rth_put(RTH_SKY_CELLS, 0, payload, sizeof payload)) return;\n" in thread
    and "        glb_set_sky_cells(sky[0], sky[1], sky[2], sky[3], sky + 4);\n" in thread
    and "    .set_sky_cells = glb_set_sky_cells,\n" in renderer,
    "the game tells the level's cell table once a frame and only when it changed, and it reaches the renderer through the "
    "render thread",
)

vertex = between(renderer, "static const char *TEX_VS =", "static const char *TEX_FS =")
require(
    '"  if (placed && u_sky.x > 0.5 && v_depth == 1) {\\n"' in vertex
    and '"    ivec2 at = ivec2(v_tpage.x + (u0 >> 1), v_tpage.y + w0);\\n"' in vertex
    and '"      if (at.x < cell.x || at.x >= cell.x + 32 || at.y < cell.y || at.y >= cell.y + 60) continue;\\n"'
    in vertex
    and "+ a_uv.x + a_q - float(u0)" in vertex
    and "p.x - u_sky_frame.x, p.y - u_sky_frame.y);" in vertex,
    "a placed tile's corner finds its cell from its first texel and tells the panorama coordinate it shows, slide included, "
    "and how far it stands from the projection centre",
)

sample = between(renderer, '"bool sky_sample(out vec3 rgb, out int stp){\\n"', '"const vec2 kAround[8]')
require(
    '"  u = int(mod(float(u), float(period))); v = clamp(v, 0, u_sky_shape.x * 60 - 1);' in renderer
    and '"  return vram_at(v_clut.x + ((px >> ((u & 1) * 8)) & 0xFF), g_row);\\n"' in renderer
    and '"    float phi = atan(v_sky.z / u_sky_frame.z);\\n"' in sample
    and '"    at.x += u_sky.y * phi - v_sky.z * u_sky.w / u_sky_frame.z;\\n"' in sample
    and '"  if (u_sky.x > 1.5 && u_sky_frame.z > 0.0) {\\n"' in sample
    and "at.y +=" not in sample
    and '"  if (v_sky_on != 0 ? !sky_sample(rgb, stp)\\n"' in renderer
    and '"    if (v_sky_on != 0 ? sky_sample(next, next_stp) : sample_texture(sample_uv, next, next_stp)) ' in renderer,
    "each sky pixel reads its texel from the cells with the tile's own palette row, wraps the panorama with a modulo that "
    "is defined for negatives, the fade's next palette row from the same texel, and the matched mode turns it across through the world's focal, its rows left as the game has them",
)

require(
    "    if (sky_drawn(pano) <= 0 || (int)floorf(depth + 0.5f) != 1) return 0;\n" in renderer
    and "(int)floorf(page_x + 0.5f) + ((int)floorf(u0 + 0.5f) >> 1)" in renderer
    and "        if (x >= pano->cells[2 * i] && x < pano->cells[2 * i] + 32 && y >= pano->cells[2 * i + 1] && "
    "y < pano->cells[2 * i + 1] + 60)\n"
    in renderer
    and "sky_finds(&s_sky, vp[8], vp[9], vp[14], vp[15], vp[12])" in renderer
    and "const int any=sky_finds(&s->sky,corner[8],corner[9],corner[14],corner[15],corner[12]);" in presenter
    and "sky_drawn(&s_sky) > 0;" not in renderer
    and "sky_drawn(&s->sky)>0;" not in presenter,
    "a pinned end slides past its column only when the vertex shader's own test draws the tile from the cells: the mirror and "
    "an in-between picture ask it of the tile's page, first texel and depth",
)

require(
    "    p_glUniform4f(sky, (float)mode, pano->texels_a_radian, pano->focal > 0.0f ? pano->texels_a_radian / pano->focal : 0.0f, pano->focal);\n"
    in renderer
    and "    if (*sent == pano->generation) return;\n" in renderer
    and "                     (float)g_wide_cur_base + s_yaw_center_x, (float)s_off_y + s_yaw_center_y_relative, s_yaw_focal_x);\n"
    in renderer
    and "    s_sky.texels_a_radian = count ? (float)(period / 6.28318530717958647692) : 0.0f;\n" in renderer
    and "    return forced >= 0 ? forced : 1;  /* 2 turns the sky with the world: stretched 2.2x at the 16:9 edge, kept for a skybox */\n"
    in renderer
    and "sky_perspective" not in read("src/game/settings/options.cpp") + renderer,
    "the mirror gets the panorama with the world's own centre and focal, its cells again only when the table changed, and "
    "the game's own lay is the mode, the world-matched one only through PSX_SKY_MAP=2 and in no menu",
)

require(
    "            s->sky=s_sky;\n" in presenter
    and "    s->sky_frame[1]-=y;  /* the horizon in the band's rows, as the vertices */\n" in presenter
    and presenter.count(
        "    if (s->backdrop) sky_uniforms(u->sky,u->sky_frame,u->sky_cells,u->sky_shape,&u->sky_sent,&s->sky,"
    )
    == 3
    and "                const float room=any ? 1.0e6f : psx_ws_screen_tile_room(corner[2],s->backdrop_column,end);\n"
    in presenter
    and "        *slide = psx_ws_screen_tile_slide(offset, s_tile.moved, s_tile.scale, end,\n"
    "                                          any ? 1.0e6f : psx_ws_screen_tile_room(u, s_tile.column, end),\n"
    in renderer,
    "an in-between picture draws the panorama from the frame's own copy, its horizon in the band's rows as its vertices, "
    "and with the pass on no column bounds a pinned end's slide",
)

print("Disruptor sky pass source contract: PASS")
