#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's gameplay frame-rate unlock."""

import re
import struct
import sys
from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

RETAIL_INSTRUCTIONS = {
    0x80043A50: 0x8F8202EC,
    0x80043A60: 0x938305A4,
    0x80043A80: 0x93830310,
    0x80043A98: 0x2A220006,
    0x80043AA4: 0x0C00E514,
    0x80043AAC: 0x0235102A,
    0x80043E7C: 0x0C012CF5,
    0x80043E80: 0x00002021,
    0x80043E84: 0x0C012CF5,
    0x80043E88: 0x2404FFFF,
    0x80043E8C: 0x00561023,
    0x80043E90: 0x28420002,
    0x80043E94: 0x10400003,
    0x80043E9C: 0x0C012CF5,
    0x80043EAC: 0x0056A823,
    0x80043EB0: 0x0040B021,
    0x8004B3D4: 0x3C028006,
    0x8004B414: 0x3C028006,
    0x8004B418: 0x8C42B920,
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/game/frame/frame_rate.cpp")
header = read("src/game/frame/frame_rate.h")
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
manifest = read("framework/FILES.txt")
cycle_header = read("psxrecomp/runtime/include/psx_cyc.h")
cycle_source = read("psxrecomp/runtime/src/psx_cycles.c")

require(
    "    disruptor_contract(frame_rate RETAIL)\n" in cmake,
    "the build gate must run this contract with its inputs required",
)
require(
    "src/game/frame/frame_rate.cpp" in cmake
    and "    disruptor_contract(frame_rate RETAIL)\n" in cmake
    and "    disruptor_unit_test(disruptor_frame_rate " in cmake,
    "frame-rate source, its unit test and its contract test must remain registered",
)

for name in ("game.toml", "game-widescreen.toml"):
    config = read(name)
    entries = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", config)
    require(
        entries is not None and '"0x8004B3D4"' in entries.group(1),
        f"{name} must hook the VSync entry that restores the retail floor",
    )
    patch = re.search(
        r'\[\[recompiler\.patch\]\]\s*id = "disruptor-gameplay-frame-floor"\s*'
        r'address = "0x80043E90"\s*expected = "0x28420002"\s*replacement = "0x28420001"',
        config,
    )
    require(patch is not None, f"{name} must carry the guarded frame-floor patch")
    require(
        re.search(r"^\[audio\]\nbuffer_ms = 45\n", config, re.MULTILINE) is not None,
        f"{name} must hold the sound 45 ms ahead of the device, the default 180 put it 0.2 s behind the movies",
    )
    require(
        re.search(r"^\[localization\]\ncapture = false\n", config, re.MULTILINE) is not None,
        f"{name} must stop the text capture scan, it took 40% of the emulation thread in a crowd",
    )

loader = read("psxrecomp/recompiler/src/config_loader.cpp")
translation = read("psxrecomp/runtime/src/text_xlate.cpp")
runtime_main = read("psxrecomp/runtime/src/main.cpp")
require(
    '        if (loc.contains("capture"))\n            rt.text_capture = toml::find<bool>(loc, "capture");\n' in loader
    and "            text_xlate_set_capture(gc.runtime.text_capture ? 1 : 0);\n" in runtime_main
    and "    g_capture_on.store(capenv && capenv[0] ? capenv[0] != '0' : enabled != 0);\n" in translation,
    "the configuration's capture switch must reach the translation layer, the environment still first",
)
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
uploads = renderer[renderer.index("static void flush_cpu_upload(void) {") :]
uploads = uploads[: uploads.index("\n}\n")]
require(
    "        p_glBindBuffer(PSXGL_PIXEL_UNPACK_BUFFER, s_up_pbo);\n"
    "        p_glBufferData(PSXGL_PIXEL_UNPACK_BUFFER, (ptrdiff_t)bytes, pixels, PSXGL_STREAM_DRAW);\n"
    "        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, format, type, (const void *)0);\n"
    "        p_glBindBuffer(PSXGL_PIXEL_UNPACK_BUFFER, 0);\n"
    in renderer
    and "    p_glGenBuffers(1, &s_up_pbo);\n" in renderer
    and uploads.count("upload_rect(") == 2
    and "glTexSubImage2D" not in uploads,
    "uploads of the game's pictures go through a buffer: a direct one waited for the queued draws, 45 ms a second",
)
snapshot = presenter[presenter.index("static int temporal_texture(TemporalScene *s) {") :]
snapshot = snapshot[: snapshot.index("\n}\n")]
require(
    "    const int queried = !p_glCopyImageSubData || !s->textures[i];\n"
    "    if (queried) {\n"
    "        glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &read_fbo);\n"
    "        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);\n"
    "    }\n"
    in snapshot
    and snapshot.count("glGetIntegerv(") == 2
    and "        p_glCopyImageSubData(s_raw_tex, GL_TEXTURE_2D, 0, 0, 0, 0, s->textures[i], GL_TEXTURE_2D, 0, 0, 0, 0,\n"
    in presenter
    and '|| SDL_GL_ExtensionSupported("GL_ARB_copy_image")\n' in renderer,
    "a scene's texture snapshot asks the driver nothing when it can copy texture to texture: the queries waited for it",
)

for token in (
    "0x8004B3D4u",
    "0x80043E84u",
    "0x800438FCu",
    "0x800442F4u",
    "0x8005B920u",
    "0x80071438u",
    "0x800716F0u",
    "0x8007145Cu",
):
    require(token in source, f"reviewed frame-rate identity missing: {token}")

require(
    "std::atomic<bool> g_unlocked{false}" in source,
    "the retail 30 FPS cadence must stay the default",
)
require(
    "if (!unlocked) cpu->gpr[4] = kRetailFloorVBlanks;" in source and "kRetailFloorVBlanks = 2u" in source,
    "the locked path must restore the retail two-VBlank floor",
)
require(
    "simulation_follows_elapsed_vblanks()" in source
    and "kAttractLoopMode" in source
    and "kSingleStepPlayerState" in source
    and "kFixedStepOverride" in source,
    "fixed-step loop states must keep the retail cadence",
)
require(
    "static_cast<int32_t>(cpu->gpr[4]) >= 0" in source and "    if (waits) set_cpu_shift(0u);\n" in source,
    "every waiting VSync call must run at the stock instruction cost",
)
require(
    'g_unlocked.store(env_enabled("PSX_DISRUPTOR_FRAME_UNLOCK"),' in source,
    "the environment must be able to switch the unlock on at start",
)
require("psx_mod_write" not in source, "the unlock must not write guest memory")

for token in (
    "disruptor_frame_rate_unlocked",
    "disruptor_frame_rate_set_unlocked",
    "disruptor_frame_rate_last_window",
):
    require(token in header and token in source, f"frame-rate API is missing {token}")

require(
    "disruptor_frame_rate_set_unlocked(unlocked ? 1 : 0);\n        mark_frame_unlock(unlocked);" in menu
    and '    if (option_toggle("disruptor.frame_unlock", &unlocked)) {\n' in menu
    and '"disruptor.frame_unlock", "60 FPS gameplay", 1,' in options,
    "the settings menu must apply the switch live and stage it for saving",
)
require(
    'void mark_frame_unlock(bool value) { mark("disruptor.frame_unlock", value); }\n' in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_frame_unlock, &Settings::frame_unlock>(\n        "disruptor.frame_unlock",'
    in options,
    "a changed switch must reach settings.toml",
)
require(
    '    if (!env_override_present("PSX_DISRUPTOR_FRAME_UNLOCK"))\n'
    '        disruptor_frame_rate_set_unlocked(stored(settings, *find_option("disruptor.frame_unlock")));' in menu,
    "the saved switch, or the shipped default where the file names none, must be applied at start unless the environment overrides it",
)
settings_loader = read("src/game/settings/saved.cpp")
require(
    's.frame_unlock = toml::find<bool>(d, "frame_unlock");' in settings_loader
    and 'f << "frame_unlock = " << (s.frame_unlock ? "true" : "false")' in settings_loader
    and "s.has_perspective_textures || s.has_frame_unlock ||" in settings_loader,
    "settings.toml must read and write [disruptor] frame_unlock",
)

for path in ("runtime/include/psx_cyc.h", "runtime/src/psx_cycles.c"):
    require(path in manifest.splitlines(), f"overlay manifest must list {path}")
require(
    "static uint32_t s_psx_gcs_cfg = 1u;" in cycle_source and "    if (scale < 1u) scale = 1u;\n" in cycle_source,
    "the guest CPU divisor must default to faithful timing",
)
require(
    "    uint64_t t = (uint64_t)cpu_cycles * g_psx_gcs_recip_q16 + g_psx_gcs_frac;\n"
    "    g_psx_gcs_frac = (uint32_t)(t & 0xFFFFu);\n"
    "    return (uint32_t)(t >> 16);\n" in read("psxrecomp/runtime/include/psx_cycles.h"),
    "the divisor must charge whole cycles and carry the remainder",
)
waits_header = read("psxrecomp/runtime/include/psx_cyc_overclock.h")
icache = read("psxrecomp/runtime/src/psx_icache.c")
memory = read("psxrecomp/runtime/src/memory.c")
dma = read("psxrecomp/runtime/src/dma.c")
cycles_header = read("psxrecomp/runtime/include/psx_cycles.h")


def c_body(source: str, signature: str) -> str:
    begin = source.index(signature)
    return source[begin : source.index("\n}\n", begin)]


muldiv = c_body(cycle_source, "void psx_muldiv_set(CPUState* cpu, uint32_t latency) {")
gte_set = c_body(cycle_source, "void psx_gte_set(")
charge = c_body(cycles_header, "static inline void psx_cpu_charge(uint32_t cycles) {")
require(
    "    return PSX_GCS_ACTIVE() ? psx_gcs_scale(cpu_cycles) : cpu_cycles;"
    in c_body(cycles_header, "static inline uint32_t psx_cpu_cycles(uint32_t cpu_cycles) {")
    and muldiv.index("    latency = psx_cpu_cycles(latency);")
    < muldiv.index("    cpu->muldiv_ts_done = psx_cycle_count + (uint64_t)latency;")
    and gte_set.index("    latency = psx_cpu_cycles(latency);")
    < gte_set.index("    cpu->gte_ts_done = psx_cycle_count + (uint64_t)latency;")
    and "    if (PSX_GCS_ACTIVE()) {\n        cycles = psx_gcs_scale(cycles);\n" in charge
    and "psx_cpu_charge(fudge + cost);" in memory
    and "    advance_mdec_channel(0, cycles);\n    advance_mdec_channel(1, cycles);\n" in dma
    and dma.count("psx_guest_cycle_scale()") == 2
    and "    if (scale != psx_guest_cycle_scale_config()) psx_guest_cycle_scale_set(scale);\n" in source,
    "the overclock divides the GTE and multiplier deadlines and main RAM loads with the CPU's charge, devices other than "
    "the GPU transfer keep their time, and frame unlock sets the framework's scale only where it changes",
)
GPU_TRANSFER = (
    "        case 2: {\n"
    "            /* Overclocked, GPU transfers end as much sooner, or the CPU outruns the GPU into an area both upload to."
    " */\n"
    "            schedule_delayed_complete(2, execute_ch2_gpu(), DMA_GPU_CYCLES_PER_WORD);\n"
    "            DMADelayedComplete *gpu = &delayed_complete[2];\n"
    "            if (gpu->active) gpu->cycles_remaining = psx_cyc_overclocked_latency(gpu->cycles_remaining);\n"
    "            break;\n"
    "        }\n"
    "        case 3:\n"
)
require(
    "                const uint32_t scale = psx_guest_cycle_scale();\n"
    "                DMADelayedComplete *gpu = &delayed_complete[2];\n"
    "                if (scale > 1u && gpu->active) gpu->cycles_remaining = (gpu->cycles_remaining + scale / 2u) / scale;\n"
    in dma
    and "        const uint32_t gpu_cycles = scale > 1u ? (cycles > UINT32_MAX / scale ? UINT32_MAX : cycles * scale) : cycles;\n"
    "        dma_gpu_ll_advance(&gpu_linked_list, gpu_cycles, &gpu_ll_ops, NULL);\n"
    in dma
    and dma.count("psx_guest_cycle_scale()") == 2,
    "a GPU transfer ends as much sooner as the overclock runs the CPU, the other channels keep their time: "
    "at 8x the pause menu's pictures went into the sprite chain's area before it ended",
)

present_rate = read("src/game/frame/present_rate.h")
present_control = read("src/game/frame/present_rate.cpp")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
host = read("psxrecomp/runtime/src/main.cpp")
require(
    "    disruptor_unit_test(disruptor_present_rate " in cmake
    and "constexpr int kDisruptorPresentRateLowest = 30;" in present_rate
    and "constexpr int kDisruptorPresentRateLeastTop = 320;" in present_rate,
    "the in-between frame rate control must run from 30 FPS to at least 320 and keep its unit test",
)
require(
    "    if(DISRUPTOR_FRAME_INTERPOLATION)\n"
    "        list(APPEND DISRUPTOR_EXTRA_SOURCES\n"
    '            "${CMAKE_CURRENT_SOURCE_DIR}/src/game/frame/present_rate.cpp")\n'
    in cmake
    and "#ifndef PSX_DISABLE_FRAME_INTERPOLATION\n"
    "    int present_enabled = 0;\n"
    "    if (disruptor_present_rate_switch(\n"
    '            find_option("video.frame_interpolation")->label,\n'
    '            find_option("video.frame_interpolation")->hint,\n'
    '            applies("video.frame_interpolation") ? 1 : 0,\n'
    "            &present_enabled))\n"
    "        mark_present_enabled(present_enabled != 0);\n"
    "    ImGui::Indent();\n"
    "    int present_rate = 0;\n"
    "    if (disruptor_present_rate_control(\n"
    '            find_option("video.frame_interpolation_fps")->label,\n'
    '            find_option("video.frame_interpolation_fps")->hint, &present_rate))\n'
    "        mark_present_rate(present_rate);\n"
    "    ImGui::Unindent();\n"
    "#endif\n"
    in menu
    and "#ifndef PSX_DISABLE_FRAME_INTERPOLATION\n"
    '    if (changed("video.frame_interpolation"))\n'
    "        (void)disruptor_present_rate_enable(pending.frame_interpolation ? 1 : 0);\n"
    '    if (changed("video.frame_interpolation_fps"))\n'
    "        (void)disruptor_present_rate_apply(pending.frame_interpolation_fps);\n"
    "#endif\n"
    in menu,
    "a build without the frame interpolator must have no rate control, in the menu or in the sources",
)
require(
    '    const bool moved = ImGui::SliderInt(label, &shown, kDisruptorPresentRateLowest, top, "%d FPS",\n'
    "                                        ImGuiSliderFlags_AlwaysClamp);\n"
    "    disruptor::ui::hint(hint);\n"
    "    if (moved && disruptor_present_rate_apply(shown)) {\n"
    "        if (accepted) *accepted = shown;\n"
    "        changed = 1;\n"
    in present_control
    and "const int top = disruptor_present_rate_top(display_hz);" in present_control
    and "int shown = disruptor_present_rate_shown(target, top);" in present_control,
    "the control must apply the rate at once and report only a rate the host accepted",
)
require(
    "    return enabled && psx_host_video_set_interpolation(1, frames_per_second, blend) != 0;\n" in present_control,
    "the rate control must not switch in-between frames on by itself, and must keep their blend mode",
)
require(
    '"Build the experimental presentation-only frame interpolator"\n    ON)' in cmake,
    "the in-between frames must be built unless a build opts out",
)
require(
    "    return psx_host_video_set_interpolation(enabled ? 1 : 0, target, blend) != 0;\n" in present_control
    and '"video.frame_interpolation", "In-between frames", 0,' in options
    and 'slider("video.frame_interpolation_fps", "In-between frame rate", 0,' in options
    and "int disruptor_present_rate_switch(const char *label, const char *hint, int available, int *enabled) {\n"
    in present_control
    and "int disruptor_present_rate_control(const char *label, const char *hint, int *accepted) {\n" in present_control
    and "    const bool clicked = ImGui::Checkbox(label, &checked);\n"
    "    disruptor::ui::hint(hint);\n"
    "    if (clicked && disruptor_present_rate_enable(checked ? 1 : 0)) {\n"
    "        if (enabled) *enabled = checked ? 1 : 0;\n"
    in present_control,
    "the switch must keep the rate and the blend mode and report only a state the host accepted",
)
require(
    'void mark_present_enabled(bool value) { mark("video.frame_interpolation", value); }\n' in menu
    and MERGED in menu
    and '    toggle<&Settings::has_frame_interpolation, &Settings::frame_interpolation>(\n        "video.frame_interpolation",'
    in options,
    "a changed switch must reach settings.toml",
)
require(
    "    if (!enabled) {\n"
    "        (void)disruptor_present_rate_enable(0);\n"
    "        mark_present_enabled(false);\n" in menu,
    "switching exact geometry off must ask the host to switch the in-between frames off and always save them as off",
)
require(
    "        if (us.has_frame_interpolation)\n"
    "            g_frame_interpolation = us.frame_interpolation ? 1 : 0;\n"
    "        if (us.has_frame_interpolation_fps)\n"
    in host
    and "        if (g_frame_interpolation_blend == PSX_HOST_FRAME_INTERPOLATION_GEOMETRY &&\n"
    "            !gpu_geometry_correction_enabled())\n"
    "            g_frame_interpolation = 0;\n"
    in host,
    "the host must restore the saved switch, and geometry in-between frames not without exact geometry",
)
require(
    'void mark_present_rate(int value) { mark("video.frame_interpolation_fps", value); }\n' in menu
    and MERGED in menu
    and "write<&Settings::has_frame_interpolation_fps, &Settings::frame_interpolation_fps>, needs_frames)" in options,
    "a changed in-between frame rate must reach settings.toml",
)
require(
    "(target_fps < 30 || target_fps > 1000))" in host
    and "if (fps == 0 || (fps >= 30 && fps <= 1000)) g_frame_interpolation_fps = fps;" in host
    and "if (us.has_frame_interpolation_fps)\n            g_frame_interpolation_fps = us.frame_interpolation_fps;"
    in host,
    "the host must take a rate from 30 FPS, from the menu, the environment and the saved settings",
)
require(
    ": (target_hz >= 30.0 ? target_hz : host_hz);" in renderer
    and "(effective_hz < 0.0 || effective_hz >= 30.0)) ? 1 : 0;" in renderer
    and "            if (hz < 30.0) hz = 60.0;\n" in renderer,
    "the presenter must run at any rate from 30 FPS",
)
require(
    "(s.frame_interpolation_fps >= 30 && s.frame_interpolation_fps <= 1000))"
    in read("psxrecomp/recompiler/src/config_loader.cpp"),
    "settings.toml must keep an in-between frame rate from 30 FPS",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    for address, instruction in RETAIL_INSTRUCTIONS.items():
        offset = ROM_TEXT_OFFSET + address - LOAD_ADDRESS
        require(0 <= offset <= len(image) - 4, f"retail instruction outside image: 0x{address:08X}")
        require(
            struct.unpack_from("<I", image, offset)[0] == instruction,
            f"retail frame-loop identity changed at 0x{address:08X}",
        )

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    require(
        text.count("psx_mod_function_entry(cpu, 0x8004B3D4u)") == 1,
        "generated guest code must contain exactly one VSync entry hook",
    )
    require(
        text.count("/* 0x80043E90: 0x28420001 */") == 1,
        "generated guest code must contain the patched frame-floor compare",
    )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"Disruptor frame-rate source/codegen contract: PASS ({checked})")
