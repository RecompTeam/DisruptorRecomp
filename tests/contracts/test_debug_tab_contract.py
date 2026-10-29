#!/usr/bin/env python3
"""Source contract for the Debug tab: the diagnostic views a capsule carries and the screen recorder."""

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
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
temporal = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start : source.index("\n}\n", start) + 3]


menu = menu_text(ROOT)
loader = read("src/game/settings/saved.cpp")
settings = read("src/game/settings/game_user_settings.h")

require("    disruptor_contract(debug_tab)\n" in cmake, "this contract is registered")

debug = between(menu, "void draw_debug_tab() {", "void draw_cheats_tab() {")
require(
    '            if (ImGui::BeginTabItem("Debug")) {\n                draw_debug_tab();\n' in menu
    and '    if (ImGui::Button("Record the screen") && !gl_renderer_recording_shown()) g_menu.record_shown = true;\n'
    in debug
    and """    int view = gl_renderer_uv_view() ? 6 : gl_renderer_mesh_view() ? 5 : gl_renderer_ssao_view();
"""
    in debug
    and """    if (ImGui::Combo("Diagnostic view", &view, "Off\\0Shading alone\\0Depth\\0Surface normals\\0Fog fade\\0Mesh\\0Texture coordinates\\0")) {
        apply_diagnostic_view(view);
        mark_diagnostic_view(view);
    }
"""
    in debug
    and menu.count('"Diagnostic view"') == 1
    and menu.count('"Record the screen"') == 1,
    "the Debug tab holds the recorder's button, which arms nothing while a recording runs, and the seven views in the "
    "order the settings file names them",
)

require(
    'inline constexpr const char* kDisruptorDiagnosticViews[] = {"off", "shading", "depth", "normals", "fade", "mesh", "uv"};\n'
    in settings
    and """void apply_diagnostic_view(int view) {
    gl_renderer_set_ssao_view(view >= 1 && view <= 4 ? view : 0);
    gl_renderer_set_mesh_view(view == 5 ? 1 : 0);
    gl_renderer_set_uv_view(view == 6 ? 1 : 0);
}
"""
    in menu
    and "void gl_renderer_set_mesh_view(int enabled) { GL_RT_SYNC(__func__); s_mesh_view = enabled != 0; }\n"
    in renderer
    and "    s_ssao_view = view >= 1 && view <= 4 ? view : 0;\n" in renderer,
    "views 1 to 4 are layers of the occlusion pass and view 5 is the mesh, never two at once",
)

require(
    """void mark_diagnostic_view(int value) {
    g_preferences.pending.has_diagnostic_view = true;
    g_preferences.pending.diagnostic_view = value;
    g_preferences.diagnostic_view_changed = true;
}
"""
    in menu
    and """    if (g_preferences.diagnostic_view_changed) {
        settings.has_diagnostic_view = true;
        settings.diagnostic_view = pending.diagnostic_view;
    }
"""
    in menu
    and """        apply_diagnostic_view(kDeveloperBuild && settings.has_diagnostic_view &&
                                      env_override_present("PSX_DISRUPTOR_CAPSULE_REPLAY")
                                  ? settings.diagnostic_view : 0);
"""
    in menu
    and """    if (g_preferences.diagnostic_view_changed)
        apply_diagnostic_view(pending.diagnostic_view);
"""
    in menu,
    "a view is written to the file so that a capsule carries it, and at a start only a capsule's replay takes it from there",
)

require(
    """            const std::string view = toml::find<std::string>(d, "diagnostic_view");
"""
    in loader
    and """                if (view == kDisruptorDiagnosticViews[index]) {
                    s.diagnostic_view = index;
                    s.has_diagnostic_view = true;
"""
    in loader
    and """        if (s.has_diagnostic_view && s.diagnostic_view >= 0 && s.diagnostic_view < kDisruptorDiagnosticViewCount)
            f << "diagnostic_view = " << quoted(kDisruptorDiagnosticViews[s.diagnostic_view]) << "\\n";
"""
    in loader
    and '"disruptor.diagnostic_view",' in read("tests/contracts/test_launcher_contract.py"),
    "the file names a view by its word, a word it does not know is no view, and the launcher is decided not to offer it",
)

require(
    """void start_shown_recording() {
    g_menu.record_shown = false;
"""
    in menu
    and '    g_menu.shown_note = gl_renderer_record_shown(utf8(folder).c_str(), 120, 240, 960) ? "Last recording: " + utf8(folder)\n'
    in menu
    and "        if (g_menu.record_shown) start_shown_recording();\n" in menu
    and '    const fs::path folder = g_preferences.path.parent_path() / "shown" / std::to_string(now);\n' in menu,
    "the recording starts when the menu closes: 240 pictures at 960 wide after 120 swaps, in a folder of their own beside "
    "the settings",
)

require(
    """int gl_renderer_record_shown(const char *folder, int skip, int count, int width) {
    GL_RT_SYNC(__func__);
    if (!folder || !folder[0] || strlen(folder) >= sizeof s_shown_dir || count <= 0 || count > 2000 || width < 160 ||
        width > 3840 || s_shown_left > 0) return 0;
"""
    in renderer
    and "int gl_renderer_recording_shown(void) { return s_shown_left > 0; }\n" in renderer
    and renderer.count("    shown_capture(0, 1.0f);\n") == 4
    and renderer.count("    shown_capture(1, shown);\n") == 1
    and '"index,kind,alpha,microseconds,faces,camera_samples,camera_inliers,carried,held,conflicting,departed\\n"'
    in renderer
    and '            fprintf(file, "P6\\n%d %d\\n255\\n", s_shown_w, s_shown_h);\n' in renderer
    and "            for (int y = s_shown_h - 1; y >= 0; --y)\n" in renderer,
    "a request out of bounds or during a recording is refused, every swap of a game frame and of an in-between frame is "
    "captured, and the files are a list with a header and pictures written top row first",
)

flush = body(renderer, "static void flush_tex_batch(void) {")
require(
    "static int s_uv_view = 0;\n" in renderer
    and "void gl_renderer_set_uv_view(int enabled) {\n    GL_RT_SYNC(__func__);\n    s_uv_view = enabled != 0;\n    ssao_mode();\n}\nint gl_renderer_uv_view(void) { return s_uv_view; }\n"
    in renderer
    and "    const int mode = s_uv_view ? 0 : s_ssao_view ? s_ssao_view + 1 : (s_ssao_on || s_ddgi_light || s_ddgi_shadows ||"
    in renderer
    and "s_yaw_center_x > 0.0f ? s_yaw_center_x : 160.0f, nearer, s_uv_view ? 1.0f : 0.0f);\n" in renderer
    and "    const int on = !s_uv_view && (grown > 1 || (s_aa_edges && edges)) && s_aa_prog && lw > 0 && lh > 0;\n"
    in renderer
    and '        "  if (v_bill > 0.5 && u_fade_at.w > 0.5) {\\n"\n'
    '        "    if (blended && v_bill > 0.9) discard;\\n"\n'
    '        "    blend_factor = vec4(0.0);\\n"\n'
    '        "    vec2 cell = floor(mod(sample_uv, 256.0) / 16.0);\\n"\n'
    '        "    frag.rgb = vec3(abs(fract(sample_uv / 32.0) * 2.0 - 1.0), (cell.x + 16.0 * cell.y) / 255.0);\\n"\n'
    '        "  }\\n"\n'
    '        "}\\n";\n'
    in renderer
    and "inline constexpr int kDisruptorDiagnosticViewCount = 7;\n" in settings
    and "static void apply_texel_blend(int mode, int mirror) {\n    if (s_uv_view && mirror) glDisable(GL_BLEND);\n    else apply_psx_blend(mode);\n}\n"
    in renderer
    and "        apply_texel_blend(semi, s_visual_on);          /* Pass 2: STP=1 texels (blended) */\n" in renderer
    and "    s_visual_on = on > 0.0f;\n"
    in body(renderer, "static void geometry_visual_uniforms(GLint visual, GLint camera, GLint options,")
    and flush.index("    hr_begin(1);\n")
    < flush.index("geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 0);")
    < flush.index("    tex_batch_draw_passes(nverts, semi);\n")
    < flush.index(
        "        wide_target_begin(dx, s_tex_uXoff, s_tex_uXhalf);\n"
        "        geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 1);\n"
    )
    < flush.index(
        "        geometry_visual_uniforms(s_tex_uVisual, s_tex_uCamera, s_tex_uVisualOptions, 0);\n        wide_target_end("
    )
    and flush.count("geometry_visual_uniforms(") == 3
    and "apply_psx_blend" not in body(renderer, "static void tex_batch_draw_passes(int nverts, int semi) {")
    and temporal.count("apply_texel_blend(c->semi, 1); temporal_mask(c->check,1);") == 3
    and "apply_texel_blend" not in temporal.replace("apply_texel_blend(c->semi, 1); temporal_mask(c->check,1);", ""),
    "the UV view starts off and shows, on the mirror alone, where a textured pixel lies in its texture: u and v as waves of 16 "
    "texels down and 16 up with no step, and the 16-texel cell, with the world's blended fragments left out and a flat "
    "picture's (the HUD, the held weapon) drawn opaque in real and in-between frames while the canonical picture blends as "
    "ever, and with the shading and the picture's filters off so that a recording keeps the values",
)

print("Disruptor debug tab source contract: PASS")
