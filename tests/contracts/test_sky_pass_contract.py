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
    and "    const bool changed = !(cells == g_cells_told);\n" in module
    and "        gpu_sky_panorama(cells.rows, cells.columns, cells.tall, sky::kPeriod, sky::kFocal, cells.xy.data(), "
    "cells.irregular ? 1 : 0);\n"
    in module
    and "    gr_set_sky_cells(rows, columns, tall, period, focal, cells, irregular);\n" in gpu
    and "        if (rth_put(RTH_SKY_CELLS, 0, payload, sizeof payload)) return;\n" in thread
    and "        glb_set_sky_cells(sky[0], sky[1], sky[2], sky[3], sky[4], sky + 6, sky[5]);\n" in thread
    and "    s_sky.irregular = count && irregular;\n" in renderer
    and "    s_sky.tall = count ? tall : 0;\n" in renderer
    and "    .set_sky_cells = glb_set_sky_cells,\n" in renderer,
    "the game tells the level's cell table once a frame and only when it changed, its cells' rows and whether irregular, and it "
    "reaches the renderer through the render thread",
)

vertex = between(renderer, "static const char *TEX_VS =", "static const char *TEX_FS =")
require(
    '"  if ((placed || tilted) && u_sky.x > 0.5 && v_depth == 1 && (u_sky_irregular == 0 || (tilted && want >= 0))) {\\n"'
    in vertex
    and '"  int want = int(a_sky_column + 0.5) - 1;\\n"' in vertex
    and '"  bool tilted = u_sky_frame.w != 0.0;\\n"' in vertex
    and '"      if (i == k ? want >= 0 && k % u_sky_shape.y != want : want < 0 || u_sky_irregular != 0) continue;'
    in vertex
    and '"    ivec2 at = ivec2(v_tpage.x + (u0 >> 1), v_tpage.y + w0);\\n"' in vertex
    and '"      if (at.x < cell.x || at.x >= cell.x + 32 || at.y < cell.y || at.y >= cell.y + u_sky_shape.z) continue;\\n"'
    in vertex
    and '"    float frame_row = p.y - u_sky_frame.y + 120.0 + u_sky_frame.w, nearest = 1.0e9;\\n"' in vertex
    and '"    for (int i = 0; i < 2 * n && (i < n || v_sky_on == 0); ++i) {\\n"' in vertex
    and '"      if (v_sky_on != 0 && abs(found.y - frame_row) >= nearest) continue;\\n"' in vertex
    and "float((k / u_sky_shape.y) * u_sky_shape.z + at.y - cell.y) + a_uv.y - float(w0)" in vertex
    and "+ a_uv.x + (placed ? a_q : 0.0) - float(u0)" in vertex
    and "p.y - u_sky_frame.y);" in vertex,
    "a placed tile's corner, or with the horizon tilted any sky tile's, finds its cell from its first texel in the column the tile "
    "tells first, an irregular table's only there and only tilted, a block the column holds twice by the frame row it stands at, and "
    "tells the panorama coordinate it shows, a placed one's slide included, and how far it stands from the projection centre",
)

texel = between(renderer, '"int sky_texel(int u, int v, bool grown){\\n"', '"vec3 sky_cap(int u){\\n"')
sample = between(renderer, '"bool sky_sample(out vec3 rgb, out int stp){\\n"', '"const vec2 kAround[8]')
require(
    '"  u = int(mod(float(u), float(period)));  /* % of a negative is undefined in GLSL */\\n"' in texel
    and '"  if (u_sky_grown.x > 0 && v < (grown ? u_sky_grown.y : 0))\\n"' in texel
    and "texelFetch(u_sky_ext, ivec2(u, max(u_sky_grown.x - u_sky_grown.y + v, 0)), 0).r" in texel
    and '"  v = clamp(v, 0, u_sky_shape.x * u_sky_shape.z - 1);\\n"' in texel
    and '"  return vram_at(v_clut.x + ((px >> ((u & 1) * 8)) & 0xFF), g_row);\\n"' in texel
    and '"    float phi = atan(v_sky.z / u_sky_frame.z);\\n"' in sample
    and '"    at.x += u_sky.y * phi - v_sky.z * u_sky.w / u_sky_frame.z;\\n"' in sample
    and '"  if (u_sky.x > 1.5 && u_sky_frame.z > 0.0) {\\n"' in sample
    and '"  vec2 at = vec2(v_sky.x, v_sky.y - u_sky_frame.w);  /* the vertical look moves the sky with the horizon, whole */\\n"'
    in sample
    and '"  if (at.y < 0.0 && u_sky_grown.x == 0) {  /* above the top while no sky is grown there yet: its top rows\' colour */\\n"'
    in sample
    and "clamp(u_sky_frame.w / 16.0, 0.0, 1.0)" in sample
    and '"  rgb = !has_band ? grown : !has_grown ? band : mix(band, grown, given);\\n"' in sample
    and "120.0 + u_sky_frame.w)" not in sample
    and '"  if (v_sky_on != 0 ? !sky_sample(rgb, stp)\\n"' in renderer
    and '"    if (v_sky_on != 0 ? sky_sample(next, next_stp) : sample_texture(sample_uv, next, next_stp)) ' in renderer,
    "each sky pixel reads its texel from the cells with the tile's own palette row, wraps the panorama with a modulo that is "
    "defined for negatives, the fade's next palette row from the same texel, and the matched mode turns it across through the world's "
    "focal: the vertical look moves the whole sky with the horizon, never stretched, above its top the sky grown from it, its top band "
    "giving way to that over the first 16 rows of the look, and its top rows' colour until it is grown",
)

require(
    "    if (sky_drawn(pano) <= 0 || (int)floorf(depth + 0.5f) != 1) return 0;\n" in renderer
    and "(int)floorf(page_x + 0.5f) + ((int)floorf(u0 + 0.5f) >> 1)" in renderer
    and "            x >= pano->cells[2 * i] && x < pano->cells[2 * i] + 32 && y >= pano->cells[2 * i + 1] && "
    "y < pano->cells[2 * i + 1] + pano->tall)\n"
    in renderer
    and "sky_finds(&s_sky, vp[8], vp[9], vp[14], vp[15], vp[12], s_yaw_center_y_relative - 120.0f, s_sky_column)"
    in renderer
    and "const int any=sky_finds(&s->sky,corner[8],corner[9],corner[14],corner[15],corner[12],s->sky_frame[3]+s_temp_sky_moved,corner[27]);"
    in presenter
    and "    if (pano->irregular && (shift == 0.0f || want < 0)) return 0;" in renderer
    and "        if ((!pano->irregular || i % pano->columns == want) &&\n" in renderer
    and "sky_drawn(&s_sky) > 0;" not in renderer
    and "sky_drawn(&s->sky)>0;" not in presenter,
    "a pinned end slides past its column only when the vertex shader's own test draws the tile from the cells, an irregular table's "
    "by the tile's column and tilted: the mirror and an in-between picture ask it of the tile's page, first texel and depth",
)

require(
    "    p_glUniform4f(sky, (float)mode, pano->texels_a_radian, pano->focal > 0.0f ? pano->texels_a_radian / pano->focal : 0.0f, pano->focal);\n"
    in renderer
    and "    if (*sent == pano->generation) return;\n" in renderer
    and "                     (float)g_wide_cur_base + s_yaw_center_x, (float)s_off_y + s_yaw_center_y_relative, s_yaw_focal_x,\n"
    "                     s_yaw_center_y_relative - 120.0f);\n"
    in renderer
    and "    sky_uniforms(s_tex_uSky, s_tex_uSkyFrame, s_tex_uSkyCells, s_tex_uSkyShape, s_tex_uSkyIrregular, s_tex_uSkyGrown, "
    "&s_tex_sky_generation, &s_sky, s_yaw_center_x,\n"
    "                 (float)s_off_y + s_yaw_center_y_relative, s_yaw_focal_x, s_yaw_center_y_relative - 120.0f);\n"
    in renderer
    and "    p_glUniform2i(grown, pano->ext_rows, pano->ext_band);\n" in renderer
    and "    p_glUniform4i(shape, pano->rows, pano->columns, pano->tall, 0);\n" in renderer
    and "    p_glUniform1i(irregular, pano->irregular);\n" in renderer
    and "    s_sky.texels_a_radian = count ? (float)(period / 6.28318530717958647692) : 0.0f;\n" in renderer
    and "    return forced >= 0 ? forced : 1;  /* 2 turns the sky with the world: stretched 2.2x at the 16:9 edge, kept for a skybox */\n"
    in renderer
    and "sky_perspective" not in read("src/game/settings/options.cpp") + renderer,
    "the mirror and the game's own frame get the panorama with the world's own centre and focal and the horizon's shift, its cells "
    "again only when the table changed, and "
    "the game's own lay is the mode, the world-matched one only through PSX_SKY_MAP=2 and in no menu",
)

require(
    "            s->sky=s_sky;\n" in presenter
    and "    s->sky_frame[1]-=y;  /* the horizon in the band's rows, as the vertices */\n" in presenter
    and presenter.count(
        "    if (s->sky_known) sky_uniforms(u->sky,u->sky_frame,u->sky_cells,u->sky_shape,u->sky_irregular,u->sky_grown,&u->sky_sent,&s->sky,"
    )
    == 3
    and presenter.count("                                 s->sky_frame[3]+s_temp_sky_moved);") == 3
    and "s->sky_frame[3]=s_yaw_center_y_relative-120.0f;" in presenter
    and "        s->sky_back=a->sky_known && s->sky_known ? a->sky_shift-s->sky_frame[3] : 0.0f;" in presenter
    and "    a->sky_known=s_temp_curr.sky_known;\n" in presenter
    and "s->sky_known = 0; s->sky_frame[3] = 0.0f;\n" in presenter
    and "    s_temp_sky_moved=(1.0f-alpha)*s->sky_back;\n" in presenter
    and "        if (textured && (v[19] < -1.5f || !s->sky_known)) {" in presenter
    and "                const float room=any ? 1.0e6f : psx_ws_screen_tile_room(corner[2],s->backdrop_column,end);\n"
    in presenter
    and "        *slide = psx_ws_screen_tile_slide(offset, s_tile.moved, s_tile.scale, end,\n"
    "                                          any ? 1.0e6f : psx_ws_screen_tile_room(u, s_tile.column, end),\n"
    in renderer,
    "an in-between picture draws the panorama from the frame's own copy, a 4:3 frame's too, its horizon in the band's rows as its "
    "vertices, the vertical look's shift on its way from the frame before, "
    "and with the pass on no column bounds a pinned end's slide",
)

grower = read("src/game/render/sky_grower.cpp")
require(
    "    if (cells.rows == 0) return;\n" in module
    and "    disruptor::sky_grower::look(cells, clut_x, clut_y, changed, now);\n" in module
    and "constexpr uint32_t kRecheck = 60;\n" in grower
    and "            Grown grown{job.revision, extension::grow(job.picture, job.palette, extension::kRows, job.revision)};\n"
    in grower
    and "                gpu_sky_extension(0, 0, 0, nullptr);\n                g_grower.ask({revision, std::move(picture), palette});\n"
    in grower
    and "    if (std::optional<Grown> grown = g_grower.take(); grown && grown->revision == g_revision && grown->made.rows > 0)\n"
    "        gpu_sky_extension(grown->made.width, grown->made.rows, grown->made.band, grown->made.indices.data());\n"
    in grower
    and "    gr_set_sky_extension(width, rows, band, indices);\n" in gpu
    and "        glb_set_sky_extension(head[0], head[1], head[2], (const uint8_t *)payload + 12);\n" in thread
    and "    const GLuint texture = make_tex(PSXGL_R8UI, width, rows, PSXGL_RED_INTEGER, GL_UNSIGNED_BYTE);\n"
    in renderer
    and "    glFinish();  /* once a level: another context sees an object's change only after it is complete there */\n"
    in renderer
    and "        if (s_sky_ext_retired) glDeleteTextures(1, &s_sky_ext_retired);\n        s_sky_ext_retired = s_sky.ext_tex;\n"
    in renderer
    and "glTexImage2D(GL_TEXTURE_2D, 0, (GLint)PSXGL_R8UI" not in renderer
    and "    ++s_sky.generation;\n    if (width <= 0 || width > 4096" in renderer
    and "    *sent = pano->generation;\n    if (pano->ext_rows > 0) {" in renderer
    and "        glBindTexture(GL_TEXTURE_2D, pano->ext_tex);\n"
    in between(renderer, "static void sky_uniforms(", "static int sky_finds(")
    and "    if (width <= 0 || width > 4096 || rows <= 0 || rows > 512 || band < 0 || band >= rows || !indices) return;\n"
    in renderer
    and '    p_glUniform1i(p_glGetUniformLocation(s_tex_prog, "u_sky_ext"), SKY_EXT_UNIT);\n' in renderer
    and '    p_glUniform1i(p_glGetUniformLocation(p,"u_sky_ext"),SKY_EXT_UNIT);\n' in presenter
    and '    s_tex_uSkyGrown = p_glGetUniformLocation(s_tex_prog, "u_sky_grown");\n' in renderer
    and '    s_tex_uSkyShape = p_glGetUniformLocation(s_tex_prog, "u_sky_shape");\n' in renderer
    and 'TEMP_U(sky_irregular,"u_sky_irregular"); TEMP_U(sky_grown,"u_sky_grown");' in presenter
    and 'TEMP_U(sky_shape,"u_sky_shape");' in presenter,
    "the sky above the panorama is grown on a worker from the level's own indices and palette, seeded by them, asked again only "
    "for a new revision and dropped meanwhile, and reaches the renderer as an index texture through the render thread, a texture of "
    "its own kept by each scene, finished before use and freed a level later, that every context binds on its own unit when the "
    "panorama's generation changes, the in-between presenter's included",
)

print("Disruptor sky pass source contract: PASS")
