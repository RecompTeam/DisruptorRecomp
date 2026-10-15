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
    '    "    level = clamp((range - u_fade.x)/u_fade.y, 0.0, float(rows - 1));\\n"\n'
    '    "    g_row = v_clut.y - v_clut.y % rows + int(level);\\n"\n' in renderer
    and '    "  if (rows > 1 && v_fade.x > 0.0 && v_depth != 2) {\\n"\n' in renderer
    and '    "  int rows = int(u_fade.z + 0.5); float level = -1.0;\\n"\n' in renderer,
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
    and "    hr_begin(1);\n    p_glUseProgram(s_tex_prog);\n    smooth_fade_uniforms(s_tex_uFade, s_tex_uFadeAt, 1.0f);\n" in renderer
    and renderer.count("smooth_fade_uniforms(") == 2
    and "    s_fade_rows = rows; s_fade_start = start; s_fade_step = step > 0.0f ? step : 1.0f; s_fade_side = side;\n" in renderer,
    "the fade is off until it is switched on and the game has said its rows, and a textured batch is told its numbers with its own program bound",
)
require(
    '            "  v_fade = (a_visual_flags > 1.5 && a_order > -0.9999 && a_order < 0.9999) ? vec2((1.0 - a_order)/32.0, a_visual_pos.x) : vec2(-1.0, 0.0);\\n"\n'
    in presenter
    and presenter.count("smooth_fade_uniforms(u->fade, u->fade_at, s_temp_fade_nearer);") == 2
    and '    TEMP_U(fade,"u_fade"); TEMP_U(fade_at,"u_fade_at");\n' in presenter
    and "    if (!s->mesh_stats.departed_faces || p->width!=s->width || p->scale!=s->scale) return;\n"
    "    s_temp_fade_nearer=covered ? TEMP_ORDER_CLEARANCE : 1.0f;\n" in presenter
    and "    s_temp_scene_depth=1; s_temp_underlaying=0;\n    s_temp_fade_nearer=1.0f;\n}\n" in presenter
    and "    return depth>16.0f ? 1.0f-32.0f/depth : TEMP_ORDER_NEAR;\n" in presenter,
    "an in-between frame fades as the real ones do: its depth comes back out of the order it sorts by, and a carried face "
    "drawn nearer than its surface is faded by the surface's depth",
)
require(
    "    gl_renderer_set_palette_fade(12, 8.0f * static_cast<float>(psx_mod_read_word(kFadeStart)), 8.0f * 64.0f, 0.375f);\n" in arena
    and "constexpr uint32_t kFadeStart = 0x800715E0u, kFarDistance = 0x800715ECu;" in arena,
    "the game fades in twelve rows of 64 units of its range from the start it keeps in memory, and counts three eighths of the step aside: "
    "the renderer's depth is eight to a unit",
)
require(
    "    toggle<&Settings::has_smooth_fade, &Settings::smooth_fade>(\n"
    '        "disruptor.smooth_fade", "Smooth distance fade", 1,\n' in options
    and "The game itself steps through twelve shades.\", needs_geometry),\n" in options
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
require("    disruptor_contract(smooth_fade)\n" in cmake, "this contract must remain registered")

print("Disruptor smooth fade source contract: PASS")
