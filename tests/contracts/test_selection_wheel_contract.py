#!/usr/bin/env python3
"""Check wheel routing, defaults and the retail inventory identity."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(name: str) -> str:
    return (ROOT / name).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


mouse = read("src/game/input/mouse_aim.cpp")
wheel = read("src/game/input/selection_wheel.cpp")
menu = read("src/game/ui/menu/menu.cpp")
options = read("src/game/settings/options.cpp")
settings = read("src/game/settings/game_user_settings.h")
saved = read("src/game/settings/saved.cpp")
preferences = read("src/game/ui/menu/preferences.cpp")
require(
    "bool has_selection_wheels = false; bool selection_wheels = false;" in settings
    and '"disruptor.selection_wheels", "Weapon and psionic wheels", 0,' in options,
    "both wheels must share one option that defaults off",
)
require(
    "s.has_selection_wheels = true;" in saved
    and "if (s.has_selection_wheels)" in saved
    and "settings.has_selection_wheels && settings.selection_wheels" in preferences
    and 'changed("disruptor.selection_wheels")' in preferences,
    "the single wheel preference must save and apply both true and false",
)
require(
    mouse.index("if (disruptor_selection_wheel_frame())") < mouse.index("if (!g_mouse.enabled || !sdl_window) return;")
    and "press = disruptor_selection_wheel_filter_press(press);" in mouse,
    "wheels must read the final input sample even with modern controls off, and prevent later selector injection",
)
require(
    "psx_keybinds_get_button(1, PSX_KB_L1)" in wheel and "psx_keybinds_get_button(1, PSX_KB_R1)" in wheel,
    "the wheels must follow the existing configured selection bindings",
)
require(
    "gl_renderer_set_host_ui_suspended(1);" in menu
    and "disruptor_selection_wheels_set_available(g_menu.imgui_ready && backend == PSX_HOST_UI_BACKEND_OPENGL)" in menu,
    "only a ready overlay can replace the selectors, and drawing must suspend interpolation",
)
require(
    "disruptor_selection_wheel_cancel();" in read("src/game/host.cpp"),
    "loading a save state must cancel stale wheel input",
)
require("disruptor_capsule_note_byte(address" in wheel, "wheel selections must be present in replay capsules")
require(
    "    disruptor_capsule_after_input();\n    disruptor_selection_wheel_guard_input();" in read("src/game/host.cpp"),
    "open wheels must also neutralise input on fast-forward frames with no presentation hook",
)

retail = (ROOT / "input/SLUS_002.24.code", ROOT / "input/SLUS_002.24")
require(
    "--require-artifacts" not in sys.argv[1:] or all(artifact.exists() for artifact in retail),
    "the build gate must check the retail image and executable: one is absent",
)
path = ROOT / "input/SLUS_002.24.code"
if path.exists():
    image = path.read_bytes()
    for pc, opcode in {
        0x8003E128: 0x24E77629,  # current psionic
        0x8003E134: 0x24A57B5C,  # psionic ownership base
        0x8003E190: 0x2A220005,  # five selectable psionic IDs
        0x80039A30: 0xA0227629,  # native selector stores only the ID
        0x8003D9C0: 0x28620008,  # native ammo display excludes special weapon IDs >= 8
        0x8003D9D4: 0x90226A60,  # secondary ammo index table
        0x8003D9FC: 0x90226A54,  # primary ammo index table
        0x8003DA14: 0x00822021,  # displayed count adds both pools
    }.items():
        actual = struct.unpack_from("<I", image, 0x800 + pc - 0x80011200)[0]
        require(actual == opcode, f"native inventory changed at 0x{pc:08X}")

path = ROOT / "input/SLUS_002.24"
if path.exists():
    image = path.read_bytes()
    load = struct.unpack_from("<I", image, 0x18)[0]
    for address, indices in {
        0x80056A54: (7, 1, 1, 2, 2, 4, 6, 6, 7, 7),
        0x80056A60: (7, 0, 0, 3, 3, 5, 0, 0, 7, 7),
    }.items():
        offset = 0x800 + address - load
        require(
            image[offset : offset + len(indices)] == bytes(indices), f"retail ammo mapping changed at 0x{address:08X}"
        )

print("Weapon and psionic wheel contract: PASS")
