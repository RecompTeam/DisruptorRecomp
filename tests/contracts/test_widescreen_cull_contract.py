#!/usr/bin/env python3
"""Version and source contract for the side planes of Disruptor's draw lists under a wide picture."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# Per funnel: bound test against the step aside, negated step test, branches. The game keeps 45 degrees, the 4:3 picture.
FUNNELS = {
    "the first funnel": (0x8003B8EC, (0x0067102A, 0x144000A8, 0x00071023, 0x0062102A, 0x144000A5)),
    "the second funnel": (0x8003BD00, (0x0067102A, 0x144000AD, 0x00071023, 0x0062102A, 0x144000AA)),
    "the third funnel": (0x8003C194, (0x02A9102A, 0x14400256, 0x00091023, 0x02A2102A, 0x14400253)),
    "the fourth funnel": (0x8003CDA4, (0x0068102A, 0x144001C7, 0x00081023, 0x0062102A, 0x144001C4)),
}
# The same five instructions test the step up and down right after the step aside in three of the funnels, and stay vanilla.
VERTICAL = (0x8003B900, 0x8003BD14, 0x8003CDB8)
# Fourth funnel after projection, either facing: keep when the left edge is under 300 and the right edge 21 or more.
SCREEN_BOUNDS = {
    "slti_sites": ((0x8003D0F8, 0x28E2012C, 300), (0x8003D110, 0x2842012C, 300)),
    "slti_lower_sites": ((0x8003D108, 0x28420015, 21), (0x8003D118, 0x28E20015, 21)),
}
# The same funnel's bounds up and down, 220 and 21 of the 240 rows, stay vanilla.
VERTICAL_BOUNDS = (0x8003D128, 0x8003D138, 0x8003D140, 0x8003D148)
SITES = [
    (f"0x{first + offset:08X}", f"0x{words[index]:08X}")
    for first, words in FUNNELS.values()
    for offset, index in ((0, 0), (12, 3))
]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def listed(path: str) -> list[tuple[str, str]]:
    """The side-plane sites a game file names, or nothing when a block of them is not an address and an instruction."""
    text = read(path)
    sites = re.findall(
        r'^\[\[widescreen\.cull\.horizontal_slt\]\]\naddress = "(0x[0-9A-F]{8})"\nexpected = "(0x[0-9A-F]{8})"$',
        text,
        re.MULTILINE,
    )
    return sites if len(sites) == text.count("[[widescreen.cull.horizontal_slt]]") else []


def shape(words: tuple[int, ...]) -> str:
    """What stops five instructions from being one bound tested against a step and against its negation."""
    near, near_branch, negated, far, far_branch = words
    bound, step = near >> 21 & 31, near >> 16 & 31
    if near >> 26 != 0 or near & 0x7FF != 0x2A or near >> 11 & 31 != 2 or bound in (0, 2) or step in (0, 2):
        return "the first test is not slt v0, bound, step"
    if negated != (step << 16 | 2 << 11 | 0x23):
        return "the step is not negated into v0"
    if far != (bound << 21 | 2 << 16 | 2 << 11 | 0x2A):
        return "the second test is not slt v0, bound, v0"
    if any(branch >> 16 != 0x1440 for branch in (near_branch, far_branch)):
        return "a test is not followed by its branch"
    return ""


def bounds(path: str, key: str) -> list[str]:
    """The addresses a game file lists under one key of its widescreen cull table."""
    table = re.search(r"^\[widescreen\.cull\]\n((?:\w+ = \[.*\]\n)+)", read(path), re.MULTILINE)
    line = re.search(rf"^{key} = \[(.*)\]$", table.group(1), re.MULTILINE) if table else None
    return re.findall(r'"(0x[0-9A-F]{8})"', line.group(1)) if line else []


audit = read("tools/audit_codegen.py")
runtime = read("psxrecomp/runtime/src/gpu.c")
codegen = read("psxrecomp/recompiler/src/code_generator.cpp")
cmake = read("CMakeLists.txt")
table = audit[audit.index("HORIZONTAL_SIDE_PLANE_SITES = {") :]
audited = re.findall(r'"(0x[0-9A-F]{8})": "(0x[0-9A-F]{8})"', table[: table.index("}")])

require(
    listed("game.toml") == SITES and listed("game-widescreen.toml") == SITES and audited == SITES,
    "both game files and the audit of the translated code must name the side-plane tests of all four funnels: "
    "one left at 45 degrees drops its pickups a turn step before they leave a 16:9 picture, a seventh inside the edge of a 21:9 one",
)
for key, sites in SCREEN_BOUNDS.items():
    table = audit[
        audit.index("SCREEN_RIGHT_BOUND_SITES = {" if key == "slti_sites" else "SCREEN_LEFT_BOUND_SITES = {") :
    ]
    named = [(f"0x{address:08X}", f"0x{word:08X}") for address, word, _ in sites]
    require(
        bounds("game.toml", key) == bounds("game-widescreen.toml", key) == [address for address, _ in named]
        and re.findall(r'"(0x[0-9A-F]{8})": "(0x[0-9A-F]{8})"', table[: table.index("}")]) == named
        and all(word >> 26 == 10 and word >> 16 & 31 == 2 and word & 0xFFFF == bound for _, word, bound in sites),
        f"both game files and the audit must name the fourth funnel's two {key}: left as they are, a sprite goes away "
        "20 pixels inside a wide picture's edge, as it did under a television's frame",
    )
require(
    "int psx_ws_cull_slti(uint32_t sx, uint32_t imm) {\n    return ((int32_t)sx < (int32_t)imm + psx_ws_x_margin()) ? 1 : 0;\n}\n"
    in runtime
    and "int psx_ws_cull_slti_lower(uint32_t sx, uint32_t imm) {\n"
    "    int32_t bound = (int32_t)(int16_t)(uint16_t)imm;\n"
    "    return ((int32_t)sx < bound - psx_ws_x_margin()) ? 1 : 0;\n}\n"
    in runtime
    and '            return fmt::format("{} = psx_ws_cull_slti({}, {});"\n' in codegen
    and '            return fmt::format("{} = psx_ws_cull_slti_lower({}, {});"\n' in codegen,
    "the two bounds move out by the picture's margin and stay where they are at 4:3, where the margin is nothing",
)
require(
    all(not shape(words) for _, words in FUNNELS.values())
    and shape((0x0068182A, 0x144001C7, 0x00081023, 0x0062102A, 0x144001C4))
    == "the first test is not slt v0, bound, step"
    and shape((0x0068102A, 0x144001C7, 0x00071023, 0x0062102A, 0x144001C4)) == "the step is not negated into v0"
    and shape((0x0068102A, 0x144001C7, 0x00081023, 0x0068102A, 0x144001C4))
    == "the second test is not slt v0, bound, v0"
    and shape((0x02A9102A, 0x14400256, 0x00091023, 0x0062102A, 0x14400253))
    == "the second test is not slt v0, bound, v0"
    and shape((0x0068102A, 0x104001C7, 0x00081023, 0x0062102A, 0x144001C4)) == "a test is not followed by its branch",
    "every funnel must test one bound against a step aside and against its negation: the widened test takes the bound first",
)
require(
    '            "{} = psx_ws_horizontal_slt({}, {});"\n'
    '            "  /* aspect-scaled camera-horizontal side plane */{}",\n'
    "            reg_name(get_rd(instr)), reg_name(get_rs(instr)),\n"
    "            reg_name(get_rt(instr)), comment);"
    in codegen
    and "        if (instr != site.expected) {\n            if (config_.overlay_mode) continue;" in codegen,
    "the generator must refuse a site whose instruction is not the expected one and hand the bound to the widened test first",
)
require(
    "    disruptor_contract(widescreen_cull RETAIL)\n" in cmake, "the gate must run this contract with the retail image"
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    retail = struct.unpack_from(f"<{(len(image) - ROM_TEXT_OFFSET) // 4}I", image, ROM_TEXT_OFFSET)
    for name, (first, words) in FUNNELS.items():
        found = retail[(first - LOAD_ADDRESS) // 4 :][:5]
        require(found == words, f"retail side-plane code changed in {name}: {[f'0x{word:08X}' for word in found]}")
    for key, sites in SCREEN_BOUNDS.items():
        for address, word, _ in sites:
            require(retail[(address - LOAD_ADDRESS) // 4] == word, f"retail screen bound changed at 0x{address:08X}")
    drawing = range((0x80038000 - LOAD_ADDRESS) // 4, (0x80044000 - LOAD_ADDRESS) // 4)
    compared = sorted(
        LOAD_ADDRESS + at * 4 for at in drawing if retail[at] >> 26 == 10 and retail[at] & 0xFFFF in (300, 21, 220)
    )
    require(
        compared
        == sorted([address for sites in SCREEN_BOUNDS.values() for address, _, _ in sites] + list(VERTICAL_BOUNDS)),
        f"the drawing code compares with 300, 21 or 220 somewhere else: {[f'0x{at:08X}' for at in compared]}",
    )
    tests = [LOAD_ADDRESS + at * 4 for at in range(len(retail) - 4) if not shape(retail[at : at + 5])]
    require(
        tests == sorted([first for first, _ in FUNNELS.values()] + list(VERTICAL)),
        f"the game holds a test of this shape that is neither a listed step aside nor a known step up and down: {[f'0x{at:08X}' for at in tests]}",
    )

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    translated = "".join(path.read_text(encoding="utf-8") for path in generated)
    emitted = {
        "psx_ws_horizontal_slt": [
            (
                f"cpu->gpr[{int(word, 16) >> 11 & 31}] = psx_ws_horizontal_slt(cpu->gpr[{int(word, 16) >> 21 & 31}], cpu->gpr[{int(word, 16) >> 16 & 31}]);",
                address,
                word,
            )
            for address, word in SITES
        ],
        **{
            helper: [
                (
                    f"cpu->gpr[{word >> 16 & 31}] = {helper}(cpu->gpr[{word >> 21 & 31}], {word & 0xFFFF});",
                    f"0x{address:08X}",
                    f"0x{word:08X}",
                )
                for address, word, _ in SCREEN_BOUNDS[key]
            ]
            for helper, key in (("psx_ws_cull_slti", "slti_sites"), ("psx_ws_cull_slti_lower", "slti_lower_sites"))
        },
    }
    for helper, calls in emitted.items():
        found = re.findall(
            r"(cpu->gpr\[\d+\] = " + helper + r"\([^;\n]*\);)[^\n]*/\* (0x[0-9A-F]{8}): (0x[0-9A-F]{8}) \*/", translated
        )
        require(
            sorted(found) == sorted(calls) and translated.count(helper + "(") == len(calls),
            f"the translated code calls {helper} as {sorted(found)}, the game files and the retail instructions give {sorted(calls)}: regenerate it",
        )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("translated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the translated code: {checked}",
)
print(f"Disruptor widescreen cull source contract: PASS ({checked})")
