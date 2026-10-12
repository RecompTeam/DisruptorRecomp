#!/usr/bin/env python3
"""Source contract for the experimental ambient occlusion: its marks in the mirror, its switches and the setting."""

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
settings = read("psxrecomp-overlay/recompiler/src/config_loader.h")

require("    disruptor_contract(ambient_occlusion)\n" in cmake, "this contract is registered")

require(
    '"  v_deep = abs(a_camz) > 16.0 ? 1.0 : -1000.0;\\n"' in renderer
    and '"  if (v_bill > 0.5 && v_deep > -999.5 && v_deep < 0.999) frag.a = 0.25;' in renderer
    and "static float         s_ssao_tilt = 0.35f;" in renderer
    and '"float flat_surf(vec2 uv){\\n"' in renderer
    and '"    float a = flat_surf(v_uv - step), b = flat_surf(v_uv + step);\\n"' in renderer,
    "a polygon with a corner that has no depth is marked as no surface, a joint under 20 degrees is no corner, and a depth "
    "is mended from surfaces alone",
)

require(
    """static void ssao_mode(void) {
    const int mode = s_ssao_view ? s_ssao_view + 1 : s_ssao_on;
    if (mode && !s_ssao) memset(s_wide_depth_shown, 0xFF, sizeof s_wide_depth_shown);
    s_ssao = mode;
}
void gl_renderer_set_ssao(int enabled) {
    s_ssao_on = enabled != 0;
    ssao_mode();
}
void gl_renderer_set_ssao_view(int view) {
    s_ssao_view = view >= 1 && view <= 4 ? view : 0;
    ssao_mode();
}
""" in renderer
    and """void gl_renderer_set_ssao_tilt(float tilt) {
    if (tilt >= 0.0f && tilt < 0.95f) s_ssao_tilt = tilt;
}
void gl_renderer_set_ssao_shape(float radius, float strength) {
    if (radius > 0.0f) s_ssao_radius = radius;
    if (strength >= 0.0f) s_ssao_strength = strength;
}
""" in renderer
    and "int gl_renderer_ssao(void) { return s_ssao_on; }\n" in renderer,
    "the setting and a diagnostic view are kept apart, the first of them has the mirror's depth made afresh, and a reach "
    "or a strength that is not one changes nothing",
)

experimental = between(menu, "void draw_experimental_tab() {", "\n}\n")
require(
    """    bool occlusion = gl_renderer_ssao() != 0;
    const bool occlusion_applies = applies("disruptor.ambient_occlusion");
    if (!occlusion_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.ambient_occlusion", &occlusion)) {
        gl_renderer_set_ssao(occlusion ? 1 : 0);
        mark_occlusion(occlusion);
    }
    if (!occlusion_applies) ImGui::EndDisabled();
""" in experimental
    and """    const bool shape_applies = applies("disruptor.occlusion_reach") &&
                               applies("disruptor.occlusion_strength");
    if (!shape_applies) ImGui::BeginDisabled();
    ImGui::SetNextItemWidth(260.0f);
    const bool reach_moved = option_slider("disruptor.occlusion_reach", &reach);
""" in experimental
    and """        option_slider("disruptor.occlusion_strength", &strength);
    if (!shape_applies) ImGui::EndDisabled();
    if (reach_moved || strength_moved) {
        gl_renderer_set_ssao_shape(static_cast<float>(reach), strength / 100.0f);
        mark_occlusion_shape(reach, strength);
    }
""" in experimental
    and menu.count('option_toggle("disruptor.ambient_occlusion"') == 1
    and experimental.count("ImGui::BeginDisabled()") == experimental.count("ImGui::EndDisabled()") == 2
    and experimental.count("option_toggle(") == 1
    and "ImGui::Checkbox(" not in experimental
    and '            if (ImGui::BeginTabItem("Experimental")) {\n                draw_experimental_tab();\n' in menu,
    "the Experimental tab holds the occlusion alone: a switch greyed without geometry correction and two bounded sliders greyed without the switch",
)

require(
    """void mark_occlusion(bool value) { mark("disruptor.ambient_occlusion", value); }

void mark_occlusion_shape(int reach, int strength) {
    mark("disruptor.occlusion_reach", reach);
    mark("disruptor.occlusion_strength", strength);
}
""" in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_ambient_occlusion, &Settings::ambient_occlusion>(\n        "disruptor.ambient_occlusion",' in options
    and "write<&Settings::has_occlusion_reach, &Settings::occlusion_reach>, needs_occlusion)" in options
    and "write<&Settings::has_occlusion_strength, &Settings::occlusion_strength>, needs_occlusion)" in options
    and """    if (!env_override_present("PSX_GL_SSAO")) {
        if (settings.has_ambient_occlusion) gl_renderer_set_ssao(settings.ambient_occlusion ? 1 : 0);
""" in menu
    and """    gl_renderer_set_ssao_shape(
        settings.has_occlusion_reach && !env_override_present("PSX_GL_SSAO_RADIUS")
            ? static_cast<float>(settings.occlusion_reach) : -1.0f,
        settings.has_occlusion_strength && !env_override_present("PSX_GL_SSAO_STRENGTH")
            ? settings.occlusion_strength / 100.0f : -1.0f);
""" in menu
    and """    if (changed("disruptor.ambient_occlusion"))
        gl_renderer_set_ssao(pending.ambient_occlusion ? 1 : 0);
    if (changed("disruptor.occlusion_reach") ||
        changed("disruptor.occlusion_strength"))
        gl_renderer_set_ssao_shape(static_cast<float>(pending.occlusion_reach), pending.occlusion_strength / 100.0f);
""" in menu,
    "the switch and the shape are each staged as options of the shared table, merged into the file's settings, applied at the "
    "next start unless the environment decides that one, and applied when another run saved them",
)

require(
    """            const int reach = toml::find<int>(d, "occlusion_reach");
""" in loader
    and """            const int percent = toml::find<int>(d, "occlusion_strength");
""" in loader
    and """        if (s.has_occlusion_reach && s.occlusion_reach >= 50 && s.occlusion_reach <= 800)
            f << "occlusion_reach = " << s.occlusion_reach << "\\n";
        if (s.has_occlusion_strength && s.occlusion_strength >= 0 && s.occlusion_strength <= 300)
            f << "occlusion_strength = " << s.occlusion_strength << "\\n";
""" in loader
    and """        if (s.has_ambient_occlusion)
            f << "ambient_occlusion = " << (s.ambient_occlusion ? "true" : "false") << "\\n";
""" in loader
    and "    bool has_ambient_occlusion = false; bool ambient_occlusion = false;\n" in settings
    and "    bool has_occlusion_reach = false; int occlusion_reach = 160;" in settings
    and "    bool has_occlusion_strength = false; int occlusion_strength = 80;" in settings,
    "the settings file keeps the switch, off until made, and a reach and a strength only inside the sliders' bounds",
)

switch = between(options, '"disruptor.ambient_occlusion", "Ambient occlusion", 0,', "),\n")
reach = between(options, 'slider("disruptor.occlusion_reach", "Occlusion reach", 160,', "),\n")
strength = between(options, 'slider("disruptor.occlusion_strength", "Occlusion strength", 80,', "),\n")
require(
    switch.endswith("needs_geometry")
    and "50, 800, Unit::none," in reach
    and reach.endswith("needs_occlusion")
    and "0, 300, Unit::percent," in strength
    and strength.endswith("needs_occlusion")
    and "constexpr const char* needs_occlusion[] = {occlusion_key};\n" in options
    and 'constexpr const char* occlusion_key = "disruptor.ambient_occlusion";\n' in options
    and '    {"disruptor.ambient_occlusion", Page::experimental},\n'
    '    {"disruptor.occlusion_reach", Page::experimental},\n'
    '    {"disruptor.occlusion_strength", Page::experimental},\n' in launcher
    and launcher.count("Page::experimental") == 3,
    "the launcher's Experimental page holds these three rows: the switch needs geometry correction, the sliders the switch, "
    "with the menu's own bounds",
)

print("Disruptor ambient occlusion source contract: PASS")
