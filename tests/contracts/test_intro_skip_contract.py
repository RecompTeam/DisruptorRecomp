#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's intro skip."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# A logo screen's frame tests, the boot call of movie 0, the movie-number test that sets the
# 120 frame floor, and the player loop's skip-button test.
RETAIL_INSTRUCTIONS = {
    0x80020A80: 0x2A02005E,
    0x80020A84: 0x1040000F,
    0x80020AB0: 0x2A020019,
    0x80020AC0: 0x3410005E,
    0x80020B98: 0x34020014,
    0x80020C30: 0x2A02006E,
    0x80020CB4: 0x0C005148,
    0x80020CB8: 0x00002021,
    0x800147AC: 0xAF80027C,
    0x800147B0: 0x2A820003,
    0x800147B4: 0x10400002,
    0x800147B8: 0x34020078,
    0x800147BC: 0xAF82027C,
    0x80048670: 0x0C012CD5,
    0x80048678: 0x30420940,
    0x8004867C: 0x10400007,
    0x80048688: 0x8C4213C8,
    0x80048690: 0x0051102A,
    0x80048694: 0x1440000A,
}
SEAMS = {0x80020A80: 0x2A02005E, 0x800147B0: 0x2A820003, 0x80048678: 0x30420940}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/game/session/intro_skip.cpp")
header = read("src/game/session/intro_skip.h")
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
generator = read("psxrecomp/recompiler/src/code_generator.cpp")
interpreter = read("psxrecomp/runtime/src/dirty_ram_interp.c")
audit = read("tools/audit_codegen.py")
settings_loader = read("psxrecomp/recompiler/src/config_loader.cpp")

require(
    "src/game/session/intro_skip.cpp" in cmake
    and "PSX_HAS_DISRUPTOR_INTRO_SKIP=1" in cmake
    and "    disruptor_contract(intro_skip RETAIL)\n" in cmake
    and "    disruptor_unit_test(disruptor_intro_skip " in cmake,
    "intro-skip source, its interpreter switch and both tests must remain registered",
)

require(
    "    disruptor_contract(intro_skip RETAIL)\n" in cmake,
    "the build gate must run this contract with its inputs required",
)
for address, word in SEAMS.items():
    require(
        f"{{0x{address:08X}u, 0x{word:08X}u}}," in generator,
        f"the generator must pin the intro-skip seam at 0x{address:08X}",
    )
    require(
        f"(pc == 0x{address:08X}u && insn == 0x{word:08X}u)" in interpreter,
        f"the dirty-RAM interpreter must pin the intro-skip seam at 0x{address:08X}",
    )
    require(f"0x{address:08X}: 0x{word:08X}," in audit, f"the codegen audit must expect the seam at 0x{address:08X}")
    require(f"0x{address:08X}u" in source and f"0x{word:08X}u" in source, f"the hook must check 0x{address:08X}")

require(
    "if (!cpu || phase != 1 || g_ls_mode != 0 || g_ls_replay_active != 0) return;" in source,
    "lockstep and replay runs must keep the retail intro",
)
require(
    "static_cast<int32_t>(cpu->gpr[kMovieRegister]) == kTitleMovie;" in source
    and "constexpr int32_t kTitleMovie = 0;" in source,
    "only the title movie may lose its floor",
)
require(
    "if ((enabled || g_logo_cut_pending) && frame >= kLogoEarliestCut && frame < kLogoFadeOut) {" in source
    and "constexpr int32_t kLogoEarliestCut = 25;" in source
    and "constexpr int32_t kLogoFadeOut = 94;" in source,
    "a logo may be cut only where the game itself accepts a button, and only to its fade-out",
)
require(
    "    if (address == kButtonsSite && instruction == kButtonsWord) {\n        if (!g_movie_skip && requested()) {\n"
    in source
    and "        if (g_title_movie || g_movie_skip) cpu->gpr[kResultRegister] |= kStart;" in source
    and "        g_movie_skip = false;\n" in source,
    "START may be held only for the title movie the floor seam marked or for a movie the skip key ended",
)
require(
    source.count("write_") == 1
    and "            psx_mod_write_word(kMovieFloor, 0u);\n" in source
    and "constexpr uint32_t kMovieFloor = 0x800713C8u;" in source,
    "the intro skip may write one guest word: the floor of a movie the skip key ends",
)
require(
    "constexpr int64_t kRequestLifeMs = 500;" in source
    and "    return at != kNoRequest && now_ms() - at <= kRequestLifeMs;" in source
    and "    if (!g_menu.open && scancode_event(event, SDL_SCANCODE_ESCAPE))\n        disruptor_intro_skip_request();\n"
    in menu,
    "Escape outside the menu must ask for a skip that lasts half a second",
)
for token in ("disruptor_intro_skip_enabled", "disruptor_intro_skip_set_enabled", "disruptor_intro_skip_request",
              "disruptor_intro_skip_fast_forward"):
    require(token in header and token in source, f"intro-skip API is missing {token}")

host_header = read("psxrecomp/runtime/include/host_ui.h")
host = read("psxrecomp/runtime/src/main.cpp")
require(
    'extern "C" int disruptor_intro_skip_fast_forward(void) {\n'
    "    if (g_boot_over || !psx_mod_game_started()) return 0;\n"
    "    const bool enabled = g_enabled.load(std::memory_order_relaxed);\n"
    "    if (g_ls_mode != 0 || g_ls_replay_active != 0 || psx_mod_read_byte(kMenuState) == kMainMenu ||\n"
    "        (enabled && ++g_boot_frames > kBootBudget)) {\n"
    "        g_boot_over = true;\n"
    "        return 0;\n"
    "    }\n"
    "    return enabled ? 1 : 0;\n}" in source
    and "constexpr uint32_t kMenuState = 0x800715FCu;" in source
    and "constexpr uint8_t kMainMenu = 0x18u;" in source
    and "constexpr int kBootBudget = 1500;" in source
    and source.count("g_boot_over = ") == 3,
    "the boot may be fast-forwarded only with the switch on, only until the main menu, and never twice or in lockstep",
)
require(
    "    if (address == kRendererEntry) g_boot_over = true;" in source
    and "constexpr uint32_t kRendererEntry = 0x80040E68u;" in source
    and '    psx_mod_register_function_entry_plugin("disruptor.intro_skip.renderer", kRendererEntry, renderer_entry);\n'
    "    psx_host_set_fast_forward_query(disruptor_intro_skip_fast_forward);" in source,
    "play must end the boot's fast-forward, and the frontend must be given the module's own query",
)
require(
    "void psx_host_set_fast_forward_query(int (*query)(void));" in host_header
    and 'extern "C" void psx_host_set_fast_forward_query(int (*query)(void));' in source
    and 'extern "C" void psx_host_set_fast_forward_query(int (*query)(void)) {\n    g_fast_forward_query = query;\n}' in host,
    "the module and the frontend must agree on the query's shape",
)
require(
    "    const bool module_fast_forward = g_fast_forward_query &&\n"
    "        !psx_netplay_active() && g_fast_forward_query() != 0;\n" in host
    and host.count("g_fast_forward_query()") == 1
    and "                     module_fast_forward ||\n"
    "                     (turbo_loads_active && g_turbo_audio_sink_enabled));" in host
    and "    if (g_headless || module_fast_forward) {\n        netplay_tail.skip_pace();\n        return;\n    }" in host
    and host.index("const bool module_fast_forward") < host.index("    if (g_headless || module_fast_forward) {")
    < host.index("    if (debug_server_turbo_enabled()) {"),
    "the frontend must ask once per VBlank and never under netplay, drop the sound, and neither pace nor present the frame",
)

require(
    "disruptor_intro_skip_set_enabled(skip_intro ? 1 : 0);\n        mark_skip_intro(skip_intro);" in menu
    and '    if (option_toggle("disruptor.skip_intro", &skip_intro)) {\n' in menu
    and '"disruptor.skip_intro", "Skip the logos and the title movie", 0,' in options,
    "the settings menu must apply the switch live and stage it for saving",
)
require(
    'void mark_skip_intro(bool value) { mark("disruptor.skip_intro", value); }\n' in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_skip_intro, &Settings::skip_intro>(\n        "disruptor.skip_intro",' in options,
    "a changed switch must reach settings.toml",
)
require(
    "if (settings.has_skip_intro &&\n"
    '        !env_override_present("PSX_DISRUPTOR_SKIP_INTRO"))\n'
    "        disruptor_intro_skip_set_enabled(settings.skip_intro ? 1 : 0);" in menu,
    "a saved switch must be applied at start unless the environment overrides it",
)
require(
    's.skip_intro = toml::find<bool>(d, "skip_intro");' in settings_loader
    and 'f << "skip_intro = " << (s.skip_intro ? "true" : "false")' in settings_loader
    and "s.has_skip_intro ||" in settings_loader,
    "settings.toml must read and write [disruptor] skip_intro",
)

runtime_main = read("psxrecomp/runtime/src/main.cpp")
require(
    "        if (psx_game_settings_loaded(us)) fast_boot = true;\n" in runtime_main
    and "        if (psx_game_fast_boot_forced()) fast_boot = true;\n" in runtime_main
    and '    return settings.has_skip_intro && settings.skip_intro && !std::getenv("PSX_DISRUPTOR_SKIP_INTRO");\n'
    in read("src/game/host.cpp")
    and '    const char *forced = std::getenv("PSX_DISRUPTOR_SKIP_INTRO");\n'
    "    return forced && forced[0] && forced[0] != '0';\n" in read("src/game/host.cpp"),
    "the saved switch, or the environment in its place, must also skip the BIOS animation",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    for address, instruction in RETAIL_INSTRUCTIONS.items():
        offset = ROM_TEXT_OFFSET + address - LOAD_ADDRESS
        require(0 <= offset <= len(image) - 4, f"retail instruction outside image: 0x{address:08X}")
        require(
            struct.unpack_from("<I", image, offset)[0] == instruction,
            f"retail movie-skip identity changed at 0x{address:08X}",
        )

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    for address, word in SEAMS.items():
        require(
            text.count(f"disruptor_intro_skip_instruction_hook(cpu, 0x{address:08X}u, 0x{word:08X}u, 1);") == 1,
            f"generated guest code must contain exactly one intro-skip hook at 0x{address:08X}",
        )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"Disruptor intro-skip source/codegen contract: PASS ({checked})")
