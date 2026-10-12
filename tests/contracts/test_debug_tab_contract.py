#!/usr/bin/env python3
"""Source contract for the Debug tab: the diagnostic views a capsule carries and the screen recorder."""

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
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
menu = read("src/game/ui/dev_menu.cpp")
loader = read("src/game/settings/saved.cpp")
settings = read("src/game/settings/game_user_settings.h")

require("    disruptor_contract(debug_tab)\n" in cmake, "this contract is registered")

debug = between(menu, "void draw_debug_tab() {", "void draw_cheats_tab() {")
require(
    '            if (ImGui::BeginTabItem("Debug")) {\n                draw_debug_tab();\n' in menu
    and "    if (ImGui::Button(\"Record the screen\") && !gl_renderer_recording_shown()) g_menu.record_shown = true;\n" in debug
    and """    int view = gl_renderer_mesh_view() ? 5 : gl_renderer_ssao_view();
""" in debug
    and """    if (ImGui::Combo("Diagnostic view", &view, "Off\\0Shading alone\\0Depth\\0Surface normals\\0Fog fade\\0Mesh\\0")) {
        apply_diagnostic_view(view);
        mark_diagnostic_view(view);
    }
""" in debug
    and menu.count('"Diagnostic view"') == 1
    and menu.count('"Record the screen"') == 1,
    "the Debug tab holds the recorder's button, which arms nothing while a recording runs, and the six views in the "
    "order the settings file names them",
)

require(
    'inline constexpr const char* kDisruptorDiagnosticViews[] = {"off", "shading", "depth", "normals", "fade", "mesh"};\n' in settings
    and """void apply_diagnostic_view(int view) {
    gl_renderer_set_ssao_view(view >= 1 && view <= 4 ? view : 0);
    gl_renderer_set_mesh_view(view == 5 ? 1 : 0);
}
""" in menu
    and "void gl_renderer_set_mesh_view(int enabled) { s_mesh_view = enabled != 0; }\n" in renderer
    and "    s_ssao_view = view >= 1 && view <= 4 ? view : 0;\n" in renderer,
    "views 1 to 4 are layers of the occlusion pass and view 5 is the mesh, never two at once",
)

require(
    """void mark_diagnostic_view(int value) {
    g_preferences.pending.has_diagnostic_view = true;
    g_preferences.pending.diagnostic_view = value;
    g_preferences.diagnostic_view_changed = true;
}
""" in menu
    and """    if (g_preferences.diagnostic_view_changed) {
        settings.has_diagnostic_view = true;
        settings.diagnostic_view = pending.diagnostic_view;
    }
""" in menu
    and """        apply_diagnostic_view(settings.has_diagnostic_view && env_override_present("PSX_DISRUPTOR_CAPSULE_REPLAY")
                                  ? settings.diagnostic_view : 0);
""" in menu
    and """    if (g_preferences.diagnostic_view_changed)
        apply_diagnostic_view(pending.diagnostic_view);
""" in menu,
    "a view is written to the file so that a capsule carries it, and at a start only a capsule's replay takes it from there",
)

require(
    """            const std::string view = toml::find<std::string>(d, "diagnostic_view");
""" in loader
    and """                if (view == kDisruptorDiagnosticViews[index]) {
                    s.diagnostic_view = index;
                    s.has_diagnostic_view = true;
""" in loader
    and """        if (s.has_diagnostic_view && s.diagnostic_view >= 0 && s.diagnostic_view < kDisruptorDiagnosticViewCount)
            f << "diagnostic_view = " << quoted(kDisruptorDiagnosticViews[s.diagnostic_view]) << "\\n";
""" in loader
    and '"disruptor.diagnostic_view",' in read("tests/contracts/test_launcher_contract.py"),
    "the file names a view by its word, a word it does not know is no view, and the launcher is decided not to offer it",
)

require(
    """void start_shown_recording() {
    g_menu.record_shown = false;
""" in menu
    and '    g_menu.shown_note = gl_renderer_record_shown(utf8(folder).c_str(), 120, 240, 960) ? "Last recording: " + utf8(folder)\n' in menu
    and "        if (g_menu.record_shown) start_shown_recording();\n" in menu
    and '    const fs::path folder = g_preferences.path.parent_path() / "shown" / std::to_string(now);\n' in menu,
    "the recording starts when the menu closes: 240 pictures at 960 wide after 120 swaps, in a folder of their own beside "
    "the settings",
)

require(
    """int gl_renderer_record_shown(const char *folder, int skip, int count, int width) {
    if (!folder || !folder[0] || strlen(folder) >= sizeof s_shown_dir || count <= 0 || count > 2000 || width < 160 ||
        width > 3840 || s_shown_left > 0) return 0;
""" in renderer
    and "int gl_renderer_recording_shown(void) { return s_shown_left > 0; }\n" in renderer
    and renderer.count("    shown_capture(0, 1.0f);\n") == 4
    and renderer.count("    shown_capture(1, shown);\n") == 1
    and '"index,kind,alpha,microseconds,faces,camera_samples,camera_inliers,carried,held,conflicting,departed\\n"' in renderer
    and '            fprintf(file, "P6\\n%d %d\\n255\\n", s_shown_w, s_shown_h);\n' in renderer
    and "            for (int y = s_shown_h - 1; y >= 0; --y)\n" in renderer,
    "a request out of bounds or during a recording is refused, every swap of a game frame and of an in-between frame is "
    "captured, and the files are a list with a header and pictures written top row first",
)

print("Disruptor debug tab source contract: PASS")
