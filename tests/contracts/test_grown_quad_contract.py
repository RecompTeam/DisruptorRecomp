#!/usr/bin/env python3
"""Version and source contract for the provenance of Disruptor's grown world quads."""

import hashlib
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800

# Flag test, second-packet step, one load and shift, first and last position stores.
RETAIL_INSTRUCTIONS = {
    0x80046D90: 0x30E20001,
    0x80046D94: 0x1C400003,
    0x80046DA4: 0x23390034,
    0x80046DD8: 0x8F2CFFF8,
    0x80046E28: 0x00108103,
    0x80046EEC: 0xAF2C0008,
    0x80046EF8: 0xAF28002C,
    0x80046F20: 0x23390034,
}
EMITTER = (0x80046D90, 0x80046F24)
EMITTER_SHA256 = "79dd22a279ebec12812036cac7d840bdbad48079d7d07f011b151b1c157a49cb"


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/game/render/grown_quad.cpp")
gpu = read("psxrecomp/runtime/src/gpu.c")
host = read("src/game/host.cpp")

require(
    '"${CMAKE_CURRENT_SOURCE_DIR}/src/game/render/grown_quad.cpp"' in cmake
    and "    PSX_HAS_GAME_HOST=1)\n" in cmake
    and "    return disruptor_grown_quad_sources(command_address, packet, source_address, source_word);\n" in host
    and "    disruptor_unit_test(disruptor_grown_quad " in cmake
    and "    disruptor_contract(grown_quad RETAIL)\n" in cmake,
    "the grown quad source, its renderer switch, its unit test and this contract with its input must stay registered",
)
require(
    "constexpr uint32_t kPacketBytes = 0x34u;" in source
    and "constexpr std::array<uint32_t, 4> kPositions{1u, 4u, 7u, 10u};" in source
    and "if (grown[corner] != packet[kPositions[corner]]) return 0;" in source,
    "a packet may borrow corners only when all four of its positions are the arithmetic of the packet before it",
)
require(
    "psx_mod_write" not in source and "write_word" not in source,
    "recognising a grown quad must not write guest memory",
)
require(
    "#ifdef PSX_HAS_GAME_HOST\n"
    "    /* The game's second, grown copy of a world quad stands on the first packet's corners. */\n"
    "    const int grown = count == 4 && psx_game_grown_quad_sources(\n"
    "        gp0_cmd_source_addr, gp0_cmd_buf, grown_addr, grown_word);\n"
    "#else\n"
    "    const int grown = 0;\n"
    "#endif\n"
    in gpu
    and "#if defined(PSX_HAS_GAME_HOST) && !defined(PSX_DISABLE_FRAME_INTERPOLATION)\n"
    "    if (grown) {\n"
    "        grown_corner_depths(precise_x, precise_y, raw_x, raw_y, indices);\n"
    in gpu
    and "#if defined(PSX_HAS_GAME_HOST) && !defined(PSX_DISABLE_FRAME_INTERPOLATION)\n"
    "/* A grown corner lies off its source corner, on the same plane"
    in gpu
    and "disruptor" not in gpu.lower()
    and "        const uint32_t word = grown ? grown_word[i] : gp0_cmd_buf[indices[i]];\n"
    "        uint32_t addr = grown_addr[i];\n"
    in gpu
    and "        if (!grown && !gp0_source_word_address(indices[i], &addr)) {\n" in gpu,
    "a grown quad must resolve each corner through the address and the word of the first packet's corner",
)
require(
    "            if (!grown && gp0_source_word_address(indices[i], &identity_addr))\n" in gpu,
    "a grown corner must not take the model identity of the corner it was pushed away from",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()
    for address, instruction in RETAIL_INSTRUCTIONS.items():
        offset = ROM_TEXT_OFFSET + address - LOAD_ADDRESS
        require(
            struct.unpack_from("<I", image, offset)[0] == instruction,
            f"retail grown-quad emitter changed at 0x{address:08X}",
        )
    block = image[ROM_TEXT_OFFSET + EMITTER[0] - LOAD_ADDRESS : ROM_TEXT_OFFSET + EMITTER[1] - LOAD_ADDRESS]
    require(
        hashlib.sha256(block).hexdigest() == EMITTER_SHA256,
        "the arithmetic of the retail grown-quad emitter is not the one the source reproduces",
    )

checked = f"retail image {'checked' if image_path.exists() else 'absent'}"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    f"the build gate must check the retail image: {checked}",
)
print(f"Disruptor grown quad source contract: PASS ({checked})")
