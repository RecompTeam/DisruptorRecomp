#!/usr/bin/env python3
"""Version and source contract for the light the game sets apart for a shot of a known kind."""

import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TEXT_OFFSET = 0x800

# The shot draw: a sector's first shot into $s3, its kind, the kind's description, its stage picking the hit's frame,
# the effect funnel's projection and its two packets, and the next shot into $s3 at the loop's end.
RETAIL_INSTRUCTIONS = {
    0x8003C03C: 0x8C337BF4,
    0x8003C04C: 0x9262001A,
    0x8003C060: 0x8C236DE8,
    0x8003C554: 0x9263001D,
    0x8003C844: 0x0C013180,
    0x8003CAC8: 0x0C0130F6,
    0x8003CAF4: 0x8E730000,
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
rules = read("src/game/render/shot_lights.h")
depth = read("src/game/render/sprite_depth.cpp")
gpu = read("psxrecomp/runtime/src/gpu.c")
ddgi = read("psxrecomp/runtime/src/gpu_ddgi.c.inc")
scene = read("psxrecomp/runtime/src/gpu_ddgi_scene.c.inc")

require(
    "    disruptor_unit_test(disruptor_shot_lights tests/unit/disruptor_shot_lights_test.cpp" in cmake
    and "    disruptor_contract(shot_lights RETAIL)\n" in cmake,
    "the shot lights' unit test and this contract must stay registered",
)
require(
    "inline constexpr uint32_t kKindByte = 26u;" in rules
    and "inline constexpr uint32_t kKinds = 0x80076DE8u;" in rules
    and "inline constexpr uint32_t kNoKind = 255u;" in rules
    and "inline constexpr uint32_t kStageByte = 29u;" in rules
    and "constexpr uint8_t kShotRecordGpr = 19u;" in depth
    and "disruptor::shot_lights::gain(CpuMemory{*cpu}, cpu->gpr[kShotRecordGpr])" in depth
    and "if (gain != 1.0f) gpu_temporal_light_sprite(packet, gain);" in depth,
    "the effect funnel's packet reads the shot's kind and description where the draw holds them",
)
require(
    "if (t && t->stamp == (uint32_t)s_frame_count && gain > 0.0f) t->light = gain;" in gpu
    and "    t->light = t->ground = t->reach = 0.0f;\n" in gpu
    and "card->light = tag->light > 0.0f ? tag->light : 1.0f;" in ddgi
    and "* area * card->light;" in scene,
    "the gain goes with the sprite's tag to its card and into its lamp's power, which sets the reach too",
)

image_path = ROOT / "input" / "SLUS_002.24"
require(
    "--require-artifacts" not in sys.argv[1:] or image_path.exists(),
    "the build gate must check the retail image: it is absent",
)
if not image_path.exists():
    print("Disruptor shot lights source contract: PASS (retail image absent)")
    sys.exit(0)
image = image_path.read_bytes()
load = struct.unpack_from("<I", image, 0x18)[0]
for address, instruction in RETAIL_INSTRUCTIONS.items():
    require(
        struct.unpack_from("<I", image, TEXT_OFFSET + address - load)[0] == instruction,
        f"the retail instruction at 0x{address:08X} is not 0x{instruction:08X}",
    )

print("Disruptor shot lights source and version contract: PASS")
