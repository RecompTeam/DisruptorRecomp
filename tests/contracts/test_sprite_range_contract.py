#!/usr/bin/env python3
"""Version and source contract for the range of Disruptor's world sprites under the widescreen squash."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# Per funnel: step aside from the projection's X, three eighths added to depth, range read again for fade and sort slot.
RETAIL_INSTRUCTIONS = {
    0x8003B8B0: 0x00E09021,
    0x8003B8C4: 0x00121040,
    0x8003B8CC: 0x00521021,
    0x8003B8D0: 0x000210C3,
    0x8003B8D4: 0x02029021,
    0x8003BA3C: 0x02428823,
    0x8003BB78: 0x001220C3,
    0x8003BCAC: 0x00E09821,
    0x8003BCC8: 0x00131040,
    0x8003BCCC: 0x00531021,
    0x8003BCD0: 0x000210C3,
    0x8003BCD4: 0x02029821,
    0x8003BE4C: 0x02628823,
    0x8003BF94: 0x001320C3,
    0x8003C12C: 0xAFA20018,
    0x8003C140: 0x0040B821,
    0x8003C160: 0x02E03021,
    0x8003C164: 0x00061040,
    0x8003C16C: 0x00461021,
    0x8003C170: 0x000210C3,
    0x8003C174: 0x02A2B821,
    0x8003C634: 0x02E28823,
    0x8003C82C: 0x001720C3,
    0x8003CAE0: 0x001720C3,
    0x8003CD3C: 0x0100B821,
    0x8003CD48: 0x02E03021,
    0x8003CD5C: 0x00061040,
    0x8003CD60: 0x00461021,
    0x8003CD68: 0x000210C3,
    0x8003CD6C: 0x0282B821,
    0x8003CEC8: 0x26F7FFE8,
    0x8003CEE0: 0x2AE20008,
    0x8003CEEC: 0x34170008,
    0x8003D1B4: 0xA4379EA0,
    0x8003D294: 0x02E21023,
    0x8003D4B4: 0x001720C3,
    # The world renderer's side of it: a vertex's column from 160 picks the table entry its depth grows by.
    0x80046360: 0x216FFF60,
    0x8004637C: 0x95EF0000,
    0x800463CC: 0x01AF6820,
    # The lead: the shadow's seam and its link; the link routine; each sector's funnels in order, the deferred pass later.
    0x8003D488: 0xA2060025,
    0x8003D4BC: 0x0C013180,
    0x8004C600: 0x3C0600FF,
    0x8004C604: 0x34C6FFFF,
    0x8004C608: 0x3C07FF00,
    0x8004C60C: 0x8CA30000,
    0x8004C610: 0x8C820000,
    0x8004C614: 0x00671824,
    0x8004C618: 0x00461024,
    0x8004C61C: 0x00621825,
    0x8004C620: 0xACA30000,
    0x8004C624: 0x8C820000,
    0x8004C628: 0x00A62824,
    0x8004C62C: 0x00471024,
    0x8004C630: 0x00451025,
    0x8004C634: 0x03E00008,
    0x8004C638: 0xAC820000,
    0x800411A4: 0x0C00EDE3,
    0x800411AC: 0x0C00EEF8,
    0x800411B4: 0x0C00F000,
    0x800411BC: 0x0C00F2CE,
    0x80043FD8: 0x0C010C02,
    # The deferred pass: the least key into $s4, the cut and the fade by it, the CLUT at 8($s1), the seam, the link.
    0x80043090: 0x84239EA0,
    0x800430A4: 0x0060A021,
    0x80043144: 0x001420C3,
    0x80043184: 0x8F820494,
    0x80043190: 0x02829023,
    0x80043194: 0x2A420300,
    0x800431A0: 0x341202FF,
    0x800431A4: 0x06410002,
    0x800431AC: 0x00009021,
    0x800431C4: 0x00129183,
    0x80043314: 0x00121980,
    0x80043318: 0x00431021,
    0x8004331C: 0xA6220008,
    0x800433A0: 0x02A02821,
    0x800433A4: 0x001420C3,
    0x800433AC: 0x0C013180,
}
# Per funnel: first instruction, step-aside and range sites, projection seam, cull exit, X/depth/range registers, the leads and floors.
EFFECT_LEADS = {0x8003C68C, 0x8003C69C, 0x8003C6F8}
FUNNELS = (
    (0x8003B78C, 0x8003B8B0, 0x8003B8D4, 0x8003B98C, 0x8003BB94, 7, 16, 18, set(), {0x8003BA78}),
    (0x8003BBE0, 0x8003BCAC, 0x8003BCD4, 0x8003BDA0, 0x8003BFBC, 7, 16, 19, set(), {0x8003BE90, 0x8003BEA0}),
    (0x8003C000, 0x8003C12C, 0x8003C174, 0x8003C4B0, 0x8003CAF4, None, 21, 23, set(), EFFECT_LEADS),
    (0x8003CB38, 0x8003CD3C, 0x8003CD6C, 0x8003D078, 0x8003D4C8, 8, 20, 23, {0x8003CEC8, 0x8003CEEC}, set()),
)
LOADS = range(0x20, 0x27)
STORES = {0x28: 1, 0x29: 2, 0x2A: 4, 0x2B: 4, 0x2E: 4, 0x3A: 4}
STACK = 29
GLOBALS = 28
X_SLOT = range(0x18, 0x1C)
GUEST_WRITE = re.compile(r"cpu(?:\.|->)gpr\[[^\]]+\] [-+*/|&^]?=(?!=)")
NO_SPECIAL_TARGET = {0x08, 0x0C, 0x0D, 0x11, 0x13, 0x18, 0x19, 0x1A, 0x1B}


def written(word: int) -> int | None:
    """The general register an instruction writes, from the R3000's opcode table."""
    operation, rs, rt, rd = word >> 26, word >> 21 & 31, word >> 16 & 31, word >> 11 & 31
    if operation == 0x00:
        return None if word & 0x3F in NO_SPECIAL_TARGET else rd
    if operation == 0x01:
        return 31 if rt & 0x10 else None
    if operation == 0x03:
        return 31
    if 0x08 <= operation <= 0x0F or operation in LOADS:
        return rt
    if operation in (0x10, 0x12) and rs in (0x00, 0x02):
        return rt
    return None


def signed(half: int) -> int:
    return half - 0x10000 if half & 0x8000 else half


def target(address: int, word: int) -> int | None:
    """Where a jump or a branch goes, when the instruction itself says so."""
    operation = word >> 26
    if operation in (0x02, 0x03):
        return (address + 4) & 0xF0000000 | (word & 0x03FFFFFF) << 2
    if 0x04 <= operation <= 0x07 or (operation == 0x01 and (word >> 16 & 31) in (0x00, 0x01, 0x10, 0x11)):
        return address + 4 + (signed(word & 0xFFFF) << 2)
    return None


def calls(word: int) -> bool:
    return word >> 26 == 0x03 or (word >> 26 == 0x00 and word & 0x3F == 0x09)


def stores_x(word: int) -> bool:
    """A store into the stack word that holds the third funnel's X."""
    size = STORES.get(word >> 26)
    if size is None or word >> 21 & 31 != STACK:
        return False
    first = signed(word & 0xFFFF)
    return first < X_SLOT.stop and first + size > X_SLOT.start


def aliases_stack(word: int) -> bool:
    """An instruction that makes another register out of the stack pointer."""
    operation = word >> 26
    reads = word >> 21 & 31 == STACK or (operation == 0x00 and word >> 16 & 31 == STACK)
    return reads and operation not in STORES and operation not in LOADS and written(word) not in (None, STACK)


def faults(words: list[int]) -> list[str]:
    """What in the funnels' code would let a range be squashed that is not the game's own of that depth and step."""

    def at(address: int) -> int:
        return words[(address - LOAD_ADDRESS) // 4]

    jumps = [
        (LOAD_ADDRESS + 4 * index, goes)
        for index, word in enumerate(words)
        if (goes := target(LOAD_ADDRESS + 4 * index, word)) is not None
    ]
    found = []
    for start, taken, made, projection, leaves, x, depth, held, expected, later in FUNNELS:
        name = f"the funnel projecting at 0x{projection:08X}"
        if written(at(made)) != held:
            found.append(f"{name}: its range is not made in register {held}")
        writers = {address for address in range(made + 4, projection, 4) if written(at(address)) == held}
        if writers != expected:
            found.append(f"{name}: its range register is written at {sorted(map(hex, writers))} before the projection")
        writers = {address for address in range(projection + 4, leaves, 4) if written(at(address)) == held}
        if writers != later:
            found.append(f"{name}: its range register is written at {sorted(map(hex, writers))} after the projection")
        if any(written(at(address)) == depth for address in range(made, projection, 4)):
            found.append(f"{name}: its depth is written between the range and the projection")
        before = [at(address) for address in range(taken + 4, projection, 4)]
        if x is None and any(written(word) == STACK or stores_x(word) for word in before):
            found.append(f"{name}: the stack word of its X is stored again or the stack moves")
        if x is None and (
            any(word >> 26 in STORES and word >> 21 & 31 not in (STACK, GLOBALS) for word in before)
            or any(aliases_stack(at(address)) for address in range(start, projection, 4))
        ):
            found.append(f"{name}: a store may reach the stack word of its X through another register")
        if x is not None and any(written(word) == x or calls(word) for word in before):
            found.append(f"{name}: the register of its X is written, or a call may write it")
        leaving = {
            goes
            for address in range(made, projection, 4)
            if (goes := target(address, at(address))) is not None
            and not calls(at(address))
            and not made <= goes <= projection
        }
        if leaving != {leaves}:
            found.append(
                f"{name}: a sprite leaves for {sorted(map(hex, leaving))} between the range and the projection"
            )
        if any(projection < goes < leaves and not projection <= address < leaves for address, goes in jumps):
            found.append(f"{name}: the code after the projection is entered from elsewhere")
    return found


LINK = 0x0C013180
RANGE, KEY, CLUT_HOLDER, FADE_ROW = 23, 20, 17, 18
# The shadow's seam, its `sra a0,s7,3`, the end of the fourth funnel's loop, the loop's head, the range's next write.
SHADOW_SITE = (0x8003D488, 0x8003D4B4, 0x8003D4D4, 0x8003CB8C, 0x8003CD3C)
RECORD_KEY_OFFSET = 0x9EA0
SELECT, CUT, FADE, FADE_END, CLUT_ADD, CLUT_STORE, SEAM, PASS_LINK = (
    0x800430A4,
    0x80043144,
    0x80043190,
    0x800431C4,
    0x80043314,
    0x8004331C,
    0x800433A0,
    0x800433A4,
)


def reads(word: int) -> set[int]:
    """The general registers an instruction reads, from the R3000's opcode table."""
    operation, rs, rt = word >> 26, word >> 21 & 31, word >> 16 & 31
    if operation == 0x00:
        function = word & 0x3F
        if function in (0x00, 0x02, 0x03):
            return {rt}
        if function in (0x08, 0x09, 0x11, 0x13):
            return {rs}
        if function in (0x0C, 0x0D, 0x10, 0x12):
            return set()
        return {rs, rt}
    if operation in (0x01, 0x06, 0x07) or 0x08 <= operation <= 0x0E or operation in LOADS or operation in (0x32, 0x3A):
        return {rs}
    if operation in (0x04, 0x05) or operation in STORES:
        return {rs, rt}
    if operation == 0x12 and rs in (0x04, 0x06):
        return {rt}
    return set()


def stores_clut(word: int) -> bool:
    """A store through $s1 that reaches the CLUT halfword at 8($s1)."""
    size = STORES.get(word >> 26)
    first = signed(word & 0xFFFF)
    return size is not None and word >> 21 & 31 == CLUT_HOLDER and first < 10 and first + size > 8


def lead_faults(words: list[int]) -> list[str]:
    """What in the retail code would let a lead at a seam reach more than the slot it was meant for."""

    def at(address: int) -> int:
        return words[(address - LOAD_ADDRESS) // 4]

    found = []
    site, slot, end, head, rewritten = SHADOW_SITE
    if any(written(at(address)) == RANGE for address in range(site, slot, 4)):
        found.append("the shadow's range is written between its seam and its slot")
    if at(slot + 8) != LINK:
        found.append("the link does not follow the shadow's slot")
    if any(RANGE in reads(at(address)) for address in [*range(slot + 4, end + 4, 4), *range(head, rewritten, 4)]):
        found.append("the shadow's range is read again after its slot")
    keyed = sorted(
        LOAD_ADDRESS + 4 * index
        for index, word in enumerate(words)
        if (word >> 26 in LOADS or word >> 26 in STORES) and word & 0xFFFF == RECORD_KEY_OFFSET
    )
    if keyed != [0x8003D1B4, 0x80043090]:
        found.append(f"the record key is accessed at {list(map(hex, keyed))}")
    if any(written(at(address)) == KEY for address in range(SELECT + 4, PASS_LINK, 4)):
        found.append("the deferred pass's key register is written between its choice and its link")
    if any(written(at(address)) == FADE_ROW for address in range(FADE_END + 4, CLUT_ADD, 4)):
        found.append("the fade row is rewritten before it goes into the CLUT")
    between = [at(address) for address in range(CLUT_STORE + 4, SEAM + 4, 4)]
    if any(written(word) == CLUT_HOLDER for word in between):
        found.append("the CLUT's holder moves between its store and the seam")
    if any(stores_clut(word) for word in between):
        found.append("the CLUT is stored again before the seam")
    return found


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/game/render/sprite_depth.cpp")
gte = read("psxrecomp/runtime/src/gte.cpp")

require(
    "    disruptor_contract(sprite_range RETAIL)\n" in cmake
    and "    disruptor_unit_test(disruptor_sprite_depth " in cmake,
    "this contract with its input and the unit test of the range must stay registered",
)
require(
    "    {0x8003B98Cu, 16u, {0x8003BB88u, 0u}, {7u, 0u}, {5u, 0u}, Size::Words, 18u, false},\n"
    "    {0x8003BDA0u, 16u, {0x8003BFB0u, 0u}, {7u, 0u}, {6u, 0u}, Size::Bytes, 19u, false},\n"
    "    {0x8003C4B0u, 21u, {kEffectPacketSite, 0x8003CAD4u}, {0u, 0x18u}, {0u, 0x20u}, Size::Effect, 23u, false},\n"
    "    {kDeferredProjection, 20u, {kShadowPacketSite, 0u}, {8u, 0u}, {5u, 0u}, Size::Body, 23u, true},\n"
    in source
    and "constexpr uint32_t kDeferredProjection = 0x8003D078u;" in source
    and "constexpr int64_t kBodyLead = 0x18;" in source
    and "constexpr int64_t kNearest = 8;\n" in source,
    "each funnel must name the registers of its depth, its step aside and its range, and the fourth its lead",
)
require(
    "    const int64_t range = depth + (3 * aside >> 3);\n"
    "    return funnel.led ? std::max(range - kBodyLead, kNearest) : range;\n" in source,
    "the range must be the game's: the depth and three eighths of the step aside, the fourth funnel's led and floored",
)
require(
    "    gte_ws_x_squash(&num, &den);\n    const std::optional<int32_t> x = operand(cpu, funnel.x);\n"
    "    const auto depth = static_cast<int32_t>(cpu.gpr[funnel.depth_gpr]);\n"
    "    if (num <= 0 || den <= 0 || !x || depth <= 0) return;\n"
    in source
    and "    const int64_t aside = *x < 0 ? -static_cast<int64_t>(*x) : *x;\n"
    "    if (static_cast<int32_t>(cpu.gpr[funnel.range_gpr]) != range_of(funnel, depth, aside)) return;\n"
    "    cpu.gpr[funnel.range_gpr] = static_cast<uint32_t>(range_of(funnel, depth, aside * num / den));\n"
    in source
    and GUEST_WRITE.findall(source) == ["cpu.gpr[funnel.range_gpr] ="]
    and all(name not in source for name in ("psx_mod_write", "write_byte", "cpu.pc", "cpu->pc")),
    "only while the GTE squashes, only a register that holds the game's own range",
)
require(
    source.count("uint32_t& ") == 1
    and "    uint32_t& range = cpu.gpr[kFunnels.back().range_gpr];\n" in source
    and source.count("    range = ") == 1
    and "    range = static_cast<uint32_t>(body.walked);\n" in source
    and source.count("cpu.write_half(") == 3
    and "            cpu.write_half(first_key + kRecordBytes * static_cast<uint32_t>(index), static_cast<uint16_t>(least));\n"
    in source
    and "    if (record.led != key) cpu.write_half(at, static_cast<uint16_t>(record.led));\n" in source
    and "    cpu.write_half(at, static_cast<uint16_t>(cpu.read_half(at) + (rows << kClutRow)));\n" in source
    and source.count("cpu.write_word(") == 4
    and "    cpu.write_word(entry, (cpu.read_word(entry) & kTagWords) | rest);\n" in source
    and "        cpu.write_word(sprite.before, (before & kTagWords) | (cpu.read_word(sprite.at) & kTagNext));\n"
    in source
    and "        cpu.write_word(sprite->at, (cpu.read_word(sprite->at) & kTagWords) | (head & kTagNext));\n" in source
    and "        cpu.write_word(entry, (head & kTagWords) | sprite->node);\n" in source,
    "besides the range the guest gets only the lead: the record key, the shadow's range, the sprites moved along and the CLUT",
)
require(
    "constexpr int64_t kBodyReach = 64;\nconstexpr int64_t kHalvedBelow = kNearest + 2 * kBodyReach;\n" in source
    and "    if (range <= kNearest) return range;\n"
    "    return range >= kHalvedBelow ? range - kBodyReach : kNearest + (range - kNearest) / 2;\n"
    in source
    and "constexpr uint32_t kOrderingTable = 836u;" in source
    and "constexpr uint32_t kFadeStart = 1172u;" in source
    and "constexpr int32_t kFadeLast = 767;\nconstexpr int32_t kFadeStep = 6;\nconstexpr int32_t kClutRow = 6;"
    in source
    and "constexpr uint32_t kClutStore = 8u;" in source
    and f"constexpr uint8_t kRecordKey = {KEY}u;" in source
    and f"constexpr uint8_t kRecordClut = {CLUT_HOLDER}u;" in source
    and f"constexpr uint8_t kGlobals = {GLOBALS}u;" in source,
    "a lead that keeps the order of keys, never past the game's floor, and the deferred pass's own fade arithmetic",
)
require(
    "constexpr uint32_t kTagNext = 0x00FFFFFFu;" in source
    and "constexpr uint32_t kTagWords = 0xFF000000u;" in source
    and "constexpr int kLinksWalked = 8192;\n" in source
    and "constexpr std::size_t kMovedMost = 64;\n" in source
    and "    for (int link = 0; link < kLinksWalked; ++link) {\n" in source
    and "constexpr uint32_t kSpriteWords = 9u;" in source
    and "constexpr uint32_t kSpriteCode = 0x2Cu;\nconstexpr uint32_t kSpriteCodeMask = 0xFCu;" in source
    and "    if (tag >> 24 != kSpriteWords || (cpu.read_byte(at + 7u) & kSpriteCodeMask) != kSpriteCode) return false;\n"
    in source
    and "    return gte_precision_load_word_ex(at + 8u - kKseg0, cpu.read_word(at + 8u), &x16, &y16, &z) != GTE_PRECISION_LOOKUP_ACCEPTED;\n"
    in source
    and "        at = kKseg0 | psx_mod_gpu_dma_resolve_address(node);\n" in source
    and "        if (node == kTagNext) return std::nullopt;\n" in source
    and "        if (at == last) return Walk{.sprites = passed, .reached = to};\n" in source
    and "            if (passed.size() == kMovedMost) return std::nullopt;\n" in source
    and "    keep_order(cpu, index, at - kRecordBytes * static_cast<uint32_t>(index));\n}\n" in source
    and source.count("keep_order(") == 2
    and "    if (!box || !front) return key;\n" in source
    and "    if (!reached) return key;\n" in source
    and "        const int32_t least = record.key > behind && floor != INT32_MIN ? floor + 1 : floor;\n" in source
    and "        if (record.led < least) {\n" in source
    and "    if (nearer >> kSlotShift == own) return nearer;\n" in source
    and source.count("    g_moved.clear();\n") == 1
    and "        if (rest != sprite.node) return false;\n" in source
    and "        if (after == static_cast<int64_t>(chain.size())) return false;\n" in source,
    "the walk reads the chain as the GPU's DMA does, takes only a funnel's POLY_FT4, and moves nothing unless it reaches the new slot",
)
require(
    '#include "gte_precision.h"\n' in source
    and "constexpr double kUpright = 0.6;" in source
    and "        if (gte_precision_load_word_ex(word - kKseg0, cpu.read_word(word), &x16, &y16, &z) != GTE_PRECISION_LOOKUP_ACCEPTED"
    in source
    and "        const bool upright = std::fabs(normal[1]) < kUpright * length;\n" in source
    and "        const double nearest = over(upright ? box : upper);\n"
    "        if (upright && nearest > 0.0 && past_plane(normal, length, camera[a], middle) > 0.0) {\n"
    "            const double above = over(upper);\n"
    "            if (above > 0.0 && above < front.depth) return Cover::past;\n"
    "        }\n"
    "        if (found == Cover::none && nearest > 0.0 && nearest < front.depth - front.reach)"
    " found = upright ? Cover::front : Cover::over;\n"
    in source
    and "    return (eye * side < 0.0 ? 1.0 : -1.0) * std::fabs(side) / length;\n" in source
    and "    const Box upper{.left = box.left, .top = box.top, .right = box.right, .bottom = box.top + (box.bottom - box.top) / 2};\n"
    in source
    and "    for (const auto& point : shape) nearest = std::max(nearest, point[2]);\n" in source
    and "        } else if (const Cover cover = occludes(cpu, at, tag, box, front); cover != Cover::none) {\n"
    "            if (!may_stop) return std::nullopt;\n"
    "            if (cover == Cover::over) return Walk{.sprites = {}, .reached = from};\n"
    in source
    and "            std::erase_if(passed, [slot](const Passed& sprite) { return sprite.slot == slot; });\n" in source
    and source.count("occludes(") == 3
    and "    const std::optional<Walk> walk = passed_sprites(cpu, table, key >> kSlotShift, nearer >> kSlotShift, box, front, true);\n"
    in source
    and "passed_sprites(cpu, table, key >> kSlotShift, nearer >> kSlotShift, box, front, false, &chain);" in source,
    "an upright face that the GTE put in front of the body over its box, or over its upper half with the body's middle past "
    "its plane, holds the body in its slot, and fails the shadow's walk",
)
require(
    "constexpr int64_t kFartherSlots = (kBodyLead >> kSlotShift) + 1;" in source
    and "constexpr int64_t kLastSlot = 509;" in source
    and "        if (!behind && !world_sprite(cpu, at, tag) && occludes(cpu, at, tag, box, front) == Cover::past) behind = slot;\n"
    in source
    and "        if (const std::optional<int64_t> behind = face_behind(cpu, table, std::min(own + kFartherSlots, kLastSlot), own, *box, *front)) {\n"
    "            return static_cast<int32_t>(*behind << kSlotShift | (key & ((1 << kSlotShift) - 1)));\n"
    in source
    and "        floor = std::max(floor, std::min(record.led, record.key));\n" in source
    and "    if (body.walked < body.key && body.walked >> kSlotShift != body.key >> kSlotShift &&\n" in source,
    "a face the body lies behind that the game's own lead sorted farther takes the body back to its slot, within the "
    "table, with its shadow and without taking the bodies in front of it along",
)
require(
    "[[nodiscard]] bool comparator_active() { return g_ls_mode != 0 || g_ls_replay_active != 0; }" in source
    and "        if (!comparator_active()) lead_record(*cpu, index, g_record[index]);\n" in source
    and "        if (!comparator_active()) restore_fade(*cpu, sprite);\n" in source
    and "    if (packet_site == kShadowPacketSite && !comparator_active()) lead_shadow(*cpu, sprite, packet);\n"
    in source
    and source.count("lead_record(") == 2
    and source.count("restore_fade(") == 2
    and source.count("lead_shadow(") == 2
    and source.count("move_over(") == 2
    and source.count("join_over(") == 2,
    "the comparator's halves run the game's own order, and each part of the lead runs at its one seam",
)
require(
    "    const uint32_t at = cpu.gpr[1] + static_cast<uint32_t>(static_cast<int16_t>(kRecordWord & 0xFFFFu));\n"
    in source
    and "    if (record.led == record.key || static_cast<int32_t>(cpu.gpr[kRecordKey]) != record.led" in source
    and "    return std::clamp(key - start, 0, kFadeLast) >> kFadeStep;\n" in source
    and "    if (body.walked == body.key || static_cast<int32_t>(range) != body.key || !front || !writable(cpu)) return;\n"
    in source
    and "    if (body.walked < body.key && body.walked >> kSlotShift != body.key >> kSlotShift &&\n"
    "        !join_over(cpu, body.key, body.walked, packet_box(cpu, packet), *front)) {\n"
    in source,
    "the record key is the one the game just stored, the shadow's range is its body's, and a CLUT is put back only for "
    "the record the pass holds",
)
require(
    "        g_pending = projected(*cpu, funnel);\n        squash_range(*cpu, funnel);\n        return;\n" in source
    and source.count("squash_range(") == 2,
    "the range is brought under the squash at a funnel's projection and nowhere else",
)
require(
    "static bool ws_x_squashed() { return s_ws_xnum != s_ws_xden && !gpu_ws_present_native_43(); }\n" in gte
    and "    const bool squashed = ws_x_squashed();\n    *num = squashed ? s_ws_xnum : 1;\n    *den = squashed ? s_ws_xden : 1;\n"
    in gte
    and "    bool do_squash = ws_x_squashed();\n" in gte
    and gte.count("s_ws_xnum != s_ws_xden") == 1,
    "the ratio handed to the funnels must be the one RTPS squashes the world by, decided in one place",
)
require(
    [
        written(word)
        for word in (0x26F7FFE8, 0x34170008, 0xA4379EA0, 0x0000B012, 0x8FB7004C, 0x0C011346, 0x480D9800, 0x00E09021)
    ]
    == [23, 23, None, 22, 23, 31, 13, 18]
    and [written(word) for word in (0x01000008, 0x0062001A, 0x0411FFFF, 0x1440FFFF, 0x8AE20000, 0x9AE20000, 0x4A180001)]
    == [None, None, 31, None, 2, 2, None],
    "the scan below must know which register an instruction writes",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    retail = list(struct.unpack_from(f"<{(len(image) - ROM_TEXT_OFFSET) // 4}I", image, ROM_TEXT_OFFSET))

    def changed(address: int, word: int) -> list[int]:
        copy = list(retail)
        copy[(address - LOAD_ADDRESS) // 4] = word
        return copy

    def spare(first: int, last: int) -> int:
        """A nop of the retail code to put a seeded instruction in."""
        return next(address for address in range(first, last, 4) if retail[(address - LOAD_ADDRESS) // 4] == 0)

    def branch(address: int, goes: int) -> int:
        return 0x10000000 | (goes - address - 4) >> 2 & 0xFFFF

    for address, instruction in RETAIL_INSTRUCTIONS.items():
        require(
            retail[(address - LOAD_ADDRESS) // 4] == instruction, f"retail sprite range code changed at 0x{address:08X}"
        )
    require(not faults(retail), f"the retail funnels do not hold their operands to the projection: {faults(retail)}")
    first, second, third, fourth = ((made + 4, projection) for _, _, made, projection, *_ in FUNNELS)
    seeded = {
        "X of the first funnel lowered before its projection": (0x8003B960, 0x24E7FFFF, "register of its X is written"),
        "the first funnel's depth changed": (spare(*first), 0x26100001, "its depth is written"),
        "the first funnel's range changed": (spare(*first), 0x26520001, "before the projection"),
        "the first funnel's range overwritten before its sort": (0x8003BB74, 0x02009021, "after the projection"),
        "the second funnel's lead gone": (0x8003BE90, 0x00000000, "after the projection"),
        "a pointer made to the third funnel's stack": (spare(*third), 0x27A80018, "through another register"),
        "a pointer made to its stack before its step aside": (
            spare(*FUNNELS[2][:2]),
            0x27A80018,
            "through another register",
        ),
        "a store through a register in the third funnel": (spare(*third), 0xAD000000, "through another register"),
        "a call while the first funnel's X is in a register": (spare(*first), 0x0C01124E, "or a call may write it"),
        "a call through a register in the second funnel": (spare(*second), 0x0040F809, "or a call may write it"),
        "the third funnel's X stored again": (spare(*third), 0xAFA20018, "stored again or the stack moves"),
        "one byte of the third funnel's X stored": (spare(*third), 0xA3A2001B, "stored again or the stack moves"),
        "the third funnel's stack moved": (spare(*third), 0x27BDFFF8, "stored again or the stack moves"),
        "X of the fourth funnel changed": (spare(*fourth), 0x25080001, "register of its X is written"),
        "the fourth funnel's lead gone": (0x8003CEC8, 0x00000000, "before the projection"),
        "the fourth funnel's range made elsewhere": (0x8003CD6C, 0x0282B021, "its range is not made in register"),
        "a way from the second funnel's range past its projection": (
            spare(*second),
            branch(spare(*second), FUNNELS[1][3] + 8),
            "a sprite leaves for",
        ),
        "a way into the code after the first projection": (
            spare(FUNNELS[0][0], FUNNELS[0][1]),
            0x08000000 | (FUNNELS[0][3] + 8 & 0x0FFFFFFF) >> 2,
            "is entered from elsewhere",
        ),
    }
    for name, (address, word, complaint) in seeded.items():
        said = faults(changed(address, word))
        require(any(complaint in fault for fault in said), f"the scan must refuse {name}, it said {said}")

    require(not lead_faults(retail), f"the retail seams do not hold a lead to its slot: {lead_faults(retail)}")
    seeded_leads = {
        "the shadow's range changed after its seam": (0x8003D4B0, 0x26F70008, "between its seam and its slot"),
        "the shadow's link gone": (0x8003D4BC, 0x00000000, "the link does not follow the shadow's slot"),
        "the shadow's range read after its link": (0x8003D4C4, 0x02E01821, "read again after its slot"),
        "the body funnel's range read before it is made": (
            spare(0x8003CB8C, 0x8003CD3C),
            0x02E01821,
            "read again after its slot",
        ),
        "the record key read elsewhere": (spare(0x80043000, 0x80043400), 0x84239EA0, "the record key is accessed at"),
        "the pass's key changed before its link": (spare(SELECT + 4, PASS_LINK), 0x26940008, "key register is written"),
        "the fade row changed before the CLUT": (
            spare(FADE_END + 4, CLUT_ADD),
            0x26520001,
            "the fade row is rewritten",
        ),
        "the CLUT's holder moved": (spare(CLUT_STORE + 4, SEAM), 0x26310004, "the CLUT's holder moves"),
        "the CLUT stored again": (spare(CLUT_STORE + 4, SEAM), 0xA6220008, "the CLUT is stored again"),
        "a word over the CLUT stored": (spare(CLUT_STORE + 4, SEAM), 0xAE200006, "the CLUT is stored again"),
    }
    for name, (address, word, complaint) in seeded_leads.items():
        said = lead_faults(changed(address, word))
        require(any(complaint in fault for fault in said), f"the lead scan must refuse {name}, it said {said}")

checked = f"retail image {'checked' if image_path.exists() else 'absent'}"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    f"the build gate must check the retail image: {checked}",
)
print(f"Disruptor sprite range source contract: PASS ({checked})")
