#!/usr/bin/env python3
"""Version and codegen contract for Disruptor's widescreen backdrop tiles."""

import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOAD_ADDRESS = 0x80011200
ROM_TEXT_OFFSET = 0x800
BACKDROP_BEGIN = 0x8003AF68
BACKDROP_END = 0x8003B6E0
SET_SPRT_CALL = 0x0C0131F3

TILE_SITES = {
    0x8003B328: 0x8FB80040,
    0x8003B338: 0x00621821,
    0x8003B348: 0x8FB80028,
    0x8003B5A8: 0x8F8205B8,
    0x8003B638: 0x8FB80040,
}

RETAIL_INSTRUCTIONS = {
    **TILE_SITES,
    0x8003AF68: 0x27BDFF60,
    0x8003B33C: 0xAFB80038,
    0x8003B340: 0x0303C021,
    0x8003B344: 0xAFB80018,
    0x8003B350: 0x0303C021,
    0x8003B354: 0xAFB80028,
    0x8003B360: 0x2B020500,
    0x8003B514: 0x8F910390,
    0x8003B538: SET_SPRT_CALL,
    0x8003B564: 0xA6380008,
    0x8003B570: 0xA628000A,
    0x8003B57C: 0xA6370010,
    0x8003B580: 0xA63E0012,
    0x8003B5A0: 0x0C013180,
    0x8003B5A4: 0xA622000E,
    0x8003B5C8: 0x26310014,
    0x8003B640: 0xAFB80038,
}


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
source = read("src/game/render/backdrop_tiles.cpp")
gpu = read("psxrecomp-overlay/runtime/src/gpu.c")
gpu_header = read("psxrecomp-overlay/runtime/include/gpu.h")
codegen = read("psxrecomp-overlay/recompiler/src/code_generator.cpp")
interpreter = read("psxrecomp-overlay/runtime/src/dirty_ram_interp.c")
audit = read("tools/audit_codegen.py")

require(
    "src/game/render/backdrop_tiles.cpp" in cmake
    and "    disruptor_contract(backdrop_tiles RETAIL)\n" in cmake
    and "    disruptor_unit_test(disruptor_backdrop_tiles " in cmake,
    "backdrop tile source, its unit test and its contract test must remain registered",
)
require(
    "    disruptor_contract(backdrop_tiles RETAIL)\n" in cmake,
    "the build gate must run this contract with its inputs required",
)
require(
    "PSX_HAS_DISRUPTOR_BACKDROP_TILES=1" in cmake,
    "the Disruptor target must opt into the dirty-interpreter backdrop seams",
)
require(
    "+runtime/include/gpu_ws_screen_tile.h" in read("PSXRECOMP_OVERLAY_FILES.txt").splitlines(),
    "the tile header must be applied to the framework checkout",
)

for text, name in ((source, "host hook"), (codegen, "code generator"), (interpreter, "dirty interpreter"), (audit, "codegen audit")):
    upper = text.upper()
    require(
        all(f"0X{address:08X}" in upper and f"0X{word:08X}" in upper for address, word in TILE_SITES.items()),
        f"the {name} must carry the five audited seams",
    )
require(
    "disruptor_backdrop_tiles_instruction_hook(cpu, " in codegen
    and "disruptor_backdrop_tiles_instruction_hook(cpu, pc, insn, 1);" in interpreter,
    "compiled and interpreted code must reach the same host hook",
)
require(
    "gpu_ws_tag_screen_tile(cpu, cpu->gpr[kPacketRegister]);" in source and "kPacketRegister = 17;" in source,
    "tiles must be marked from the packet pointer the routine holds in $s1",
)
require("psx_mod_write" not in source, "the backdrop hooks must not write guest memory")
require("mod_plugins.h" not in source, "the backdrop hooks must not need a function-entry plugin")

require(
    "void gpu_ws_tag_screen_tile(CPUState *cpu, uint32_t primitive_addr);" in gpu_header
    and "int32_t gpu_ws_widen_x(int32_t x, int round_up);" in gpu_header,
    "the renderer must export the backdrop tile seams",
)
tagging = gpu[gpu.index("void gpu_ws_tag_screen_tile") : gpu.index("int32_t gpu_ws_widen_x")]
widening = gpu[gpu.index("int32_t gpu_ws_widen_x") : gpu.index("static int ws_screen_tile_take")]
taking = gpu[gpu.index("static int ws_screen_tile_take") : gpu.index("void psx_ws_sprite_tag")]
require(
    "if (!ws_active() || !cpu || !cpu->read_word) return;" in tagging
    and "cpu->read_word(primitive_addr + 4u + i * 4u)" in tagging
    and "    psx_ws_screen_tile_mark(&ws_screen_tiles,\n"
    "                            psx_mod_gpu_dma_resolve_address(primitive_addr),\n"
    "                            words, (uint32_t)s_frame_count);\n" in tagging,
    "tiles must be marked with their packet words, and only in classic widescreen",
)
require(
    "if (!ws_active()) return x;" in widening
    and "return psx_ws_widen_about(x, ws_disp_w() / 2, ws_xnum, ws_xden, round_up);" in widening,
    "edges must stay untouched outside classic widescreen",
)
require(
    "    return psx_ws_screen_tile_take(\n"
    "        &ws_screen_tiles,\n"
    "        psx_mod_gpu_dma_resolve_address(gp0_cmd_source_addr - 4u),\n"
    "        gp0_cmd_buf, (uint32_t)s_frame_count);\n" in taking,
    "a drawn packet must be matched by its tag address against the words it was marked with",
)
require(
    "if (ws_screen_tile_take()) {" in gpu
    and "psx_ws_squash_about(x0 + w, centre, ws_xnum, ws_xden);" in gpu
    and "x0 = psx_ws_squash_about(x0, centre, ws_xnum, ws_xden);" in gpu,
    "marked tiles must squash about the display centre through shared edges",
)

image_path = ROOT / "input" / "SLUS_002.24.code"
if image_path.exists():
    image = image_path.read_bytes()

    def word(address: int) -> int:
        return struct.unpack_from("<I", image, ROM_TEXT_OFFSET + address - LOAD_ADDRESS)[0]

    for address, instruction in RETAIL_INSTRUCTIONS.items():
        require(word(address) == instruction, f"retail backdrop identity changed at 0x{address:08X}")
    calls = [address for address in range(BACKDROP_BEGIN, BACKDROP_END, 4) if word(address) == SET_SPRT_CALL]
    require(calls == [0x8003B538], "the backdrop routine must build tiles through one SetSprt call")

generated = list((ROOT / "generated").glob("SLUS_002.24.code_full_*.c"))
if generated:
    text = "".join(path.read_text(encoding="utf-8") for path in generated)
    emitted = re.findall(
        r"disruptor_backdrop_tiles_instruction_hook\(cpu, 0x([0-9A-F]{8})u, 0x([0-9A-F]{8})u, 1\)", text
    )
    require(
        sorted((int(address, 16), int(instruction, 16)) for address, instruction in emitted) == sorted(TILE_SITES.items()),
        "generated guest code must call the hook at exactly the five audited seams",
    )

checked = ", ".join(
    f"{name} {'checked' if present else 'absent'}"
    for name, present in (("retail image", image_path.exists()), ("generated code", bool(generated)))
)
require(
    "--require-artifacts" not in sys.argv[1:] or (image_path.exists() and bool(generated)),
    f"the build gate must check the retail image and the generated code: {checked}",
)
print(f"Disruptor backdrop tile source/codegen contract: PASS ({checked})")
