#!/usr/bin/env python3
"""Source-only contract for the host-side Disruptor developer menu."""

from __future__ import annotations

import re
from pathlib import Path

from menu_sources import menu_sources, menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^;{{]*\)\s*\{{", source)
    require(match is not None, f"missing function {name}")
    start = match.end()
    depth = 1
    index = start
    while index < len(source) and depth:
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
        index += 1
    require(depth == 0, f"unterminated function {name}")
    return source[start : index - 1]


def host_ui_gl_restore_helper(source: str) -> tuple[str, str, str, str]:
    definitions = re.finditer(
        r"\b(?:static\s+)?void\s+(\w+)\s*\(\s*int\s+(\w+)\s*,\s*"
        r"int\s+(\w+)\s*\)\s*\{",
        source,
    )
    candidates: list[tuple[str, str, str, str]] = []
    for definition in definitions:
        name, width, height = definition.groups()
        body = function_body(source, name)
        if "psx_host_ui_render_gl" in body:
            candidates.append((name, width, height, body))
    require(len(candidates) == 1, "host UI GL rendering must be centralised in one state-restoring helper")
    return candidates[0]


cmake = read("CMakeLists.txt")
manifest = read("framework/FILES.txt")
host_ui = read("psxrecomp/runtime/include/host_ui.h") + read("psxrecomp/runtime/include/host_ui_flags.h")
main_cpp = read("psxrecomp/runtime/src/main.cpp")
gl = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
gte = read("psxrecomp/runtime/src/gte.cpp")
menu = menu_text(ROOT)
options = read("src/game/settings/options.cpp")
STAGED = (
    "void mark(const char *key, int value) {\n"
    "    const Option *option = find_option(key);\n"
    "    option->set(g_preferences.pending, value);\n"
    "    g_preferences.changed.insert(option);\n"
    "}\n"
)
MERGED = (
    "    for (const Option *option : g_preferences.changed)\n        option->set(settings, option->get(pending));\n"
)
mouse = read("src/game/input/mouse_aim.cpp")
cheats = read("src/game/session/cheats.cpp")
cheats_h = read("src/game/session/cheats.h")
far_rendering_h = read("src/game/render/far_rendering.h")
config_h = read("psxrecomp/recompiler/src/config_loader.h") + read("src/game/settings/game_user_settings.h")
config_cpp = read("psxrecomp/recompiler/src/config_loader.cpp") + read("src/game/settings/saved.cpp")
run_ps1 = read("run.ps1")
run_sh = read("run.sh")
game_toml = read("game.toml")
game_wide_toml = read("game-widescreen.toml")

require(
    "option(DISRUPTOR_DEV_MENU" in cmake
    and "if(DISRUPTOR_DEV_MENU)\n    target_compile_definitions(psx-runtime PRIVATE PSX_FRAME_INTERPOLATION_GEOMETRY_DEFAULT=1)\n"
    in cmake
    and read("psxrecomp/runtime/src/main.cpp").count("#if defined(PSX_FRAME_INTERPOLATION_GEOMETRY_DEFAULT)\n") == 2
    and "DISRUPTOR_DEV_MENU" not in read("psxrecomp/runtime/src/main.cpp"),
    "developer menu must remain independently build-selectable, and with it in-between frames start in the geometry mode",
)
require(
    "v1.93.0.tar.gz" in cmake and "f7bf6bc76e5d79d40d751096663e6c5d18daf08129dd52e7f7b09e4251c8eeee" in cmake.lower(),
    "Dear ImGui source must remain versioned and integrity-pinned",
)
menu_files = menu_sources(ROOT)
require(
    [name.rsplit("/", 1)[1] for name in menu_files]
    == [
        "menu.h",
        "preferences.cpp",
        "rows.cpp",
        "tab_controls.cpp",
        "tab_display.cpp",
        "tab_enhancements.cpp",
        "tab_audio.cpp",
        "tab_experimental.cpp",
        "tab_debug.cpp",
        "tab_cheats.cpp",
        "tab_achievements.cpp",
        "achievements_overlay.cpp",
        "tab_system.cpp",
        "tab_language.cpp",
        "menu.cpp",
    ]
    and sorted(menu_files)
    == sorted(found.relative_to(ROOT).as_posix() for found in (ROOT / "src/game/ui/menu").iterdir())
    and "target_link_libraries(psx-runtime PRIVATE disruptor-imgui)" in cmake,
    "every file of the menu's folder must be built, and linked only into the game runtime",
)
for name in menu_files[1:]:
    source = read(name)
    require(
        source.startswith('#include "game/ui/menu/menu.h"\n\nnamespace disruptor::menu {\n')
        and source.endswith("}  // namespace disruptor::menu\n")
        and source.count("namespace disruptor::menu {") == 1
        and "#include" not in source[10:],
        f"{name} must hold its definitions in the menu's namespace, behind the menu's one header",
    )
for tab in ("controls", "enhancements", "audio", "experimental", "debug", "cheats", "system", "language"):
    source = read(f"src/game/ui/menu/tab_{tab}.cpp")
    require(
        f"\nvoid draw_{tab}_tab() {{\n" in source
        and source.count("_tab() {") == 1
        and f"void draw_{tab}_tab();\n" in read("src/game/ui/menu/menu.h")
        and f"draw_{tab}_tab();" in function_body(read("src/game/ui/menu/menu.cpp"), "draw_menu"),
        f"the {tab} tab is drawn by its own file, and the menu draws it",
    )
require(
    "PreferenceState g_preferences;\n" in read("src/game/ui/menu/preferences.cpp")
    and "DevMenuState g_menu;\n" in read("src/game/ui/menu/menu.cpp")
    and "MenuRegistration g_registration;\n" in read("src/game/ui/menu/menu.cpp")
    and "bool option_toggle(const char *key, bool *value) {\n" in read("src/game/ui/menu/rows.cpp"),
    "the staged settings, the menu's own state and its registration, and the rows each have one file",
)
require(
    "src/game/session/cheats.cpp" in cmake and "tests/unit/disruptor_cheats_test.cpp" in cmake,
    "version-pinned cheats and their behavioral test must be built",
)
require(
    "+runtime/include/host_ui.h" in manifest and "+runtime/include/host_ui_flags.h" in manifest,
    "new host UI ABI headers must remain reviewed in the overlay manifest",
)

for token in (
    "PSX_HOST_UI_ABI_VERSION",
    "PSX_HOST_UI_CAPTURE_KEYBOARD",
    "PSX_HOST_UI_CAPTURE_MOUSE",
    "PSX_HOST_UI_CAPTURE_GAMEPAD",
    "PSX_HOST_UI_SUSPEND_INTERPOLATION",
    "PSX_HOST_UI_VISIBLE",
    "psx_host_video_set_vsync",
    "psx_host_video_set_interpolation",
    "psx_host_video_get_display_aspect",
    "psx_host_video_set_display_aspect",
    "psx_host_video_get_adaptive_view",
    "psx_host_video_set_adaptive_view",
    "psx_host_video_get_internal_scale",
    "psx_host_video_set_internal_scale",
    "psx_host_video_get_fullscreen_mode",
    "psx_host_video_set_fullscreen_mode",
    "psx_host_audio_get_master_volume",
    "psx_host_audio_set_master_volume",
    "psx_host_audio_get_muted",
    "psx_host_audio_set_muted",
    "psx_host_user_settings_path",
):
    require(token in host_ui, f"host UI ABI is missing {token}")

vblank_start = main_cpp.index("static void sdl_vblank_present")
event_start = main_cpp.index("while (SDL_PollEvent(&ev))", vblank_start)
event_end = main_cpp.index("SDL_GameControllerUpdate", event_start)
event_body = main_cpp[event_start:event_end]
require("psx_host_ui_handle_event(&ev)" in event_body, "SDL events must reach the menu before runtime hotkeys")
require(
    event_body.index("psx_host_ui_handle_event(&ev)") < event_body.index("key >= SDLK_F1"),
    "menu event consumption must precede save-state hotkeys",
)
require(
    "psx_host_ui_capture_flags()" in function_body(main_cpp, "sample_pad_into_sio"),
    "polled keyboard/gamepad input must be neutralised while the menu is open",
)
require(
    "host_ui_runtime_shutdown();" in function_body(main_cpp, "shutdown_runtime"),
    "menu GL resources must be destroyed before renderer/window teardown",
)

host_ui_helper, drawable_width, drawable_height, host_ui_helper_body = host_ui_gl_restore_helper(gl)
callback_pos = host_ui_helper_body.index("psx_host_ui_render_gl")
default_fbo = re.search(
    r"p_glBindFramebuffer\s*\(\s*PSXGL_(?:DRAW_)?FRAMEBUFFER\s*,\s*0\s*\)",
    host_ui_helper_body[callback_pos:],
)
drawable_viewport = re.search(
    rf"glViewport\s*\(\s*0\s*,\s*0\s*,\s*"
    rf"{re.escape(drawable_width)}\s*,\s*{re.escape(drawable_height)}\s*\)",
    host_ui_helper_body[callback_pos:],
)
require(
    default_fbo is not None and drawable_viewport is not None and default_fbo.start() < drawable_viewport.start(),
    "host UI helper must restore default FBO then the current drawable viewport",
)

for present_name in (
    "gl_renderer_present",
    "gl_renderer_present_blank",
    "gl_renderer_present_vram",
    "gl_renderer_present_wide_fbo",
):
    body = function_body(gl, present_name)
    helper_call = f"{host_ui_helper}("
    require(
        helper_call in body and "psx_host_ui_render_gl" not in body and "SDL_GL_SwapWindow" in body,
        f"{present_name} must use the state-restoring host UI helper",
    )
    require(
        body.index(helper_call) < body.index("SDL_GL_SwapWindow"), f"{present_name} renders the menu after SwapWindow"
    )

require(
    "s_interp_content_suspended" in gl and "s_interp_host_ui_suspended" in gl,
    "FMV and menu interpolation suspension reasons must remain independent",
)
require(
    "        gl_renderer_set_host_ui_suspended(psx_host_ui_stops_interpolation(ui_flags) ||\n"
    "                                          (psx_host_ui_in_layer(ui_flags) && gl_renderer_host_ui_layer_failed()));\n"
    in main_cpp,
    "visible host UI stops the interpolation unless it goes into a layer that could be made",
)
layer_update = function_body(gl, "interp_layer_update")
require(
    "psx_host_ui_in_layer(psx_host_ui_capture_flags())" in layer_update
    and "psx_host_ui_render_gl" not in layer_update
    and layer_update.index("p_glWaitSync(s_interp_draw_fence")
    < layer_update.index(f"{host_ui_helper}(ww, wh);")
    < layer_update.index("s_layer_fence[i] = p_glFenceSync(")
    < layer_update.index("s_layer_shown = i;"),
    "UI that only shows is drawn into the layer by the same helper, after the presenter's last draw, then shown",
)
after_helper = layer_update[layer_update.index(f"{host_ui_helper}(ww, wh);") :]
require(
    after_helper.index("p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);")
    < after_helper.index("glViewport(0, 0, ww, wh);")
    < after_helper.index("s_layer_fence[i] = p_glFenceSync("),
    "the layer's drawing leaves the window bound even when the helper drew nothing",
)
require(
    re.findall(r"s_layer_next\b[^;]*;", gl)
    == ["s_layer_next = 0, s_layer_failed = 0;", "s_layer_next;", "s_layer_next = 1 - i;"]
    and layer_update.index("const int i = s_layer_next;")
    < layer_update.index("s_layer_shown = i;")
    < layer_update.index("s_layer_next = 1 - i;")
    and set(re.findall(r"s_layer_shown = ([^,;]+)[,;]", gl)) == {"-1", "i"}
    and re.findall(r"s_layer_shown = ([^,;]+)[,;]", gl).count("i") == 1
    and "    if (drawn && !s_layer_prog) s_layer_failed = 1;\n" in layer_update
    and "if (!make_fbo(&s_layer_fbo[i], s_layer_tex[i], 0)) {\n                s_layer_failed = 1;\n" in layer_update
    and "s_layer_failed = 0;" in function_body(gl, "gl_renderer_shutdown"),
    "the layer drawn is never the one shown: the two take turns, a shown one is only ever the one just drawn or none",
)
for present_name in ("gl_renderer_present_vram", "gl_renderer_present_wide_fbo"):
    require(
        "    if (interp_pair) {\n        interp_layer_update(ww, wh);\n" in function_body(gl, present_name),
        f"{present_name} keeps the layer when the presenter shows its pictures",
    )
presenter = function_body(gl, "interp_present")
require(
    presenter.index("interp_draw_quad(")
    < presenter.index("interp_layer_draw(ww, wh);")
    < presenter.index("s_interp_draw_fence = p_glFenceSync(")
    < presenter.index("shown_capture(1, shown);")
    < presenter.index("SDL_GL_SwapWindow"),
    "the presenter lays the layer over its picture before the fence the main thread waits on, the read-back and the swap",
)
require(
    "s_layer_shown = -1;" in function_body(gl, "interp_reset_history_unlocked"),
    "a history reset drops the layer with the pictures",
)

interpolation_setter = function_body(main_cpp, "psx_host_video_set_interpolation")
require(
    interpolation_setter.count("gl_renderer_interpolation_diag") >= 2
    and "actual != enabled" in interpolation_setter
    and "return 0" in interpolation_setter,
    "live interpolation must report renderer setup failure",
)

aspect_setter = function_body(main_cpp, "psx_host_video_set_display_aspect")
for token in (
    "is_host_fixed_display_aspect",
    "g_pending_video_aspect.store",
    "std::memory_order_release",
):
    require(token in aspect_setter, f"live aspect setter is missing {token}")
fixed_aspects = function_body(main_cpp, "is_host_fixed_display_aspect")
for token in ("numerator == 4", "numerator == 16", "numerator == 21", "numerator == 32"):
    require(token in fixed_aspects, f"fixed aspect validation is missing {token}")
adaptive_setter = function_body(main_cpp, "psx_host_video_set_adaptive_view")
for token in (
    "enabled != 0",
    "enabled != 1",
    "PENDING_VIDEO_ASPECT_ADAPTIVE_ON",
    "PENDING_VIDEO_ASPECT_ADAPTIVE_OFF",
    "g_pending_video_aspect.store",
    "std::memory_order_release",
):
    require(token in adaptive_setter, f"live adaptive-aspect setter is missing {token}")
require(
    "g_ws_adaptive_max_num = ws_adaptive_view_supported" in main_cpp
    and "? 32 : (ws_ultrawide_offered ? 21 : 16)" in main_cpp,
    "saved Match window mode must restore with its 32:9 cap",
)

scale_setter = function_body(main_cpp, "psx_host_video_set_internal_scale")
for token in (
    "scale < 1",
    "scale > SW_MAX_INTERNAL_SCALE",
    "PSX_HOST_UI_BACKEND_OPENGL",
    "std::realloc",
    "gr_set_scale(scale)",
    "gr_scale()",
    "g_video_scale = applied",
):
    require(token in scale_setter, f"live internal-scale setter is missing {token}")

sw_header = read("psxrecomp/runtime/include/gpu_sw_renderer.h")
require(
    "#define SW_MAX_INTERNAL_SCALE 8" in sw_header and "#define GL_MAX_INTERNAL_SCALE SW_MAX_INTERNAL_SCALE" in gl,
    "renderers must share the 8x internal scale limit",
)
require(
    "supersampling out of range (1..8)" in config_cpp and "n >= 1 && n <= 8" in config_cpp,
    "config and persisted internal scale must support 1x-8x",
)

fullscreen_apply = function_body(main_cpp, "psx_apply_window_fullscreen_mode")
for token in (
    "mode < 0",
    "mode > 2",
    "SDL_SetWindowFullscreenMode",
    "SDL_GetClosestFullscreenDisplayMode",
    "SDL_SetWindowFullscreen",
    "SDL_SyncWindow",
    "psx_window_fullscreen_mode(window) == mode",
):
    require(token in fullscreen_apply, f"live fullscreen transition is missing {token}")
fullscreen_setter = function_body(main_cpp, "psx_host_video_set_fullscreen_mode")
for token in (
    "mode < 0",
    "mode > 2",
    "psx_apply_window_fullscreen_mode",
    "g_fullscreen = mode",
):
    require(token in fullscreen_setter, f"live fullscreen setter is missing {token}")
require(
    "g_fullscreen == 2" in main_cpp and "failed to enter configured exclusive fullscreen" in main_cpp,
    "SDL3 exclusive fullscreen must be completed after window creation",
)
require(
    "Deliberately do not update g_fullscreen here" in main_cpp, "the fullscreen hotkey must remain a transient toggle"
)

resize_scale = function_body(gl, "resize_gpu_raster")
for token in (
    "flush_flat_batch",
    "flush_tex_batch",
    "flush_cpu_upload",
    "new_hr_tex",
    "new_scratch_tex",
    "new_wide_tex",
    "p_glBlitFramebuffer",
    "rebuild_target_stencil",
    "u_shift",
    "gl_renderer_invalidate_present",
    "delete_scale_target",
):
    require(token in resize_scale, f"transactional GL internal-scale rebuild is missing {token}")
aspect_apply = function_body(main_cpp, "apply_live_display_aspect")
for token in (
    "gl_renderer_set_display_aspect",
    "SDL_RenderSetLogicalSize",
    "reshape_window_for_fixed_aspect",
    "gte_set_display_aspect",
    "gpu_ws_configure",
    "gl_renderer_invalidate_present",
):
    require(token in aspect_apply, f"live aspect transition is missing {token}")
vblank_aspect_start = main_cpp.index("static void sdl_vblank_present")
vblank_aspect_prefix = main_cpp[vblank_aspect_start : main_cpp.index("#ifndef PSX_NO_DEBUG_TOOLS", vblank_aspect_start)]
require(
    "PendingAspectBoundary" in vblank_aspect_prefix
    and "~PendingAspectBoundary()" in vblank_aspect_prefix
    and "apply_pending_display_aspect" in vblank_aspect_prefix
    and "update_adaptive_widescreen" in vblank_aspect_prefix,
    "aspect requests must apply on vblank-present scope exit",
)
require(
    main_cpp.count("update_adaptive_widescreen();") == 3,
    "resize-driven aspect must change only at vblank-present scope exit",
)
for config_name, config in (
    ("game.toml", game_toml),
    ("game-widescreen.toml", game_wide_toml),
):
    require("offer_ultrawide = true" in config, f"{config_name} would clamp a saved 21:9 aspect on restart")
    require("adaptive_view = true" in config, f"{config_name} would clamp saved Match window mode off")
    require(
        'unsquash_funcs = ["0x8003A0F0"]' in config, f"{config_name} is missing the finite-skyline diagnostic candidate"
    )

require("static int s_ws_backdrop_repair_enabled = 0;" in gte, "the unvalidated finite-skyline probe must default off")

for token in (
    "SDL_SCANCODE_GRAVE",
    "SDL_SCANCODE_ESCAPE",
    'BeginTabItem("Controls")',
    'BeginTabItem("Enhancements")',
    'BeginTabItem("Cheats")',
    'BeginTabItem("Audio")',
    'BeginTabItem("System")',
    "gpu_geometry_correction_set",
    "gpu_texture_correction_set",
    "psx_host_video_set_vsync",
    "psx_host_video_set_display_aspect",
    "psx_host_video_set_internal_scale",
    "psx_host_video_set_fullscreen_mode",
):
    require(token in menu, f"developer menu is missing {token}")

launcher_settings = read("src/launcher/launcher_settings.cpp")
require(
    all(
        f'#if DISRUPTOR_DEV_TOOLS\n            if (ImGui::BeginTabItem("{tab}")) {{\n' in menu
        for tab in ("Experimental", "Cheats")
    )
    and "#if DISRUPTOR_DEV_TOOLS\nconstexpr bool kDeveloperBuild = true;\n#else\nconstexpr bool kDeveloperBuild = false;\n"
    in menu
    and "return kDeveloperBuild ? stored(settings, *find_option(key)) : 0; };\n"
    "    gl_renderer_set_ddgi_environment(kDeveloperBuild);\n"
    in menu
    and "        apply_diagnostic_view(kDeveloperBuild && settings.has_diagnostic_view &&\n" in menu
    and '#if DISRUPTOR_DEV_TOOLS\n    ImGui::Separator();\n    if (ImGui::Button("Record the screen")' in menu
    and "    ImGui::Separator();\n    draw_mission_select();\n#endif\n" in menu
    and "    if (!asked && s_ddgi_environment) {\n" in read("psxrecomp/runtime/src/gpu_gl_ddgi.c.inc")
    and "bool page_rows_shown(Page page, bool dev_tools) { return dev_tools || page != Page::experimental; }"
    in launcher_settings
    and "#if DISRUPTOR_DEV_TOOLS\nbool page_rows_shown(Page page) { return page_rows_shown(page, true); }\n#else\n"
    in launcher_settings
    and "    if (page_rows_shown(static_cast<Page>(page_))) rows();\n    else coming_soon();\n"
    in read("src/launcher/view.cpp")
    and "    target_compile_definitions(disruptor-launcher-model PUBLIC DISRUPTOR_DEV_TOOLS=1)\n"
    in read("src/launcher/CMakeLists.txt")
    and '"DISRUPTOR_DEV_TOOLS": "OFF",' in read("tools/package_release.py"),
    "a release has no Experimental tab, a note in place of the Experimental page's rows, no Cheats tab and no recorder, views or probes on Debug, and its"
    " lighting and views stay off whatever a developer build saved",
)

for forbidden in (
    'BeginTabItem("Diagnostics")',
    "draw_diagnostics_tab",
    "gpu_geometry_correction_stats_detailed",
    "gpu_ws_get_debug",
    "draw_billboard_aspect_diagnostic_controls",
    "draw_finite_backdrop_controls",
    "draw_far_rendering_controls",
    "gte_ws_get_sz_stats",
    "disruptor_far_rendering_get_diagnostics",
    "disruptor_far_rendering_set_depth_fade_mode",
    "PREF_PRECISE_CAMERA",
    "mark_precise_camera",
    '"Sub-byte camera presentation"',
    '"Camera presentation"',
    '"Developer build"',
    '"Widescreen sprite diagnosis"',
    '"Finite skyline diagnosis"',
    '"Experimental: Draw distance"',
):
    require(forbidden not in menu, f"public settings must omit removed debug items: {forbidden}")
require('ImGui::Begin("Disruptor Settings"' in menu, "the public menu must use the settings title")
for function_name in ("on_runtime_ready", "on_runtime_shutdown"):
    session_body = function_body(menu, function_name)
    require(
        "disruptor_high_precision_camera_set_enabled(0)" in session_body
        and "gte_ws_set_backdrop_repair_enabled(0)" in session_body,
        f"{function_name} must reset removed camera and skyline experiments",
    )

for forbidden in (
    "psx_write_byte",
    "psx_write_word",
    "psx_mod_set_native_vblank_rate",
    "PSX_FORCE_INTERP",
):
    require(forbidden not in menu, f"normal developer menu must not expose unsafe guest control {forbidden}")

flags_body = function_body(menu, "current_flags")
for flag in (
    "PSX_HOST_UI_CAPTURE_KEYBOARD",
    "PSX_HOST_UI_CAPTURE_MOUSE",
    "PSX_HOST_UI_CAPTURE_GAMEPAD",
    "PSX_HOST_UI_VISIBLE",
):
    require(flag in flags_body, f"visible menu must request {flag}")

require(
    "psx_host_ui_game_input_captured()" in function_body(mouse, "mouse_aim_frame"),
    "mouse mod must cooperate with host menu input capture",
)

for token in (
    "disruptor_cheats_set_god_mode",
    "disruptor_cheats_grant_all_weapons",
    "Grant all weapons + psionics",
    "marked as cheated",
):
    require(token in menu or token in cheats_h, f"Cheats tab is missing {token}")
require(
    "psx_mod_register_function_entry_plugin" in cheats and "0x80020DD8u" in cheats and "cpu->gpr[4] = 0u" in cheats,
    "God mode must neutralise the audited central damage invocation",
)
require(
    "psx_netplay_active()" in cheats and "psx_mod_game_started()" in cheats,
    "cheats must fail closed outside gameplay and during netplay",
)
require("SDL_" not in cheats and "sio_" not in cheats, "cheat semantics must not synthesize host or guest input")
for forbidden in ("has_god_mode", "has_all_weapons", "PREF_GOD", "PREF_CHEAT"):
    require(
        forbidden not in config_h and forbidden not in config_cpp and forbidden not in menu,
        f"cheat setting must remain session-only: {forbidden}",
    )
require(
    "disruptor_cheats_reset_session();" in function_body(menu, "on_runtime_ready")
    and "disruptor_cheats_reset_session();" in function_body(menu, "on_runtime_shutdown"),
    "cheat toggles must reset at both session boundaries",
)

for token in (
    "DISRUPTOR_FAR_RENDERING_RETAIL",
    "DISRUPTOR_FAR_RENDERING_EXTENDED",
    "DISRUPTOR_FAR_RENDERING_FAR",
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL",
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED",
    "DisruptorFarRenderingDiagnostics",
    "disruptor_far_rendering_set_preset",
    "disruptor_far_rendering_set_depth_fade_mode",
    "disruptor_far_rendering_get_depth_fade_mode",
    "disruptor_far_rendering_get_diagnostics",
    "disruptor_far_rendering_reset_session",
):
    require(token in far_rendering_h, f"far-rendering session ABI is missing {token}")

require(
    "disruptor_far_rendering_reset_session();" in function_body(menu, "on_runtime_ready")
    and "disruptor_far_rendering_reset_session();" in function_body(menu, "on_runtime_shutdown"),
    "far-rendering presets must reset at both session boundaries",
)
for forbidden in ("has_depth_fade", "depth_fade_mode"):
    require(
        forbidden not in config_h and forbidden not in config_cpp,
        f"the depth fade switch must remain outside persistence: {forbidden}",
    )
require(
    "mark_depth_fade" not in menu and "PREF_DEPTH_FADE" not in menu,
    "the depth fade switch must not enter the menu preference layer",
)

for token in (
    "has_frame_interpolation_blend",
    "has_mouse_aim",
    "has_modern_controls",
    "has_horizontal_sensitivity",
    "has_invert_horizontal",
    "has_vertical_look",
    "has_vertical_sensitivity",
    "has_invert_vertical",
    "has_high_precision_camera",
    "has_geometry_correction",
    "has_perspective_textures",
    "has_master_volume",
    "has_audio_muted",
):
    require(token in config_h and token in config_cpp, f"settings.toml schema is missing {token}")

save_body = function_body(config_cpp, "save_user_settings")
require(
    "temporary" in save_body
    and "atomic_replace_user_settings" in save_body
    and "std::ofstream f(temporary" in save_body,
    "settings must be serialized to a sibling temporary file",
)
require(
    "MoveFileExW" in config_cpp and "MOVEFILE_REPLACE_EXISTING" in config_cpp and "std::rename" in config_cpp,
    "settings publication must atomically replace on Windows and POSIX",
)

require(
    "load_preferences_for_session();" in function_body(menu, "on_runtime_ready"),
    "preferences must load independently of opening the ImGui menu",
)
require(
    'follow_scheme(disruptor_modern_controls_enabled() != 0, !env_override_present("PSX_DISRUPTOR_MODERN_CONTROLS"));'
    in function_body(menu, "apply_saved_preferences")
    and "    follow_scheme(enabled, true);\n    disruptor_modern_controls_set_enabled(enabled ? 1 : 0);\n"
    in function_body(menu, "set_modern_controls_live")
    and "apply_modern_keybinds" not in menu
    and "base_keybinds" not in menu,
    "the keys follow the scheme by keybinds.ini's one layout, saved unless the environment chose the scheme",
)
for token in (
    'option_toggle("disruptor.vertical_look", &vertical_look)',
    'option_slider("disruptor.vertical_sensitivity", &vertical_sensitivity)',
    'option_toggle("disruptor.invert_vertical", &invert_y)',
    "Recenter vertical view",
    "disruptor_mouse_set_vertical_look_enabled",
    "disruptor_mouse_set_vertical_sensitivity",
    "disruptor_mouse_set_invert_vertical",
):
    require(token in menu, f"vertical-look menu is missing {token}")
for token in (
    "PSX_DISRUPTOR_VERTICAL_LOOK",
    "PSX_DISRUPTOR_MOUSE_SENSITIVITY_Y",
    "PSX_DISRUPTOR_MOUSE_INVERT_Y",
):
    require(token in menu or token in mouse, f"vertical settings precedence is missing {token}")
require(
    "take_relative_motion" in mouse and mouse.count("take_relative_motion()") == 2,
    "mouse X/Y must come from exactly one relative sample per frame",
)
require(
    "kMaximumPitchUnits = 22.0" in mouse
    and "disruptor_vertical_camera_set_requested_pitch" in mouse
    and "disruptor_vertical_camera_recenter" in mouse
    and "disruptor_vertical_camera_input_allowed()" in mouse,
    "vertical input must use the bounded camera/weapon-aim sink",
)
shutdown_body = function_body(menu, "on_runtime_shutdown")
require(
    "flush_preferences();" in shutdown_body
    and shutdown_body.index("flush_preferences();") < shutdown_body.index("ImGui_ImplOpenGL3_Shutdown"),
    "dirty preferences must flush before UI/renderer teardown",
)
require("io.IniFilename = nullptr" in menu, "unreviewed ImGui layout state must remain outside persistence")
require(
    "PSX_DISABLE_FRAME_INTERPOLATION=1" in cmake, "a Disruptor build must be able to compile frame interpolation out"
)
for forbidden in (
    "psx_host_video_set_interpolation",
    "psx_host_video_get_interpolation",
    "gl_renderer_interpolation_diag",
    "draw_geometry_interpolation_stats",
    "mark_interpolation_",
    "PREF_INTERP_",
    "PSX_HOST_UI_SUSPEND_INTERPOLATION",
    '"Frame interpolation"',
    '"Interpolation mode"',
    '"Presentation target"',
):
    require(forbidden not in menu, f"disabled interpolation must not enter the menu: {forbidden}")
require(
    "#ifdef PSX_DISABLE_FRAME_INTERPOLATION" in interpolation_setter
    and "return enabled ? 0 : 1;" in interpolation_setter,
    "the live API must reject interpolation activation in disabled builds",
)
for name in ("psx_mod_set_frame_interpolation", "psx_mod_set_frame_interpolation_blend"):
    body = function_body(main_cpp, name)
    require(
        body.lstrip().startswith("#ifdef PSX_DISABLE_FRAME_INTERPOLATION") and "return 0;" in body.split("#else", 1)[0],
        f"mods must not enable compiled-out interpolation: {name}",
    )
require(
    '#ifndef PSX_DISABLE_FRAME_INTERPOLATION\n#include "gpu_gl_temporal.c.inc"' in gl
    and "#define interp_capture(...) 0" in gl
    and "#define temporal_draw(...) ((void)0)" in gl
    and "#define temporal_begin(...) ((void)0)" in gl,
    "disabled builds must omit temporal recording and the presenter worker",
)

aspect_controls = function_body(menu, "draw_aspect_controls")
for token in (
    'Checkbox("Widescreen"',
    'option_list("video.aspect_ratio", &selected, 1)',
    "psx_host_video_get_display_aspect",
    "psx_host_video_set_display_aspect",
    "psx_host_video_get_adaptive_view",
    "psx_host_video_set_adaptive_view",
    "mark_aspect",
    'draw_status_badge("LIVE"',
):
    require(token in aspect_controls, f"live aspect controls are missing {token}")

scale_controls = function_body(menu, "draw_internal_resolution_controls")
for token in (
    'SeparatorText("Rendering resolution")',
    'option_list("video.supersampling", &selected)',
    "kScaleCount = 8",
    "psx_host_video_get_internal_scale",
    "psx_host_video_set_internal_scale",
    "mark_supersampling",
    'draw_status_badge("LIVE"',
):
    require(token in scale_controls, f"live internal-resolution controls are missing {token}")
require(
    'void mark_supersampling(int value) { mark("video.supersampling", value - 1); }\n' in menu
    and MERGED in function_body(menu, "merge_dirty_preferences")
    and "void set_scale(Settings& settings, int index) { settings.has_supersampling = true; "
    "settings.supersampling = index + 1; }\n"
    in options
    and "saved<&Settings::has_supersampling>, scale, set_scale)" in options,
    "internal scale must be merged into the user settings",
)
require(
    'changed("video.supersampling")' in function_body(menu, "apply_pending_preferences")
    and "psx_host_video_set_internal_scale" in function_body(menu, "apply_pending_preferences"),
    "a pending internal scale must survive a soft runtime session",
)
require(
    'BulletText("Supersampling/internal framebuffer allocation")' not in menu,
    "live internal scale must not remain listed as restart-only",
)

fullscreen_controls = function_body(menu, "draw_fullscreen_controls")
for token in (
    'SeparatorText("Window presentation")',
    'option_list("video.fullscreen", &mode)',
    "psx_host_video_get_fullscreen_mode",
    "psx_host_video_set_fullscreen_mode",
    "mark_fullscreen",
    'draw_status_badge("LIVE"',
    "runtime rejected the requested display mode",
    "Alt+Enter remains",
    "does not overwrite the ",
    "saved mode",
):
    require(token in fullscreen_controls, f"live fullscreen controls are missing {token}")
require(
    'void mark_fullscreen(int value) { mark("video.fullscreen", value); }\n' in menu
    and MERGED in function_body(menu, "merge_dirty_preferences")
    and "write<&Settings::has_fullscreen, &Settings::fullscreen>)" in options,
    "explicit fullscreen selections must be merged into user settings",
)
require(
    'changed("video.fullscreen")' in function_body(menu, "apply_pending_preferences")
    and "psx_host_video_set_fullscreen_mode" in function_body(menu, "apply_pending_preferences"),
    "a pending fullscreen selection must survive a soft runtime session",
)
require(
    "settings.has_fullscreen" in function_body(menu, "apply_saved_preferences")
    and "psx_host_video_set_fullscreen_mode" in function_body(menu, "apply_saved_preferences"),
    "saved fullscreen mode must be restored for a soft runtime session",
)
require(
    "mark_fullscreen" not in function_body(menu, "on_sdl_event"),
    "menu event handling must not persist transient fullscreen hotkeys",
)

audio_controls = function_body(menu, "draw_audio_tab")
for token in (
    'SeparatorText("Audio output")',
    'option_slider("audio.master_volume", &volume)',
    'option_list("disruptor.bass_cut", &bass)',
    "disruptor_bass_cut_set(bass);",
    "mark_bass_cut(bass);",
    'option_toggle("disruptor.mute_in_background", &in_background)',
    'option_toggle("audio.muted", &muted)',
    "psx_host_audio_get_master_volume",
    "psx_host_audio_set_master_volume",
    "psx_host_audio_get_mute_in_background",
    "psx_host_audio_set_mute_in_background",
    "psx_host_audio_get_muted",
    "psx_host_audio_set_muted",
    "mark_master_volume",
    "mark_mute_in_background",
    "mark_audio_muted",
    'draw_status_badge("LIVE"',
    "game audio state and saves are unchanged",
):
    require(token in audio_controls, f"live host-audio controls are missing {token}")
require(
    "psx_host_audio" not in function_body(menu, "draw_system_tab"),
    "the sound is on its own Audio tab, as on the launcher's Audio page",
)
merge_preferences = function_body(menu, "merge_dirty_preferences")
require(
    'void mark_master_volume(int value) { mark("audio.master_volume", value); }\n' in menu
    and 'void mark_audio_muted(bool value) { mark("audio.muted", value); }\n' in menu
    and 'void mark_mute_in_background(bool value) { mark("disruptor.mute_in_background", value); }\n' in menu
    and MERGED in merge_preferences
    and "write<&Settings::has_master_volume, &Settings::master_volume>)" in options
    and '    toggle<&Settings::has_audio_muted, &Settings::audio_muted>("audio.muted",' in options,
    "host-audio selections must be merged into user settings",
)
pending_preferences = function_body(menu, "apply_pending_preferences")
require(
    'changed("audio.master_volume")' in pending_preferences
    and "psx_host_audio_set_master_volume" in pending_preferences
    and 'changed("audio.muted")' in pending_preferences
    and "psx_host_audio_set_muted" in pending_preferences
    and 'changed("disruptor.mute_in_background")' in pending_preferences
    and "psx_host_audio_set_mute_in_background" in pending_preferences,
    "pending host-audio settings must survive a soft runtime session",
)
saved_preferences = function_body(menu, "apply_saved_preferences")
require(
    "settings.has_master_volume" in saved_preferences
    and "psx_host_audio_set_master_volume" in saved_preferences
    and "settings.has_audio_muted" in saved_preferences
    and "psx_host_audio_set_muted" in saved_preferences
    and "settings.has_mute_in_background" in saved_preferences
    and "psx_host_audio_set_mute_in_background" in saved_preferences,
    "saved host-audio settings must restore for a soft runtime session",
)

volume_setter = function_body(main_cpp, "psx_host_audio_set_master_volume")
require(
    "percent < 0" in volume_setter
    and "percent > 100" in volume_setter
    and "g_audio_master_volume_percent.store" in volume_setter,
    "master-volume API must validate and publish 0..100 percent",
)
mute_setter = function_body(main_cpp, "psx_host_audio_set_muted")
require(
    "muted != 0" in mute_setter and "muted != 1" in mute_setter and "g_audio_master_muted.store" in mute_setter,
    "mute API must validate and publish a boolean latch",
)
background_setter = function_body(main_cpp, "psx_host_audio_set_mute_in_background")
focus = function_body(main_cpp, "sdl_audio_track_window_focus")
require(
    "enabled != 0" in background_setter
    and "enabled != 1" in background_setter
    and "g_audio_mute_in_background.store" in background_setter
    and "SDL_WINDOW_INPUT_FOCUS" in focus
    and "g_audio_window_in_background.store" in focus
    and main_cpp.index("psx_game_after_input();") < main_cpp.index("    sdl_audio_track_window_focus();\n")
    and "(void)psx_host_audio_set_mute_in_background(settings.has_mute_in_background && settings.mute_in_background);"
    in function_body(read("src/game/host.cpp"), "psx_game_settings_loaded"),
    "mute in the background is its own latch beside the player's mute, the window's focus is read once a frame "
    "outside netplay's gate, and the saved choice applies at start in a build without the menu too",
)
master_gain = function_body(main_cpp, "sdl_audio_apply_master_gain")
for token in (
    "g_audio_master_volume_percent.load",
    "g_audio_master_muted.load",
    "g_audio_mute_in_background.load",
    "g_audio_window_in_background.load",
    "muted ? 0.0f",
    "sdl_audio_gain_ramp",
):
    require(token in master_gain, f"host-output gain stage is missing {token}")
drc_callback = function_body(main_cpp, "sdl_drc_callback")
require(
    drc_callback.index("rab_pull")
    < drc_callback.index("sdl_audio_apply_master_gain")
    < drc_callback.index("audio_trace_pcm"),
    "bridge audio must apply master gain at the final host-output boundary",
)
audio_pump = function_body(main_cpp, "sdl_audio_pump")
require(
    audio_pump.index("sdl_audio_fadein_left")
    < audio_pump.index("sdl_audio_apply_master_gain")
    < audio_pump.index("psx_sdl_audio_queue("),
    "legacy audio must apply master gain after transition fade and before queueing",
)
audio_update = function_body(main_cpp, "sdl_audio_update")
require(
    audio_update.index("sdl_audio_apply_master_gain") < audio_update.index("psx_sdl_audio_queue("),
    "the legacy turbo-mute tail must retain the selected master gain",
)
require(
    main_cpp.index("us.has_master_volume") < main_cpp.index("psx_sdl_audio_open")
    and main_cpp.index("us.has_audio_muted") < main_cpp.index("psx_sdl_audio_open"),
    "persisted host-audio controls must load before the audio device opens",
)
host = read("src/game/host.cpp")
require(
    main_cpp.count("psx_game_audio_output(") == 2
    and "psx_game_audio_output" not in drc_callback
    and audio_pump.index("spu_render(sdl_audio_buf, frames);")
    < audio_pump.index("g_turbo_audio_sink_frames += (uint64_t)frames;")
    < audio_pump.index("psx_game_audio_output(sdl_audio_buf, frames, 44100);")
    < audio_pump.index("if (sdl_audio_fadein_left > 0)")
    < audio_pump.index("sdl_audio_apply_master_gain")
    and audio_update.index("spu_render(sdl_audio_buf, tail);")
    < audio_update.index("psx_game_audio_output(sdl_audio_buf, tail, 44100);")
    < audio_update.index("sdl_audio_gain_ramp(sdl_audio_buf, tail, g0, 0.0f);")
    and "disruptor::audio::shape_output(stereo, frames, rate);" in function_body(host, "psx_game_audio_output")
    and "disruptor_bass_cut_set(settings.has_bass_cut ? settings.bass_cut : 0);"
    in function_body(host, "psx_game_settings_loaded"),
    "the bass cut shapes the SPU's output where it is rendered, before every fade, gain and the bridge, so a mute "
    "or a fade to silence ends in silence, and the saved cut applies at start in a build without the menu too",
)

for function_name in ("on_runtime_ready", "on_runtime_shutdown"):
    session_body = function_body(menu, function_name)
    require(
        "disruptor_billboard_aspect_all_site_mask" in session_body
        and "disruptor_billboard_aspect_set_site_mask" in session_body,
        f"{function_name} must restore every billboard repair path",
    )
require(
    'g_preferences.changed.insert(find_option("video.aspect_ratio"));' in function_body(menu, "mark_aspect")
    and MERGED in function_body(menu, "merge_dirty_preferences")
    and "    settings.has_aspect_ratio = settings.has_adaptive_view = true;\n"
    "    settings.adaptive_view = index == window_aspect;\n"
    in options
    and "aspect_saved, aspect, set_aspect)" in options,
    "fixed and adaptive aspect choices must persist atomically",
)
require(
    'changed("video.aspect_ratio")' in function_body(menu, "apply_pending_preferences")
    and "psx_host_video_set_display_aspect" in function_body(menu, "apply_pending_preferences")
    and "psx_host_video_set_adaptive_view" in function_body(menu, "apply_pending_preferences"),
    "a pending aspect choice must survive a soft runtime session",
)
require(
    "        if (us.has_frame_interpolation)\n            g_frame_interpolation = us.frame_interpolation ? 1 : 0;\n"
    in main_cpp
    and "        !g_geometry_correction)\n        g_frame_interpolation = 0;\n" in main_cpp,
    "a saved in-between frames switch must be restored, and geometry in-between frames never without exact geometry",
)

main_settings = main_cpp.index("load_user_settings(settings_path)")
main_env = main_cpp.index('std::getenv("PSX_GEOMETRY_CORRECTION")')
require(
    main_settings < main_cpp.index("us.has_geometry_correction") < main_env
    and main_settings < main_cpp.index("us.has_perspective_textures") < main_env,
    "saved enhancements must load before explicit environment overrides",
)
aspect_env = main_cpp.index('std::getenv("PSX_VIDEO_ASPECT")')
require(
    main_settings < main_cpp.index("us.has_aspect_ratio") < aspect_env
    and "is_host_fixed_display_aspect" in main_cpp[aspect_env:main_env],
    "explicit aspect override must be validated after saved settings load",
)
require(
    "UserSettings seed =" in main_cpp
    and "load_user_settings(" in main_cpp[main_cpp.index("UserSettings seed =") : main_cpp.index("seed.renderer =")],
    "a generic launcher save must merge rather than drop game-owned fields",
)

require(
    "PSX_DISRUPTOR_MODERN_CONTROLS = if" not in run_ps1
    and "PSX_TEXTURE_CORRECTION = if" not in run_ps1
    and "$env:PSX_DISRUPTOR_MOUSE_AIM = '0'" not in run_ps1,
    "PowerShell must not synthesize disabling overrides for absent switches",
)
require(
    "[switch]$VerticalLook" in run_ps1
    and "PSX_DISRUPTOR_VERTICAL_LOOK" in run_ps1
    and "--vertical-look" in run_sh
    and "PSX_DISRUPTOR_VERTICAL_LOOK=1" in run_sh,
    "launchers must expose an explicit vertical-look opt-in",
)
require(
    "PSX_DISRUPTOR_VERTICAL_LOOK = if" not in run_ps1 and "export PSX_DISRUPTOR_VERTICAL_LOOK=0" not in run_sh,
    "launchers must not synthesize a disabling vertical override",
)
require(
    "export PSX_DISRUPTOR_MODERN_CONTROLS=$MODERN_CONTROLS" not in run_sh
    and "export PSX_DISRUPTOR_MOUSE_AIM=0" not in run_sh,
    "POSIX launcher must not synthesize disabling overrides",
)
require(
    "--perspective-textures" in run_sh and "PERSPECTIVE_TEXTURES=1; GEOMETRY_CORRECTION=1" in run_sh,
    "POSIX perspective-texture launch must explicitly imply geometry",
)
require(
    "PSX_VIDEO_ASPECT'] = '16:9'" in run_ps1 and "export PSX_VIDEO_ASPECT=16:9" in run_sh,
    "explicit widescreen launch switches must override a saved aspect",
)

shared_keys = set(re.findall(r'\b(?:toggle|choice|slider)(?:<[^>]*>)?\(\s*"([\w.]+)"', options))
staged_keys = set(re.findall(r'\bmark\("([\w.]+)"', menu)) | set(
    re.findall(r'g_preferences\.changed\.insert\(\s*find_option\("([\w.]+)"\)\)', menu)
)
applied_keys = set(re.findall(r'\bchanged\("([\w.]+)"\)', function_body(menu, "apply_pending_preferences")))
require(
    len(shared_keys) == 65
    and staged_keys == shared_keys
    and applied_keys == shared_keys
    and set(re.findall(r'\bfind_option\("([\w.]+)"', menu)) <= shared_keys,
    "the menu must stage every shared option, apply every one it staged and name no key the table lacks",
)
require(
    'void mark_vsync(int value) { mark("video.vsync", value + 1); }\n' in menu
    and "    g_preferences.pending.horizontal_sensitivity = value;\n"
    "    g_preferences.changed.insert(\n"
    '        find_option("disruptor.horizontal_sensitivity"));\n'
    in function_body(menu, "mark_sensitivity")
    and "    g_preferences.pending.vertical_sensitivity = value;\n"
    "    g_preferences.changed.insert(\n"
    '        find_option("disruptor.vertical_sensitivity"));\n'
    in function_body(menu, "mark_vertical_sensitivity")
    and "(void)disruptor_mouse_set_horizontal_sensitivity(\n            pending.horizontal_sensitivity);"
    in function_body(menu, "apply_pending_preferences")
    and "int vsync(const Settings& settings) { return std::clamp(settings.vsync + 1, 0, 2); }\n" in options
    and "    return static_cast<int>(std::lround(settings.*Value * 1000.0));\n" in options
    and "    settings.*Value = value / 1000.0;\n" in options,
    "a value the menu holds in its own unit must be staged in the shared table's, and a sensitivity kept whole "
    "for the running game and rounded only where it is written",
)
require(
    STAGED in menu
    and "    return g_preferences.changed.contains(find_option(key));\n" in menu
    and "    std::set<const Option *> changed;\n" in menu
    and "        return !changed.empty() || language_disc_changed ||\n               diagnostic_view_changed;\n" in menu
    and "        changed.clear();\n        language_disc_changed = diagnostic_view_changed = false;\n" in menu
    and "    if (!g_preferences.dirty()) return true;\n" in function_body(menu, "flush_preferences")
    and "    g_preferences.clean();\n    g_preferences.save_failed = false;\n"
    in function_body(menu, "flush_preferences")
    and "PreferenceDirty" not in menu
    and "PREF_" not in menu
    and "    list(APPEND DISRUPTOR_EXTRA_SOURCES\n"
    '        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/options.cpp"\n'
    '        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/menu/preferences.cpp"\n'
    in cmake,
    "a staged change must be an option of the shared table, and a save must write and then forget every one",
)

HELPERS = (
    "bool option_toggle(const char *key, bool *value) {\n"
    "    const Option &option = *find_option(key);\n"
    "    const bool moved = ImGui::Checkbox(option.label, value);\n"
    "    disruptor::ui::hint(option.hint);\n"
    "    return moved;\n"
    "}\n"
    "\n"
    "bool option_slider(const char *key, int *value) {\n"
    "    const Option &option = *find_option(key);\n"
    '    const char *format = option.unit == Unit::percent ? "%d%%" : option.unit == Unit::degrees ? "%d\\xc2\\xb0" : "%d";\n'
    "    const bool moved =\n"
    "        ImGui::SliderInt(option.label, value, option.lowest, option.highest,\n"
    "                         format, ImGuiSliderFlags_AlwaysClamp);\n"
    "    disruptor::ui::hint(option.hint);\n"
    "    return moved;\n"
    "}\n"
    "\n"
    "bool option_slider(const char *key, float *value) {\n"
    "    const Option &option = *find_option(key);\n"
    "    // Shown as a multiple of what a new installation has, as the launcher shows it.\n"
    "    const float unit = static_cast<float>(option.initial) / 1000.0f;\n"
    "    float times = *value / unit;\n"
    "    const bool moved = ImGui::SliderFloat(\n"
    "        option.label, &times, option.lowest / 1000.0f / unit,\n"
    '        option.highest / 1000.0f / unit, times < 1.0f ? "%.2fx" : "%.1fx",\n'
    "        ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat);\n"
    "    if (moved) *value = times * unit;\n"
    "    disruptor::ui::hint(option.hint);\n"
    "    return moved;\n"
    "}\n"
    "\n"
    "bool option_list(const char *key, int *index, size_t skipped) {\n"
    "    const Option &option = *find_option(key);\n"
    "    const auto offered = option.choices.first(std::min(option.choices.size(), static_cast<size_t>(option.highest + 1)));\n"
    "    const auto shown = offered.subspan(std::min(skipped, offered.size()));\n"
    "    const bool moved = ImGui::Combo(option.label, index, shown.data(),\n"
    "                                    static_cast<int>(shown.size()));\n"
    "    disruptor::ui::hint(option.hint);\n"
    '    if (option.notice) ImGui::TextColored(ImVec4(0.878f, 0.643f, 0.227f, 1.0f), "%s", option.notice);\n'
    "    return moved;\n"
    "}\n"
)
shared_labels = re.findall(r'\b(?:toggle(?:<[^>]*>)?|choice|slider)\(\s*"[\w.]+",\s*"([^"]+)"', options)
shared_choices = re.findall(
    r'"([^"]+)"', "".join(re.findall(r"constexpr const char\* \w+\[\] = \{([^}]*)\};", options))
)
drawn_keys = set(re.findall(r'\boption_(?:toggle|slider|list)\("([\w.]+)"', menu)) | set(
    re.findall(r'find_option\("([\w.]+)"\)->label', menu)
)
present = read("src/game/frame/present_rate.cpp")
require(
    HELPERS in menu
    and "bool option_list(const char *key, int *index, size_t skipped = 0);\n" in menu
    and len(shared_labels) == 65
    and drawn_keys == shared_keys
    and not [text for text in [*shared_labels, *shared_choices] if f'"{text}"' in menu + present]
    and "    static constexpr int kScaleCount = 8;\n" in menu
    and '"1x (native)", "2x", "3x", "4x", "5x", "6x", "7x", "8x"' in options
    and '{"4:3", "16:9", "21:9", "32:9", "Match window (up to 32:9)"}' in options
    and '{"Windowed", "Borderless fullscreen", "Exclusive fullscreen"}' in options
    and '{"Adaptive", "Immediate", "Synchronised"}' in options
    and '"Horizontal sensitivity", 80,\n           "", 5, 2000, Unit::thousandths,' in options
    and '"Vertical sensitivity", 80,\n           "", 5, 2000, Unit::thousandths,' in options
    and '"Master volume", 100,\n           "", 0, 100, Unit::percent,' in options,
    "the menu must draw every shared option by its key, with the table's label, bounds and choices and none of its own",
)

RULE = (
    "bool applies(const Option& option, const std::function<bool(const Option&)>& on) {\n"
    "    return std::all_of(option.needs.begin(), option.needs.end(), [&](const char* key) {\n"
    "        const Option* needed = find_option(key);\n"
    "        return needed && on(*needed) && applies(*needed, on);\n"
    "    });\n"
    "}\n"
)
LIVE = (
    "bool prerequisite_on(const Option &option) {\n"
    "    const std::string_view key(option.key);\n"
    '    if (key == "disruptor.geometry_correction")\n'
    "        return gpu_geometry_correction_enabled() != 0;\n"
    '    if (key == "disruptor.mouse_aim") return disruptor_mouse_aim_enabled() != 0;\n'
    '    if (key == "disruptor.ambient_occlusion") return gl_renderer_ssao() != 0;\n'
    '    if (key == "disruptor.smooth_fade") return gl_renderer_smooth_fade() != 0;\n'
    '    if (key == "disruptor.probe_lighting") return gl_renderer_ddgi() != 0;\n'
    '    if (key == "disruptor.dynamic_shadows") return gl_renderer_ddgi_shadows() != 0;\n'
    '    if (key == "disruptor.shot_lights") return gl_renderer_ddgi_lamps() != 0;\n'
    '    if (key == "disruptor.sun_lighting") return gl_renderer_ddgi_sun() != 0;\n'
    '    if (key == "disruptor.vertical_look") return disruptor_mouse_vertical_look_enabled() != 0;\n'
    '    if (key == "disruptor.number_keys") return disruptor_number_keys_enabled() != 0;\n'
    '    if (key == "disruptor.stick_layout") return disruptor::sticks::tuning().layout != disruptor::sticks::Layout::classic;\n'
    '    if (key == "disruptor.achievements_popups") return disruptor::achievements::presentation().popups;\n'
    '    if (key == "disruptor.separate_vertical_speed")\n'
    "        return disruptor_mouse_vertical_follows_horizontal() == 0;\n"
    "    return false;\n"
    "}\n"
    "\n"
    "}  // namespace\n"
    "\n"
    "bool applies(const char *key) {\n"
    "    return disruptor::settings::applies(*find_option(key), prerequisite_on);\n"
    "}\n"
)
key_names = dict(re.findall(r'constexpr const char\* (\w+_key) = "([\w.]+)";', options))
needs_lists = {
    name: [key_names[word.strip()] for word in words.split(",")]
    for name, words in re.findall(r"constexpr const char\* (needs_\w+)\[\] = \{([^}]*)\};", options)
}
table_rows = re.split(
    r"\n    (?=toggle[<(]|choice\(|slider\()", options[options.index("constexpr Option option_list[]") :]
)[1:]
needing = {}
for row in table_rows:
    row = row.split("\n};")[0]
    ending = re.search(r"\b(needs_\w+)(?:, \w+_offered(?:,\s*\"[^\"]*\")?)?\),?\s*$", row)
    if ending:
        needing[re.search(r'"([\w.]+)"', row)[1]] = needs_lists[ending[1]]
asked = set(re.findall(r'\bapplies\("([\w.]+)"\)', menu))
told = set(re.findall(r'key == "([\w.]+)"', function_body(menu, "prerequisite_on")))
require(
    RULE in options
    and LIVE in menu
    and len(needing) == 34
    and needing["disruptor.high_precision_camera"] == ["disruptor.mouse_aim", "disruptor.geometry_correction"]
    and asked == set(needing) - {"video.frame_interpolation_fps"}
    and told == {key for keys in needing.values() for key in keys} - {"video.frame_interpolation"}
    and "    if (!enabled) ImGui::BeginDisabled();\n" in function_body(present, "disruptor_present_rate_control")
    and "if (!geometry)" not in menu
    and "turn_applies = mouse_aim" not in menu,
    "the menu must grey a control by the shared table's rule alone, asked of the running game, and the "
    "rate control must grey itself while in-between frames are off",
)


def greyed_by_own_rule(source: str, key: str) -> bool:
    rule = re.search(r'const bool (\w+) =[^;]*\bapplies\("' + re.escape(key) + r'"\)[^;]*;\n', source)
    if rule is None:
        return False
    begin, end = f"    if (!{rule[1]}) ImGui::BeginDisabled();\n", f"    if (!{rule[1]}) ImGui::EndDisabled();\n"
    control = re.search(r'\boption_(?:toggle|slider|list)\("' + re.escape(key) + '"', source)
    if control is None or source.count(begin) != 1 or source.count(end) != 1:
        return False
    opened, closed = source.index(begin), source.index(end)
    return (
        rule.end() <= opened < control.start() < closed
        and "Disabled()" not in source[opened + len(begin) : control.start()]
        and "BeginDisabled()" not in source[control.start() : closed]
    )


greyed = asked - {"video.frame_interpolation"}
require(
    len(greyed) == 32
    and all(greyed_by_own_rule(menu, key) for key in greyed)
    and not greyed_by_own_rule(
        menu.replace("    if (!textures_apply) ImGui::BeginDisabled();\n", "    if (false) ImGui::BeginDisabled();\n"),
        "disruptor.perspective_textures",
    )
    and not greyed_by_own_rule(
        menu.replace('applies("disruptor.gap_filling")', 'applies("disruptor.vsync")'), "disruptor.gap_filling"
    ),
    "each control that needs something must be drawn inside a disabled block opened by its own rule",
)

HINT = (
    "inline void hint(const char *text) {\n"
    "    if (!*text || !ImGui::BeginItemTooltip()) return;\n"
    "    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);\n"
    "    for (std::string_view rest = text;;) {\n"
    "        const std::size_t end = rest.find('\\n');\n"
    "        const std::string_view line = rest.substr(0, end);\n"
    "        if (const std::size_t name = disruptor::settings::choice_name(line)) {\n"
    "            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.44f, 0.81f, 0.48f, 1.0f));\n"
    "            ImGui::TextUnformatted(line.data(), line.data() + name);\n"
    "            ImGui::PopStyleColor();\n"
    "            ImGui::SameLine(0.0f, 0.0f);\n"
    "            ImGui::TextUnformatted(line.data() + name, line.data() + line.size());\n"
    "        } else {\n"
    "            ImGui::TextUnformatted(line.data(), line.data() + line.size());\n"
    "        }\n"
    "        if (end == std::string_view::npos) break;\n"
    "        rest.remove_prefix(end + 1);\n"
    "    }\n"
    "    ImGui::PopTextWrapPos();\n"
    "    ImGui::EndTooltip();\n"
    "}\n"
)
LITERAL = r'"((?:[^"\\]|\\.)*)"'
shared_hints = [
    "".join(re.findall(LITERAL, pieces))
    for pieces in re.findall(
        r'\b(?:toggle(?:<[^>]*>)?|choice|slider)\(\s*"[\w.]+",\s*"[^"]+",\s*-?\d+,\s*((?:"(?:[^"\\]|\\.)*"\s*)+)',
        options,
    )
]
written = "".join(re.findall(LITERAL, menu + present))
require(
    HINT in read("src/game/ui/hint.h")
    and len(shared_hints) == 65
    and all(not text or 20 <= len(text) <= 240 for text in shared_hints)
    and not [text for text in shared_hints if text and text[:32] in written]
    and "Also needs Exact-provenance geometry"
    in shared_hints[
        [key for key in re.findall(r'\b(?:toggle(?:<[^>]*>)?|choice|slider)\(\s*"([\w.]+)"', options)].index(
            "disruptor.high_precision_camera"
        )
    ],
    "the menu must show an option's description from the shared table while the pointer rests on its control, "
    "and write none of the descriptions itself",
)


runtime = read("psxrecomp/runtime/src/main.cpp")
require(
    '[controller]\ndefault_mode = "digital"\n# A connected gamepad plays beside the keyboard and mouse.\n'
    'p1_device = "keyboard+gamepad"\n'
    in read("game.toml")
    and "if (gc.runtime.has_default_p1_device) p1_device = gc.runtime.default_p1_device;" in runtime
    and runtime.index("if (gc.runtime.has_default_p1_device)")
    < runtime.index("if (us.has_p1_device) p1_device = us.p1_device;")
    and 'else if (d == "keyboard+gamepad") { p.kind = 2; p.keyboard_too = true; }' in runtime
    and "    p.keyboard_too = false;\n" in function_body(runtime, "set_player_device")
    and "        return controller_pad_buttons(p.handle, suppress_stick_axes) & pad_from_keyboard(player);\n"
    in function_body(runtime, "pad_buttons_for")
    and "if (keyboard[axis] != 0x80) out[axis] = keyboard[axis];" in function_body(runtime, "pad_sticks_for")
    and "if (p.kind == 1 || p.keyboard_too || dev_any) {" in function_body(runtime, "hybrid_dpad_active"),
    "Player 1 reads the keyboard and the first gamepad together unless settings.toml names a device",
)
require(
    'if (changed("controller.deadzone")) psx_input_set_deadzone(pending.deadzone);' in menu
    and '    mark("controller.deadzone", percent);\n    psx_input_set_deadzone(g_preferences.pending.deadzone);\n'
    in menu
    and "psx_input_pad_name(0)" in menu,
    "the game's Controls tab names the pad it reads and moves the dead zone live, as the launcher saves it",
)

require(
    "    set_window_icon(window);\n" in function_body(menu, "on_runtime_ready")
    and "SDL_LoadBMP_IO(SDL_IOFromConstMem(disruptor_window_icon_bmp, disruptor_window_icon_bmp_size), true)" in menu
    and "    SDL_SetWindowIcon(window, icon);\n" in menu
    and 'set(DISRUPTOR_WINDOW_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/launcher/assets/app-64.bmp")' in cmake
    and 'list(APPEND DISRUPTOR_EXTRA_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/window_icon.cpp")' in cmake
    and 'target_sources(psx-runtime PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/game/icon.rc")' in cmake
    and read("src/game/icon.rc") == '1 ICON "../launcher/assets/app.ico"\n',
    "the game's window and executable carry the launcher's icon",
)

print("developer menu contract: PASS")
