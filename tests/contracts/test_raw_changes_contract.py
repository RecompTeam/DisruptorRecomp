#!/usr/bin/env python3
"""Source contract for an in-between scene's texture snapshots: every change of the raw picture is noted with its place."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
snapshot = presenter[presenter.index("static int temporal_texture(TemporalScene *s) {") :]
snapshot = snapshot[: snapshot.index("\n}\n")]

require(
    "+runtime/include/gpu_raw_changes.h" in read("framework/FILES.txt").splitlines()
    and "static PsxRawChanges s_temporal_raw;\n" in renderer
    and "static void temporal_note_raw_change(int x, int y, int w, int h) {\n"
    "    const int x1 = x + w < VRAM_W ? x + w : VRAM_W, y1 = y + h < VRAM_H ? y + h : VRAM_H;\n    if (x < 0) x = 0;\n    if (y < 0) y = 0;\n"
    "    if (x < x1 && y < y1) psx_raw_changes_note(&s_temporal_raw, x, y, x1 - x, y1 - y);\n}\n"
    in renderer
    and "#define temporal_note_raw_begin() psx_raw_changes_begin(&s_temporal_raw)\n" in renderer,
    "the renderer keeps the raw picture's changes in the shared table, each place cut to the picture",
)
require(
    renderer.count("upload_rect(s_raw_tex,") == 1
    and renderer.count("p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_raw_fbo);") == 2
    and renderer.count("glBindTexture(GL_TEXTURE_2D, s_raw_tex);\n        glTexSubImage2D(") == 1
    and renderer.count("temporal_note_raw_change(") == 4
    and renderer.count("temporal_note_raw_begin(") == 2
    and "((size_t)(h - 1) * VRAM_W + (size_t)w) * sizeof(uint16_t), VRAM_W);\n        temporal_note_raw_change(x, y, w, h);\n"
    in renderer
    and "    coh_record(GL_COH_PACK, x, y, x + w - 1, y + h - 1);\n    temporal_note_raw_change(x, y, w, h);\n"
    in renderer
    and "    if (s_raw_fbo) {\n        temporal_note_raw_change(x0, y0, rw, rh);\n"
    "        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_raw_fbo);\n        glViewport(0, 0, VRAM_W, VRAM_H);\n"
    "        glScissor(x0, y0, rw, rh);\n"
    in renderer
    and "    s_raw_tex     = make_tex(PSXGL_R16UI, VRAM_W, VRAM_H, PSXGL_RED_INTEGER, GL_UNSIGNED_SHORT);\n"
    "    temporal_note_raw_begin();\n"
    in renderer,
    "the raw picture has three writers and its creation, and each notes the change with the place it writes",
)
require(
    "#define TEMP_MAX_TEXTURES 128 " in presenter
    and "        if (s->texture_generation[i] == s_temporal_raw.generation) return i;\n" in snapshot
    and "    if (s->texture_count == TEMP_MAX_TEXTURES) { s->bad = 3; return -1; }\n" in snapshot
    and "    int count = s->textures[i]\n"
    "                    ? psx_raw_changes_since(&s_temporal_raw, s->texture_generation[i], (uint64_t)VRAM_W * VRAM_H, rects)\n"
    "                    : -1;\n"
    in snapshot
    and "    if (count < 0) {\n        rects[0].x = rects[0].y = 0; rects[0].w = VRAM_W; rects[0].h = VRAM_H;\n        count = 1;\n    }\n"
    in snapshot
    and "            p_glCopyImageSubData(s_raw_tex, GL_TEXTURE_2D, 0, rects[r].x, rects[r].y, 0, s->textures[i], GL_TEXTURE_2D, 0,\n"
    "                                 rects[r].x, rects[r].y, 0, rects[r].w, rects[r].h, 1);\n"
    in snapshot
    and "            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, rects[r].x, rects[r].y, rects[r].x, rects[r].y, rects[r].w, rects[r].h);\n"
    in snapshot
    and snapshot.rstrip().endswith("    s->texture_generation[i] = s_temporal_raw.generation;\n    return i;")
    and presenter.count("texture_generation[") == 4,
    "a snapshot takes the whole raw picture when new and the changed places when kept, and holds the generation it was made at",
)
require(
    "    if (s->texture_count > TEMP_KEPT_TEXTURES) s->texture_quiet = 0;\n"
    "    else if (s->texture_quiet < TEMP_QUIET_USES && ++s->texture_quiet == TEMP_QUIET_USES) {\n"
    "        glDeleteTextures(TEMP_MAX_TEXTURES - TEMP_KEPT_TEXTURES, s->textures + TEMP_KEPT_TEXTURES);\n"
    "        memset(s->textures + TEMP_KEPT_TEXTURES, 0, (TEMP_MAX_TEXTURES - TEMP_KEPT_TEXTURES) * sizeof(s->textures[0]));\n"
    "    }\n    s->texture_count = 0;"
    in presenter
    and "        if (context_current) glDeleteTextures(TEMP_MAX_TEXTURES,s->textures);\n        memset(s,0,sizeof(*s));\n"
    in presenter,
    "a scene gives back the snapshots past the first sixteen once it has not needed them for a while",
)

for name in ("gl_renderer_fbo_peek", "gl_renderer_vram_diff"):
    operation = renderer[renderer.index(f"int {name}(") :].split("\n}\n", 1)[0]
    require(
        "    flush_flat_batch();\n    flush_tex_batch();\n    flush_cpu_upload();\n" in operation,
        f"{name} reads completed flat and textured draws even without a pending CPU upload",
    )

print("Disruptor raw picture changes source contract: PASS")
