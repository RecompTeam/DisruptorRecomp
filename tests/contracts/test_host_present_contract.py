#!/usr/bin/env python3
"""Source contract for the host's present paths that a framework raise lost once: the render thread's wide present,
the menu over a disabled display, unbuffered output, the debug ring's readback, and a refused load leaving the
machine and its picture alone."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp/runtime/src/main.cpp")
debug = read("psxrecomp/runtime/src/debug_server.c")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")

wide = host[host.index("            if (wide_present) {\n                /* GPU-direct native-wide present") :]
wide = wide[: wide.index("            } else {")]
require(
    wide.index(
        "                if (gl_renderer_render_thread_active()) {\n"
        "                    gl_renderer_present_wide_or_vram(wide_base, (int)di.display_y, (int)h,"
    )
    < wide.index("                if (gl_renderer_present_wide_fbo(wide_base, (int)di.display_y,")
    and "(int)w, (int)h, nw_pin ? 1 : 0, (int)di.display_x);" in wide,
    "with the render thread on, the wide present is recorded for it: the game thread has no GL context, so a direct "
    "gl_renderer_present_wide_fbo showed a black screen (main menu, demo after a refused load)",
)
require(
    "    if (rth_record_mode()) {" in renderer[renderer.index("void gl_renderer_present_wide_or_vram(") :][:200]
    and "        if (rth_rec_call(RTH_PRESENT_WIDE, v, 14)) return;\n" in renderer
    and "        gl_renderer_present_vram(canonical_x, disp_y, w, h, linear, force_4_3);\n" in renderer,
    "gl_renderer_present_wide_or_vram records RTH_PRESENT_WIDE under the render thread, its canonical fallback at the "
    "displayed x, not the wide base",
)
require(
    "            if (!s_disabled_frame_presented || s_force_present_after_load ||\n"
    "                psx_host_ui_gl_visible()) {\n" in host,
    "a disabled display keeps presenting while the host menu is shown, or the menu freezes during loads",
)
require(
    "    std::setvbuf(stdout, nullptr, _IONBF, 0);\n    std::setvbuf(stderr, nullptr, _IONBF, 0);\n" in host
    and "std::setvbuf(stderr, nullptr, _IOLBF" not in host,
    "output stays unbuffered: MSVC's _IOLBF buffers fully and a hang cut the log mid-line",
)
ring = debug[debug.index("static void disp_ring_capture(void)") :]
ring = ring[: ring.index("\n}\n")]
require(
    "        if (gl_renderer_render_thread_active()) return;\n" in ring,
    "the debug display ring skips its per-frame VRAM capture while the render thread runs",
)
compat = read("psxrecomp/runtime/src/disruptor_renderer_compat.c")
game_sources = "".join(
    (ROOT / "src" / "game" / name).read_text(encoding="utf-8")
    for name in ("input/mouse_aim.cpp", "input/selection_wheel.cpp", "session/capsule.cpp")
)
resample = host[host.index("            SDL_PumpEvents(); // retain native timing when no policy exists\n") :][:500]
reapply = host[host.index("static void reapply_pad_edits(void) {") :][:600]
require(
    "            sample_pad_into_sio(override);\n" in resample
    and "            env_pad_hold_apply();\n            reapply_pad_edits();\n" in resample
    and "            sio_set_pad_state_slot(slot, psx_pad_edits_apply(&s_pad_edits, slot, sio_get_pad_buttons_slot(slot)));\n"
    in reapply
    and "static void sdl_vblank_present(void) {\n    psx_pad_edits_clear(&s_pad_edits);\n" in host
    and "sio_set_pad_state_slot" not in game_sources
    and "sio_set_pad_sticks" not in game_sources,
    "the game edits the pads only through psx_game_pad_*, which the late low-latency re-sample applies again: without it "
    "mouse clicks, weapon and psionic keys and a capsule's replayed input never reached the game",
)
owns = compat[compat.index("int gl_renderer_interpolation_owns_cadence(void) {") :][:80]
fit = compat[compat.index("int gl_renderer_fit_wide_aspect(") :][:200]
require(
    "return 0;" in owns and "memset" not in fit,
    "our in-between frames never own the guest's cadence (the wall pacer stops and the game runs fast), and the wide "
    "aspect fit leaves the aspect as it is",
)
states = read("psxrecomp/runtime/src/savestate.c")
load = states[states.index("        uint8_t* rollback = NULL;\n") :]
load = load[: load.index("        free(slot_file);\n")]


def body(text: str, head: str) -> str:
    """A function's whole text, from its head to its closing brace."""
    found = text[text.index(head) :]
    return found[: found.index("\n}\n") + 3]


require(
    body(states, "static int header_accepted(")
    == "static int header_accepted(const uint8_t* state, size_t len) {\n"
    "    char reason[256];  /* without one the check passes every mismatch but the layout's */\n"
    "    return boot_state_check_buffer(state, len, s_bios_checksum, s_entry_pc, reason, sizeof(reason));\n"
    "}\n"
    and body(states, "static uint8_t* read_state_file(")
    == "static uint8_t* read_state_file(const char* path, size_t* len) {\n"
    '    FILE* f = fopen(path, "rb");\n'
    "    uint8_t* bytes = NULL;\n"
    "    long size = -1;\n"
    "    *len = 0;\n"
    "    if (!f) return NULL;\n"
    "    if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);\n"
    "    if (size > 0 && size <= 64L * 1024L * 1024L && fseek(f, 0, SEEK_SET) == 0)\n"
    "        bytes = (uint8_t*)malloc((size_t)size);\n"
    "    if (bytes && fread(bytes, 1, (size_t)size, f) != (size_t)size) {\n"
    "        free(bytes);\n"
    "        bytes = NULL;\n"
    "    }\n"
    "    fclose(f);\n"
    "    if (bytes) *len = (size_t)size;\n"
    "    return bytes;\n"
    "}\n"
    and "            slot_file = read_state_file(path, &slot_len);\n"
    "        const int refused_unread = s_load_blob && s_load_blob_len > 0\n"
    "            ? !header_accepted(s_load_blob, s_load_blob_len)\n"
    "            : !slot_file || !header_accepted(slot_file, slot_len);\n"
    in load
    and "            loaded = slot_file && boot_state_load_buffer_checked(slot_file, slot_len, s_bios_checksum, s_entry_pc,\n"
    in load
    and "boot_state_load_checked(" not in load
    and load.count(
        "         * the original machine silently, without splitting its input stream. */\n"
        "        if (!loaded && have_rollback && !refused_unread)\n"
        "            (void)boot_state_load_buffer_quiet(rollback, rollback_len,\n"
    )
    == 1
    and load.count("boot_state_load_buffer_quiet(") == 1,
    "a load the header check refuses restores nothing, checked on the very bytes the load applies: the rollback's "
    "GPU and VRAM restore dropped the wide picture, so the menu's background went 4:3 and flashed black",
)
preferences = read("src/game/ui/menu/preferences.cpp")
session = preferences[preferences.index("void load_preferences_for_session() {") :]
session = session[: session.index("\n}\n")]
require(
    "        !gpu_geometry_correction_enabled())\n            g_frame_interpolation = 0;\n" in host
    and session.index("    apply_saved_preferences(disruptor::settings::with_defaults(settings));\n")
    < session.index(
        "    if (asked ? std::atoi(asked) != 0 : settings.has_frame_interpolation && settings.frame_interpolation)\n"
        "        (void)disruptor_present_rate_enable(1);\n"
    ),
    "the framework turns geometry in-between frames off at start while exact geometry is still off, and the game "
    "switches that on only later: the game asks for the saved or launcher choice again after it, or they were off "
    "at every start",
)
print("Host present source contract: PASS")
