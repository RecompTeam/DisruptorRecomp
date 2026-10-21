#!/usr/bin/env python3
"""Pin direct weapon selection to the retail inventory, selector and HUD order."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


source = (ROOT / "src/game/input/mouse_aim.cpp").read_text(encoding="utf-8")
for address in ("0x80077627u", "0x800770F4u", "0x8007145Cu"):
    require(address in source, f"weapon selection changed retail address {address}")

image_path = ROOT / "input/SLUS_002.24.code"
exe_path = ROOT / "input/SLUS_002.24"
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and exe_path.exists()),
    "the build gate must check the retail image and executable: one is absent",
)
if image_path.exists():
    image = image_path.read_bytes()
    # The original weapon-list confirm checks ownership then stores the ID.
    for pc, opcode in {
        0x8003990C: 0x90637628,  # highlighted weapon ID
        0x80039914: 0x00031080,  # four-byte ownership entries
        0x80039920: 0x8C2270F4,  # inventory base, including baton and pistol
        0x80039934: 0xA0237627,  # selected ID; native update handles animation
        0x80039938: 0xA3800310,  # close weapon selection, return to scene mode 0
        0x80032658: 0x90637627,  # native weapon update consumes selected ID
    }.items():
        actual = struct.unpack_from("<I", image, 0x800 + pc - 0x80011200)[0]
        require(actual == opcode, f"retail weapon instruction changed at 0x{pc:08X}")

if exe_path.exists():
    image = exe_path.read_bytes()
    base = struct.unpack_from("<I", image, 0x18)[0]
    names = (
        b"Beeatch",
        b"18mm Semi",
        b"18mm Auto",
        b"Phase Rifle",
        b"Phase Repeater",
        b"Lockon Cannon",
        b"AM Blaster",
        b"AM Cyclone",
        b"Zodiac",
        b"Plasma Lance",
    )
    for weapon, name in enumerate(names):
        offset = 0x800 + 0x8005725C - base + weapon * 16
        actual = image[offset : offset + 16].split(b"\0", 1)[0]
        require(actual == name, f"retail HUD weapon order changed in slot {weapon + 1}")

print("Disruptor weapon selection retail contract: PASS")
