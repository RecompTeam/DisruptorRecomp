#!/usr/bin/env python3
"""Source-only contract for the host-side Disruptor developer menu."""

from __future__ import annotations

import re
from pathlib import Path


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
    require(len(candidates) == 1,
            "host UI GL rendering must be centralised in one state-restoring helper")
    return candidates[0]


cmake = read("CMakeLists.txt")
manifest = read("PSXRECOMP_OVERLAY_FILES.txt")
host_ui = read("psxrecomp-overlay/runtime/include/host_ui.h")
main_cpp = read("psxrecomp-overlay/runtime/src/main.cpp")
gl = read("psxrecomp-overlay/runtime/src/gpu_gl_renderer.c")
gte = read("psxrecomp-overlay/runtime/src/gte.cpp")
menu = read("src/game/ui/dev_menu.cpp")
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
mouse = read("src/game/input/mouse_aim.cpp")
cheats = read("src/game/session/cheats.cpp")
cheats_h = read("src/game/session/cheats.h")
far_rendering_h = read("src/game/render/far_rendering.h")
config_h = read("psxrecomp-overlay/recompiler/src/config_loader.h")
config_cpp = read("psxrecomp-overlay/recompiler/src/config_loader.cpp")
run_ps1 = read("run.ps1")
run_sh = read("run.sh")
game_toml = read("game.toml")
game_wide_toml = read("game-widescreen.toml")

require("option(DISRUPTOR_DEV_MENU" in cmake,
        "developer menu must remain independently build-selectable")
require("v1.92.6.tar.gz" in cmake and
        "5b17c01f69545bde732b14936d89ce0f508adb83e8b56fa82448371845172bc3"
        in cmake.lower(),
        "Dear ImGui source must remain versioned and integrity-pinned")
require("src/game/ui/dev_menu.cpp" in cmake and
        "target_link_libraries(psx-runtime PRIVATE disruptor-imgui)" in cmake,
        "menu implementation must be linked only into the game runtime")
require("src/game/session/cheats.cpp" in cmake and
        "tests/unit/disruptor_cheats_test.cpp" in cmake,
        "version-pinned cheats and their behavioral test must be built")
require("+runtime/include/host_ui.h" in manifest,
        "new host UI ABI header must remain reviewed in the overlay manifest")

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
require("psx_host_ui_handle_event(&ev)" in event_body,
        "SDL events must reach the menu before runtime hotkeys")
require(event_body.index("psx_host_ui_handle_event(&ev)") <
        event_body.index("key >= SDLK_F1"),
        "menu event consumption must precede save-state hotkeys")
require("psx_host_ui_capture_flags()" in
        function_body(main_cpp, "sample_pad_into_sio"),
        "polled keyboard/gamepad input must be neutralised while the menu is open")
require("host_ui_runtime_shutdown();" in
        function_body(main_cpp, "shutdown_runtime"),
        "menu GL resources must be destroyed before renderer/window teardown")

host_ui_helper, drawable_width, drawable_height, host_ui_helper_body = (
    host_ui_gl_restore_helper(gl)
)
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
require(default_fbo is not None and drawable_viewport is not None and
        default_fbo.start() < drawable_viewport.start(),
        "host UI helper must restore default FBO then the current drawable viewport")

for present_name in (
    "gl_renderer_present",
    "gl_renderer_present_blank",
    "gl_renderer_present_vram",
    "gl_renderer_present_wide_fbo",
):
    body = function_body(gl, present_name)
    helper_call = f"{host_ui_helper}("
    require(helper_call in body and "psx_host_ui_render_gl" not in body and
            "SDL_GL_SwapWindow" in body,
            f"{present_name} must use the state-restoring host UI helper")
    require(body.index(helper_call) <
            body.index("SDL_GL_SwapWindow"),
            f"{present_name} renders the menu after SwapWindow")

require("s_interp_content_suspended" in gl and
        "s_interp_host_ui_suspended" in gl,
        "FMV and menu interpolation suspension reasons must remain independent")

interpolation_setter = function_body(main_cpp,
                                     "psx_host_video_set_interpolation")
require(interpolation_setter.count("gl_renderer_interpolation_diag") >= 2 and
        "actual != enabled" in interpolation_setter and
        "return 0" in interpolation_setter,
        "live interpolation must report renderer setup failure")

aspect_setter = function_body(main_cpp,
                              "psx_host_video_set_display_aspect")
for token in (
    "is_host_fixed_display_aspect",
    "g_pending_video_aspect.store",
    "std::memory_order_release",
):
    require(token in aspect_setter,
            f"live aspect setter is missing {token}")
fixed_aspects = function_body(main_cpp, "is_host_fixed_display_aspect")
for token in ("numerator == 4", "numerator == 16", "numerator == 21",
              "numerator == 32"):
    require(token in fixed_aspects,
            f"fixed aspect validation is missing {token}")
adaptive_setter = function_body(main_cpp,
                                "psx_host_video_set_adaptive_view")
for token in (
    "enabled != 0", "enabled != 1", "PENDING_VIDEO_ASPECT_ADAPTIVE_ON",
    "PENDING_VIDEO_ASPECT_ADAPTIVE_OFF", "g_pending_video_aspect.store",
    "std::memory_order_release",
):
    require(token in adaptive_setter,
            f"live adaptive-aspect setter is missing {token}")
require("g_ws_adaptive_max_num = ws_adaptive_view_supported" in main_cpp and
        "? 32 : (ws_ultrawide_offered ? 21 : 16)" in main_cpp,
        "saved Match window mode must restore with its 32:9 cap")

scale_setter = function_body(main_cpp, "psx_host_video_set_internal_scale")
for token in (
    "scale < 1", "scale > SW_MAX_INTERNAL_SCALE",
    "PSX_HOST_UI_BACKEND_OPENGL", "std::realloc", "gr_set_scale(scale)",
    "gr_scale()", "g_video_scale = applied",
):
    require(token in scale_setter,
            f"live internal-scale setter is missing {token}")

sw_header = read("psxrecomp-overlay/runtime/include/gpu_sw_renderer.h")
require("#define SW_MAX_INTERNAL_SCALE 8" in sw_header and
        "#define GL_MAX_INTERNAL_SCALE SW_MAX_INTERNAL_SCALE" in gl,
        "renderers must share the 8x internal scale limit")
require("supersampling out of range (1..8)" in config_cpp and
        "n >= 1 && n <= 8" in config_cpp,
        "config and persisted internal scale must support 1x-8x")

fullscreen_apply = function_body(main_cpp,
                                 "psx_apply_window_fullscreen_mode")
for token in (
    "mode < 0", "mode > 2", "SDL_SetWindowFullscreenMode",
    "SDL_GetClosestFullscreenDisplayMode", "SDL_SetWindowFullscreen",
    "SDL_SyncWindow", "psx_window_fullscreen_mode(window) == mode",
):
    require(token in fullscreen_apply,
            f"live fullscreen transition is missing {token}")
fullscreen_setter = function_body(main_cpp,
                                  "psx_host_video_set_fullscreen_mode")
for token in (
    "mode < 0", "mode > 2", "psx_apply_window_fullscreen_mode",
    "g_fullscreen = mode",
):
    require(token in fullscreen_setter,
            f"live fullscreen setter is missing {token}")
require("g_fullscreen == 2" in main_cpp and
        "failed to enter configured exclusive fullscreen" in main_cpp,
        "SDL3 exclusive fullscreen must be completed after window creation")
require("Deliberately do not update g_fullscreen here" in main_cpp,
        "the fullscreen hotkey must remain a transient toggle")

resize_scale = function_body(gl, "resize_gpu_raster")
for token in (
    "flush_flat_batch", "flush_tex_batch", "flush_cpu_upload",
    "new_hr_tex", "new_scratch_tex", "new_wide_tex",
    "p_glBlitFramebuffer", "rebuild_target_stencil", "u_shift",
    "gl_renderer_invalidate_present", "delete_scale_target",
):
    require(token in resize_scale,
            f"transactional GL internal-scale rebuild is missing {token}")
aspect_apply = function_body(main_cpp, "apply_live_display_aspect")
for token in (
    "gl_renderer_set_display_aspect",
    "SDL_RenderSetLogicalSize",
    "reshape_window_for_fixed_aspect",
    "gte_set_display_aspect",
    "gpu_ws_configure",
    "gl_renderer_invalidate_present",
):
    require(token in aspect_apply,
            f"live aspect transition is missing {token}")
vblank_aspect_start = main_cpp.index("static void sdl_vblank_present")
vblank_aspect_prefix = main_cpp[
    vblank_aspect_start:
    main_cpp.index("#ifndef PSX_NO_DEBUG_TOOLS", vblank_aspect_start)
]
require("PendingAspectBoundary" in vblank_aspect_prefix and
        "~PendingAspectBoundary()" in vblank_aspect_prefix and
        "apply_pending_display_aspect" in vblank_aspect_prefix and
        "update_adaptive_widescreen" in vblank_aspect_prefix,
        "aspect requests must apply on vblank-present scope exit")
require(main_cpp.count("update_adaptive_widescreen();") == 3,
        "resize-driven aspect must change only at vblank-present scope exit")
for config_name, config in (
    ("game.toml", game_toml),
    ("game-widescreen.toml", game_wide_toml),
):
    require("offer_ultrawide = true" in config,
            f"{config_name} would clamp a saved 21:9 aspect on restart")
    require("adaptive_view = true" in config,
            f"{config_name} would clamp saved Match window mode off")
    require('unsquash_funcs = ["0x8003A0F0"]' in config,
            f"{config_name} is missing the finite-skyline diagnostic candidate")

require("static int s_ws_backdrop_repair_enabled = 0;" in gte,
        "the unvalidated finite-skyline probe must default off")

for token in (
    "SDL_SCANCODE_GRAVE",
    "SDL_SCANCODE_ESCAPE",
    'BeginTabItem("Controls")',
    'BeginTabItem("Enhancements")',
    'BeginTabItem("Cheats")',
    'BeginTabItem("System")',
    "gpu_geometry_correction_set",
    "gpu_texture_correction_set",
    "psx_host_video_set_vsync",
    "psx_host_video_set_display_aspect",
    "psx_host_video_set_internal_scale",
    "psx_host_video_set_fullscreen_mode",
):
    require(token in menu, f"developer menu is missing {token}")

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
    "disruptor_far_rendering_set_preset",
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
    require(forbidden not in menu,
            f"public settings must omit removed debug items: {forbidden}")
require('ImGui::Begin("Disruptor Settings"' in menu,
        "the public menu must use the settings title")
for function_name in ("on_runtime_ready", "on_runtime_shutdown"):
    session_body = function_body(menu, function_name)
    require("disruptor_high_precision_camera_set_enabled(0)" in session_body and
            "gte_ws_set_backdrop_repair_enabled(0)" in session_body,
            f"{function_name} must reset removed camera and skyline experiments")

for forbidden in (
    "psx_write_byte",
    "psx_write_word",
    "psx_mod_set_native_vblank_rate",
    "PSX_FORCE_INTERP",
):
    require(forbidden not in menu,
            f"normal developer menu must not expose unsafe guest control {forbidden}")

flags_body = function_body(menu, "current_flags")
for flag in (
    "PSX_HOST_UI_CAPTURE_KEYBOARD",
    "PSX_HOST_UI_CAPTURE_MOUSE",
    "PSX_HOST_UI_CAPTURE_GAMEPAD",
    "PSX_HOST_UI_VISIBLE",
):
    require(flag in flags_body, f"visible menu must request {flag}")

require("psx_host_ui_game_input_captured()" in
        function_body(mouse, "mouse_aim_frame"),
        "mouse mod must cooperate with host menu input capture")

for token in (
    "disruptor_cheats_set_god_mode",
    "disruptor_cheats_grant_all_weapons",
    "Grant all weapons + psionics",
    "marked as cheated",
):
    require(token in menu or token in cheats_h,
            f"Cheats tab is missing {token}")
require("psx_mod_register_function_entry_plugin" in cheats and
        "0x80020DD8u" in cheats and "cpu->gpr[4] = 0u" in cheats,
        "God mode must neutralise the audited central damage invocation")
require("psx_netplay_active()" in cheats and
        "psx_mod_game_started()" in cheats,
        "cheats must fail closed outside gameplay and during netplay")
require("SDL_" not in cheats and "sio_" not in cheats,
        "cheat semantics must not synthesize host or guest input")
for forbidden in ("has_god_mode", "has_all_weapons", "PREF_GOD", "PREF_CHEAT"):
    require(forbidden not in config_h and forbidden not in config_cpp and
            forbidden not in menu,
            f"cheat setting must remain session-only: {forbidden}")
require("disruptor_cheats_reset_session();" in
        function_body(menu, "on_runtime_ready") and
        "disruptor_cheats_reset_session();" in
        function_body(menu, "on_runtime_shutdown"),
        "cheat toggles must reset at both session boundaries")

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
    require(token in far_rendering_h,
            f"far-rendering session ABI is missing {token}")

require("disruptor_far_rendering_reset_session();" in
        function_body(menu, "on_runtime_ready") and
        "disruptor_far_rendering_reset_session();" in
        function_body(menu, "on_runtime_shutdown"),
        "far-rendering presets must reset at both session boundaries")
for forbidden in (
    "has_draw_distance",
    "has_far_rendering",
    "has_depth_fade",
    "draw_distance_preset",
    "far_rendering_preset",
    "depth_fade_mode",
):
    require(forbidden not in config_h and forbidden not in config_cpp,
            f"experimental draw distance must remain outside persistence: {forbidden}")
require("mark_draw_distance" not in menu and
        "mark_far_rendering" not in menu and
        "mark_depth_fade" not in menu and
        "PREF_DRAW_DISTANCE" not in menu and
        "PREF_FAR_RENDERING" not in menu and
        "PREF_DEPTH_FADE" not in menu,
        "experimental draw distance must not enter the menu preference layer")

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
    require(token in config_h and token in config_cpp,
            f"settings.toml schema is missing {token}")

save_body = function_body(config_cpp, "save_user_settings")
require("temporary" in save_body and
        "atomic_replace_user_settings" in save_body and
        "std::ofstream f(temporary" in save_body,
        "settings must be serialized to a sibling temporary file")
require("MoveFileExW" in config_cpp and "MOVEFILE_REPLACE_EXISTING" in config_cpp and
        "std::rename" in config_cpp,
        "settings publication must atomically replace on Windows and POSIX")

require("load_preferences_for_session();" in
        function_body(menu, "on_runtime_ready"),
        "preferences must load independently of opening the ImGui menu")
require("apply_modern_keybinds(disruptor_modern_controls_enabled() != 0)" in
        function_body(menu, "apply_saved_preferences"),
        "saved or environment-enabled modern controls must install conflict-free binds")
for token in (
    "Vertical mouse look",
    "Vertical sensitivity",
    "Invert vertical mouse",
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
    require(token in menu or token in mouse,
            f"vertical settings precedence is missing {token}")
require("take_relative_motion" in mouse and
        mouse.count("take_relative_motion()") == 2,
        "mouse X/Y must come from exactly one relative sample per frame")
require("kMaximumPitchUnits = 22.0" in mouse and
        "disruptor_vertical_camera_set_requested_pitch" in mouse and
        "disruptor_vertical_camera_recenter" in mouse and
        "disruptor_vertical_camera_input_allowed()" in mouse,
        "vertical input must use the bounded camera/weapon-aim sink")
shutdown_body = function_body(menu, "on_runtime_shutdown")
require("flush_preferences();" in shutdown_body and
        shutdown_body.index("flush_preferences();") <
        shutdown_body.index("ImGui_ImplOpenGL3_Shutdown"),
        "dirty preferences must flush before UI/renderer teardown")
require("io.IniFilename = nullptr" in menu,
        "unreviewed ImGui layout state must remain outside persistence")
require("PSX_DISABLE_FRAME_INTERPOLATION=1" in cmake,
        "a Disruptor build must be able to compile frame interpolation out")
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
    require(forbidden not in menu,
            f"disabled interpolation must not enter the menu: {forbidden}")
require("#ifdef PSX_DISABLE_FRAME_INTERPOLATION" in interpolation_setter and
        "return enabled ? 0 : 1;" in interpolation_setter,
        "the live API must reject interpolation activation in disabled builds")
for name in ("psx_mod_set_frame_interpolation",
             "psx_mod_set_frame_interpolation_blend"):
    body = function_body(main_cpp, name)
    require(body.lstrip().startswith("#ifdef PSX_DISABLE_FRAME_INTERPOLATION") and
            "return 0;" in body.split("#else", 1)[0],
            f"mods must not enable compiled-out interpolation: {name}")
require('#ifndef PSX_DISABLE_FRAME_INTERPOLATION\n'
        '#include "gpu_gl_temporal.c.inc"' in gl and
        "#define interp_capture(...) 0" in gl and
        "#define temporal_draw(...) ((void)0)" in gl and
        "#define temporal_begin(...) ((void)0)" in gl,
        "disabled builds must omit temporal recording and the presenter worker")

aspect_controls = function_body(menu, "draw_aspect_controls")
for token in (
    'Checkbox("Widescreen"',
    'Combo("Aspect ratio"',
    '"16:9", "21:9", "32:9"',
    "psx_host_video_get_display_aspect",
    "psx_host_video_set_display_aspect",
    "psx_host_video_get_adaptive_view",
    "psx_host_video_set_adaptive_view",
    "mark_aspect",
    '"Match window (up to 32:9)"',
    'draw_status_badge("LIVE"',
):
    require(token in aspect_controls,
            f"live aspect controls are missing {token}")

scale_controls = function_body(menu, "draw_internal_resolution_controls")
for token in (
    'SeparatorText("Rendering resolution")',
    'Combo("Internal resolution scale"',
    '"1x (native)", "2x", "3x", "4x", "5x", "6x", "7x", "8x"',
    "kScaleCount = 8",
    "psx_host_video_get_internal_scale",
    "psx_host_video_set_internal_scale",
    "mark_supersampling",
    'draw_status_badge("LIVE"',
):
    require(token in scale_controls,
            f"live internal-resolution controls are missing {token}")
require('void mark_supersampling(int value) { mark("video.supersampling", value - 1); }\n' in menu and
        MERGED in function_body(menu, "merge_dirty_preferences") and
        "void set_scale(Settings& settings, int index) { settings.has_supersampling = true; "
        "settings.supersampling = index + 1; }\n" in options and
        "saved<&Settings::has_supersampling>, scale, set_scale)" in options,
        "internal scale must be merged into the user settings")
require('changed("video.supersampling")' in
        function_body(menu, "apply_pending_preferences") and
        "psx_host_video_set_internal_scale" in
        function_body(menu, "apply_pending_preferences"),
        "a pending internal scale must survive a soft runtime session")
require('BulletText("Supersampling/internal framebuffer allocation")' not in menu,
        "live internal scale must not remain listed as restart-only")

fullscreen_controls = function_body(menu, "draw_fullscreen_controls")
for token in (
    'SeparatorText("Window presentation")',
    'Combo("Display mode"',
    '"Windowed", "Borderless fullscreen", "Exclusive fullscreen"',
    "psx_host_video_get_fullscreen_mode",
    "psx_host_video_set_fullscreen_mode",
    "mark_fullscreen",
    'draw_status_badge("LIVE"',
    "runtime rejected the requested display mode",
    "Alt+Enter remains",
    "does not overwrite the ",
    "saved mode.",
):
    require(token in fullscreen_controls,
            f"live fullscreen controls are missing {token}")
require('void mark_fullscreen(int value) { mark("video.fullscreen", value); }\n' in menu and
        MERGED in function_body(menu, "merge_dirty_preferences") and
        "write<&Settings::has_fullscreen, &Settings::fullscreen>)" in options,
        "explicit fullscreen selections must be merged into user settings")
require('changed("video.fullscreen")' in
        function_body(menu, "apply_pending_preferences") and
        "psx_host_video_set_fullscreen_mode" in
        function_body(menu, "apply_pending_preferences"),
        "a pending fullscreen selection must survive a soft runtime session")
require("settings.has_fullscreen" in
        function_body(menu, "apply_saved_preferences") and
        "psx_host_video_set_fullscreen_mode" in
        function_body(menu, "apply_saved_preferences"),
        "saved fullscreen mode must be restored for a soft runtime session")
require("mark_fullscreen" not in function_body(menu, "on_sdl_event"),
        "menu event handling must not persist transient fullscreen hotkeys")

audio_controls = function_body(menu, "draw_audio_controls")
for token in (
    'SeparatorText("Audio output")',
    'SliderInt("Master volume"',
    'Checkbox("Mute all audio"',
    "psx_host_audio_get_master_volume",
    "psx_host_audio_set_master_volume",
    "psx_host_audio_get_muted",
    "psx_host_audio_set_muted",
    "mark_master_volume",
    "mark_audio_muted",
    'draw_status_badge("LIVE"',
    "game audio state and saves are unchanged",
):
    require(token in audio_controls,
            f"live host-audio controls are missing {token}")
merge_preferences = function_body(menu, "merge_dirty_preferences")
require('void mark_master_volume(int value) { mark("audio.master_volume", value); }\n' in menu and
        'void mark_audio_muted(bool value) { mark("audio.muted", value); }\n' in menu and
        MERGED in merge_preferences and
        "write<&Settings::has_master_volume, &Settings::master_volume>)" in options and
        '    toggle<&Settings::has_audio_muted, &Settings::audio_muted>("audio.muted",' in options,
        "host-audio selections must be merged into user settings")
pending_preferences = function_body(menu, "apply_pending_preferences")
require('changed("audio.master_volume")' in pending_preferences and
        "psx_host_audio_set_master_volume" in pending_preferences and
        'changed("audio.muted")' in pending_preferences and
        "psx_host_audio_set_muted" in pending_preferences,
        "pending host-audio settings must survive a soft runtime session")
saved_preferences = function_body(menu, "apply_saved_preferences")
require("settings.has_master_volume" in saved_preferences and
        "psx_host_audio_set_master_volume" in saved_preferences and
        "settings.has_audio_muted" in saved_preferences and
        "psx_host_audio_set_muted" in saved_preferences,
        "saved host-audio settings must restore for a soft runtime session")

volume_setter = function_body(main_cpp,
                              "psx_host_audio_set_master_volume")
require("percent < 0" in volume_setter and "percent > 100" in volume_setter and
        "g_audio_master_volume_percent.store" in volume_setter,
        "master-volume API must validate and publish 0..100 percent")
mute_setter = function_body(main_cpp, "psx_host_audio_set_muted")
require("muted != 0" in mute_setter and "muted != 1" in mute_setter and
        "g_audio_master_muted.store" in mute_setter,
        "mute API must validate and publish a boolean latch")
master_gain = function_body(main_cpp, "sdl_audio_apply_master_gain")
for token in (
    "g_audio_master_volume_percent.load",
    "g_audio_master_muted.load",
    "muted ? 0.0f",
    "sdl_audio_gain_ramp",
):
    require(token in master_gain,
            f"host-output gain stage is missing {token}")
drc_callback = function_body(main_cpp, "sdl_drc_callback")
require(drc_callback.index("rab_pull") <
        drc_callback.index("sdl_audio_apply_master_gain") <
        drc_callback.index("audio_trace_pcm"),
        "bridge audio must apply master gain at the final host-output boundary")
audio_pump = function_body(main_cpp, "sdl_audio_pump")
require(audio_pump.index("sdl_audio_fadein_left") <
        audio_pump.index("sdl_audio_apply_master_gain") <
        audio_pump.index("psx_sdl_audio_queue("),
        "legacy audio must apply master gain after transition fade and before queueing")
audio_update = function_body(main_cpp, "sdl_audio_update")
require(audio_update.index("sdl_audio_apply_master_gain") <
        audio_update.index("psx_sdl_audio_queue("),
        "the legacy turbo-mute tail must retain the selected master gain")
require(main_cpp.index("us.has_master_volume") <
        main_cpp.index("psx_sdl_audio_open") and
        main_cpp.index("us.has_audio_muted") <
        main_cpp.index("psx_sdl_audio_open"),
        "persisted host-audio controls must load before the audio device opens")

for function_name in ("on_runtime_ready", "on_runtime_shutdown"):
    session_body = function_body(menu, function_name)
    require("disruptor_billboard_aspect_all_site_mask" in session_body and
            "disruptor_billboard_aspect_set_site_mask" in session_body,
            f"{function_name} must restore every billboard repair path")
require('g_preferences.changed.insert(find_option("video.aspect_ratio"));' in
        function_body(menu, "mark_aspect") and
        MERGED in function_body(menu, "merge_dirty_preferences") and
        "    settings.has_aspect_ratio = settings.has_adaptive_view = true;\n"
        "    settings.adaptive_view = index == window_aspect;\n" in options and
        "aspect_saved, aspect, set_aspect)" in options,
        "fixed and adaptive aspect choices must persist atomically")
require('changed("video.aspect_ratio")' in function_body(menu, "apply_pending_preferences") and
        "psx_host_video_set_display_aspect" in
        function_body(menu, "apply_pending_preferences") and
        "psx_host_video_set_adaptive_view" in
        function_body(menu, "apply_pending_preferences"),
        "a pending aspect choice must survive a soft runtime session")
require("        if (us.has_frame_interpolation)\n"
        "            g_frame_interpolation = us.frame_interpolation ? 1 : 0;\n"
        in main_cpp and
        "        !g_geometry_correction)\n"
        "        g_frame_interpolation = 0;\n" in main_cpp,
        "a saved in-between frames switch must be restored, and geometry "
        "in-between frames never without exact geometry")

main_settings = main_cpp.index("load_user_settings(settings_path)")
main_env = main_cpp.index('std::getenv("PSX_GEOMETRY_CORRECTION")')
require(main_settings < main_cpp.index("us.has_geometry_correction") < main_env and
        main_settings < main_cpp.index("us.has_perspective_textures") < main_env,
        "saved enhancements must load before explicit environment overrides")
aspect_env = main_cpp.index('std::getenv("PSX_VIDEO_ASPECT")')
require(main_settings < main_cpp.index("us.has_aspect_ratio") < aspect_env and
        "is_host_fixed_display_aspect" in main_cpp[aspect_env:main_env],
        "explicit aspect override must be validated after saved settings load")
require("UserSettings seed =" in main_cpp and
        "load_user_settings(" in
        main_cpp[main_cpp.index("UserSettings seed ="):main_cpp.index("seed.renderer =")],
        "a generic launcher save must merge rather than drop game-owned fields")

require("PSX_DISRUPTOR_MODERN_CONTROLS = if" not in run_ps1 and
        "PSX_TEXTURE_CORRECTION = if" not in run_ps1 and
        "$env:PSX_DISRUPTOR_MOUSE_AIM = '0'" not in run_ps1,
        "PowerShell must not synthesize disabling overrides for absent switches")
require("[switch]$VerticalLook" in run_ps1 and
        "PSX_DISRUPTOR_VERTICAL_LOOK" in run_ps1 and
        "--vertical-look" in run_sh and
        "PSX_DISRUPTOR_VERTICAL_LOOK=1" in run_sh,
        "launchers must expose an explicit vertical-look opt-in")
require("PSX_DISRUPTOR_VERTICAL_LOOK = if" not in run_ps1 and
        "export PSX_DISRUPTOR_VERTICAL_LOOK=0" not in run_sh,
        "launchers must not synthesize a disabling vertical override")
require("export PSX_DISRUPTOR_MODERN_CONTROLS=$MODERN_CONTROLS" not in run_sh and
        "export PSX_DISRUPTOR_MOUSE_AIM=0" not in run_sh,
        "POSIX launcher must not synthesize disabling overrides")
require("--perspective-textures" in run_sh and
        "PERSPECTIVE_TEXTURES=1; GEOMETRY_CORRECTION=1" in run_sh,
        "POSIX perspective-texture launch must explicitly imply geometry")
require("PSX_VIDEO_ASPECT'] = '16:9'" in run_ps1 and
        "export PSX_VIDEO_ASPECT=16:9" in run_sh,
        "explicit widescreen launch switches must override a saved aspect")

shared_keys = set(re.findall(r'\b(?:toggle|choice|slider)(?:<[^>]*>)?\(\s*"([\w.]+)"', options))
staged_keys = set(re.findall(r'\bmark\("([\w.]+)"', menu)) | set(
    re.findall(r'g_preferences\.changed\.insert\(\s*find_option\("([\w.]+)"\)\)', menu))
applied_keys = set(re.findall(r'\bchanged\("([\w.]+)"\)', function_body(menu, "apply_pending_preferences")))
require(len(shared_keys) == 27 and staged_keys == shared_keys and applied_keys == shared_keys and
        set(re.findall(r'\bfind_option\("([\w.]+)"', menu)) <= shared_keys,
        "the menu must stage every shared option, apply every one it staged and name no key the table lacks")
require('void mark_vsync(int value) { mark("video.vsync", value + 1); }\n' in menu and
        "    g_preferences.pending.horizontal_sensitivity = value;\n"
        "    g_preferences.changed.insert(\n"
        '        find_option("disruptor.horizontal_sensitivity"));\n' in function_body(menu, "mark_sensitivity") and
        "    g_preferences.pending.vertical_sensitivity = value;\n"
        "    g_preferences.changed.insert(\n"
        '        find_option("disruptor.vertical_sensitivity"));\n' in function_body(menu, "mark_vertical_sensitivity") and
        "(void)disruptor_mouse_set_horizontal_sensitivity(\n            pending.horizontal_sensitivity);" in
        function_body(menu, "apply_pending_preferences") and
        "int vsync(const Settings& settings) { return std::clamp(settings.vsync + 1, 0, 2); }\n" in options and
        "    return static_cast<int>(std::lround(settings.*Value * 1000.0));\n" in options and
        "    settings.*Value = value / 1000.0;\n" in options,
        "a value the menu holds in its own unit must be staged in the shared table's, and a sensitivity kept whole "
        "for the running game and rounded only where it is written")
require(STAGED in menu and
        "    return g_preferences.changed.contains(find_option(key));\n" in menu and
        "    std::set<const Option *> changed;\n" in menu and
        "        return !changed.empty() || language_disc_changed ||\n"
        "               diagnostic_view_changed;\n" in menu and
        "        changed.clear();\n"
        "        language_disc_changed = diagnostic_view_changed = false;\n" in menu and
        "    if (!g_preferences.dirty()) return true;\n" in function_body(menu, "flush_preferences") and
        "    g_preferences.clean();\n    g_preferences.save_failed = false;\n" in function_body(menu, "flush_preferences") and
        "PreferenceDirty" not in menu and "PREF_" not in menu and
        '    list(APPEND DISRUPTOR_EXTRA_SOURCES\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/settings/options.cpp"\n'
        '        "${CMAKE_CURRENT_SOURCE_DIR}/src/game/ui/dev_menu.cpp")\n' in cmake,
        "a staged change must be an option of the shared table, and a save must write and then forget every one")

print("developer menu contract: PASS")
