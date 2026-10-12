#!/usr/bin/env python3
"""Source contract for gap filling: what a filler is, how the mirror draws one under its batch, and the setting."""

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
renderer = read("psxrecomp-overlay/runtime/src/gpu_gl_renderer.c")
menu = read("src/game/ui/dev_menu.cpp")
launcher = read("src/launcher/launcher_settings.cpp")
options = read("src/game/settings/options.cpp")
STAGED = (
    "void mark(const char *key, int value) {\n"
    "    const Option *option = find_option(key);\n"
    "    option->set(g_preferences.pending, value);\n"
    "    g_preferences.changed.insert(option);\n"
    "}\n"
)
MERGED = (
    "    for (const Option *option : g_preferences.changed)\n"
    "        option->set(settings, option->get(pending));\n"
)
loader = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")

require("    disruptor_contract(gap_filling)\n" in cmake, "this contract is registered")

require(
    "    const int filler = s_grown_under && exact && s_next_world == 2;\n" in renderer
    and "    return (world || sprite || unpinned ? 1 : 0) | (exact ? 2 : 0) | (filler ? 4 : 0);\n" in renderer
    and "            s_tb_fillers = 0;\n" in renderer
    and "        if (visual_flags & 4) s_tb_fillers++;\n" in renderer
    and "#define GROWN_COPY_MARGIN 0.03f\n" in renderer
    and "static float s_crack_fill = 0.5f, s_crack_width = 0.5f;\n" in renderer
    and "static int   s_grown_under = 1;\n" in renderer,
    "the game's grown copy of an exact polygon is flagged as a filler and counted in its batch, and both fillers start on",
)

require(
    """static void filler_pass_begin(float pass) {
    s_fill_pass = pass;
    geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);
}
static void filler_pass_end(void) {
    s_fill_pass = 0.0f;
    geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 1);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0x01);
    glDepthFunc(GL_ALWAYS);
}
""" in renderer
    and """    if (s_fill_pass > 0.0f) {  /* a filler obeys the mask and leaves it as it is */
        if (!s_mask_check) { glDisable(GL_STENCIL_TEST); return; }
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x00);
        glStencilFunc(GL_EQUAL, 0, 0x01);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        return;
    }
""" in renderer,
    "a filler pass draws colour alone where the kept depth is farther, and leaves the mask bit, the stencil and the depth as "
    "it found them",
)

require(
    """static float filler_line_width(int scale) {
    const float width = s_crack_fill * (float)scale;
    return width < 1.0f ? 1.0f : width > s_line_width_max ? s_line_width_max : width;
}
""" in renderer
    and """static void tex_batch_outline(int nverts, int semi) {
    int exact = 0;
    for (int i = 0; i < nverts && !exact; i += 3) exact = s_tb[i * TEXV + 19] > 1.5f;
    if (!exact) return;
    filler_pass_begin(2.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(filler_line_width(s_scale));
    glDisable(GL_BLEND);
    mask_stencil(s_tb_mask);
    p_glUniform1i(s_uSemimode, semi < 0 ? 0 : semi);
    p_glUniform1i(s_uSemipass, semi < 0 ? 0 : 4);
    glDrawArrays(GL_TRIANGLES, 0, nverts);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    filler_pass_end();
}
""" in renderer,
    "the outline of a batch's exact polygons is a line of the asked width within what the context draws, drawn as a "
    "filler pass, and the fill mode is put back",
)

flush = between(renderer, "static void flush_tex_batch(void) {\n    if (s_tb_n == 0) return;", "\n}\n")
outline = flush.index("            if (mirror_depth() && s_crack_fill > 0.0f) tex_batch_outline(nverts, semi);\n")
copies = flush.index(
    """            if (mirror_depth() && fillers) {  /* in sequence each copy would break the batch: 135 batches a frame against 2 */
                filler_pass_begin(GROWN_COPY_MARGIN);
                tex_batch_draw_passes(nverts, semi);
                filler_pass_end();
            }
"""
)
require(
    "    const int fillers = s_tb_fillers;\n" in flush
    and "        s_draw_filler = mirror_depth() && fillers;\n" in flush
    and outline < copies < flush.index("filler_pass_end();", copies) < flush.index("tex_batch_draw_passes(nverts, semi);", flush.index("filler_pass_end();", copies)),
    "under a batch the mirror draws the outlines, then the grown copies, then the batch itself, and the presenter is told "
    "that the batch holds copies",
)

require(
    """void gl_renderer_set_gap_fill(int enabled) {
    if (s_raster_ok) { flush_flat_batch(); flush_tex_batch(); }
    const float width = enabled ? s_crack_width : 0.0f;
    if ((enabled != 0) != s_grown_under || width != s_crack_fill) interp_reset_history();
    if (enabled && !mirror_depth()) memset(s_wide_depth_shown, 0xFF, sizeof s_wide_depth_shown);
    s_grown_under = enabled != 0;
    s_crack_fill = width;
""" in renderer
    and "int gl_renderer_gap_fill(void) { return s_grown_under || s_crack_fill > 0.0f; }\n" in renderer
    and '            const char *fill = getenv("PSX_GL_CRACK_FILL"), *under = getenv("PSX_GL_GROWN_UNDER");\n' in renderer,
    "the setting switches both fillers together, draws what is open under the old rule, starts the presenter's history "
    "anew and has the mirror's depth made afresh",
)

enhancements = between(menu, "void draw_enhancements_tab() {", "const char *cheat_result_message(")
require(
    """    bool gaps = gl_renderer_gap_fill() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (option_toggle("disruptor.gap_filling", &gaps)) {
        gl_renderer_set_gap_fill(gaps ? 1 : 0);
        mark_gap_filling(gaps);
    }
    if (!geometry) ImGui::EndDisabled();
""" in enhancements
    and menu.count('option_toggle("disruptor.gap_filling"') == 1
    and enhancements.count("ImGui::BeginDisabled()") == enhancements.count("ImGui::EndDisabled()"),
    "the setting is on the Enhancements tab alone, greyed without geometry correction, applied and staged when changed",
)

require(
    'void mark_gap_filling(bool value) { mark("disruptor.gap_filling", value); }\n' in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_gap_filling, &Settings::gap_filling>(\n        "disruptor.gap_filling",' in options
    and """    if (settings.has_gap_filling && !env_override_present("PSX_GL_CRACK_FILL") && !env_override_present("PSX_GL_GROWN_UNDER"))
        gl_renderer_set_gap_fill(settings.gap_filling ? 1 : 0);
""" in menu
    and """    if (changed("disruptor.gap_filling"))
        gl_renderer_set_gap_fill(pending.gap_filling ? 1 : 0);
""" in menu,
    "a change is staged as an option of the shared table, merged into the file's settings, applied at the next start unless the "
    "environment decides either filler, and applied when another run saved it",
)

require(
    """        if (d.contains("gap_filling")) try_get([&]{
            s.gap_filling = toml::find<bool>(d, "gap_filling");
            s.has_gap_filling = true;
""" in loader
    and """        if (s.has_gap_filling)
            f << "gap_filling = " << (s.gap_filling ? "true" : "false") << "\\n";
""" in loader
    and "s.has_gap_filling" in between(loader, "    if (s.has_mouse_aim", "{\n")
    and "    bool has_gap_filling = false; bool gap_filling = true;\n" in read("psxrecomp-overlay/recompiler/src/config_loader.h"),
    "the settings file keeps the choice, which is on until changed",
)

row = between(options, '"disruptor.gap_filling", "Gap filling", 1,', "),\n")
require(
    row.endswith("geometry_key")
    and options.count('"disruptor.gap_filling"') == launcher.count('"disruptor.gap_filling"') == 1
    and '    {"disruptor.gap_filling", Page::enhancements},\n' in launcher,
    "the launcher offers it on the Enhancements page, on at first and only with geometry correction",
)

print("Disruptor gap filling source contract: PASS")
