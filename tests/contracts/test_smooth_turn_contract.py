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
    "    PsxTurnCorner cut[PSX_TURN_CLIP_MAX];\n"
    "    const int near_face = s_near_set;\n"
    "    const int cut_corners = near_face ? -1 : turn_clip(px, py, visual_flags, cut);\n"
    "    const float whole_q[3] = {pq[0], pq[1], pq[2]};\n"
    "    turned_perspective(px, pq, visual_flags);\n" in renderer
    and renderer.count("turned_perspective(px, pq, visual_flags);") == 1,
    "every textured triangle with exact corners has its weights turned by the turn the picture is drawn under",
)

temporal = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
require(
    overlay.count("+runtime/include/gpu_turn_clip.h\n") == 1
    and '#include "gpu_turn_clip.h"\n' in renderer
    and "    disruptor_unit_test(disruptor_gpu_turn_clip tests/unit/gpu_turn_clip_test.cpp RUNTIME_HEADERS)\n" in cmake,
    "the turn clip's header is applied to the framework and its test is registered",
)
clip = between(renderer, "static int turn_clip(", "\n}\n")
require(
    "    if (!visual_flags ||\n"
    "        !psx_turn_band(s_yaw_sin, s_yaw_cos, (float)g_wide_cur_base + s_yaw_center_x, s_yaw_focal_x, &low, &high))\n"
    "        return -1;\n" in clip
    and "    if (px[0] >= low && px[0] <= high && px[1] >= low && px[1] <= high && px[2] >= low && px[2] <= high) return -1;\n"
    in clip
    and clip.endswith("    return psx_turn_clip(px, py, low, high, cut);"),
    "a triangle the picture turns is cut to the band of the turn it is drawn under, and only when a corner leaves the band",
)
require(
    "        if (s_tb_n + 3 + (cut_corners > 2 ? 3 * (cut_corners - 2) : 0) > TEXBATCH_MAXV) { s_batch_reason[6]++; flush_tex_batch(); }\n"
    in renderer
    and "            vp[20] = visual_flags ? px[cut_corners >= 0 ? 0 : i] : (float)xs[i];" in renderer
    and "            vp[21] = visual_flags ? py[cut_corners >= 0 ? 0 : i] : (float)ys[i];\n" in renderer
    and "        for (int piece = 0; piece + 2 < cut_corners; ++piece) {" in renderer
    and "            const int corner_of[3] = {0, piece + 1, piece + 2};\n"
    "            const int perspective = whole_q[0] > 0.0f && whole_q[1] > 0.0f && whole_q[2] > 0.0f;\n" in renderer
    and "                piece_q[i] = mixed_q[i] = perspective ? psx_turn_mix(&cut[corner_of[i]], whole_q) : 0.0f;\n" in renderer
    and "            turned_perspective(piece_x, piece_q, visual_flags);\n" in renderer
    and "                memcpy(vp, whole, TEXV * sizeof(float));\n" in renderer
    and "                    vp[channel] = own >= 0 ? whole[own * TEXV + channel]\n"
    "                                           : psx_turn_mix(corner, of_corners) / (channel < 4 && perspective ? mixed_q[i] : 1.0f);\n"
    in renderer
    and "                vp[20] = corner->x;\n                vp[21] = corner->y;\n                vp[22] = piece_q[i];\n" in renderer
    and "        const uint64_t cut_id = pid[0] == pid[1] && pid[1] == pid[2] ? pid[0] : 0u;" in renderer
    and "                s_tb_identity[s_tb_n + i] = own >= 0 ? pid[own] : cut_id;\n" in renderer,
    "a cut textured triangle keeps its canonical picture, is one point in the mirror, and its pieces are one point in the "
    "canonical picture with texture, colour and weights taken along the triangle",
)
require(
    "        cut_corners = turn_clip(px, py, visual_flags, cut);\n" in renderer
    and "    if (s_fb_n + n + (cut_corners > 2 ? 3 * (cut_corners - 2) : 0) > FLATBATCH_MAXV)\n" in renderer
    and "        v[7] = visual_flags ? px[cut_corners >= 0 ? 0 : i] : (float)xs[i];" in renderer
    and "        v[8] = visual_flags ? py[cut_corners >= 0 ? 0 : i] : (float)ys[i];\n" in renderer
    and "    const float *whole = &s_fb[(s_fb_n - n) * GEOV];\n" in renderer
    and "            memcpy(v, whole, GEOV * sizeof(float));\n" in renderer
    and "            v[7] = corner->x;\n            v[8] = corner->y;\n" in renderer
    and "            s_fb_identity[s_fb_n++] = own >= 0 ? pid[own] : pid[0] == pid[1] && pid[1] == pid[2] ? pid[0] : 0u;\n"
    in renderer,
    "an untextured triangle is cut the same way",
)
require(
    "static float turned_depth(float x, float depth, int visual_flags) {\n"
    "    return visual_flags ? psx_turned_depth(x, depth, s_yaw_sin, s_yaw_cos, (float)g_wide_cur_base + s_yaw_center_x, s_yaw_focal_x)\n"
    "                        : depth;\n" in renderer
    and "        for (int k = 0; k < cut_corners; ++k) cut_z[k] = turned_depth(cut[k].x, psx_turn_depth(&cut[k], pz), visual_flags);\n"
    in renderer
    and "            const float piece_z[3] = {cut_z[0], cut_z[piece + 1], cut_z[piece + 2]};\n" in renderer
    and "            vp[23] = turned_z[i];" in renderer
    and "                vp[23] = cut_z[corner_of[i]];\n" in renderer
    and "        v[9] = turned_depth(px[i], pz[i], visual_flags);" in renderer
    and "            v[9] = turned_depth(corner->x, psx_turn_depth(corner, pz), visual_flags);\n" in renderer,
    "every turned corner carries its depth along the turned view, a cut one the depth of its place in the triangle",
)
require(
    "#define TEMPORAL_CUT_IDENTITY UINT64_C(0x1000000000000000)" in renderer
    and "            s_tb_identity[s_tb_n + i] = cut_corners >= 0 ? TEMPORAL_CUT_IDENTITY : pid[i];\n" in renderer
    and "        s_fb_identity[s_fb_n] = cut_corners >= 0 ? TEMPORAL_CUT_IDENTITY : pid[i];\n" in renderer
    and "        const int cut = identities && identities[i]==TEMPORAL_CUT_IDENTITY &&\n"
    "            identities[i+1]==TEMPORAL_CUT_IDENTITY && identities[i+2]==TEMPORAL_CUT_IDENTITY;\n"
    "        t->world = !cut;\n" in temporal
    and "        t->modelled = !sprite && !partial && !cut && identities && identities[i] && identities[i+1] && identities[i+2] &&\n"
    in temporal,
    "the whole of a cut triangle is marked, and the in-between presenter takes a marked triangle for no world face",
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
