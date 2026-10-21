#!/usr/bin/env python3
"""Source contract for the experimental smooth distance fade: the law in the mirror's shaders, the presenter's copy, the setting."""

from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
arena = read("src/game/render/render_arena.cpp")
menu = menu_text(ROOT)
enhancements = read("src/game/ui/menu/tab_enhancements.cpp")
options = read("src/game/settings/options.cpp")
saved = read("src/game/settings/saved.cpp")
launcher = read("src/launcher/launcher_settings.cpp")

require(
    '    "    float range = (1.0 + u_fade.w*abs(v_fade.y - u_fade_at.x)/u_fade_at.y)/(v_fade.x*u_fade_at.z);\\n"\n'
    '    "    float into = (range - u_fade.x)/(u_fade.y*float(rows));\\n"\n'
    '    "    level = clamp(float(rows)*(into > 0.0 ? pow(min(into, 1.0), max(u_fade_power, 1.0)) : into), 0.0, float(rows - 1));\\n"\n'
    '    "    if (u_dissolve.y > 0.5) dissolve = smoothstep(u_dissolve.x, u_dissolve.w, 1.0/(v_fade.x*u_fade_at.z));\\n"\n'
    '    "    g_row = v_clut.y - v_clut.y % rows + int(level);\\n"\n' in renderer
    and '    "  if (rows > 1 && v_fade.x > 0.0 && v_depth != 2) {\\n"\n' in renderer
    and '    "  int rows = int(u_fade.z + 0.5); float level = -1.0, dissolve = 0.0;\\n"\n' in renderer,
    "a pixel's fade level is its range past the fade's start in rows: the range is the depth, more by a share of the step aside, "
    "and the palette's block of rows begins at a multiple of their count",
)
require(
    '    "  if (!sample_texture(sample_uv, rgb, stp)) discard;\\n"\n'
    '    "  if (level >= 0.0 && int(level) < rows - 1) {\\n"\n'
    '    "    vec3 next; int next_stp; g_row += 1;\\n"\n'
    '    "    if (sample_texture(sample_uv, next, next_stp)) rgb = mix(rgb, next, fract(level));\\n"\n'
    '    "  }\\n"\n' in renderer
    and '    "  g_row = v_clut.y;\\n"\n' in renderer
    and renderer.count("), g_row);\\n") == 2
    and "v_clut.y);\\n" not in renderer,
    "the texel is taken from the row of the level and mixed with the next row by the level's fraction, and a triangle "
    "with no fade is sampled from its own row as before",
)
require(
    '    static const char fading[] = "  v_fade = (u_visual > 0.5 && abs(a_camz) > 16.0 && a_visual_flags > 1.5) ? vec2(1.0/abs(a_camz), a_pos.x) : vec2(-1.0, 0.0);\\n";\n'
    in renderer
    and "depth_writing_shader(GEO_VS, 4, 0), *tex_vs = depth_writing_shader(TEX_VS, 12, 1);" in renderer
    and '    "void main(){ v_uv = a_uv; v_col = a_col; v_fade = vec2(-1.0, 0.0);\\n"\n' in renderer,
    "only the display mirror fades, and only the world's own triangles with a depth: the game's picture and a sprite keep the row the game chose",
)
require(
    "static int s_smooth_fade = 0, s_fade_rows = 0;\n" in renderer
    and "    p_glUniform4f(fade, s_fade_start, s_fade_step, s_smooth_fade ? (float)s_fade_rows : 0.0f, s_fade_side);\n" in renderer
    and "    p_glUniform4f(at, (float)g_wide_cur_base + s_yaw_center_x, s_yaw_center_x > 0.0f ? s_yaw_center_x : 160.0f, nearer, s_uv_view ? 1.0f : 0.0f);\n" in renderer
    and "    hr_begin(1);\n    p_glUseProgram(s_tex_prog);\n    smooth_fade_uniforms(s_tex_uFade, s_tex_uFadeAt, s_tex_uFadePower, 1.0f);\n    edge_dissolve_batch(s_tex_uDissolve, s_tex_uClear, semi, s_mask_check);\n" in renderer
    and renderer.count("smooth_fade_uniforms(") == 2
    and "    s_fade_rows = rows; s_fade_start = start; s_fade_step = step > 0.0f ? step : 1.0f; s_fade_side = side;\n" in renderer,
    "the fade is off until it is switched on and the game has said its rows, and a textured batch is told its numbers with its own program bound",
)
require(
    '            "  v_fade = (a_visual_flags > 1.5 && a_order > -0.9999 && a_order < 0.9999) ? vec2((1.0 - a_order)/32.0, a_visual_pos.x) : vec2(-1.0, 0.0);\\n"\n'
    in presenter
    and presenter.count("smooth_fade_uniforms(u->fade, u->fade_at, u->fade_power, s_temp_fade_nearer);") == 2
    and '    TEMP_U(fade,"u_fade"); TEMP_U(fade_at,"u_fade_at"); TEMP_U(fade_power,"u_fade_power"); TEMP_U(dissolve,"u_dissolve"); TEMP_U(clear,"u_clear");\n' in presenter
    and "    if (!s->mesh_stats.departed_faces || p->width!=s->width || p->scale!=s->scale) return;\n"
    "    s_temp_fade_nearer=covered ? TEMP_ORDER_CLEARANCE : 1.0f;\n" in presenter
    and "    s_temp_scene_depth=1; s_temp_underlaying=0;\n    s_temp_fade_nearer=1.0f;\n}\n" in presenter
    and "    return depth>16.0f ? 1.0f-32.0f/depth : TEMP_ORDER_NEAR;\n" in presenter,
    "an in-between frame fades as the real ones do: its depth comes back out of the order it sorts by, and a carried face "
    "drawn nearer than its surface is faded by the surface's depth",
)
require(
    "    disruptor_far_rendering_drawn_distances(&far_distance, &fade_start);\n" in arena
    and "    disruptor_far_rendering_fog_ramp(&ramp_start, &ramp_length, &ramp_power);\n" in arena
    and "    gl_renderer_set_palette_fade(12, 8.0f * static_cast<float>(ramp_start), 8.0f * static_cast<float>(ramp_length) / 12.0f, 0.375f);\n" in arena,
    "the game fades in twelve rows of 64 units of its range from the start its renderer gets, its own or the draw distance's, and counts "
    "three eighths of the step aside: "
    "the renderer's depth is eight to a unit",
)
require(
    "    toggle<&Settings::has_smooth_fade, &Settings::smooth_fade>(\n"
    '        "disruptor.smooth_fade", "Smooth distance fade", 1,\n' in options
    and "The world's far edge dissolves instead of appearing in squares.\", needs_geometry),\n" in options
    and '        if (d.contains("smooth_fade")) try_get([&]{\n            s.smooth_fade = toml::find<bool>(d, "smooth_fade");\n            s.has_smooth_fade = true;\n'
    in saved
    and '        if (s.has_smooth_fade)\n            f << "smooth_fade = " << (s.smooth_fade ? "true" : "false") << "\\n";\n' in saved
    and "s.has_occlusion_strength || s.has_smooth_fade ||" in saved
    and '    {"disruptor.smooth_fade", Page::enhancements},\n' in launcher,
    "the fade is a saved setting that starts on, needs geometry correction and stands on the launcher's Enhancements page",
)
require(
    '    const bool fade_applies = applies("disruptor.smooth_fade");\n'
    "    if (!fade_applies) ImGui::BeginDisabled();\n"
    '    if (option_toggle("disruptor.smooth_fade", &smooth_fade)) {\n'
    "        gl_renderer_set_smooth_fade(smooth_fade ? 1 : 0);\n"
    "        mark_smooth_fade(smooth_fade);\n"
    "    }\n"
    "    if (!fade_applies) ImGui::EndDisabled();\n" in enhancements
    and "smooth_fade" not in read("src/game/ui/menu/tab_experimental.cpp")
    and "    if (settings.has_smooth_fade) gl_renderer_set_smooth_fade(settings.smooth_fade ? 1 : 0);\n" in menu
    and '    if (changed("disruptor.smooth_fade"))\n        gl_renderer_set_smooth_fade(pending.smooth_fade ? 1 : 0);\n' in menu
    and menu.count('option_toggle("disruptor.smooth_fade"') == 1,
    "the Experimental tab switches the fade at once and stages it for saving, and a start with the setting saved applies it",
)
require(
    '    "uniform vec4 u_dissolve;' in renderer
    and '    s_tex_uDissolve = p_glGetUniformLocation(s_tex_prog, "u_dissolve");\n    s_tex_uClear = p_glGetUniformLocation(s_tex_prog, "u_clear");\n' in renderer
    and '    "uniform vec4 u_clear;' in renderer
    and '    "  if (dissolve > 0.0 && u_dissolve.z < 0.5) {\\n"\n'
    '    "    vec3 last; int last_stp; int row = g_row; g_row = v_clut.y - v_clut.y % rows + rows - 1;\\n"\n'
    '    "    bool same = u_clear.w > 0.5 && sample_texture(sample_uv, last, last_stp);\\n"\n'
    '    "    g_row = row;\\n"\n'
    '    "    if (same && (v_raw & 1) == 0) last = clamp(last * v_col.rgb * 2.0, 0.0, 1.0);\\n"\n'
    '    "    if (!same || any(greaterThan(abs(last - u_clear.rgb), vec3(1.5/31.0)))) dissolve = 0.0;\\n"\n' in renderer
    and renderer.index('"  if (dissolve > 0.0 && u_dissolve.z < 0.5) {') > renderer.index('"  if ((v_raw & 1) == 0) rgb = clamp(rgb * v_col.rgb * 2.0, 0.0, 1.0);\\n"')
    and renderer.index('"  if (dissolve > 0.0 && u_dissolve.z < 0.5) {') < renderer.index('"  if (dissolve > 0.0 && dst_factor == 0.0) {')
    and "    if (w >= EDGE_CLEAR_WIDTH && h >= EDGE_CLEAR_HEIGHT && x <= s_area_x1 && y <= s_area_y1 && x + w - 1 >= s_area_x2 &&\n        y + h - 1 >= s_area_y2) {\n        s_clear_rgb[0] = r; s_clear_rgb[1] = g; s_clear_rgb[2] = b;\n        s_clear_known = 1;\n    }\n" in renderer
    and renderer.count("s_clear_known = 1;") == 1 and renderer.count("s_clear_rgb[0] = ") == 1
    and "#define EDGE_DISSOLVE_BAND 512.0f " in renderer and "EDGE_DISSOLVE_FROM" not in renderer
    and "#define EDGE_CLEAR_WIDTH 256 " in renderer and "#define EDGE_CLEAR_HEIGHT 192\n" in renderer
    and '    "  if (dissolve > 0.0 && dst_factor == 0.0) {  /* where the sky or a background like the fog shows through: no filler, its colour thinned, its depth kept */\\n"\n'
    '    "    if (u_visual_options.w > 0.0) discard;\\n"\n'
    '    "    if (u_semipass == 0) { rgb *= 1.0 - dissolve; dst_factor = dissolve; }\\n"\n' in renderer
    and "u_semipass >= 3) discard" not in renderer
    and renderer.index('"  if (dissolve > 0.0 && dst_factor == 0.0) {') > renderer.index('"  if (u_semimode == 4 && v_semi != 0 && stp != 0) {\\n"')
    and "void gl_renderer_set_edge_sky(int sky) { s_edge_sky = sky != 0; }\n" in renderer
    and "void gl_renderer_set_edge_depth(float depth) { s_edge_depth = depth; }\n" in renderer
    and "    return s_smooth_fade && (s_edge_sky || s_clear_known) && s_fade_rows > 1 && s_edge_depth > EDGE_DISSOLVE_BAND && !s_uv_view;\n" in renderer
    and "    if (!edge_dissolving()) { glDisable(GL_BLEND); return; }\n    glEnable(GL_BLEND);\n" in renderer
    and "    p_glBlendFuncSeparate(GL_ONE, PSXGL_SRC1_ALPHA, GL_ONE, GL_ZERO);\n}\n\n/* Draw the queued textured batch" in renderer
    and "static int batch_dissolves(int semi, int check) { return edge_dissolving() && (semi < 0 || (semi == 4 && !check)); }\n" in renderer
    and "    p_glUniform4f(dissolve, s_edge_depth - EDGE_DISSOLVE_BAND, batch_dissolves(semi, check) ? 1.0f : 0.0f, s_edge_sky ? 1.0f : 0.0f,\n                  s_edge_depth);\n" in renderer
    and "    p_glUniform4f(clear, s_clear_rgb[0], s_clear_rgb[1], s_clear_rgb[2], s_clear_known ? 1.0f : 0.0f);\n" in renderer
    and "temporal_colour_without_depth" not in presenter
    and presenter.count("edge_dissolve_batch(u->dissolve,u->clear,c->semi,c->check);") == 2
    and "    if (t) smooth_fade_uniforms(u->fade, u->fade_at, u->fade_power, s_temp_fade_nearer);\n    if (t) edge_dissolve_batch(u->dissolve,u->clear,c->semi,c->check);\n" in presenter
    and "    smooth_fade_uniforms(u->fade, u->fade_at, u->fade_power, s_temp_fade_nearer);\n    edge_dissolve_batch(u->dissolve,u->clear,c->semi,c->check);\n" in presenter
    and "    if (semi < 0) {\n        opaque_blend();\n" in renderer
    and presenter.count("opaque_blend()") == 3
    and "    } else if (c->semi<0) {\n        opaque_blend(); temporal_mask(c->check,c->mask);\n        p_glUniform1i(u->pass,0);" in presenter
    and "    gl_renderer_set_edge_sky(disruptor_backdrop_tiles_sky_behind(psx_mod_read_word(kVBlanks)));\n" in read("src/game/render/render_arena.cpp")
    and "    gl_renderer_set_edge_depth(8.0f * static_cast<float>(far_distance + kCullReach));\n" in read("src/game/render/render_arena.cpp")
    and "constexpr uint32_t kCullReach = 160u;" in read("src/game/render/render_arena.cpp")
    and "            if (g_sky_seen) g_sky_at = cpu->read_word(kVBlanks);\n" in read("src/game/render/backdrop_tiles.cpp")
    and "constexpr uint32_t kSkyVBlanks = 300u;" in read("src/game/render/backdrop_tiles.cpp"),
    "under the smooth fade the world dissolves over the last 64 units before the depth the game drops whole polygons at (far + 160, "
    "the drawn far, so the edge margin does not move it) while the game draws the sky behind it (a sky tile "
    "in the last five seconds), or where its texture's last fog shade is within a step of the frame's background (the last "
    "fill over the whole drawing area): what shows through is that background or farther fogged geometry. An opaque pixel is thinned by the second blend "
    "output in every pass, a batch only where its colour pass blends so; its fillers and outlines are dropped, its depth kept, "
    "for the void fill and the rim eraser read depth as geometry",
)
require("    disruptor_contract(smooth_fade)\n" in cmake, "this contract must remain registered")

print("Disruptor smooth fade source contract: PASS")
