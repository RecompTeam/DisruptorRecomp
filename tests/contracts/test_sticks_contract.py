#!/usr/bin/env python3
"""Source contract for the gamepad sticks: the game folds them, the runtime's map stops folding them but in netplay."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def between(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    return source[start : source.index(closing, start)]


runtime = read("psxrecomp/runtime/src/main.cpp")
mouse = read("src/game/input/mouse_aim.cpp")
folding = between(runtime, "static uint16_t controller_pad_buttons(", "\n}\n")

require(
    "    const bool game_sticks = game_reads_sticks && !psx_netplay_active();\n" in folding
    and "            if ((suppress_stick_axes || game_sticks) && source_is_stick_axis(source)) continue;\n" in folding,
    "the map's stick sources press nothing while the game reads the sticks, and press again in a network game",
)
require(
    "controller_pad_buttons(p.handle, suppress_stick_axes)"
    in between(runtime, "static uint16_t pad_buttons_for(", "\n}\n")
    and "controller_pad_buttons(h, suppress_stick_axes)"
    in between(runtime, "static uint16_t dev_all_controllers_buttons(", "\n}\n")
    and runtime.count("controller_pad_buttons(") == 4,
    "every pad sample folds the map through that one function, so no path folds the sticks beside it",
)
require(
    mouse.count("psx_input_set_game_sticks(") == 2
    and "        psx_input_set_game_sticks(1);\n" in between(mouse, "struct MouseAimRegistration {", "\n};\n"),
    "the game takes the sticks once, before the first sample, and never hands them back",
)
require(
    "const bool pad = !netplay && !g_mouse.replaying && psx_input_pad_sticks(0, axes) != 0;" in mouse,
    "the game's fold leaves a network game and a capsule's replay to the runtime and the recording",
)
