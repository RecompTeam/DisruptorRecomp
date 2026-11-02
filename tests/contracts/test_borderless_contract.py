#!/usr/bin/env python3
"""Source contract for the borderless window that must not cover its screen exactly: where the host and the renderer use it."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp/runtime/src/main.cpp")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
cmake = read("CMakeLists.txt")

keep = host[
    host.index("static void psx_borderless_keep_windowed(void) {") : host.index(
        "static void psx_borderless_keep_windowed(void) {}"
    )
]
require(
    "#if defined(_WIN32) && defined(PSX_SDL3)\n/* Keeps a borderless OpenGL window" in host
    and "    int hidden = 0;\n    if (sdl_window && g_video_renderer == 1 && s_window_fullscreen.mode == 1) {\n"
    in keep
    and "GetMonitorInfoW(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST), &info) && GetWindowRect(handle, &now)"
    in keep
    and "            if (psx_borderless_wanted(window, screen, &wanted) &&\n"
    "                SetWindowPos(handle, nullptr, wanted.left, wanted.top, wanted.right - wanted.left, wanted.bottom - wanted.top,\n"
    "                             SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER)) {\n                window = wanted;\n"
    in keep,
    "only a borderless window of the OpenGL renderer is moved, to where the rule says, and by nothing but its place and size",
)
require(
    "            SDL_GetWindowSizeInPixels(sdl_window, &pixels_wide, &pixels_high);\n"
    "            hidden = psx_borderless_hidden_rows(window, screen, pixels_high);\n"
    in keep
    and keep.rstrip().endswith("    gl_renderer_set_hidden_rows(hidden);\n}\n#else")
    and "static void sdl_vblank_present(void) {\n    psx_pad_edits_clear(&s_pad_edits);\n    psx_borderless_keep_windowed();\n"
    in host
    and "SDL_SetWindowSize(sdl_window, g_video_res_w, g_video_res_h);\n"
    "        psx_borderless_keep_windowed(); /* at once: a present may come before the next vblank */\n"
    "        apply_present_cadence();\n"
    in host,
    "every frame the renderer is told how many rows are above the screen, none in any other mode: SDL fits the window "
    "to the screen again when it is restored",
)
require(
    "void gl_renderer_set_hidden_rows(int rows) {\n    const int32_t v[] = {rows};\n    if (rth_rec_call(RTH_HIDDEN_ROWS, v, 1)) return;\n    s_hidden_rows = rows > 0 ? rows : 0;\n}\n"
    "static void gl_visible_size(int *w, int *h) {\n    SDL_GL_GetDrawableSize(s_win, w, h);\n"
    "    if (*h > s_hidden_rows) *h -= s_hidden_rows;\n}\n"
    in renderer
    and renderer.count("    int ww = 0, wh = 0; gl_visible_size(&ww, &wh);\n") == 5
    and renderer.count("SDL_GL_GetDrawableSize(") == 1
    and "void gl_renderer_set_hidden_rows(int rows) { (void)rows; }\n" in renderer,
    "every present of the OpenGL renderer fits its picture to the rows that are on the screen",
)
require(
    "+runtime/include/host_borderless.h" in read("framework/FILES.txt").splitlines()
    and "    disruptor_unit_test(host_borderless " in cmake
    and "    add_test(NAME host_borderless_contract\n" in cmake
    and '/tests/contracts/test_borderless_contract.py")' in cmake,
    "the header is part of the overlay and its tests remain registered",
)

fullscreen = read("psxrecomp/runtime/include/window_fullscreen.h")
require(
    "    if (mode == 1) {\n#if defined(_WIN32) && defined(PSX_SDL3)\n"
    in fullscreen
    and "        if (!SDL_SetWindowFullscreen(window, true)) return -1;\n        SDL_SyncWindow(window);\n#elif defined(_WIN32)\n"
    in fullscreen,
    "on Windows borderless is SDL's desktop fullscreen: a plain borderless window the size of the screen stayed D3D full "
    "screen for the shell even one row taller, and Alt+Tab blacked the screen out",
)
require(
    "    const bool show_when_fullscreen = g_fullscreen != 0 && !g_hidden_window;\n" in host
    and host.index("        win_flags |= SDL_WINDOW_HIDDEN;\n    }\n    sdl_window = SDL_CreateWindow(")
    < host.index("    if (psx_window_fullscreen_set(sdl_window, &s_window_fullscreen, g_fullscreen) != 0)")
    < host.index("    if (show_when_fullscreen) SDL_ShowWindow(sdl_window);\n"),
    "a fullscreen start shows the window only once it is fullscreen: shown first, its bordered windowed frame flashed white",
)
print("Host borderless window source contract: PASS")
