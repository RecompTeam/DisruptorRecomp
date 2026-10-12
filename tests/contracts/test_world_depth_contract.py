#!/usr/bin/env python3
"""Source contract for the depth-sorted world: its rule's header, the renderer's two passes and the setting."""

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
overlay = read("PSXRECOMP_OVERLAY_FILES.txt")
header = read("psxrecomp-overlay/runtime/include/gpu_world_depth.h")
renderer = read("psxrecomp-overlay/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp-overlay/runtime/src/gpu_gl_temporal.c.inc")
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

require(
    overlay.count("+runtime/include/gpu_world_depth.h\n") == 1
    and '#include "gpu_world_depth.h"\n' in renderer
    and "    disruptor_unit_test(disruptor_gpu_world_depth tests/unit/gpu_world_depth_test.cpp RUNTIME_HEADERS)\n" in cmake
    and "    disruptor_contract(world_depth)\n" in cmake,
    "the rule's header is applied to the framework and its test and this contract are registered",
)

require(
    "    const float stored = 1.0f - 32.0f * (1.0f + pull) / depth;\n" in header
    and "max(1.0 - 32.0*(1.0 + u_pull)/(abs(a_camz)*(1.0 + max(u_visual_options.w, 0.0))), -1.0)" in renderer
    and "    return depth>16.0f ? 1.0f-32.0f/depth : TEMP_ORDER_NEAR;\n" in presenter,
    "the header, the mirror's shader and the presenter store one depth for one corner",
)

require(
    "        const int world = psx_world_depth_class(s_world_depth, visual_flags, pz, s_world_margin);\n"
    "        if (s_tb_n > 0 && world != s_tb_world) flush_tex_batch();\n" in renderer
    and "            s_tb_world = world;\n" in renderer
    and "static float s_world_margin = 0.03f;\n" in renderer
    and "static int   s_world_depth = 0;\n" in renderer,
    "a triangle's class comes from the header, is a key of the batch, and the setting starts off with a margin of 3 percent",
)

by_depth = between(renderer, "static void tex_batch_by_depth(int nverts, int semi) {", "\n}\n")
require(
    by_depth
    == """static void tex_batch_by_depth(int nverts, int semi) {
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    p_glUniform1i(s_uSemimode, semi < 0 ? 0 : semi);
    p_glUniform1i(s_uSemipass, semi < 0 ? 3 : 4);
    glDrawArrays(GL_TRIANGLES, 0, nverts);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    p_glUniform1f(s_tex_uPull, s_world_margin);
    tex_batch_draw_passes(nverts, semi);
    p_glUniform1f(s_tex_uPull, 0.0f);
    glDepthFunc(GL_ALWAYS);""",
    "a batch of the class writes its true depths first, then draws its colours pulled by the margin without writing depth, "
    "and leaves the pull and the depth test as it found them",
)

require(
    "            if (world && mirror_depth()) tex_batch_by_depth(nverts, semi);\n"
    "            else tex_batch_draw_passes(nverts, semi);\n" in renderer
    and "static int mirror_depth(void) { return s_geometry_correction && (s_ssao || s_grown_under || s_crack_fill > 0.0f || s_world_depth); }\n"
    in renderer
    and '    s_tex_uPull = p_glGetUniformLocation(s_tex_prog, "u_pull");\n' in renderer,
    "only a batch of the class is drawn by depth, the setting alone keeps the mirror's depth, and the shader's pull is found",
)

require(
    """void gl_renderer_set_world_depth(int enabled) {
    if (s_raster_ok) { flush_flat_batch(); flush_tex_batch(); }
    if ((enabled != 0) != s_world_depth) interp_reset_history();
    if (enabled && !mirror_depth()) memset(s_wide_depth_shown, 0xFF, sizeof s_wide_depth_shown);
    s_world_depth = enabled != 0;
}
int gl_renderer_world_depth(void) { return s_world_depth; }
""" in renderer
    and '            if (getenv("PSX_GL_WORLD_DEPTH")) s_world_depth = atoi(getenv("PSX_GL_WORLD_DEPTH")) != 0;\n' in renderer,
    "switching draws what is open under the old rule, starts the presenter's history anew and has the mirror's depth made afresh",
)

require(
    "        p_glUniform1f(s_temp_uPull,s_world_margin);\n" in presenter
    and "        p_glUniform1f(s_temp_uPull,0.0f); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);\n" in presenter
    and "    for (last_world=s->nt-1;last_world>=0 && !s->triangles[last_world].world;--last_world) {}\n" in presenter
    and "            if (s_world_depth && (now->world || ti<last_world))\n" in presenter
    and "                    if (!(now->world && now->depth[k]>0.0f)) s_temp_scratch[(j+k)*TEXV+(t?23:9)]=TEMP_ORDER_FAR;\n" in presenter,
    "an in-between frame draws the world by the same margin, and a corner without a depth is far while world faces follow it",
)

enhancements = between(menu, "void draw_enhancements_tab() {", "const char *cheat_result_message(")
require(
    """    bool by_depth = gl_renderer_world_depth() != 0;
    if (!geometry) ImGui::BeginDisabled();
    if (option_toggle("disruptor.depth_sorted_world", &by_depth)) {
        gl_renderer_set_world_depth(by_depth ? 1 : 0);
        mark_world_depth(by_depth);
    }
    if (!geometry) ImGui::EndDisabled();
""" in enhancements
    and menu.count('option_toggle("disruptor.depth_sorted_world"') == 1
    and enhancements.count("ImGui::BeginDisabled()") == enhancements.count("ImGui::EndDisabled()"),
    "the setting is on the Enhancements tab alone, greyed without geometry correction, applied and staged when changed",
)

require(
    "    std::set<const Option *> changed;\n" in menu
    and 'void mark_world_depth(bool value) { mark("disruptor.depth_sorted_world", value); }\n' in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_depth_sorted_world, &Settings::depth_sorted_world>(\n        "disruptor.depth_sorted_world",' in options
    and """    if (settings.has_depth_sorted_world && !env_override_present("PSX_GL_WORLD_DEPTH"))
        gl_renderer_set_world_depth(settings.depth_sorted_world ? 1 : 0);
""" in menu
    and """    if (changed("disruptor.depth_sorted_world"))
        gl_renderer_set_world_depth(pending.depth_sorted_world ? 1 : 0);
""" in menu,
    "a change is staged as an option of the shared table, merged into the file's settings, applied at the next start unless the "
    "environment decides, and applied when another run saved it",
)

require(
    """        if (d.contains("depth_sorted_world")) try_get([&]{
            s.depth_sorted_world = toml::find<bool>(d, "depth_sorted_world");
            s.has_depth_sorted_world = true;
""" in loader
    and """        if (s.has_depth_sorted_world)
            f << "depth_sorted_world = " << (s.depth_sorted_world ? "true" : "false") << "\\n";
""" in loader
    and "s.has_depth_sorted_world" in between(loader, "    if (s.has_mouse_aim", "{\n")
    and "    bool has_depth_sorted_world = false; bool depth_sorted_world = false;\n"
    in read("psxrecomp-overlay/recompiler/src/config_loader.h"),
    "the settings file keeps the choice, which is off until made",
)

row = between(options, '"disruptor.depth_sorted_world", "Depth-sorted world", 0,', "),\n")
require(
    row.endswith("geometry_key")
    and options.count('"disruptor.depth_sorted_world"') == launcher.count('"disruptor.depth_sorted_world"') == 1
    and '    {"disruptor.depth_sorted_world", Page::enhancements},\n' in launcher,
    "the launcher offers it on the Enhancements page, off at first and only with geometry correction",
)

print("Disruptor depth-sorted world source contract: PASS")
