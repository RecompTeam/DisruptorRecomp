#!/usr/bin/env python3
"""Version and source contract for the reactor's countdown keeping the HUD message line while a list is open."""

import hashlib
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT_OFFSET = 0x800

# The countdown's level test, count and writes, the HUD builder's mode read and end, both lists' writes of the line,
# the draw's sprite, its reactor test and time, its list test and the send of its own list, the frame loop's world
# draw and message draw, then the draw's picture, its clear, the printer's width in $s0, the load, the sprite's v,
# palette, width and height, the centring, and the 3584 bytes a menu prints into the same buffer.
RETAIL_INSTRUCTIONS = {
    0x80039C10: 0x14620024,
    0x80039C18: 0x8F8202F0,
    0x80039C38: 0x24847694,
    0x80039C64: 0xAF8202CC,
    0x80039C6C: 0xAF800330,
    0x80039C70: 0xAF820334,
    0x8003D840: 0x93840310,
    0x8003DFD8: 0x24847694,
    0x8003DFF8: 0xAF8202CC,
    0x8003E004: 0xAF820330,
    0x8003E024: 0xAF820334,
    0x8003E774: 0x24847694,
    0x8003E79C: 0xAF8202CC,
    0x8003E7A4: 0xAF820330,
    0x8003E7C8: 0xAF820334,
    0x8003E8C8: 0x8FBF00DC,
    0x80042860: 0x8F910390,
    0x8004287C: 0x938303CC,
    0x8004288C: 0x8F8302CC,
    0x800428E4: 0x93820310,
    0x80043B70: 0x0C010399,
    0x80043C9C: 0x0C0109F4,
    0x800429B4: 0x0C0135A1,
    0x80042800: 0x261076D8,
    0x80042810: 0x34060B00,
    0x8004284C: 0x00408021,
    0x80042850: 0x0C0134D2,
    0x80042918: 0xA222000D,
    0x80042920: 0xA622000E,
    0x80042928: 0xA6300010,
    0x80042930: 0xA6220012,
    0x80042948: 0x00101843,
    0x80042954: 0x340200A0,
    0x8001A4AC: 0x34060E00,
}
# The draw's colour: grey, the reactor's flash, black in a list's mode. The printer the name is printed as.
RETAIL_CODE = {
    (0x8004286C, 0x80042908): "0cd0de706752bf877f3b1ebda6a6363fca4a29be222458e652cd4cd3304a1e25",
    (0x80044A10, 0x80044AE8): "b25ced31cce0a8a7da088e1ad787912c99b3d0b8ba7522fd21f9ac53235a2022",
}
HOOKED = (0x80039450, 0x800427D0, 0x8004D684, 0x8004D348)


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
rules = read("src/game/input/list_message.h")
hooks = read("src/game/input/list_message.cpp")
lists = read("src/game/input/weapon_list.cpp")
host = read("src/game/host.cpp")

require(
    '    "${CMAKE_CURRENT_SOURCE_DIR}/src/game/input/list_message.cpp"\n' in cmake
    and "    disruptor_unit_test(disruptor_list_message tests/unit/disruptor_list_message_test.cpp RUNTIME_HEADERS)\n"
    in cmake
    and "    disruptor_contract(list_message RETAIL)\n" in cmake,
    "the countdown line's source, its unit test and this contract with its input must stay registered",
)
for name in ("game.toml", "game-widescreen.toml"):
    listed = re.search(r"mod_function_entry_funcs\s*=\s*\[([^\]]*)\]", read(name))
    require(
        listed is not None and all(f'"0x{address:08X}"' in listed[1] for address in HOOKED),
        f"{name} must emit the tick, the message draw and the send of its list",
    )
require(
    "constexpr uint32_t kMessageDraw = 0x800427D0u;" in hooks
    and "constexpr uint32_t kDrawList = 0x8004D684u;" in hooks
    and "constexpr uint32_t kSentByDraw = 0x800429BCu;" in hooks
    and "    if (!cpu || cpu->gpr[31] != kSentByDraw || !wanted()) return;\n" in hooks
    and "constexpr uint32_t kTick = 0x80039450u;" in hooks
    and "constexpr uint32_t kLoadImage = 0x8004D348u;" in hooks
    and "constexpr uint32_t kLoadedByDraw = 0x80042858u;" in hooks
    and "constexpr int kPrintedRegister = 16;" in hooks
    and "    if (!cpu || cpu->gpr[31] != kLoadedByDraw || !wanted() || g_kept.name[0] == 0u) return;\n" in hooks
    and '"disruptor.list_message.loads", kLoadImage, loads' in hooks
    and "disruptor_language_words(name, words, sizeof words) ? words : name;" in hooks
    and '"disruptor.list_message.draw", kMessageDraw, draw' in hooks
    and '"disruptor.list_message.sent", kDrawList, sent' in hooks
    and '"disruptor.list_message.tick", kTick, tick_begins' in hooks
    and "bool wanted() { return !g_off && g_ls_mode == 0 && g_ls_replay_active == 0; }" in hooks
    and "    if (address == kModeSite && instruction == kModeWord) disruptor_list_message_builder_begins();\n"
    "    if (address == kHudEndSite && instruction == kHudEndWord) disruptor_list_message_builder_ends(g_forced);\n"
    in lists
    and "    disruptor_list_message_state_loaded();\n" in host
    and "disruptor_stock_frames_save_deferred() || disruptor_list_message_save_deferred();" in host,
    "the line is kept from the builder's mode read to its end, coloured as the draw sends it, outside the comparator's runs,"
    " forgotten by a loaded state, and no state is written while it is the hooks'",
)
require(
    "inline constexpr uint32_t kLevel = 0x80071518u;" in rules
    and "inline constexpr uint32_t kCountdown = 0x8007143Cu;" in rules
    and "inline constexpr uint32_t kMessageX = 0x8007147Cu;" in rules
    and "inline constexpr uint32_t kMessageY = 0x80071480u;" in rules
    and "inline constexpr uint32_t kMode = 0x8007145Cu;" in rules
    and "inline constexpr uint32_t kNextSprite = 0x800714DCu;" in rules
    and "inline constexpr uint32_t kFlashTime = 665u;" in rules
    and "inline constexpr uint32_t kFlashTicks = 30u, kFlashFrom = 25u;" in rules
    and "inline constexpr uint32_t kPicture = 0x800776D8u;" in rules
    and "inline constexpr uint32_t kFont = 0x80071398u;" in rules
    and "inline constexpr uint32_t kGlyphWidths = 0x80056E40u;" in rules
    and "inline constexpr uint32_t kPictureWide = 256u, kPictureBytes = 2816u;" in rules
    and "inline constexpr uint32_t kGlyphWide = 8u, kGlyphRows = 11u, kGlyphBytes = 88u, kSpaceWide = 4u;" in rules
    and "inline constexpr uint32_t kNameSprite = kPictureBytes;" in rules
    and "inline constexpr uint32_t kCentre = 160u;" in rules
    and "letter >= 97u ? letter - 97u : letter >= 65u ? letter - 39u : letter + 4u;" in rules,
    "the level, the count, the line's place, the mode, the draw's sprite and the flash, the picture, the font and the"
    " printer's rules are the retail ones (gp 0x8007114C)",
)

image_path = ROOT / "input" / "SLUS_002.24"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    "the build gate must check the retail image: it is absent",
)
if not image_path.exists():
    print("Disruptor list message source contract: PASS (retail image absent)")
    sys.exit(0)
image = image_path.read_bytes()
load = struct.unpack_from("<I", image, 0x18)[0]
for address, instruction in RETAIL_INSTRUCTIONS.items():
    require(
        struct.unpack_from("<I", image, TEXT_OFFSET + address - load)[0] == instruction,
        f"the retail instruction at 0x{address:08X} is not 0x{instruction:08X}",
    )
for (first, last), digest in RETAIL_CODE.items():
    require(
        hashlib.sha256(image[TEXT_OFFSET + first - load : TEXT_OFFSET + last - load]).hexdigest() == digest,
        f"the retail code from 0x{first:08X} to 0x{last:08X} is not the colour code the line was written against",
    )

print("Disruptor list message source and version contract: PASS")
