#!/usr/bin/env python3
"""Source contract for smooth mouse turning: the remainder's hand-over, the turned perspective and the setting."""

from pathlib import Path

from menu_sources import menu_text

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
overlay = read("framework/FILES.txt")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
gpu = read("psxrecomp/runtime/src/gpu.c")
mouse = read("src/game/input/mouse_aim.cpp")
menu = menu_text(ROOT)
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

require(
    overlay.count("+runtime/include/gpu_turned_perspective.h\n") == 1
    and '#include "gpu_turned_perspective.h"\n' in renderer
    and "    disruptor_unit_test(disruptor_gpu_turned_perspective tests/unit/gpu_turned_perspective_test.cpp RUNTIME_HEADERS)\n"
    in cmake
    and "    disruptor_contract(smooth_turn)\n" in cmake,
    "the turned perspective's header is applied to the framework and its test and this contract are registered",
)

require(
    """static void turned_perspective(const float px[3], float pq[3], int visual_flags) {
    if (visual_flags & 2)
        psx_turned_perspective(px, pq, s_yaw_sin, s_yaw_cos, (float)g_wide_cur_base + s_yaw_center_x, s_yaw_focal_x);
}
""" in renderer
    and "    const int visual_flags = take_visual_triangle(xs, ys, px, py, pq, pz, pid);\n"
    "    turned_perspective(px, pq, visual_flags);\n" in renderer
    and renderer.count("turned_perspective(px, pq, visual_flags);") == 1,
    "every textured triangle with exact corners has its weights turned by the turn the picture is drawn under",
)

require(
    """    gr_set_presentation_yaw(ws_geometry_correction &&
                                isfinite(s_geometry_camera_yaw_residual)
                                ? s_geometry_camera_yaw_residual : 0.0,
""" in gpu,
    "the picture is turned only with geometry correction on, which is why the menu greys the setting without it",
)

require(
    """    const double shown = g_mouse.shown_yaw_fraction;
    g_mouse.shown_yaw_fraction = g_mouse.fractional_yaw;
    gpu_geometry_camera_yaw_residual_set(
        g_mouse.high_precision_camera && g_mouse.captured &&
                g_mouse.mouse_aim_enabled
            ? shown
            : 0.0);
""" in mouse
    and mouse.count("    g_mouse.fractional_yaw = 0.0;\n    g_mouse.shown_yaw_fraction = 0.0;\n") == 2
    and mouse.count("g_mouse.shown_yaw_fraction = 0.0;") == 2
    and "    double shown_yaw_fraction = 0.0;  // what fractional_yaw was a frame ago\n" in mouse,
    "the renderer is handed the remainder of a frame ago, only while the mouse is captured and aims, and a reset clears both",
)

controls = between(menu, "void draw_controls_tab() {", "void apply_geometry_enabled(")
require(
    """    const bool turn_applies = applies("disruptor.high_precision_camera");
    bool smooth = disruptor_high_precision_camera_enabled() != 0;
    if (!turn_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.high_precision_camera", &smooth)) {
        apply_smooth_turn(smooth);
        mark_smooth_turn(smooth);
    }
    if (!turn_applies) ImGui::EndDisabled();
""" in controls
    and menu.count('option_toggle("disruptor.high_precision_camera"') == 1
    and controls.count("ImGui::BeginDisabled()") == controls.count("ImGui::EndDisabled()"),
    "the setting is on the Controls tab alone, greyed without mouse aim or geometry correction, applied and staged when changed",
)

require(
    """void apply_smooth_turn(bool enabled) {
    disruptor_high_precision_camera_set_enabled(enabled ? 1 : 0);
    if (!env_override_present("PSX_GEOMETRY_FULL_YAW")) gl_renderer_set_full_yaw(enabled ? 1 : 0);
}
""" in menu
    and 'void mark_smooth_turn(bool value) { mark("disruptor.high_precision_camera", value); }\n' in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_high_precision_camera, &Settings::high_precision_camera>(\n        "disruptor.high_precision_camera",' in options
    and """    if (const char *forced = std::getenv("PSX_DISRUPTOR_HIGH_PRECISION_CAMERA"))
        apply_smooth_turn(forced[0] != '\\0' && forced[0] != '0');
    else if (settings.has_high_precision_camera)
        apply_smooth_turn(settings.high_precision_camera);
""" in menu
    and """    if (changed("disruptor.high_precision_camera"))
        apply_smooth_turn(pending.high_precision_camera);
""" in menu,
    "a change switches the mouse module and the renderer's full turn together, is staged as an option of the shared table, merged "
    "into the file's settings, applied at the next start with the environment first, and applied when another run saved it",
)

row = between(options, '"disruptor.high_precision_camera", "Smooth mouse turning", 0,', "),\n")
require(
    row.endswith("needs_aim_and_geometry")
    and "constexpr const char* needs_aim_and_geometry[] = {aim_key, geometry_key};\n" in options
    and 'constexpr const char* aim_key = "disruptor.mouse_aim";\n' in options
    and options.count('"disruptor.high_precision_camera"') == launcher.count('"disruptor.high_precision_camera"') == 1
    and '    {"disruptor.high_precision_camera", Page::controls},\n' in launcher,
    "the launcher offers it on the Controls page, off at first and only with mouse aim and geometry correction",
)

print("Disruptor smooth mouse turning source contract: PASS")
