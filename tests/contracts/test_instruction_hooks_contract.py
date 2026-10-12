#!/usr/bin/env python3
"""Source contract for the game's hooks after an instruction: one list in the settings, and who reads it."""

import re
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

GENERATOR_TABLES = {
    "disruptor_vertical_camera_instruction_hook": "kDisruptorVerticalCameraSites",
    "disruptor_far_rendering_instruction_hook": "kDisruptorFarRenderingSites",
    "disruptor_billboard_aspect_instruction_hook": "kDisruptorBillboardAspectSites",
    "disruptor_backdrop_tiles_instruction_hook": "kDisruptorBackdropTileSites",
    "disruptor_sprite_depth_instruction_hook": "kDisruptorSpriteDepthSites",
    "disruptor_intro_skip_instruction_hook": "kDisruptorIntroSkipSites",
}
SITES = 66
SHARED = {0x8004279C: ["disruptor_vertical_camera_instruction_hook", "disruptor_far_rendering_instruction_hook"]}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def listed_hooks(path: str) -> dict[str, dict[int, int]]:
    """The hooks of a game configuration, by callback, in the order they are written."""
    hooks: dict[str, dict[int, int]] = {}
    for hook in tomllib.loads(read(path))["recompiler"]["instruction_hook"]:
        require(set(hook) == {"callback", "sites"}, f"{path}: a hook is a callback and its sites")
        sites = {int(address, 16): int(word, 16) for address, word in hook["sites"]}
        require(
            len(sites) == len(hook["sites"]) and hook["callback"] not in hooks,
            f"{path}: {hook['callback']} is listed twice, or one of its addresses is",
        )
        hooks[hook["callback"]] = sites
    return hooks


def generator_table(generator: str, table: str) -> dict[int, int]:
    """A site table of the framework's code generator."""
    rows = re.search(r"static const \w+ " + table + r"\[\] = \{\n(.*?)\n\};\n", generator, re.DOTALL)
    if rows is None:
        raise AssertionError(f"the generator has no table {table}")
    found = re.findall(r"\{0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u(?:, \d+u)?\},", rows.group(1))
    sites = {int(address, 16): int(word, 16) for address, word in found}
    require(len(sites) == len(found), f"{table} lists an address twice")
    return sites


generator = read("psxrecomp/recompiler/src/code_generator.cpp")
interpreter = read("psxrecomp/runtime/src/dirty_ram_interp.c")
emitter = read("psxrecomp/recompiler/src/main_psx.cpp")
settings = read("psxrecomp/recompiler/src/config_loader.cpp")
host = read("psxrecomp/runtime/include/game_host.h")
cmake = read("CMakeLists.txt")

hooks = listed_hooks("game.toml")
require(
    hooks == listed_hooks("game-widescreen.toml") and list(hooks) == list(listed_hooks("game-widescreen.toml")),
    "both game configurations must list the same hooks in the same order",
)
require(
    list(hooks) == list(GENERATOR_TABLES) and sum(len(sites) for sites in hooks.values()) == SITES,
    "the hooks are the six the generator knows, in the order it writes them, at 66 sites",
)
for callback, table in GENERATOR_TABLES.items():
    require(
        hooks[callback] == generator_table(generator, table),
        f"{callback}: native code and code that runs from RAM must be hooked at the same instructions",
    )
shared = {
    address: [callback for callback, sites in hooks.items() if address in sites]
    for address in {address for sites in hooks.values() for address in sites}
}
require(
    {address: callbacks for address, callbacks in shared.items() if len(callbacks) > 1} == SHARED,
    "one instruction has two hooks, and the camera's runs first as it does in native code",
)
require(
    "instruction_hooks" not in generator
    and "instruction_hooks" not in read("psxrecomp/recompiler/include/code_generator.h"),
    "the generator's sources make the key of a save state: its own site tables stay until the pin is raised",
)
changed = [
    line.removeprefix("+").strip()
    for line in read("framework/FILES.txt").splitlines()
    if line.strip() and not line.startswith("#")
]
naming = sorted(path for path in changed if b"disruptor" in (ROOT / "psxrecomp" / path).read_bytes().lower())
require(
    naming == ["recompiler/src/code_generator.cpp"],
    f"of the framework files the project changes only the code generator may name the game: {naming}",
)
require(
    "disruptor" not in interpreter.lower()
    and interpreter.count("psx_game_instruction_ran(") == 1
    and "    const int rv = exec_one_fetched_inner(cpu, pc, insn, next_pc_out);\n"
    "\n"
    "#ifdef PSX_HAS_GAME_HOST\n"
    "    psx_game_instruction_ran(cpu, pc, insn);\n"
    "#endif\n"
    in interpreter
    and '#ifdef PSX_HAS_GAME_HOST\n#include "game_host.h"\n#endif\n' in interpreter
    and "void psx_game_instruction_ran(CPUState *cpu, uint32_t address, uint32_t instruction);\n" in host
    and "    PSX_HAS_GAME_HOST=1" in cmake,
    "the interpreter names no game: it makes one call after an instruction, and the generated code knows the sites",
)
require(
    "        instruction_hooks = cfg.instruction_hooks;\n" in emitter
    and "        if (instruction_hooks.empty())\n            instruction_hooks = wscfg.instruction_hooks;\n" in emitter
    and "                    hooked[site.address].emplace_back(site.expected, hook.callback);\n" in emitter
    and "            if (hooked.empty())\n"
    '                ds << "    (void)cpu; (void)address; (void)instruction;\\n}\\n";\n'
    in emitter
    and "        if (!instruction_hooks.empty()) {\n" not in emitter
    and '            ds << "void psx_game_instruction_ran(CPUState* cpu, uint32_t address, uint32_t instruction) {\\n";\n'
    in emitter
    and '                        "        if (instruction == 0x{:08X}u) {}(cpu, address, instruction, 1);\\n",\n'
    in emitter,
    "the dispatch file must carry the interpreter's calls: by address, the whole word checked, after the instruction, "
    "and the function itself even when no hook is listed",
)
hashed = settings[settings.index("uint32_t overlay_codegen_config_hash(") : settings.index("// Pad mode <-> string.")]
require(
    "instruction_hook" not in hashed
    and '        for (const auto& item :\n             toml::find<toml::array>(recomp, "instruction_hook")) {\n'
    in settings
    and "                if ((site.address & 3u) != 0)\n" in settings
    and "        /*instruction_hooks*/     instruction_hooks,\n" in settings,
    "the settings reader takes the list, refuses an address between instructions, and keeps it out of the state's key",
)
require(
    "    disruptor_contract(instruction_hooks)\n" in cmake,
    "this contract must remain registered",
)

print("Disruptor instruction hook source contract: PASS")
