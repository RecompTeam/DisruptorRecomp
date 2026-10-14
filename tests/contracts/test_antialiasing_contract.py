#!/usr/bin/env python3
"""Source contract for the experimental anti-aliasing: multisampling of the mirror, the two filters of the shown picture, the HUD's mark."""

import re
from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


cmake = read("CMakeLists.txt")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
gpu = read("psxrecomp/runtime/src/gpu.c")
menu = menu_text(ROOT)
experimental = read("src/game/ui/menu/tab_experimental.cpp")
options = read("src/game/settings/options.cpp")
saved = read("src/game/settings/saved.cpp")
fields = read("src/game/settings/game_user_settings.h")
launcher = read("src/launcher/launcher_settings.cpp")

require(
    "static int s_aa_average = 0, s_aa_edges = 0;\n" in renderer
    and "    const int grown = s_aa_average && rows > lh ? 2 : 1;\n"
    "    const int on = !s_uv_view && (grown > 1 || (s_aa_edges && edges)) && s_aa_prog && lw > 0 && lh > 0;\n"
    "    if (t->tex && (!on || t->w != lw * grown || t->h != lh * grown)) {\n" in renderer
    and "        t->w = lw * grown;\n        t->h = lh * grown;\n        p_glActiveTexture(PSXGL_TEXTURE0);  /* the unit every draw binds for itself */\n"
    "        t->tex = make_tex(GL_RGBA8, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE);\n"
    "        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);\n"
    "        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);\n" in renderer
    and "    if (on && t->fbo) {\n        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, t->fbo);\n        glViewport(0, 0, t->w, t->h);\n        return grown;\n    }\n"
    "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);\n    glViewport(lx, ly, lw, lh);\n    return 0;\n}\n" in renderer,
    "both filters start off and without them a picture's last draw goes to the window as before: with one the draw goes to a "
    "target, twice the picture's size on screen where a finer source is averaged down and the picture's size where only its "
    "edges are smoothed, read through the linear filter",
)
require(
    "        if (!make_fbo(&t->fbo, t->tex, 0)) {\n"
    '            fprintf(stdout, "psxrecomp: GL anti-aliasing has no target, switched off\\n");\n'
    "            p_glDeleteFramebuffers(1, &t->fbo);\n            glDeleteTextures(1, &t->tex);\n            t->fbo = t->tex = 0;\n"
    "            t->w = t->h = 0;\n            s_aa_average = s_aa_edges = 0;\n        }\n" in renderer
    and "        if (t->fbo) p_glDeleteFramebuffers(1, &t->fbo);\n        glDeleteTextures(1, &t->tex);\n        t->fbo = t->tex = 0;\n" in renderer
    and "static AaTarget s_aa_target[2];\n" in renderer
    and "    AaTarget *t = &s_aa_target[thread];\n" in renderer,
    "a target that cannot be made is dropped whole and switches both filters off, so that switching one on again tries again: "
    "one of another size or left unused is freed, and each of the two GL contexts has its own",
)
require(
    "static void aa_end(int thread, GLuint vao, int grown, int edges, int lx, int ly, int lw, int lh) {\n"
    "    if (!grown) return;\n"
    "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);\n"
    "    glViewport(lx, ly, lw, lh);\n"
    "    p_glActiveTexture(PSXGL_TEXTURE0);\n"
    "    glBindTexture(GL_TEXTURE_2D, s_aa_target[thread].tex);\n"
    "    p_glUseProgram(s_aa_prog);\n" in renderer
    and "    p_glUniform4f(s_aa_uUvRect, 0.0f, 1.0f, 1.0f, 0.0f);" in renderer
    and "    p_glUniform4f(s_aa_uPx, 1.0f / (float)lw, 1.0f / (float)lh, s_aa_edges && edges ? 1.0f : 0.0f, 0.0f);\n" in renderer
    and "        s_aa_prog = build_program(PRESENT_VS, AA_FS);\n" in renderer,
    "the filter draws the target into the picture's place in the window, the right way up, a screen pixel a step, and smooths "
    "edges only where that filter is on and the picture carries the mirror's marks",
)
require(
    '    "bool flat_picture(float mark){ return abs(mark - 0.75) < 0.02 || abs(mark - 0.95) < 0.02; }\\n"\n' in renderer
    and '    "  vec4 here = texture(u_tex, v_uv);\\n"\n'
    '    "  frag = vec4(here.rgb, 1.0);\\n"\n'
    '    "  if (u_px.z < 0.5 || flat_picture(here.a)) return;\\n"\n' in renderer
    and '    "  if (flat_picture(north.a) || flat_picture(south.a) || flat_picture(east.a) || flat_picture(west.a)) return;\\n"\n' in renderer
    and '    "  if (contrast < max(0.0312, most * 0.125)) return;\\n"\n' in renderer
    and '    "  bool level = across_rows >= across_columns;\\n"\n' in renderer
    and '    "  float side = (level ? px.y : px.x) * (lower ? -1.0 : 1.0);\\n"\n' in renderer
    and '    "  float shift = 0.5 - min(back_far, fore_far) / (back_far + fore_far);\\n"\n'
    '    "  if (((back_far < fore_far ? back_d : fore_d) < 0.0) == (m < mean)) shift = 0.0;\\n"\n' in renderer
    and '    "  shift = max(shift, 0.75 * lone * lone);\\n"\n'
    '    "  frag = vec4(texture(u_tex, v_uv + (level ? vec2(0.0, shift * side) : vec2(shift * side, 0.0))).rgb, 1.0);\\n"\n' in renderer,
    "a pixel without an edge is the target's mean under it and nothing else, and so is a flat picture's pixel and one beside it: "
    "a world's pixel on an edge is read up to half a pixel across it by its place along the edge's step, and up to three "
    "quarters where it stands alone among its neighbours",
)
require(
    "static int aa_edges_of(int path) { return path == GL_PRES_WIDE || !s_geometry_correction; }\n" in renderer
    and "                                int lx, int ly, int lw, int lh, int path) {\n"
    "    const int edges = aa_edges_of(path), grown = aa_begin(0, h * s_scale, edges, lx, ly, lw, lh);\n" in renderer
    and "    p_glUseProgram(0);\n    aa_end(0, s_present_vao, grown, edges, lx, ly, lw, lh);\n}\n" in renderer
    and "                        disp_x, disp_y, w, h, linear, lx, ly, lw, lh, GL_PRES_VRAM);\n" in renderer
    and "                            0, disp_y, g_wide_w, disp_h, linear, lx, ly, lw, lh, GL_PRES_WIDE);\n" in renderer
    and "    const int edges = aa_edges_of(s_interp_source_path), grown = aa_begin(1, s_interp_h, edges, lx, ly, lw, lh);\n" in renderer
    and "    p_glActiveTexture(PSXGL_TEXTURE0);\n    aa_end(1, s_interp_thread_vao, grown, edges, lx, ly, lw, lh);\n    return alpha;\n" in renderer
    and "    const int grown=aa_begin(1,s->height*s->scale,1,lx,ly,lw,lh);\n    glBindTexture(GL_TEXTURE_2D,s_temp_tex);\n" in presenter
    and "    p_glBindVertexArray(0); p_glUseProgram(0);\n    aa_end(1,s_interp_thread_vao,grown,1,lx,ly,lw,lh);\n    ++s_temp_replays;\n" in presenter
    and renderer.count("aa_begin(") == 4
    and renderer.count("aa_end(") == 4
    and presenter.count("aa_begin(") == presenter.count("aa_end(") == 1,
    "the frame loop's picture, a real frame on the presenter's thread and an in-between frame each go through the target on "
    "their own context: the canonical surface, which carries no marks, has its edges smoothed only without geometry correction, "
    "where it is the world's picture and not a full-2D frame's",
)
require(
    "    const int thread = t == &s_ssao_thread;\n"
    "    const int grown = aa_begin(thread, (int)((v1 - v0) * (float)h + 0.5f), 1, lx, ly, lw, lh);\n" in renderer
    and "    p_glUniform4f(s_ssao_mix_uSpan, grown ? 0.0f : (float)lx, grown ? 0.0f : (float)ly, (float)s_ssao, (float)grown);\n"
    "    glDrawArrays(GL_TRIANGLES, 0, 3);\n    p_glBindVertexArray(0);\n    p_glUseProgram(0);\n    aa_end(thread, vao, grown, 1, lx, ly, lw, lh);\n" in renderer
    and '    "  ivec2 at = ivec2((gl_FragCoord.xy - u_span.xy) / max(u_span.w, 1.0)), most = textureSize(u_shade, 0) - 1;\\n"\n' in renderer
    and '    "  vec4 seen = texture(u_tex, v_uv);\\n"\n    "  vec3 c = seen.rgb;\\n"\n' in renderer
    and '    "  frag = vec4(u_span.z > 1.5 ? vec3(0.0, 0.0, 0.25) : c, seen.a);\\n"\n' in renderer
    and '    "  frag = vec4(u_span.z > 1.5 ? vec3(lit) : c*lit, seen.a); }\\n";\n' in renderer,
    "a shaded picture goes through the target too with the mirror's marks handed on, and its shading, a value a screen pixel, "
    "is read by the screen pixel a target pixel belongs to",
)
require(
    "static int s_world_begun = 0, s_frame_drawn = 0;\n"
    "extern int gpu_ws_drawing_flat(void);\n"
    "static float flat_picture_flags(int visual_flags) {\n"
    "    return visual_flags == 0 && (s_world_begun || gpu_ws_drawing_flat()) ? -1.0f : (float)visual_flags;\n"
    "}\n" in renderer
    and "    if (world || sprite) s_world_begun = 1;\n    s_frame_drawn = 1;\n" in renderer
    and "    if (s_frame_drawn) s_world_begun = s_frame_drawn = 0;\n    msaa_apply();\n" in renderer
    and "            vp[19] = flat_picture_flags(visual_flags);               /* a_visual_flags */\n" in renderer
    and "int gpu_ws_drawing_flat(void) { return !ws_game_mode_of(0); }\n" in gpu
    and "static int ws_game_mode(void) { return ws_game_mode_of(0); }" in gpu,
    "a flat picture is the HUD's when the frame's world has begun before it or the frame is a full-2D one to the widescreen code's own draw-time question, the backdrop's when "
    "not: a frame runs from one present to the next that something was drawn before",
)
require(
    '"  v_bill = u_visual > 0.5 ? (a_visual_flags < -0.5 && a_visual_flags > -1.5 ? 0.75 : (abs(a_camz) > 16.0 && a_visual_flags < 1.5) ? 2.0 : 1.0) : 0.0;\\n";\n' in renderer
    and '            "  v_bill = a_visual_flags < -0.5 && a_visual_flags > -1.5 ? 0.75 : (a_visual_flags < 1.5 && a_order > -0.9999 && a_order < 0.9999) ? 2.0 : 1.0; }\\n") : NULL;\n'
    in presenter
    and '        "  if (v_bill > 0.5 && v_bill < 0.9) frag.a = (!blended && frag.a > 0.5) ? 0.95 : 0.75;\\n"\n'
    '        "  else if (v_bill > 0.5 && blended) frag.a = 0.25; else if (v_bill > 1.5) frag.a = 0.5;\\n"\n' in renderer
    and '    "  if(c.a<0.5 || (c.a>0.7 && c.a<0.8)) discard; frag=vec4(0.0); }\\n";' in renderer
    and '    "  return ((mark > 0.2 && mark < 0.3) || (mark > 0.7 && mark < 0.8)) ? 0.0 : cam(uv); }\\n"\n' in renderer
    and '    "  return ((mark > 0.2 && mark < 0.6) || (mark > 0.7 && mark < 0.8)) ? 0.0 : cam(uv); }\\n"\n' in renderer
    and '    "  if (((mark > 0.2 && mark < 0.6) || (mark > 0.7 && mark < 0.8)) && u_span.z < 3.5) return;' in renderer
    and renderer.count("mark > 0.7 && mark < 0.8") == 3,
    "on the mirror and in an in-between frame a flat picture over the world marks its pixels in the alpha: 0.95 where it is "
    "opaque with the mask bit and 0.75 elsewhere, blended or not. The shading takes 0.75 as it took such a picture's blends, "
    "no surface and nothing to shade, and the stencil rebuilt from the alpha takes it for no mask bit, as that pixel had before",
)
require(
    "static int s_msaa_asked = 0, s_msaa = 0;\n" in renderer
    and "void gl_renderer_set_multisampling(int samples) { s_msaa_asked = samples >= 2 ? samples : 0; }\n" in renderer
    and "    int want = s_geometry_correction && p_glRenderbufferStorageMultisample ? s_msaa_asked : 0;\n"
    "    if (want > s_msaa_most) want = s_msaa_most;\n"
    "    if (want < 2) want = 0;\n"
    "    const int shaded = want && s_ssaa_asked && p_glMinSampleShading;\n"
    "    if (shaded != s_ssaa) sample_shading(s_ssaa = shaded);\n"
    "    if (want == s_msaa) return;\n"
    "    for (int i = 0; i < WIDE_MAX_SURF; i++) wide_twin_drop(i);\n"
    "    s_msaa = want;\n" in renderer
    and "        glGetIntegerv(PSXGL_MAX_SAMPLES, &s_msaa_most);\n" in renderer
    and '    p_glRenderbufferStorageMultisample = (void *)SDL_GL_GetProcAddress("glRenderbufferStorageMultisample");\n' in renderer,
    "multisampling starts off, runs only on the corrected mirror at the driver's most at most, and a change of the samples "
    "takes effect at a present with every twin handed back first",
)
require(
    "static int s_ssaa_asked = 0, s_ssaa = 0;\n" in renderer
    and "void gl_renderer_set_sample_shading(int enabled) { s_ssaa_asked = enabled != 0; }\n" in renderer
    and "static void sample_shading(int on) {\n"
    "    if (!on) { glDisable(PSXGL_SAMPLE_SHADING); return; }\n"
    "    glEnable(PSXGL_SAMPLE_SHADING);\n"
    "    p_glMinSampleShading(1.0f);\n"
    "}\n" in renderer
    and "#define PSXGL_SAMPLE_SHADING 0x8C36\n" in renderer
    and "        glGetIntegerv(0x821B /* GL_MAJOR_VERSION */, &major);\n"
    '        p_glMinSampleShading = major >= 4 ? (void *)SDL_GL_GetProcAddress("glMinSampleShading")\n'
    '            : SDL_GL_ExtensionSupported("GL_ARB_sample_shading") ? (void *)SDL_GL_GetProcAddress("glMinSampleShadingARB") : NULL;\n' in renderer
    and renderer.count("sample_shading(") == 4
    and "    if (s_temp_shading!=s_ssaa) sample_shading(s_temp_shading=s_ssaa);\n"
    "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_temp_ms_fbo ? s_temp_ms_fbo : s_temp_fbo);\n" in presenter
    and "    s_temp_ms_fbo=s_temp_ms_rb[0]=s_temp_ms_rb[1]=0; s_temp_samples=s_temp_shading=0;\n" in presenter,
    "supersampling starts off and is multisampling with every sample shaded: it is on only while samples are in use and the "
    "context is GL 4.0 or has the extension, whose call alone is taken, and each of the two GL contexts is told when it "
    "changes, the presenter's before it draws a frame",
)
require(
    "    if (i < 0 || s_msaa < 2) return fbo;\n"
    "    if (!s_wide_ms_fbo[i] && !wide_twin_make(i)) return fbo;\n"
    "    s_wide_ms_fresh[i][1] = 0;\n"
    "    return s_wide_ms_fbo[i];\n" in renderer
    and "    wide_blit(s_wide_fbo[i], s_wide_ms_fbo[i], 0, VRAM_H);\n    return 1;\n" in renderer
    and "    p_glRenderbufferStorageMultisample(PSXGL_RENDERBUFFER, s_msaa, GL_RGBA8, w, h);\n" in renderer
    and "    p_glRenderbufferStorageMultisample(PSXGL_RENDERBUFFER, s_msaa, PSXGL_DEPTH24_STENCIL8, w, h);\n" in renderer
    and "        wide_twin_free(i);\n        for (int other = 0; other < WIDE_MAX_SURF; other++) wide_twin_drop(other);\n        s_msaa_asked = s_msaa = 0;\n        return 0;\n"
    in renderer
    and "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, wide_drawn(target_fbo));\n" in renderer
    and "        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, wide_drawn(g_wide_cur));\n    wide_depth_begin();\n" in renderer
    and "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, wide_drawn(g_wide_cur));\n    wide_depth_begin();\n" in renderer
    and "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, wide_drawn(fbo));\n    glViewport(0, 0, g_wide_w * s_scale, H);\n" in renderer
    and "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, wide_drawn(fbo));\n    glViewport(0, 0, W, H);\n" in renderer
    and renderer.count("wide_drawn(") == 6
    and "p_glBindFramebuffer(PSXGL_FRAMEBUFFER, g_wide_cur)" not in renderer,
    "what is drawn into a mirror surface goes to its multisample twin, colour, depth and stencil, and a twin begins as the "
    "surface stands: one that cannot be made switches multisampling off with the others handed back",
)
require(
    "    if (h <= 0 || (y >= s_wide_ms_fresh[i][0] && y + h <= s_wide_ms_fresh[i][0] + s_wide_ms_fresh[i][1])) return;\n"
    "    wide_blit(s_wide_ms_fbo[i], fbo, y, h);\n    s_wide_ms_fresh[i][0] = y;\n    s_wide_ms_fresh[i][1] = h;\n" in renderer
    and "                        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);\n" in renderer
    and "    glDisable(GL_SCISSOR_TEST);\n    p_glBlitFramebuffer(0, y * s_scale, w, (y + h) * s_scale, 0, y * s_scale, w, (y + h) * s_scale,\n" in renderer
    and "    wide_blit_center(fbo, disp_x, disp_y, disp_h);   /* fast-path: authoritative centre */\n    wide_resolved(fbo, disp_y, disp_h);\n" in renderer
    and "    wide_resolved(target_fbo, 0, VRAM_H);\n    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, target_fbo);\n" in renderer
    and renderer.count("    wide_resolved(fbo, 0, VRAM_H);\n    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, fbo);\n") == 2
    and renderer.count("wide_resolved(") == 5
    and "    wide_blit(s_wide_ms_fbo[i], s_wide_fbo[i], 0, VRAM_H);\n    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0);\n    wide_twin_free(i);\n" in renderer
    and "    for (int i = 0; i < WIDE_MAX_SURF; ++i) wide_twin_drop(i);\n    for (int i = 0; i < WIDE_MAX_SURF; ++i) {\n        if (!s_wide_fbo[i]) continue;\n"
    in renderer
    and "    for (int i = 0; i < WIDE_MAX_SURF; i++) {\n        wide_twin_free(i);\n        if (s_wide_fbo[i]) { p_glDeleteFramebuffers(1, &s_wide_fbo[i]);"
    in renderer,
    "before a mirror surface is shown, captured, read back or rebuilt its twin's rows are resolved into it once for what was "
    "drawn, and a twin is handed back whole before its surface changes scale and freed with it",
)
require(
    "    const int samples=s_msaa;\n"
    "    if (s_temp_fbo && s_temp_width==width && s_temp_scale==scale && s_temp_samples==samples) return 1;\n" in presenter
    and "        p_glRenderbufferStorageMultisample(PSXGL_RENDERBUFFER,samples,GL_RGBA8,width*scale,VRAM_H*scale);\n" in presenter
    and "    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_temp_ms_fbo ? s_temp_ms_fbo : s_temp_fbo);\n    glViewport(0,0,s->width*s->scale,VRAM_H*s->scale);\n"
    in presenter
    and "    s_temp_scene_depth=0;\n    glDisable(GL_DEPTH_TEST);\n    if (s_temp_ms_fbo) {\n"
    "        p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s_temp_ms_fbo); p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,s_temp_fbo);\n"
    "        glDisable(GL_SCISSOR_TEST);\n" in presenter
    and "                            GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT,GL_NEAREST);\n        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_temp_fbo);\n" in presenter
    and "            s_temp_ms_fbo=s_temp_ms_rb[0]=s_temp_ms_rb[1]=0;\n        }\n" in presenter,
    "an in-between frame is drawn through a twin of the same samples and resolved, colour and depth, before it is shaded, "
    "shown or read: a twin that cannot be made leaves the plain surface in use",
)
require(
    "inline constexpr int kDisruptorSampleCounts[] = {0, 2, 4, 8, 16};\n" in fields
    and 'constexpr const char* antialiasing_levels[] = {"Off",     "2x MSAA", "4x MSAA", "8x MSAA", "16x MSAA",\n'
    '                                               "2x SSAA", "4x SSAA", "8x SSAA", "16x SSAA"};\n' in options
    and '    choice("disruptor.multisampling", "Anti-aliasing (MSAA / SSAA)", 0,\n' in options
    and "           antialiasing_saved, antialiasing, set_antialiasing, needs_geometry),\n" in options
    and "    return samples > 0 && settings.sample_shading ? samples + sample_levels : samples;\n" in options
    and "    settings.has_multisampling = settings.has_sample_shading = true;\n"
    "    settings.sample_shading = index > sample_levels;\n"
    "    settings.multisampling = PSXRecompV4::kDisruptorSampleCounts[settings.sample_shading ? index - sample_levels : index];\n"
    in options
    and '"disruptor.sample_shading"' not in options
    and "    toggle<&Settings::has_downscale_averaging, &Settings::downscale_averaging>(\n"
    '        "disruptor.downscale_averaging", "Downscale averaging", 0,\n' in options
    and "    toggle<&Settings::has_edge_smoothing, &Settings::edge_smoothing>(\n"
    '        "disruptor.edge_smoothing", "Edge smoothing", 0,\n' in options
    and 'textured pictures of the HUD and of menus stay as they are. Movies stay as they are. Unfinished."),\n};\n' in options
    and "                        std::span<const char* const> needs = {}) {\n"
    "    return {key, Kind::choice, label, saved, get, set, initial, 0, static_cast<int>(choices.size()) - 1,\n"
    "            choices, needs, Unit::none, hint};\n" in options,
    "the three are saved settings that start off: anti-aliasing one list over the samples and their shading that needs "
    "geometry correction, the two filters switches that need nothing",
)
require(
    '            const int samples = toml::find<int>(d, "multisampling");\n'
    "            if (std::ranges::find(kDisruptorSampleCounts, samples) != std::end(kDisruptorSampleCounts)) {\n"
    "                s.multisampling = samples;\n                s.has_multisampling = true;\n" in saved
    and '        if (s.has_multisampling)\n            f << "multisampling = " << s.multisampling << "\\n";\n' in saved
    and '        if (d.contains("downscale_averaging")) try_get([&]{\n            s.downscale_averaging = toml::find<bool>(d, "downscale_averaging");\n'
    "            s.has_downscale_averaging = true;\n" in saved
    and '        if (s.has_downscale_averaging)\n            f << "downscale_averaging = " << (s.downscale_averaging ? "true" : "false") << "\\n";\n' in saved
    and '        if (d.contains("edge_smoothing")) try_get([&]{\n            s.edge_smoothing = toml::find<bool>(d, "edge_smoothing");\n            s.has_edge_smoothing = true;\n'
    in saved
    and '        if (s.has_edge_smoothing)\n            f << "edge_smoothing = " << (s.edge_smoothing ? "true" : "false") << "\\n";\n' in saved
    and '        if (d.contains("sample_shading")) try_get([&]{\n            s.sample_shading = toml::find<bool>(d, "sample_shading");\n            s.has_sample_shading = true;\n'
    in saved
    and '        if (s.has_sample_shading)\n            f << "sample_shading = " << (s.sample_shading ? "true" : "false") << "\\n";\n' in saved
    and "        s.has_multisampling || s.has_sample_shading || s.has_downscale_averaging || s.has_edge_smoothing ||\n" in saved
    and '    {"disruptor.multisampling", Page::experimental},\n'
    '    {"disruptor.downscale_averaging", Page::experimental},\n    {"disruptor.edge_smoothing", Page::experimental},\n' in launcher
    and '"disruptor.sample_shading"' not in launcher,
    "each of the four keys is read from the file, the samples only as a count the list has, written back and counted as a "
    "setting of its own, and the launcher's Experimental page shows the anti-aliasing list over the first two",
)
require(
    "    if (settings.has_sample_shading) gl_renderer_set_sample_shading(settings.sample_shading ? 1 : 0);\n" in menu
    and '"disruptor.sample_shading"' not in menu + experimental
    and "mark_sample_shading" not in menu + experimental,
    "supersampling is the anti-aliasing list's shading: no row of its own, and a start with it saved applies it",
)
require(
    '    const Option& antialiasing = *find_option("disruptor.multisampling");\n'
    "    settings::Settings running;\n"
    "    running.multisampling = gl_renderer_multisampling();\n"
    "    running.sample_shading = gl_renderer_sample_shading() != 0;\n"
    "    int level = antialiasing.get(running);\n"
    '    const bool sampling_applies = applies("disruptor.multisampling");\n'
    "    if (!sampling_applies) ImGui::BeginDisabled();\n"
    "    ImGui::SetNextItemWidth(260.0f);\n"
    '    if (option_list("disruptor.multisampling", &level)) {\n'
    "        antialiasing.set(running, level);\n"
    "        gl_renderer_set_multisampling(running.multisampling);\n"
    "        gl_renderer_set_sample_shading(running.sample_shading ? 1 : 0);\n"
    "        mark_antialiasing(level);\n"
    "    }\n"
    "    if (!sampling_applies) ImGui::EndDisabled();\n" in experimental
    and "    bool averaging = gl_renderer_downscale_averaging() != 0;\n"
    '    if (option_toggle("disruptor.downscale_averaging", &averaging)) {\n'
    "        gl_renderer_set_downscale_averaging(averaging ? 1 : 0);\n"
    "        mark_downscale_averaging(averaging);\n"
    "    }\n" in experimental
    and "    bool edge_smoothing = gl_renderer_edge_smoothing() != 0;\n"
    '    if (option_toggle("disruptor.edge_smoothing", &edge_smoothing)) {\n'
    "        gl_renderer_set_edge_smoothing(edge_smoothing ? 1 : 0);\n"
    "        mark_edge_smoothing(edge_smoothing);\n"
    "    }\n" in experimental
    and "    if (settings.has_multisampling) gl_renderer_set_multisampling(settings.multisampling);\n" in menu
    and "    if (settings.has_downscale_averaging) gl_renderer_set_downscale_averaging(settings.downscale_averaging ? 1 : 0);\n"
    "    if (settings.has_edge_smoothing) gl_renderer_set_edge_smoothing(settings.edge_smoothing ? 1 : 0);\n" in menu
    and '    if (changed("disruptor.multisampling")) {\n        gl_renderer_set_multisampling(pending.multisampling);\n'
    "        gl_renderer_set_sample_shading(pending.sample_shading ? 1 : 0);\n    }\n" in menu
    and '    if (changed("disruptor.downscale_averaging"))\n        gl_renderer_set_downscale_averaging(pending.downscale_averaging ? 1 : 0);\n'
    '    if (changed("disruptor.edge_smoothing"))\n        gl_renderer_set_edge_smoothing(pending.edge_smoothing ? 1 : 0);\n' in menu
    and 'void mark_antialiasing(int level) { mark("disruptor.multisampling", level); }\n' in menu
    and "void gl_renderer_set_downscale_averaging(int enabled) { s_aa_average = enabled != 0; }\n" in renderer
    and "void gl_renderer_set_edge_smoothing(int enabled) { s_aa_edges = enabled != 0; }\n" in renderer,
    "the Experimental tab switches each of the three at once and stages it for saving, anti-aliasing through the shared table's "
    "own reading of the samples and their shading, "
    "and a start with a setting saved applies it",
)


def members(text: str, opening: str) -> set[str]:
    """Every data member of the record that `opening` begins: the word before each `=` or `;` of its body, comments dropped."""
    body = text[text.index(opening) + len(opening) :]
    body = re.sub(r"//[^\n]*", "", body[: re.search(r"^\s*};", body, re.M).start()])
    return set(re.findall(r"(\w+)\s*(?:=[^;]*)?;", body))


own_members = members(fields, "struct GameUserSettings {")
framework_members = members(read("psxrecomp/recompiler/src/config_loader.h"), "struct UserSettings {")
require(
    len(own_members) >= 50 and {"has_edge_smoothing", "edge_smoothing", "has_multisampling", "multisampling", "downscale_averaging", "sample_shading"} <= own_members
    and len(framework_members) > 80 and {"has_antialiasing", "antialiasing", "parse_error"} <= framework_members
    and not framework_members.intersection(own_members)
    and "struct UserSettings : GameUserSettings {\n#else\nstruct UserSettings {\n" in read("psxrecomp/recompiler/src/config_loader.h"),
    "the framework's settings record derives from the game's and has an antialiasing field of its own, the linear filter of "
    "the shown picture: a game member of a name the framework declares would be hidden by it, read and written as the framework's",
)
require("    disruptor_contract(antialiasing)\n" in cmake, "this contract must remain registered")

print("Disruptor anti-aliasing source contract: PASS")
