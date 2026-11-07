#!/usr/bin/env python3
"""Source contract: GPU DMA block and burst transfers reach the moved primitive arena (issue 49, lost once in v0.3.4)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


dma = (ROOT / "psxrecomp/runtime/src/dma.c").read_text(encoding="utf-8")
start = dma.index("static uint32_t execute_ch2_gpu(void) {")
channel = dma[start : dma.index("\n}\n", start)]

require('#include "gpu_dma_step.h"\n' in dma, "dma.c takes the step from the tested header")
require("DMA_RAM_ADDR" not in channel and "0x1FFFFC" not in channel, "no GPU transfer folds its address into RAM")
require(
    channel.count("const uint32_t used = psx_mod_gpu_dma_memory_bytes();\n") == 3
    and channel.count("uint32_t addr = psx_mod_gpu_dma_resolve_address_for(channels[2].madr, used);\n") == 3
    and channel.count("addr = psx_gpu_dma_next_for(addr, addr_step, used);\n") == 3,
    "VRAM reads, block and burst writes start and step through the aperture",
)
print("gpu_dma contract: ok")
