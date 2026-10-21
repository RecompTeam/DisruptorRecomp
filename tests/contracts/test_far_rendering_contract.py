#!/usr/bin/env python3
"""Source/codegen contract for Disruptor's experimental far rendering."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f"far rendering contract: FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


source = read("src/game/render/far_rendering.cpp")
header = read("src/game/render/far_rendering.h")
codegen = read("psxrecomp/recompiler/src/code_generator.cpp")
main = read("psxrecomp/runtime/src/main.cpp")
audit = read("tools/audit_codegen.py")
cmake = read("CMakeLists.txt")
configs = read("game.toml") + read("game-widescreen.toml")

require("src/game/render/far_rendering.cpp" in cmake and
        "tests/unit/disruptor_far_rendering_test.cpp" in cmake and
        "    disruptor_unit_test(disruptor_far_rendering " in cmake,
        "runtime source and focused behavior test must remain enabled")
require(configs.count('callback = "disruptor_far_rendering_instruction_hook"\n') == 2,
        "both game configurations must list the far hooks for code that runs from RAM")

# The far renderer shares the generated renderer-entry fan-out: a second config entry would duplicate the guest callback.
require(configs.count(
    'mod_function_entry_funcs = ["0x80011888", "0x8001A46C", "0x8001C484", "0x80020DD8", "0x80032514", "0x80039450", "0x8003B78C", "0x80040E68", "0x800429DC", "0x80044A10", "0x80044BDC", "0x80046134", "0x8004B3D4", "0x8004D348", "0x8004D410"]') == 2,
    "both configs must retain exactly one audited renderer-entry seam")
require("disruptor.far_rendering.renderer" in source and
        "psx_mod_register_function_entry_plugin" in source and
        "0x80040E68u" in source,
        "far rendering must register independently at the reviewed renderer")

distance_sites = {
    0x8003A914: 0x8F8C04A0,
    0x8003B8C8: 0x8F8304A0,
    0x8003BCE0: 0x8F8204A0,
    0x8003C168: 0x8F8304A0,
    0x8003CD64: 0x8F8304A0,
    0x8003B2CC: 0x8F820494,
    0x8003BA30: 0x8F820494,
    0x8003BE40: 0x8F820494,
    0x8003C628: 0x8F820494,
    0x8003C878: 0x8F820494,
    0x8003D28C: 0x8F820494,
    0x8003FB0C: 0x8F840494,
    0x80043184: 0x8F820494,  # billboard post-pass CLUT-row selection
}
diagnostic_sites = {
    0x8003A0F8: 0x93B300C0,  # recursion-depth load
    0x8003A390: 0xA082001F,  # completed unique-room mark
    0x8003A92C: 0x00005812,  # first portal-distance candidate
    0x8003A964: 0x00681021,  # second portal-distance candidate
    0x8003AA74: 0x00AC1021,  # third portal-distance candidate
    0x8003B8D8: 0x0072182A,  # object distance decision, pass one
    0x8003BCEC: 0x0053102A,  # object distance decision, pass two
    0x8003C178: 0x0077182A,  # object distance decision, pass three
    0x8003CD70: 0x0077182A,  # object distance decision, pass four
    0x800410E0: 0x93830310,  # visible-room count boundary
    0x800410F8: 0x8F820680,  # visible-list end boundary
    0x800411D8: 0x8F8301D4,  # final packet boundary
}
expected_sites = {
    **distance_sites,
    **diagnostic_sites,
    0x8004279C: 0x8FBF016C,
}
excluded_sites = {
    0x8004030C,  # feeds a later store back to the fade global
    0x80040464,  # feeds a later store back to the fade global
    0x800247C4, 0x80027104, 0x80035538,
}
codegen_table = codegen[
    codegen.index("kDisruptorFarRenderingSites"):
    codegen.index("struct DisruptorBillboardAspectSite")
]
found_sites = {
    int(pc, 16): int(word, 16)
    for pc, word in re.findall(
        r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u\}", codegen_table)
}
require(found_sites == expected_sites,
        "code generation must retain exactly the reviewed far/diagnostic opcodes")
require(not (set(found_sites) & excluded_sites),
        "guest-state writers and unrelated loads must never be substituted")
source_table_start = source.index("constexpr auto kDistanceLoadSites")
source_table = source[source_table_start:
                      source.index("struct SessionDiagnostics",
                                   source_table_start)]
require(re.search(
    r"kDistanceLoadSites\s*=\s*std::array<DistanceLoadSite,\s*13>",
    source) is not None,
        "the host substitution table must contain exactly 13 reviewed loads")
for pc, instruction in distance_sites.items():
    require(f"0x{pc:08X}u" in source_table and
            f"0x{instruction:08X}u" in source_table,
            f"host substitution table is missing 0x{pc:08X}")
require("Disruptor far-rendering site" in codegen and
        "disruptor_far_rendering_instruction_hook(cpu" in codegen,
        "compiled code must fail closed and emit the dedicated AFTER hook")
require('"\\n{}disruptor_far_rendering_instruction_hook(cpu, "' in codegen and
        "config_.indent, addr, instr" in codegen,
        "compiled AFTER hooks must begin a separately indented statement line")

listed_start = configs.index('callback = "disruptor_far_rendering_instruction_hook"\n')
listed_table = configs[listed_start:configs.index("[[recompiler.instruction_hook]]", listed_start)]
for pc, instruction in expected_sites.items():
    require(f'["0x{pc:08X}", "0x{instruction:08X}"],' in listed_table,
            f"code that runs from RAM is not hooked at reviewed site 0x{pc:08X}")
require('"0x{:08X}u, 0x{:08X}u, 1);"' in codegen,
        "all reviewed diagnostic sites must remain AFTER-instruction")
audit_table_start = audit.index("FAR_RENDERING_SITES")
audit_table = audit[audit_table_start:audit.index("# Tests 4-6", audit_table_start)]
for pc in excluded_sites:
    token = f"0x{pc:08X}"
    require(token not in codegen_table and token not in listed_table and
            token not in audit_table and token not in source_table,
            f"excluded far-rendering site leaked into hook tables: {token}")
require("FAR_RENDERING_SITES" in audit and
        "disruptor_far_rendering_instruction_hook" in audit,
        "generated shards must audit the exact far-rendering hook count")
require("far_rendering_raw_matches" in audit and
        "far_rendering_directive_lines" in audit and
        "far-rendering hooks must be separate generated statements" in audit and
        "far-rendering hook shares a preprocessor-directive line" in audit,
        "the generated audit must reject swallowed or coalesced hook calls")


for token in (
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL",
    "DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED",
    "disruptor_far_rendering_set_depth_fade_mode",
    "disruptor_far_rendering_get_depth_fade_mode",
    "far_load_substitutions",
    "fade_load_substitutions",
):
    require(token in header and token in source,
            f"depth-fade diagnostic ABI is missing {token}")
require("DISRUPTOR_FAR_RENDERING_DIAGNOSTICS_ABI_VERSION = 3" in header,
        "portal and packet observations require diagnostics ABI version 3")
for token in (
    "traversal_recursion_entries",
    "traversal_cap_hits",
    "portal_final_decision_flips",
    "object_decision_flips",
    "visibility_samples",
    "packet_stage_samples",
    "packet_entry_to_traversal_end_last",
    "packet_post_visible_loop_last",
):
    require(token in header and token in source,
            f"portal diagnostics ABI is missing {token}")
require("disruptor_far_rendering_abandon_metrics" in header and
        "disruptor_far_rendering_abandon_metrics" in source,
        "the module must expose savestate-abandon cleanup")
require("#ifdef PSX_HAS_GAME_HOST\n    psx_game_state_loaded();\n#endif\n" in main and
        "    disruptor_far_rendering_abandon_metrics();\n}\n" in read("src/game/host.cpp"),
        "successful savestate loads must abandon host measurement spans")

for token in (
    "disruptor_far_rendering_renderer_entry",
    "disruptor_far_rendering_instruction_hook",
):
    require(token in header and token in source,
            f"far-rendering seam is missing declared implementation: {token}")
require("DISRUPTOR_FAR_RENDERING_BEFORE_INSTRUCTION" in header and
        "DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION" in header and
        "DISRUPTOR_FAR_RENDERING_AFTER_INSTRUCTION" in source,
        "the hook ABI must declare phases and restore only after the opcode")

options = read("src/game/settings/options.cpp")
settings_h = read("src/game/settings/game_user_settings.h")
preferences = read("src/game/ui/menu/preferences.cpp")
enhancements = read("src/game/ui/menu/tab_enhancements.cpp")
launcher = read("src/launcher/launcher_settings.cpp")
arena = read("src/game/render/render_arena.cpp")


def body(text: str, name: str) -> str:
    start = text.index(name + "(")
    start = text.index("{\n", start)
    end = text.index("\n}\n", start)
    return text[start:end]


require('choice("disruptor.draw_distance", "Draw distance", 0,' in options and
        'constexpr const char* draw_distances[] = {"Original", "1.25x", "1.5x", "2x"};' in options and
        "inline constexpr int kDisruptorDrawDistances[] = {100, 125, 150, 200};" in settings_h and
        "kQuarters[DISRUPTOR_FAR_RENDERING_PRESET_COUNT] = {4u, 5u, 6u, 8u};" in source and
        "DISRUPTOR_FAR_RENDERING_RETAIL = 0," in header and
        "DISRUPTOR_FAR_RENDERING_EXTENDED = 1, /* 1.25x */" in header and
        "DISRUPTOR_FAR_RENDERING_FAR = 2,      /* 1.50x */" in header and
        "DISRUPTOR_FAR_RENDERING_DOUBLE = 3,   /* 2x */\n    DISRUPTOR_FAR_RENDERING_PRESET_COUNT = 4," in header,
        "the saved draw distance lists the presets in their own order, so a choice is a preset")
require('    (void)disruptor_far_rendering_set_preset(stored(settings, *find_option("disruptor.draw_distance")));\n'
        in body(preferences, "void apply_saved_preferences") and
        '    if (changed("disruptor.draw_distance"))\n'
        '        (void)disruptor_far_rendering_set_preset(stored(pending, *find_option("disruptor.draw_distance")));\n'
        in body(preferences, "void apply_pending_preferences"),
        "a session starts at the saved draw distance and takes a staged one, after its reset to the original")
require('    if (option_list("disruptor.draw_distance", &distance) &&\n'
        "        disruptor_far_rendering_set_preset(distance) == DISRUPTOR_FAR_RENDERING_OK)\n"
        "        mark_draw_distance(distance);\n" in enhancements and
        "disruptor_far_rendering_netplay_blocked()" in enhancements and
        '    {"disruptor.smooth_fade", Page::enhancements},\n    {"disruptor.fog_shape", Page::enhancements, nullptr, true},\n    {"disruptor.draw_distance", Page::enhancements},\n' in launcher and
        "Page::experimental}" not in launcher.split("disruptor.draw_distance")[1].split("\n")[0] and
        "draw_distances, saved<&Settings::has_draw_distance>, draw_distance, set_draw_distance, {}, distances_offered,\n           \"Longer distances come in a later release.\")," in options and
        "constexpr int distances_offered = 2;" in options,
        "the menu saves only a draw distance the game took, both front ends list it on Enhancements under the smooth fade, "
        "and only the original distance and 1.25x are offered for now")
require("        (psx_mod_widescreen_x_margin() == 0 && !disruptor_far_rendering_edge_margin() &&\n"
        "         disruptor_far_rendering_get_preset() == DISRUPTOR_FAR_RENDERING_RETAIL)) {\n" in arena and
        "    disruptor_far_rendering_drawn_distances(&far_distance, &fade_start);\n" in arena and
        "kFadeStart" not in arena and "kFarDistance" not in arena,
        "a longer draw distance moves the primitive arena at 4:3 too, and the fades follow the distances drawn")
drawn = body(source, 'extern "C" void disruptor_far_rendering_drawn_distances')
require("    *far_distance = psx_mod_read_word(kFarDistance);\n" in drawn and
        "    if (g_written.active && *far_distance == g_written.effective_far) *far_distance = g_written.visual_far;\n" in drawn and
        "    *fade_start = static_cast<int32_t>(psx_mod_read_word(kFadeStart));" in drawn and "g_preset" not in drawn,
        "the drawn distances are the two globals as they stand, the preset's only inside a written frame")
ready = body(read("src/game/ui/menu/menu.cpp"), "void on_runtime_ready")
require(0 <= ready.find("disruptor_far_rendering_reset_session();") < ready.find("load_preferences_for_session();"),
        "a session resets the draw distance before it applies the saved one")

writes = re.findall(r"psx_mod_write_word\(([^,]+),", source)
require(sorted(writes) == ["kFadeStart", "kFadeStart", "kFarDistance", "kFarDistance"] and
        body(source, "void restore_distances").count("psx_mod_read_word(") == 2,
        "far rendering writes only the two distance globals, and reads each before giving it back (the behaviour is the unit test's)")
host = read("src/game/host.cpp")
require("    disruptor_far_rendering_state_saving();" in body(host, "void psx_game_state_saving") and
        "disruptor_far_rendering_abandon_metrics();" in body(host, "void psx_game_state_loaded") and
        "    restore_distances();" in body(source, 'extern "C" void disruptor_far_rendering_state_saving') and
        "    g_written = WrittenDistances{};\n" in body(source, 'extern "C" void disruptor_far_rendering_abandon_metrics'),
        "a savestate never holds the written distances, and a loaded one is left as it holds them")
entry = source[source.index("static void begin_renderer_frame(CPUState *cpu) {"):]
entry = entry[:entry.index("\n}\n")]
require("    begin_renderer_frame(cpu);\n    if (!comparator_active()) disruptor_render_arena_publish_fades();" in
        body(source, 'extern "C" void disruptor_far_rendering_renderer_entry'),
        "the renderer's fades are published after the entry decided what the frame draws with")
require(entry.rstrip().endswith(
            "set_frame_fog(source_fade, effective_far, effective_fade);") and
        "    write_distances(source_far, source_fade, effective_far, effective_fade);\n    g_frame.extended = g_written.active;\n" in entry and
        "    restore_distances();\n" in body(source, "void finish_active_frame") and
        "    restore_distances();\n" in body(source, "void abandon_active_frame"),
        "the distances are written last at a validated entry and given back at the epilogue and at an abandon")
saving = read("psxrecomp/runtime/src/savestate.c")
require("    if (s_save_pending >= 0) {\n        if (psx_frontend_savestate_deferred()) return;\n        int slot = s_save_pending;\n" in saving and
        'extern "C" int psx_frontend_savestate_deferred(void) {\n#ifdef PSX_HAS_GAME_HOST\n    return psx_game_state_save_deferred();\n'
        in main and
        "int psx_game_state_save_deferred(void);\n" in read("psxrecomp/runtime/include/game_host.h") and
        "int psx_game_state_save_deferred(void) {\n    return disruptor_far_rendering_save_deferred() || "
        "disruptor_language_movie_sound_save_deferred();\n}\n" in host,
        "a save state asked for inside a frame drawn at the preset's distances waits for a later block leader")
require("    disruptor_render_arena_publish_fades();\n" in body(arena, "void renderer_entry") and
        "    if (pc == kBeforeVisibleLoop && instruction == kBeforeVisibleLoopOpcode && !comparator_active())\n"
        "        disruptor_render_arena_publish_fades();\n" in source,
        "the renderer's fades are set at its entry and again after the screen effects")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
gpu = read("psxrecomp/runtime/src/gpu.c")
require("int gpu_current_packet_fade(void) {\n#ifdef PSX_HAS_GAME_HOST\n    if (gp0_cmd_source_addr == 0xFFFFFFFFu) return -1;\n"
        "    const uint32_t key = psx_mod_gpu_dma_resolve_address(gp0_cmd_source_addr - 4u);\n"
        "    const int words = gp0_words_needed < 16 ? gp0_words_needed : 16;\n"
        "    return key ? psx_game_packet_fade(key, psx_gpu_command_signature(gp0_cmd_buf, words)) : -1;\n" in gpu and
        "static uint32_t gp0_cmd_buf[16];" in gpu and
        "    return disruptor_far_rendering_packet_fraction(resolved_packet, signature);\n" in host and
        "    const int packet_raw = packet_fade >= 0 ? 2 | (packet_fade << 2) : 0;\n" in renderer and
        "vp[13] = (float)((rawtex ? 1 : 0) | packet_raw);" in renderer and
        '"  } else if ((v_raw & 2) != 0) level = float(v_raw >> 2)/256.0;' in renderer and
        '"  if ((v_raw & 1) == 0) rgb = clamp(rgb * v_col.rgb * 2.0, 0.0, 1.0);\\n"' in renderer and
        "v_raw == 0" not in renderer,
        "a sprite the game faded carries its share of the next row from its packet to the renderer, the colour bit apart")
require("void gl_renderer_set_palette_fade_power(int power)" in renderer and
        "    gl_renderer_set_palette_fade_power(ramp_power);\n" in arena,
        "the renderer's fade grows with the power the far rendering hands over")

margin = body(source, "bool edge_margin_active")
require("constexpr uint32_t kEdgeMargin = 512u;" in source and
        "gl_renderer_smooth_fade() != 0 && g_depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_RETAIL &&" in margin and
        "!comparator_active() && !psx_netplay_active();" in margin and
        "? std::min(effective_far + kEdgeMargin, kMaximumSafeEffectiveFar) : effective_far;" in body(source, "void write_distances") and
        "        const bool object = site.pc != kPortalFarLoad;\n        if (object) cpu->gpr[loaded] = g_written.visual_far;\n" in source and
        "constexpr uint32_t kPortalFarLoad = 0x8003A914u;" in source and
        "g_depth_fade_mode == DISRUPTOR_FAR_RENDERING_DEPTH_FADE_DISABLED || edge_margin_active())) {" in source,
        "under the smooth fade the meshes and rooms are kept a margin past the far plane seen, objects stop at it, never in a "
        "network game or the comparator")
print("far rendering source/codegen contract: PASS")
