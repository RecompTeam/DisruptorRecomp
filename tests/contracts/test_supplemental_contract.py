#!/usr/bin/env python3
"""Source contract for polygons only the widened portal walk admits: how the renderer keeps them to what nothing drew."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def between(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    return source[start : source.index(closing, start)]


gpu = read("psxrecomp/runtime/src/gpu.c")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
module = read("src/game/render/far_rendering.cpp")

require(
    "    if (s_precise_grown) gr_set_world_triangle(2);  /* the game's grown second copy */\n"
    "    else if (s_piece_outside >= 0 ? s_piece_outside : gp0_supplemental()) gr_set_world_triangle(s_piece_outside > 0 ? 4 : 3);  /* admitted only by the widened walk */\n"
    in gpu
    and "    const uint32_t key = psx_mod_gpu_dma_resolve_address(gp0_cmd_source_addr - 4u);\n"
    "    return key ? psx_supplemental_find(&supplemental, key) : NULL;\n"
    in gpu
    and "    const PsxSupplementalRange *range = gp0_supplemental_range();\n    return range && range->left > range->right;\n"
    in gpu
    and gpu.count("    psx_supplemental_reset(&supplemental);\n") == 2
    and "    s_took_supplemental = !exact ? 0 : s_next_world == 3 ? 1 : s_next_world == 4 ? 2 : 0;\n" in renderer,
    "an exact polygon from a packet of the admitted table is flagged for the renderer, a grown copy stays a copy, and a "
    "reset or a loaded state forgets the table",
)

require(
    "        const int supp = near_face || (s_zbuffer_oracle && s_took_supplemental == 2) ? 0 : s_took_supplemental;\n"
    in renderer
    and "        if (s_tb_n > 0 && (deferred ? s_tb_supp != 0 : supp != s_tb_supp)) flush_tex_batch();\n" in renderer
    and "            s_tb_supp = deferred ? 0 : supp;\n" in renderer,
    "such a polygon and its kind are a key of its batch, so a batch holds one kind alone",
)
deferred = between(renderer, "static void flush_tex_batch(void) {", "\n}\n")
require(
    "        const int deferred = supp != 0 && mirror_depth();" in renderer
    and deferred.index("    flush_tex_batch_now();\n    if (s_tp_n == 0) return;\n")
    < deferred.index("    for (int kind = 2; kind >= 1 + s_zbuffer_oracle; --kind) {")
    < deferred.index("            memcpy(&s_tb[n * TEXV], &s_tp[at * TEXV], 3 * TEXV * sizeof(float));\n")
    < deferred.index(
        "        s_tb_supp = kind;\n        s_tb_held_back = 1;\n        flush_tex_batch_now();\n        s_tb_held_back = 0;\n"
    )
    and "    if (!s_tb_held_back) tex_batch_draw_passes(nverts, semi);" in renderer
    and "    glDepthFunc(held_back ? GL_LESS : GL_LEQUAL);\n" in renderer
    and "    p_glUniform1f(s_tex_uPull, held_back ? 0.0f : s_world_margin);\n    if (held_back) glDepthFunc(GL_EQUAL);\n"
    in renderer
    and "        glDepthFunc(c->supp?GL_LESS:GL_LEQUAL); glDepthMask(GL_TRUE);\n" in presenter
    and "        if (c->supp) glDepthFunc(GL_EQUAL);\n" in presenter
    and "    s_won_stencil = held_back;\n    tex_batch_draw_passes(nverts, semi);\n    s_won_stencil = 0;\n" in renderer
    and "        glStencilFunc(GL_EQUAL, SUPPLEMENTAL_STENCIL | HELD_BACK_WON_STENCIL, SUPPLEMENTAL_STENCIL);\n"
    in renderer
    and "    const GLint within = s_supp_stencil ? SUPPLEMENTAL_STENCIL | (s_won_stencil ? HELD_BACK_WON_STENCIL : 0) : 0;\n"
    in renderer
    and "    const GLint within=s_temp_supp ? SUPPLEMENTAL_STENCIL|(s_temp_won ? HELD_BACK_WON_STENCIL : 0) : 0;\n"
    in presenter
    and "glStencilFunc(GL_EQUAL,SUPPLEMENTAL_STENCIL|HELD_BACK_WON_STENCIL,SUPPLEMENTAL_STENCIL); glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);"
    in presenter
    and "        if (c->supp) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(1.0f,1.0f); s_temp_won=s_temp_supp; }\n"
    in presenter
    and "        if (deferred) vp = &s_tp[s_tp_n * TEXV];\n" in renderer
    and "                vp[0] = whole[0];\n                vp[1] = whole[1];\n                vp[20] = px[i];\n                vp[21] = py[i];\n"
    in renderer
    and "    const int by_depth=(s_world_depth || c->supp) && c->world && " in presenter,
    "a face of a supplemental kind waits for its batch's end and is drawn there, pieces past their span first, strictly nearer than the batch in the mirror and the in-between pictures, its canonical part left in order as one point in the mirror",
)

mark = between(renderer, "static void supplemental_mark(int nverts, int semi, int set, int kind) {", "\n}\n")
require(
    "    s_fill_pass = set && kind == 2 ? PAST_SPAN_COPY_PASS : SUPPLEMENTAL_MARK_PASS;\n" in mark
    and "    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);\n" in mark
    and "    glStencilMask(SUPPLEMENTAL_STENCIL | (set ? 0 : HELD_BACK_WON_STENCIL));\n" in mark
    and "    glStencilFunc(GL_ALWAYS, set ? SUPPLEMENTAL_STENCIL : 0, SUPPLEMENTAL_STENCIL);\n" in mark
    and "    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);\n" in mark
    and "    glDepthMask(GL_FALSE);\n" in mark
    and "    glDepthFunc(set ? GL_LESS : GL_ALWAYS);\n" in mark
    and "#define SUPPLEMENTAL_STENCIL 0x04\n#define HELD_BACK_WON_STENCIL 0x08  /* a held-back face's prepass won the sample */\n#define SUPPLEMENTAL_MARK_PASS -2.0f\n"
    in renderer
    and "(u_visual_options.w > 0.0 || u_visual_options.w < -1.5) ? 0.9999995" in renderer
    and "((u_visual_options.w > 0.0 || u_visual_options.w < -1.5) ? 0.9999995" in presenter,
    "for a face only the widened walk admits the mark sets stencil bit 2 where the batch's polygons pass a depth test at the "
    "grown copy's depth, which only the cleared depth lets through, in both shaders, and taking it back sets the bit to 0 "
    "wherever they lie; a piece past its span marks more, see below",
)

flush = between(renderer, "static void flush_tex_batch_now(void) {", "\n}\n")
require(
    "    const int supp = mirror_depth() ? s_tb_supp : 0;\n" in flush
    and flush.index("        s_draw_supp = supp;\n")
    < flush.index("        temporal_draw(s_tb, s_tb_identity, nverts, 1, GL_TRIANGLES, semi, s_tb_mask,")
    < flush.index("        if (supp) supplemental_mark(nverts, semi, 1, supp);\n        s_supp_stencil = supp;\n")
    < flush.index("            if (mirror_depth() && s_crack_fill > 0.0f) tex_batch_outline(nverts, semi);")
    < flush.index("        s_supp_stencil = 0;\n        if (supp) supplemental_mark(nverts, semi, 0, supp);\n")
    and flush.index("        gz_end(GZ_MIRROR_DEPTH);\n") < flush.index("        s_supp_stencil = 0;\n"),
    "the batch is recorded as such for the in-between frames, then marked before its outline, copies, colours and depth, "
    "and the mark is taken back after them",
)

require(
    "    const GLint within = s_supp_stencil ? SUPPLEMENTAL_STENCIL | (s_won_stencil ? HELD_BACK_WON_STENCIL : 0) : 0;\n"
    in renderer
    and "        glStencilFunc(GL_EQUAL, within, (s_mask_check ? 0x01 : 0) | within);\n" in renderer
    and "        glStencilFunc(GL_EQUAL, within, 0x01 | within);\n" in renderer
    and "        glStencilFunc(within ? GL_EQUAL : GL_ALWAYS, (write_val ? 1 : 0) | within, within ? within : 0x01);\n"
    in renderer
    and "        if (!s_mask_check && !s_tb_mask && s_fill_pass <= 0.0f && !s_supp_stencil) glDisable(GL_STENCIL_TEST);\n"
    in renderer
    and "        if (s_tb_mask || s_supp_stencil) mask_stencil(s_tb_mask); else glDisable(GL_STENCIL_TEST);\n"
    in renderer
    and renderer.count("\n    depth_stencil_within();\n") == 1
    and flush.count("            depth_stencil_within();\n") == 1
    and "    glStencilFunc(GL_EQUAL, SUPPLEMENTAL_STENCIL, SUPPLEMENTAL_STENCIL);\n" in renderer,
    "while the mark stands every colour and depth draw of the batch passes only where it is set, the mask bit kept as before",
)

submit = presenter[presenter.rindex("static void temporal_submit_faces(") :]
submit = submit[: submit.index("\n}\n") + 1]
presenter_mark = between(presenter, "static void temporal_supp_mark(", "\n}\n")
require(
    "    c->supp=textured ? s_draw_supp : 0;\n" in presenter
    and "    const GLint within=s_temp_supp ? SUPPLEMENTAL_STENCIL|(s_temp_won ? HELD_BACK_WON_STENCIL : 0) : 0;\n"
    in presenter
    and "    p_glUniform4f(u->options,0,0,s->tint,set && c->supp==2 ? PAST_SPAN_COPY_PASS : SUPPLEMENTAL_MARK_PASS);\n"
    in presenter_mark
    and "    glDepthFunc(set?GL_LESS:GL_ALWAYS); glDepthMask(GL_FALSE);\n" in presenter_mark
    and "glDepthFunc(depth_after); glDepthMask(write_after);" in presenter_mark
    and "    p_glUniform4f(u->options,0,0,s->tint,s_temp_underlaying ? -1.0f : 0.0f);\n" in presenter_mark
    and submit.index("    if (supp) { temporal_supp_mark(s,c,u,count,1,depth_after,write_after); s_temp_supp=1; }\n")
    < submit.index("    if (fill && s_crack_fill>0.0f) {")
    < submit.index("    if (supp) { s_temp_supp=0; temporal_supp_mark(s,c,u,count,0,depth_after,write_after); }\n")
    and "        } else if (s_temp_supp) { glEnable(GL_STENCIL_TEST); glStencilMask(0); glStencilFunc(GL_EQUAL,SUPPLEMENTAL_STENCIL,SUPPLEMENTAL_STENCIL); }\n"
    in submit,
    "an in-between frame draws such a command the same way: marked, kept within the mark, the mark taken back",
)

require(
    "    const int supp=s_temp_marked && c->mode==GL_TRIANGLES && (s_temp_scene_depth || s_temp_underlaying) ? c->supp : 0;\n"
    in submit
    and "    const GLenum depth_after=s_temp_underlaying && !s_temp_scene_depth ? GL_LESS : GL_ALWAYS;\n" in submit
    and "    const GLboolean write_after=!s_temp_underlaying || (s_temp_scene_depth && !s_world_depth);\n" in submit
    and "    glDepthFunc(covered ? GL_LESS : GL_ALWAYS); glDepthMask(covered ? GL_FALSE : GL_TRUE);\n" in presenter
    and "    s_temp_scene_depth=!covered; s_temp_underlaying=1;\n" in presenter
    and "    if (s_world_depth && !covered) glDepthMask(GL_FALSE);" in presenter,
    "a face of the frame before that the in-between frame carries under or over the scene keeps to the mark too, so it "
    "never covers the sky drawn before it, and the carrying pass gets its own depth rule back after the mark",
)

require(
    "    if (g_face_rule.whole || disruptor::portal_reach::beside_span(screen, g_face_rule.left, g_face_rule.right)) {\n"
    "        g_admitted_from = packet;\n        g_admitted_band = disruptor::portal_reach::kWholeFace;\n"
    in module
    and "        keep_marked_faces_on_screen(cpu);\n        note_face(cpu);\n" in module
    and "    note_room(cpu);\n" in module
    and "    gpu_supplemental_frame();\n" in module,
    "the game's module tells the renderer the packets of faces its own walk would drop, after the marked-corner rule",
)

pieces = between(
    gpu,
    "static int band_pieces(const int32_t *vx, const int32_t *vy, int a, int b, int c, PsxCoveragePiece pieces[PSX_COVERAGE_PIECES]) {",
    "\n}\n",
)
prepass = between(gpu, "static void coverage_prepass(uint32_t start_addr) {", "\n}\n")
require(
    "    const PsxSupplementalRange *range = gte_geometry_correction_enabled() && gr_has_column_band() ? gp0_supplemental_range() : NULL;\n"
    "    if (!range || range->left > range->right) return 0;\n"
    in pieces
    and "int gr_has_column_band(void) { return g_b->set_column_band != NULL; }\n"
    in read("psxrecomp/runtime/src/gpu_render.c")
    and "    return psx_coverage_pieces(&coverage, least, most, top, bottom, left, right, pieces);" in pieces
    and "void gpu_ws_prepass_linked_list(uint32_t start_addr) {\n    coverage_list = start_addr;\n" in gpu
    and "    coverage_list_unread = 1;\n    if (coverage_dump_folder()) coverage_read_list();\n" in gpu
    and prepass.endswith("    coverage_current = 1;")
    and "    coverage_read_list();\n    if (!coverage_current) return 0;\n    psx_coverage_build(&coverage);\n    return psx_coverage_pieces("
    in pieces
    and "const int sky = offset == 0 && (psx_sky_fog_opening_top(&sky_fog, addr) != INT32_MAX ||" in prepass
    and "range && range->left > range->right ? PSX_COVER_REACH : PSX_COVER_WORLD" in prepass
    and prepass.count("!(op & 0x02u)") == 2
    and "    for (int kind = 2; kind >= 1 + s_zbuffer_oracle; --kind) {" in renderer
    and "    if (s_zbuffer_oracle && c->supp==1) return;  /* the z-buffer view shows the game's own faces alone */\n"
    in presenter
    and gpu.count("band_pieces(vx, vy, ") == 6
    and "reach_face_hidden" not in gpu
    and "psx_coverage_sky_keys_has(&coverage_sky_drawn, addr)" in prepass
    and "    psx_coverage_sky_keys_add(&coverage_sky_drawn, key);\n" in gpu
    and gpu.count("psx_coverage_sky_keys_reset(&coverage_sky_drawn);\n") == 3
    and gpu.count(
        "    coverage_new_buffer = 1;\n    coverage_current = 0;\n    coverage_list_unread = 0;\n    psx_coverage_sky_keys_reset(&coverage_sky_drawn);\n    psx_sky_fog_reset(&sky_fog);\n"
    )
    == 2
    and "    if (draw_offset_x != was_x || draw_offset_y != was_y) coverage_end_buffer();\n" in gpu
    and "sky_cover" not in gpu
    and "+runtime/include/gpu_coverage.h\n" in read("framework/FILES.txt")
    and "disruptor_unit_test(disruptor_gpu_coverage tests/unit/gpu_coverage_test.cpp RUNTIME_HEADERS)\n"
    in read("CMakeLists.txt")
    and "    if (count > 1) gr_set_column_band(pieces[piece].low, pieces[piece].high, piece == 0);\n" in gpu
    and gpu.count("        for (int piece = 0; piece < (pieces > 1 ? pieces : 1); ++piece) {\n") == 4
    and gpu.count("    for (int piece = 0; piece < (pieces > 1 ? pieces : 1); ++piece) {\n") == 6
    and gpu.count("band_piece(band, pieces, piece);\n") == 6
    and gpu.count("s_piece_outside = -1;\n") == 6
    and "    const int banded = s_next_band, canonical = !banded || s_next_band_canonical;\n    s_next_band = 0;\n"
    in renderer
    and "            vp[0] = (float)xs[canonical ? i : 0];  /* a later piece adds nothing to the game's picture */\n"
    in renderer
    and "    case RTH_COLUMN_BAND: glb_set_column_band(v[0], v[1], v[2]); break;\n"
    in read("psxrecomp/runtime/src/gpu_gl_render_thread.c.inc")
    and "    } else if (disruptor::portal_reach::crossed_band(screen, g_face_rule.left, g_face_rule.right, g_admitted_band)) {\n"
    in module
    and "            gpu_supplemental_band(g_admitted_from, end, g_admitted_band.left, g_admitted_band.right);\n"
    in module,
    "a textured face of the game's room over an inner edge of its own span is drawn whole unless the game's whole picture, "
    "read from the display list before it runs, shows a tick of it over the sky; then that tick is cut away, the first "
    "piece alone in the game's picture",
)
require(
    "#define PAST_SPAN_MARK_PASS -3.0f" in renderer
    and "    if (set && kind == 2) {\n        s_fill_pass = PAST_SPAN_MARK_PASS;\n" in renderer
    and "        glDepthFunc(GL_GREATER);\n        glDrawArrays(GL_TRIANGLES, 0, nverts);\n    }\n" in renderer
    and renderer.count("supplemental_mark(nverts, semi, ") == 2
    and "    const int supp = mirror_depth() ? s_tb_supp : 0;\n" in renderer
    and renderer.count(
        "u_visual_options.w < -3.5 ? 0.99999925 : u_visual_options.w < -2.5 || u_visual_options.w >= 1.0 ? 0.99999 : "
    )
    == 1
    and presenter.count(
        "u_visual_options.w < -3.5 ? 0.99999925 : u_visual_options.w < -2.5 || u_visual_options.w >= 1.0 ? 0.99999 : "
    )
    == 1
    and "    s_fill_pass = set && kind == 2 ? PAST_SPAN_COPY_PASS : SUPPLEMENTAL_MARK_PASS;\n" in renderer
    and "    p_glUniform4f(u->options,0,0,s->tint,set && c->supp==2 ? PAST_SPAN_COPY_PASS : SUPPLEMENTAL_MARK_PASS);\n"
    in presenter
    and "    c->supp=textured ? s_draw_supp : 0;\n" in presenter
    and "    if (set && c->supp==2) { p_glUniform4f(u->options,0,0,s->tint,PAST_SPAN_MARK_PASS); glDepthFunc(GL_GREATER);"
    in presenter,
    "a piece past its span draws as the game's own face where the world, a copy or nothing was drawn, never over the sky, "
    "in the frame and in every in-between picture",
)

print("Disruptor supplemental polygons source contract: PASS")
