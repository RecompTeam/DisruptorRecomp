#!/usr/bin/env python3
"""Source contract for the depth-sorted world: its rule's header, the renderer's two passes and the setting."""

from pathlib import Path

from menu_sources import menu_text

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def between(source: str, opening: str, closing: str) -> str:
    start = source.index(opening)
    return source[start : source.index(closing, start)]


cmake = read("CMakeLists.txt")
overlay = read("framework/FILES.txt")
header = read("psxrecomp/runtime/include/gpu_world_depth.h")
renderer = read("psxrecomp/runtime/src/gpu_gl_renderer.c")
presenter = read("psxrecomp/runtime/src/gpu_gl_temporal.c.inc")
menu = menu_text(ROOT)
launcher = read("src/launcher/launcher_settings.cpp")
options = read("src/game/settings/options.cpp")
STAGED = (
    "void mark(const char *key, int value) {\n"
    "    const Option *option = find_option(key);\n"
    "    option->set(g_preferences.pending, value);\n"
    "    g_preferences.changed.insert(option);\n"
    "}\n"
)
MERGED = (
    "    for (const Option *option : g_preferences.changed)\n        option->set(settings, option->get(pending));\n"
)
loader = read("src/game/settings/saved.cpp")

require(
    overlay.count("+runtime/include/gpu_world_depth.h\n") == 1
    and '#include "gpu_world_depth.h"\n' in renderer
    and "    disruptor_unit_test(disruptor_gpu_world_depth tests/unit/gpu_world_depth_test.cpp RUNTIME_HEADERS)\n"
    in cmake
    and "    disruptor_contract(world_depth)\n" in cmake,
    "the rule's header is applied to the framework and its test and this contract are registered",
)

require(
    "    const float stored = 1.0f - 32.0f * (1.0f + pull) / depth;\n" in header
    and "max(1.0 - 32.0*(1.0 + u_pull)/(abs(a_camz)*(1.0 + max(u_visual_options.w, 0.0))), -1.0)" in renderer
    and "    return depth>16.0f ? 1.0f-32.0f/depth : TEMP_ORDER_NEAR;\n" in presenter,
    "the header, the mirror's shader and the presenter store one depth for one corner",
)

require(
    "        int world = near_face ? 0 : psx_world_depth_class(s_world_depth, visual_flags, turned_z, s_world_margin);\n"
    in renderer
    and "            world = psx_world_depth_class(s_world_depth, visual_flags, piece_z, s_world_margin);\n"
    "        }\n"
    "        if (s_tb_n > 0 && world != s_tb_world) flush_tex_batch();\n"
    in renderer
    and "            s_tb_world = world;\n" in renderer
    and "static float s_world_margin = 0.03f;\n" in renderer
    and "static int   s_world_depth = 0;\n" in renderer,
    "a triangle's class comes from the header, is a key of the batch, and the setting starts off with a margin of 3 percent",
)

by_depth = between(renderer, "static void tex_batch_by_depth(int nverts, int semi) {", "\n}\n")
require(
    by_depth
    == """static void tex_batch_by_depth(int nverts, int semi) {
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    p_glUniform1i(s_uSemimode, semi < 0 ? 0 : semi);
    p_glUniform1i(s_uSemipass, semi < 0 ? 3 : 4);
    glDrawArrays(GL_TRIANGLES, 0, nverts);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    p_glUniform1f(s_tex_uPull, s_world_margin);
    tex_batch_draw_passes(nverts, semi);
    p_glUniform1f(s_tex_uPull, 0.0f);
    glDepthFunc(GL_ALWAYS);""",
    "a batch of the class writes its true depths first, then draws its colours pulled by the margin without writing depth, "
    "and leaves the pull and the depth test as it found them",
)

require(
    "            if (world && mirror_depth()) tex_batch_by_depth(nverts, semi);\n"
    "            else tex_batch_draw_passes(nverts, semi);\n"
    in renderer
    and "static int mirror_depth(void) {\n    return s_geometry_correction && (s_ssao || s_grown_under || s_crack_fill > 0.0f || s_world_depth || s_sprite_hold);\n}\n"
    in renderer
    and '    s_tex_uPull = p_glGetUniformLocation(s_tex_prog, "u_pull");\n' in renderer,
    "only a batch of the class is drawn by depth, the setting alone keeps the mirror's depth, and the shader's pull is found",
)

require(
    """void gl_renderer_set_world_depth(int enabled) {
    GL_RT_SYNC(__func__);
    if (s_raster_ok) { flush_flat_batch(); flush_tex_batch(); }
    if ((enabled != 0) != s_world_depth) interp_reset_history();
    if (enabled && !mirror_depth()) memset(s_wide_depth_shown, 0xFF, sizeof s_wide_depth_shown);
    s_world_depth = enabled != 0;
}
int gl_renderer_world_depth(void) { return s_world_depth; }
"""
    in renderer
    and '            if (getenv("PSX_GL_WORLD_DEPTH")) s_world_depth = atoi(getenv("PSX_GL_WORLD_DEPTH")) != 0;\n'
    in renderer,
    "switching draws what is open under the old rule, starts the presenter's history anew and has the mirror's depth made afresh",
)

require(
    "        p_glUniform1f(s_temp_uPull,s_world_margin);\n" in presenter
    and "        p_glUniform1f(s_temp_uPull,0.0f); glDepthFunc(GL_ALWAYS); glDepthMask(GL_TRUE);\n" in presenter
    and "    for (last_world=s->nt-1;last_world>=0 && !s->triangles[last_world].world;--last_world) {}\n" in presenter
    and "            if (s_world_depth && (now->world || ti<last_world))\n" in presenter
    and "                    if (!(now->world && now->depth[k]>0.0f)) s_temp_scratch[(j+k)*TEXV+(t?23:9)]=TEMP_ORDER_FAR;\n"
    in presenter,
    "an in-between frame draws the world by the same margin, and a corner without a depth is far while world faces follow it",
)

enhancements = between(menu, "void draw_enhancements_tab() {", "const char *cheat_result_message(")
require(
    """    bool by_depth = gl_renderer_world_depth() != 0;
    const bool depth_applies = applies("disruptor.depth_sorted_world");
    if (!depth_applies) ImGui::BeginDisabled();
    if (option_toggle("disruptor.depth_sorted_world", &by_depth)) {
        gl_renderer_set_world_depth(by_depth ? 1 : 0);
        mark_world_depth(by_depth);
    }
    if (!depth_applies) ImGui::EndDisabled();
"""
    in enhancements
    and menu.count('option_toggle("disruptor.depth_sorted_world"') == 1
    and enhancements.count("ImGui::BeginDisabled()") == enhancements.count("ImGui::EndDisabled()"),
    "the setting is on the Enhancements tab alone, greyed without geometry correction, applied and staged when changed",
)

require(
    "    std::set<const Option *> changed;\n" in menu
    and 'void mark_world_depth(bool value) { mark("disruptor.depth_sorted_world", value); }\n' in menu
    and STAGED in menu
    and MERGED in menu
    and '    toggle<&Settings::has_depth_sorted_world, &Settings::depth_sorted_world>(\n        "disruptor.depth_sorted_world",'
    in options
    and """    if (settings.has_depth_sorted_world && !env_override_present("PSX_GL_WORLD_DEPTH"))
        gl_renderer_set_world_depth(settings.depth_sorted_world ? 1 : 0);
"""
    in menu
    and """    if (changed("disruptor.depth_sorted_world"))
        gl_renderer_set_world_depth(pending.depth_sorted_world ? 1 : 0);
"""
    in menu,
    "a change is staged as an option of the shared table, merged into the file's settings, applied at the next start unless the "
    "environment decides, and applied when another run saved it",
)

require(
    """        if (d.contains("depth_sorted_world")) try_get([&]{
            s.depth_sorted_world = toml::find<bool>(d, "depth_sorted_world");
            s.has_depth_sorted_world = true;
"""
    in loader
    and """        if (s.has_depth_sorted_world)
            f << "depth_sorted_world = " << (s.depth_sorted_world ? "true" : "false") << "\\n";
"""
    in loader
    and "s.has_depth_sorted_world" in between(loader, "    if (s.has_mouse_aim", "{\n")
    and "    bool has_depth_sorted_world = false; bool depth_sorted_world = false;\n"
    in read("src/game/settings/game_user_settings.h"),
    "the settings file keeps the choice, which is off until made",
)

row = between(options, '"disruptor.depth_sorted_world", "Depth-sorted world", 1,', "),\n")
require(
    row.endswith("needs_geometry")
    and "constexpr const char* needs_geometry[] = {geometry_key};\n" in options
    and options.count('"disruptor.depth_sorted_world"') == 1
    and launcher.count('"disruptor.depth_sorted_world"') == 1
    and '    {"disruptor.depth_sorted_world", Page::enhancements},\n' in launcher,
    "the launcher offers it on the Enhancements page, on by default and only with geometry correction",
)

flush = between(renderer, "static void flush_tex_batch(void) {", "\n}\n")
require(
    "static float s_sprite_hold_share = 0.2f;\nstatic int s_sprite_hold = 1;" in renderer
    and "    const int sprite = s_geometry_correction && s_sprite_depth > 0.0f && (visual_flags & 3) == 1;\n"
    in renderer
    and "        if (s_tb_n > 0 && world != s_tb_world) flush_tex_batch();\n"
    "        const int held = sprite && s_sprite_hold && !near_face;\n"
    "        if (s_tb_n > 0 && held != s_tb_held) flush_tex_batch();\n"
    in renderer
    and "            s_tb_fillers = 0;\n            s_tb_held = held;\n" in renderer,
    "with the hold on a world sprite is a batch of its own, so what the game drew before it has its depth written first",
)
require(
    '    static const char holding[] = "  if (u_hold > 0.0) gl_Position.z = abs(a_camz) > 16.0\\n"\n'
    '                                  "    ? max(1.0 - 32.0/(abs(a_camz)*(1.0 - u_hold)), -1.0)*gl_Position.w : -gl_Position.w;\\n";\n'
    in renderer
    and '    if (fade) strcat(out, closing);\n    strcat(out, holding);\n    if (fade) strcat(out, grounding);\n    strcat(out, " }\\n");\n'
    in renderer
    and 'uniform float u_pull; uniform float u_hold;\\n%s",\n                       (int)(body + 1 - source), source, location, fade ? grounded : "");'
    in renderer
    and '    s_tex_uHold = p_glGetUniformLocation(s_tex_prog, "u_hold");\n' in renderer,
    "while a sprite is tested its depth is the one the share nearer, after a near face's own",
)
require(
    "    const int held = s_tb_held;\n" in flush
    and "        s_draw_held = held;\n" in flush
    and "        s_draw_filler = s_draw_world = s_draw_held = 0;\n" in flush
    and "            const int read = held && !world && hold_reads_depth(semi);\n"
    "            if (read) hold_depth_begin();\n"
    "            else if (held) { glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); p_glUniform1f(s_tex_uHold, s_sprite_hold_share); }\n"
    "            if (world && mirror_depth()) tex_batch_by_depth(nverts, semi);\n"
    "            else tex_batch_draw_passes(nverts, semi);\n"
    "            if (read) hold_depth_end();\n"
    "            if (held) { p_glUniform1f(s_tex_uHold, 0.0f); glDepthFunc(GL_ALWAYS); }\n"
    in flush
    and "            if (held) glDepthFunc(GL_LEQUAL);" in flush
    and flush.index("            if (held) glDepthFunc(GL_LEQUAL);")
    < flush.index("            glDrawArrays(GL_TRIANGLES, 0, nverts);\n            glDepthFunc(GL_ALWAYS);\n"),
    "a sprite batch draws its colours tested and without writing depth, then writes the true depth where it is not "
    "behind what is there, and leaves the depth test and the share as it found them",
)
held = between(presenter, "    if (!round && held) {", "        continue;\n    }")
require(
    '"flat out float v_bill; noperspective out float v_deep; uniform float u_pull; uniform float u_hold; '
    'layout(location=13) in vec3 a_ground; flat out vec3 v_ground; noperspective out float v_texel;\\n"'
    in presenter
    and '            "  if (u_hold > 0.0) gl_Position.z = (a_order > -0.9999 && a_order < 0.9999)\\n"\n'
    '            "    ? max(1.0 - (1.0 - a_order)/(1.0 - u_hold), -1.0)*gl_Position.w : -gl_Position.w;\\n"\n'
    in presenter
    and '    s_temp_uHold=p_glGetUniformLocation(s_temp_tex_prog,"u_hold");\n' in presenter
    and "    c->held=textured && s_draw_held;\n" in presenter
    and "    const int opaque_depth=(s_ssao || (s_sprite_hold && s_geometry_correction)) && s_temp_scene_depth"
    in presenter
    and "    const int held=c->held && s_temp_marked && s_temp_scene_depth && !s_temp_underlaying && t &&\n"
    in presenter
    and held.startswith(
        "    if (!round && held) {\n        const int read=temporal_hold_reads(s);\n        const GLuint write_mask=read ? 0 : 1;\n"
        "        if (read) temporal_hold_begin(s,u);\n"
        "        else { glDepthFunc(GL_LEQUAL); glDepthMask(GL_FALSE); p_glUniform1f(s_temp_uHold,s_sprite_hold_share); }\n"
    )
    and "        p_glUniform1f(s_temp_uHold,0.0f);\n" in held
    and held.count("glDepthMask(GL_TRUE)") == 1
    and held.index("        p_glUniform1f(s_temp_uHold,0.0f);\n") < held.index("glDepthMask(GL_TRUE)")
    and held.rstrip().endswith("glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthFunc(GL_ALWAYS);")
    and presenter.index("    if (!round && held) {")
    < presenter.index("    if (!c->check && c->semi==4) {\n        glEnable(GL_BLEND);"),
    "an in-between frame tests a sprite batch the same way: the share nearer while its colours are drawn with no depth "
    "written, its true depth after, and with the hold on its blended texels write no depth, as on the real frames",
)

gpu = read("psxrecomp/runtime/src/gpu.c")
thread = read("psxrecomp/runtime/src/gpu_gl_render_thread.c.inc")
reading = between(renderer, "static int hold_reads_depth(int semi) {", "static void flush_tex_batch(void) {")
require(
    "    if (!s_hold_reads || !p_glTextureBarrier || i < 0 || !s_wide_depth[i] || s_mask_check || s_tb_mask || s_fill_pass > 0.0f)"
    " return 0;\n"
    "    if (semi >= 0 && semi != 4) return 0;\n"
    "    return !(s_wide_ms_fbo[i] && s_msaa >= 2) || (s_sample_mask_ok && s_wide_ms_depth[i]);\n"
    in reading
    and reading.index("    p_glTextureBarrier();\n")
    < reading.index("    glBindTexture(ms ? PSXGL_TEXTURE_2D_MULTISAMPLE")
    and "    glDepthFunc(GL_ALWAYS);\n    glDepthMask(GL_FALSE);\n}\n" in reading
    and "    p_glUniform1i(s_tex_uGroundSamples, 0);\n" in reading
    and "glBindTexture(GL_TEXTURE_2D, 0);" in reading
    and "glBindTexture(PSXGL_TEXTURE_2D_MULTISAMPLE, 0);" in reading,
    "a held batch reads the depth under it only after a barrier, when its colour draw writes neither depth nor stencil, "
    "and leaves the depth unbound and the test off after",
)
require(
    '"  if (depth > 0.99998 || v_ground.z <= 0.0) return false;\\n"' in renderer
    and '"  if (v_ground.y <= 0.0) return z < own * (1.0 - u_ground_share);\\n"' in renderer
    and '"  if (z >= own - min(v_ground.y, u_ground_share * own)) return false;\\n"' in renderer
    and '"  return (u_ground_view.x - y / u_ground_view.y) * z / u_ground_view.z > v_ground.x + 16.0;\\n"' in renderer
    and renderer.index('"  int kept = -1;')
    < renderer.index('"  if (u_ground_samples > 1) {\\n"')
    < renderer.index('"  gl_SampleMask[0] = kept;\\n"'),
    "a surface hides a held texel when it stands nearer than the texel by more than the sprite's half width and the share "
    "and above its ground; without a ground the share alone, and every draw writes the full sample mask but a read one",
)
held_presenter = between(presenter, "    if (!round && held) {", "        continue;\n    }")
temporal_reads = between(presenter, "static int temporal_hold_reads(const TemporalScene *s) {", "\n}\n")
require(
    "    for (int i=0;i<s->nc;++i) if (s->commands[i].check) return 0;\n" in temporal_reads
    and temporal_reads.startswith(
        "static int temporal_hold_reads(const TemporalScene *s) {\n    if (!s_hold_reads || !p_glTextureBarrier ||"
    )
    and held_presenter.count("glStencilMask(write_mask);") == 4
    and held_presenter.index(
        "        glDepthFunc(GL_LEQUAL);  /* a farther sprite leaves the nearer surface's depth */\n"
    )
    < held_presenter.index(
        "glDrawArrays(GL_TRIANGLES,0,count);\n        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthFunc(GL_ALWAYS);"
    ),
    "an in-between frame reads the depth only in a scene that checks no mask, writes no mask bits while it reads, and "
    "writes the sprite's depth after under the depth test",
)
require(
    "    if (!t || t->stamp != (uint32_t)s_frame_count || !(reach > 0.0f)) return;\n" in gpu
    and "            gr_set_temporal_sprite_ground(noted ? noted->ground : 0.0f, noted ? noted->reach : 0.0f);\n"
    "            gr_set_temporal_sprite(sprite_depth, sides);\n"
    in gpu
    and "    t->light = t->ground = t->reach = 0.0f;\n" in gpu
    and "            vp[24] = sprite ? s_sprite_ground[0] : 0.0f;\n            vp[25] = sprite ? s_sprite_ground[1] : 0.0f;\n"
    "            vp[26] = sprite ? s_sprite_depth : 0.0f;\n"
    in renderer
    and "p_glVertexAttribPointer(13, 3, GL_FLOAT, GL_FALSE, st, (void*)(24*sizeof(float)));" in renderer
    and "    const int sizes[14]={2,2,4,2,2,1,1,4,1,1,2,1,1,3};\n" in presenter
    and "    case RTH_SPRITE_GROUND: glb_set_temporal_sprite_ground(fv[0], fv[1]); break;\n" in thread
    and "    .set_temporal_sprite_ground = rtb_set_temporal_sprite_ground,\n" in thread,
    "a sprite's ground and half width go with its own rectangle only, through the render thread, into every vertex and "
    "the presenter's record",
)

preferences = read("src/game/ui/menu/preferences.cpp")
user_settings = read("src/game/settings/game_user_settings.h")
require(
    "    bool has_sprite_depth_read = false; bool sprite_depth_read = true;" in user_settings
    and '        if (d.contains("sprite_depth_read")) try_get([&]{\n'
    '            s.sprite_depth_read = toml::find<bool>(d, "sprite_depth_read");\n'
    "            s.has_sprite_depth_read = true;\n"
    in loader
    and "s.has_facing_sprites || s.has_sprite_depth_read ||" in loader
    and '            f << "sprite_depth_read = " << (s.sprite_depth_read ? "true" : "false") << "\\n";\n' in loader
    and "    if (settings.has_sprite_depth_read) gl_renderer_set_sprite_depth_read(settings.sprite_depth_read ? 1 : 0);\n"
    in preferences
    and "    s_hold_reads = enabled != 0;\n" in renderer
    and 'fprintf(stdout, "psxrecomp: a held sprite reads the depth under it: %s\\n",' in renderer
    and 'fprintf(stdout, "psxrecomp: GL %s, %s, %s\\n",' in renderer,
    "settings.toml's sprite_depth_read switches the depth read off on every driver and is kept when the file is written "
    "again, and the log names the driver and whether a held sprite reads the depth",
)

print("Disruptor depth-sorted world source contract: PASS")
