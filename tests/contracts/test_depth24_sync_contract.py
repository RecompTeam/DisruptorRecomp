#!/usr/bin/env python3
"""Source contract for a 24-bit scanout: the CPU copy of VRAM it reads and the columns it blanks."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8-sig")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def body(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start : source.index("\n}\n", start)]


def enclosing(source: str, block: str) -> list[str]:
    """The conditions open around a block of CMake, a branch after the first shown as such."""
    opened: list[str] = []
    for line in source[: source.index(block)].splitlines():
        command = line.strip()
        if command.startswith("if("):
            opened.append(command)
        elif command.startswith(("elseif(", "else(")):
            opened[-1] = f"{command} of {opened[-1]}"
        elif command.startswith("endif("):
            opened.pop()
    return opened


cmake = read("CMakeLists.txt")
gpu = read("psxrecomp/runtime/src/gpu.c")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
main = read("psxrecomp/runtime/src/main.cpp")

REGISTRATION = "    disruptor_contract(depth24_sync)\n"
require(
    cmake.count(REGISTRATION) == 1 and enclosing(cmake, REGISTRATION) == ["if(BUILD_TESTING)"],
    "this contract must stay registered as written, under the condition every contract of the repository is under",
)

mode = body(gpu, "static void gp1_display_mode(uint32_t val)")
require(
    "    const uint32_t depth = (val >> 4) & 1;\n" in mode
    and "    if (depth && !display_depth) (void)gr_vram_read(0, 0);\n    display_depth = depth;\n" in mode
    and mode.count("display_depth = ") == 1
    and mode.count("gr_vram_read(") == 1,
    "the CPU copy of VRAM must be brought up to date once, as the display goes from 15-bit to 24-bit, and before the depth changes",
)
require(
    "static uint16_t glb_vram_read(int x,int y){ ensure_cpu(); return sw_vram_read(x,y); }" in renderer
    and ".vram_write = glb_vram_write, .vram_read = glb_vram_read," in renderer,
    "reading VRAM through the GL backend must bring the CPU copy up to date first",
)
ensure = body(renderer, "static void ensure_cpu(void)")
require(
    "    if (!s_raster_ok || !s_gpu_dirty) return;\n" in ensure
    and "    flush_cpu_upload();\n    pack_flush();\n    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, s_raw_fbo);\n"
    "    glReadPixels(0, 0, VRAM_W, VRAM_H, PSXGL_RED_INTEGER, GL_UNSIGNED_SHORT, s_vram);\n"
    in ensure
    and ensure.count("glReadPixels(") == 1,
    "bringing the CPU copy up to date must first give the GPU every pending upload, or the read back would undo them, and must read the raw copy whole, as halfwords, into the CPU array",
)
policy = body(renderer, "static void depth24_upload_policy(void)")
require(
    "    int d24 = gpu_display_is_depth24();\n    if (d24 && !s_depth24_skip_up) {\n        s_up_nrects = 0;" in policy,
    "the GL backend drops pending uploads only once the display is 24-bit, which is after the sync above",
)

margin = body(main, "static void depth24_fix_trailing_margin(uint32_t *buf, uint32_t w, uint32_t h,")
require(
    "    const uint32_t margin = (uint32_t)g_video_depth24_trailing_margin;\n"
    "    if (margin == 0u || margin >= w) return;\n    uint32_t start = w - margin;\n"
    in margin
    and "8u;" not in margin,
    "a 24-bit frame must keep every column when the title sets no trailing margin, and blank only the margin it sets",
)
require(
    main.count("g_video_depth24_trailing_margin =\n                gc.runtime.video_depth24_trailing_margin;\n") == 1
    and main.count("static int           g_video_depth24_trailing_margin = 8;\n") == 1,
    "the trailing margin must come from the game's config, with the framework's 8 columns until it is read",
)

print("Disruptor 24-bit scanout source contract: PASS")
