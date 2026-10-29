#!/usr/bin/env python3
"""Retail, configuration and codegen contract for EXIT under CREDITS on the main menu."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800
GP = 0x8007114C

PATCHES = {
    "disruptor-main-menu-items": (0x8001C178, 0x2A020008, 0x2A020009),
    "disruptor-main-menu-up-wrap": (0x8001C1A4, 0x34020007, 0x34020008),
    "disruptor-main-menu-down-wrap": (0x8001C1D0, 0x28420008, 0x28420009),
}
RETAIL_INSTRUCTIONS = {
    0x8001C044: 0x27BDFFD8,
    0x8001C04C: 0x00809821,
    0x8001C11C: 0x8F820620,
    0x8001C168: 0x0C00691B,
    0x8001C174: 0x26100001,
    0x8001C184: 0x32621000,
    0x8001C18C: 0x32624000,
    0x8001C190: 0x8F820620,
    0x8001C198: 0x2442FFFF,
    0x8001C1A0: 0x04410002,
    0x8001C1BC: 0x326209E0,
    0x8001C1C0: 0x8F820620,
    0x8001C1C8: 0x24420001,
    0x8001C1E8: 0x326209E0,
    0x8001C1F4: 0x8F820620,
    0x8001C200: 0x24420001,
    0x8001C204: 0xAF820464,
    **{address: expected for address, expected, _ in PATCHES.values()},
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
header = read("src/game/session/main_menu_exit.h")
module = read("src/game/session/main_menu_exit.cpp")

require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/session/main_menu_exit.cpp"\n' in cmake
    and "    disruptor_contract(main_menu_exit RETAIL)\n" in cmake
    and "    disruptor_unit_test(disruptor_main_menu_exit tests/unit/disruptor_main_menu_exit_test.cpp INCLUDES src)\n"
    in cmake
    and "    disruptor_unit_test(disruptor_main_menu_exit_hooks tests/unit/disruptor_main_menu_exit_hooks_test.cpp\n"
    in cmake
    and "        COMMAND disruptor-main-menu-exit-hooks-test --replay)\n" in cmake,
    "the EXIT module, its unit tests with and without a replay and this contract must stay registered",
)

for name in ("game.toml", "game-widescreen.toml"):
    config = read(name)
    entries = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", config)
    require(
        entries is not None and '"0x8001C044"' in entries.group(1) and '"0x8001A46C"' in entries.group(1),
        f"{name} must hook the main menu's handler and its item draw",
    )
    for patch, (address, expected, replacement) in PATCHES.items():
        require(
            re.search(
                rf'\[\[recompiler\.patch\]\]\s*id = "{patch}"\s*address = "0x{address:08X}"\s*'
                rf'expected = "0x{expected:08X}"\s*replacement = "0x{replacement:08X}"',
                config,
            )
            is not None,
            f"{name} must carry the guarded patch {patch}",
        )

for constant in (
    "kHandler = 0x8001C044u;",
    "kItemDraw = 0x8001A46Cu;",
    "kItemDrawn = 0x8001C170u;",
    f"kCursor = 0x{GP + 0x620:08X}u;",
    "kChoose = 0x09E0u;",
    "kItems = 9u;",
    "kExit = 8u;",
):
    require(constant in header, f"the module's {constant.split(' ')[0]} must name what the retail handler uses")

require(
    module.count("std::abort();") == 1
    and '"disruptor.main_menu_exit.draw", menu_exit::kItemDraw, draw_entry' in module
    and '"disruptor.main_menu_exit.menu", menu_exit::kHandler, menu_entry' in module,
    "the patched menu needs both hooks: without them its ninth item draws RETURN TO MAIN MENU and opens the death screen",
)
choice = module.index("    cpu->gpr[menu_exit::kButtons] = menu_exit::kept(pressed);\n")
require(
    choice
    < module.index(
        "    if (g_replaying || psx_netplay_active() || g_ls_mode != 0 || g_ls_replay_active != 0) return;\n"
    )
    < module.index("    SDL_PushEvent(&quit);\n"),
    "the choice is taken from the game in every run, a replay and netplay included: only the quit is held back",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    for address, instruction in RETAIL_INSTRUCTIONS.items():
        offset = ROM_TEXT_OFFSET + address - LOAD_ADDRESS
        require(0 <= offset <= len(image) - 4, f"retail instruction outside image: 0x{address:08X}")
        require(
            struct.unpack_from("<I", image, offset)[0] == instruction,
            f"retail main menu identity changed at 0x{address:08X}",
        )

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    require(
        text.count("psx_mod_function_entry(cpu, 0x8001C044u)") == 1,
        "generated guest code must enter the main menu's hook once",
    )
    for patch, (address, _, replacement) in PATCHES.items():
        require(
            text.count(f"/* 0x{address:08X}: 0x{replacement:08X} */") == 1,
            f"generated guest code must carry {patch}",
        )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"main menu exit contract: PASS ({checked})")
