#!/usr/bin/env python3
"""Source contract: a save state loaded into a fresh process, with a window or without, runs on as the game that saved it.

Each rule was a cause of a replay leaving its recording. The first 16 bytes and the cost divider
were found by comparing the memory of a booted session with that of the same state loaded.
"""

from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


host = read("psxrecomp/runtime/src/main.cpp")
memory = read("psxrecomp/runtime/src/memory.c")
state = read("psxrecomp/runtime/src/boot_state.c")
cycles = read("psxrecomp/runtime/include/psx_cyc.h")
latch = read("psxrecomp/runtime/src/fntrace.c")
menu = menu_text(ROOT)
rate = read("src/game/frame/frame_rate.cpp")
interrupts = read("psxrecomp/runtime/src/interrupts.c")
hashed = read("psxrecomp/runtime/codegen_hash_sources.cmake")
changed = read("framework/FILES.txt").split()

require(
    "    post_exception_cooldown_until = 0;\n"
    in interrupts[interrupts.index("void interrupts_resync_after_restore(void) {") :].split("\n}\n")[0]
    and read("psxrecomp/runtime/src/savestate.c").count("            interrupts_resync_after_restore();\n") == 1
    and state.index("        psx_cycle_count = cyc;")
    < state.index(
        "        cpu->gte_ts_done = psx_cycle_count + gte_rem;\n        cpu->muldiv_ts_done = psx_cycle_count + md_rem;\n"
    ),
    "a state loaded into a running game puts the clock back: the delivery deadline and the two stall deadlines of the old "
    "clock are dropped, or no interrupt arrives until the clock is there again",
)
require(
    all(
        name not in hashed
        for name in ("runtime/include/interrupts.h", "runtime/src/interrupts.c", "runtime/src/boot_state.c")
    )
    and "runtime/include/psx_cycles.h" in hashed
    and "runtime/include/psx_cycles.h" not in changed,
    "the resets live outside the files that key save states: the scheduler's header is one of those, so its resync keeps its signature",
)
require(
    "    /* Engage widescreen at game entry: BIOS boot stays authentic 4:3. */\n"
    "    if (!g_ws_engaged) {\n        if (fntrace_is_game_started()) {\n            g_ws_engaged = true;\n"
    in host
    and host.index("    mod_call_frame_hooks();")
    < host.index("    /* Engage widescreen at game entry: BIOS boot stays authentic 4:3. */\n")
    < host.index("    if (g_headless || module_fast_forward) {"),
    "a run without a window leaves the VBlank before a windowed one engages widescreen, so it engages it on its way out",
)
require(
    'extern "C" void psx_frontend_on_savestate_loaded(void) {\n    ++s_fmv_timeline_epoch;\n'
    "    engage_widescreen_for_loaded_state();\n"
    in host
    and "static void engage_widescreen_for_loaded_state(void) {\n"
    "    if (g_ws_engaged || !fntrace_is_game_started()) return;\n    g_ws_engaged = true;\n"
    "    g_ws_projection_mode = -1;\n    refresh_widescreen_projection();\n}\n"
    in host,
    "a started game's state engages widescreen at its load: engaged at the next VBlank, the frame an unlocked loop "
    "draws before it was projected at 4:3 and a replay left its recording at frame 0",
)
require(
    "    } else {\n"
    '        std::fprintf(stdout, "psxrecomp: headless frontend enabled\\n");\n'
    "        host_ui_runtime_ready(nullptr, PSX_HOST_UI_BACKEND_NONE);"
    in host
    and host.count("    host_ui_runtime_ready(") == 2
    and "    load_preferences_for_session();\n    if (backend == PSX_HOST_UI_BACKEND_OPENGL) (void)initialize_imgui();"
    in menu
    and 'disruptor_frame_rate_set_unlocked(stored(settings, *find_option("disruptor.frame_unlock")));' in menu,
    "a run without a window tells the game module the runtime is ready: its saved settings, frame unlock among them, apply there",
)
require(
    "static void sdl_audio_pump(bool discard_output = false) {\n"
    "    /* Guest-cycle SPU advance must not depend on host audio device/backpressure.\n"
    in host
    and "    } else if (sdl_audio_device && legacy) {\n" in host
    and "if (!sdl_audio_device) return;" not in host
    and host.index("    const int host_frames = (int)spu_output_pop(sdl_audio_buf, (uint32_t)frames);\n")
    < host.index("    if (!host_queue_ok || !sdl_audio_device)"),
    "the SPU is rendered on the guest's clock with no device to play it: the game reads the SPU back",
)
require(
    "fntrace_restore_game_started(p[0]);" in state
    and "void fntrace_restore_game_started(int started) { s_game_started = started != 0; }" in latch
    and "    if (s_game_started) return;\n    s_game_started = 1;\n" in latch
    and "psx_game_address_in_text(addr) && psx_game_text_native_ok(addr)))" in latch,
    "a loaded state of a started game carries the started latch, so the latch never fires again and clears nothing",
)
require(
    "static int s_low_scratch_is_the_games;" in memory
    and "void memory_keep_low_boot_scratch(void) { s_low_scratch_is_the_games = 1; }\n" in memory
    and "void memory_clear_low_boot_scratch(void) {\n    if (s_low_scratch_is_the_games) return;\n    memset(ram, 0, 0x10u);\n}\n"
    in memory
    and "    s_low_scratch_is_the_games = 0;\n    psx_ram_apply_size_request();\n    memset(ram, 0, sizeof(ram));\n"
    in memory
    and memory.count("s_low_scratch_is_the_games") == 4
    and "    memory_clear_low_boot_scratch();\n" in latch,
    "the entry latch of a fresh process must not clear the first 16 bytes of a loaded game, and a new session clears them again",
)
require(
    "        psx_guest_cycle_scale_snapshot(g);\n" in state
    and "psx_guest_cycle_scale_restore(g);" in state
    and "        if (unlocked || !follows) set_cpu_shift(g_cpu_shift.load(std::memory_order_relaxed));\n" in rate
    and "    if (waits) set_cpu_shift(0u);\n" in rate
    and "    if (scale != psx_guest_cycle_scale_config()) psx_guest_cycle_scale_set(scale);\n" in rate,
    "a state holds the framework's guest cycle scale frame unlock sets, and frame unlock sets it only where it changes",
)

require(
    "                        uint32_t payload_off = phys_lo >= disc_load\n" in host
    and "                            memcpy(img, file + 2048 + payload_off, img_len);\n" in host
    and "            text_guard_text_size = gc.text_size;\n" in host,
    "the text guard compares the game's code from its configured load address and text size: an image read from the "
    "EXE's own load address is shifted, so every function fails the compare and runs in the interpreter",
)
print("Disruptor loaded state source contract: PASS")
