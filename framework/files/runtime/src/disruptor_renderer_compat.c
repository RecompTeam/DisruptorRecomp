/* The framework's renderer entry points the game's renderer does not carry. The framework's own renderer features
 * (render passes, stereo, its frame generation, texture banks, PGXP depth, post effects) are off for this game;
 * the core ones (live scale, timing, swap interval, restage after a load, render thread sync) run on the game's renderer. */

#include <stdint.h>
#include <string.h>

#include "gpu.h"
#include "gpu_gl_renderer.h"
#include "gpu_render.h"
#include "psx_sdl.h"

int gl_renderer_accurate_blending(void) {
    return 0;
}

int gl_renderer_anisotropy(void) {
    return 0;
}

int gl_renderer_batch_sites(int kind, int *lines, uint64_t *counts, int cap) {
    (void)cap;
    (void)counts;
    (void)lines;
    (void)kind;
    if (lines) memset(lines, 0, sizeof *lines);
    if (counts) memset(counts, 0, sizeof *counts);
    return 0;
}

void gl_renderer_bloom_debug(float strength, float threshold, int time, GlBloomInfo *out) {
    (void)out;
    (void)time;
    (void)threshold;
    (void)strength;
    if (out) memset(out, 0, sizeof *out);
}

void gl_renderer_clear_hd_texture_cache(void) {
}

int gl_renderer_cpu_auth_dual(void) {
    return 0;
}

int gl_renderer_dithering(void) {
    return 0;
}

void gl_renderer_draw_projected_triangle(const PSXProjectedVertex vertices[3], uint16_t texpage, uint16_t clut_x, uint16_t clut_y, int raw, int semi, int perspective) {
    (void)perspective;
    (void)semi;
    (void)raw;
    (void)clut_y;
    (void)clut_x;
    (void)texpage;
    (void)vertices;
}

int gl_renderer_dynamic_resolution_ceiling(void) {
    return 0;
}

void gl_renderer_dynres_stats(GlDynresStats *out) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

int gl_renderer_fbo_peek_deferred(int x, int y, int w, int h, uint16_t *out) {
    (void)out;
    (void)h;
    (void)w;
    (void)y;
    (void)x;
    if (out) memset(out, 0, sizeof *out);
    return 0;
}

void gl_renderer_fg_source(const uint32_t id[3], const int32_t pc[9], const int32_t h[3], const int32_t x[3], const int32_t y[3]) {
    (void)y;
    (void)x;
    (void)h;
    (void)pc;
    (void)id;
}

int gl_renderer_fit_wide_aspect(int disp_w, int *num, int *den) {
    (void)den;
    (void)num;
    (void)disp_w;
    return 0;  /* in and out: our wide surfaces hold every offered aspect, zeroing it made the aspect 0 */
}

uint64_t gl_renderer_fmv_chroma_frames(void) {
    return 0;
}

int gl_renderer_fmv_chroma_smoothing(void) {
    return 0;
}

int gl_renderer_fmv_hd_debug_json(char *out, size_t capacity) {
    (void)capacity;
    (void)out;
    if (out) out[0] = 0;
    return 0;
}

void gl_renderer_forget_texture_bank_range(uint16_t first, uint32_t count) {
    (void)count;
    (void)first;
}

void gl_renderer_frame_gen_configure(double refresh_hz, double guest_hz) {
    (void)guest_hz;
    (void)refresh_hz;
}

void gl_renderer_frame_gen_costs(uint64_t *generated, uint64_t *measured, uint64_t *cost_ns) {
    (void)cost_ns;
    (void)measured;
    (void)generated;
    if (generated) memset(generated, 0, sizeof *generated);
    if (measured) memset(measured, 0, sizeof *measured);
    if (cost_ns) memset(cost_ns, 0, sizeof *cost_ns);
}

void gl_renderer_frame_gen_counts(uint64_t *generated, uint64_t *real_presents) {
    (void)real_presents;
    (void)generated;
    if (generated) memset(generated, 0, sizeof *generated);
    if (real_presents) memset(real_presents, 0, sizeof *real_presents);
}

void gl_renderer_frame_gen_hold(GlFgHold kind, double secs) {
    (void)secs;
    (void)kind;
}

int gl_renderer_frame_gen_json(char *out, int cap) {
    (void)cap;
    (void)out;
    if (out) out[0] = 0;
    return 0;
}

int gl_renderer_frame_generation(void) {
    return 0;
}

int gl_renderer_get_pgxp_depth(void) {
    return 0;
}

int gl_renderer_get_texture_window_batching(void) {
    return 0;
}

int gl_renderer_hd_texture_cached(uint64_t key, uint64_t generation, uint32_t* width, uint32_t* height) {
    (void)height;
    (void)width;
    (void)generation;
    (void)key;
    if (width) memset(width, 0, sizeof *width);
    if (height) memset(height, 0, sizeof *height);
    return 0;
}

void gl_renderer_host_ledger(GlHostLedger *out) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

int gl_renderer_internal_scale_live_capable(void) {
    return gr_backend() == GR_BACKEND_OPENGL;
}

/* Our in-between frames never pace the guest: answering 1 turned the wall pacer off and the game ran fast. */
int gl_renderer_interpolation_owns_cadence(void) {
    return 0;
}

void gl_renderer_interpolation_source_diag(int *source, uint32_t *flip_period, uint64_t *captures, uint64_t *duplicates) {
    (void)duplicates;
    (void)captures;
    (void)flip_period;
    (void)source;
    if (source) memset(source, 0, sizeof *source);
    if (flip_period) memset(flip_period, 0, sizeof *flip_period);
    if (captures) memset(captures, 0, sizeof *captures);
    if (duplicates) memset(duplicates, 0, sizeof *duplicates);
}

int gl_renderer_local_pair_begin(int x, int y, int w, int h, int reuse_backup) {
    (void)reuse_backup;
    (void)h;
    (void)w;
    (void)y;
    (void)x;
    return 0;
}

int gl_renderer_local_pair_set_presentation(int enabled) {
    (void)enabled;
    return 0;
}

uint32_t gl_renderer_local_pair_unavailable(void) {
    return 1;
}

int gl_renderer_local_view_begin(int x, int y, int w, int h) {
    (void)h;
    (void)w;
    (void)y;
    (void)x;
    return 0;
}

int gl_renderer_local_view_end(int keep) {
    (void)keep;
    return 0;
}

uint32_t gl_renderer_local_view_unavailable(void) {
    return 1;
}

void gl_renderer_note_present_depth24(int d24) {
    (void)d24;
}

void gl_renderer_note_wide_triangle_recovery(int enabled) {
    (void)enabled;
}

uint64_t gl_renderer_pass_backups_reused(void) {
    return 0;
}

int gl_renderer_pass_begin(int x, int y, int w, int h, int open_gen, uint32_t period_vblanks, int reuse_backup) {
    (void)reuse_backup;
    (void)period_vblanks;
    (void)open_gen;
    (void)h;
    (void)w;
    (void)y;
    (void)x;
    return 0;
}

void gl_renderer_pass_begin_diag(GLRenderPassBeginDiag *out) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

void gl_renderer_pass_diag(uint64_t out[10]) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

void gl_renderer_pass_dump_arm(const char *dir, int generations) {
    (void)generations;
    (void)dir;
}

void gl_renderer_pass_end(uint32_t alpha_q16, int keep) {
    (void)keep;
    (void)alpha_q16;
}

void gl_renderer_pass_force_refuse(int on) {
    (void)on;
}

uint32_t gl_renderer_pass_image_textures(uint64_t *bytes) {
    (void)bytes;
    if (bytes) memset(bytes, 0, sizeof *bytes);
    return 0;
}

uint64_t gl_renderer_pass_journaled(void) {
    return 0;
}

uint32_t gl_renderer_pass_leaks(void) {
    return 0;
}

void gl_renderer_pass_note_cost(uint64_t ticks) {
    (void)ticks;
}

uint32_t gl_renderer_pass_plan(uint32_t period_vblanks, uint32_t shown_after_vblanks, uint32_t *alpha_q16, uint32_t max, uint32_t *wanted) {
    (void)wanted;
    (void)max;
    (void)alpha_q16;
    (void)shown_after_vblanks;
    (void)period_vblanks;
    if (alpha_q16) memset(alpha_q16, 0, sizeof *alpha_q16);
    if (wanted) memset(wanted, 0, sizeof *wanted);
    return 0;
}

void gl_renderer_pass_plan_diag(GLRenderPassPlanDiag *out) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

void gl_renderer_pass_service_presents(void) {
}

void gl_renderer_pass_set_flip_shown(int shown) {
    (void)shown;
}

uint32_t gl_renderer_pass_unavailable(void) {
    return 1;
}

int gl_renderer_pass_verify_vram(void) {
    return 0;
}

uint64_t gl_renderer_perf_frequency(void) {
    return (uint64_t)SDL_GetPerformanceFrequency();
}

uint64_t gl_renderer_perf_ticks(void) {
    return (uint64_t)SDL_GetPerformanceCounter();
}

void gl_renderer_pgxp_depth_clear_reasons(uint64_t out[5]) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

void gl_renderer_pgxp_render_stats(uint64_t *depth_tris, uint64_t *depth_clears, uint64_t *seam_tris) {
    (void)seam_tris;
    (void)depth_clears;
    (void)depth_tris;
    if (depth_tris) memset(depth_tris, 0, sizeof *depth_tris);
    if (depth_clears) memset(depth_clears, 0, sizeof *depth_clears);
    if (seam_tris) memset(seam_tris, 0, sizeof *seam_tris);
}

int gl_renderer_pgxp_render_wanted(void) {
    return 0;
}

int gl_renderer_post_aa(void) {
    return 0;
}

double gl_renderer_post_aa_gpu_us(void) {
    return 0.0;
}

uint64_t gl_renderer_post_aa_passes(void) {
    return 0;
}

int gl_renderer_post_shader_debug(char *out, int cap) {
    (void)cap;
    (void)out;
    if (out) out[0] = 0;
    return 0;
}

int gl_renderer_post_shader_loaded(void) {
    return 0;
}

int gl_renderer_present_hold_last(void) {
    return 0;
}

void gl_renderer_present_probe_reset(void) {
}

void gl_renderer_present_probe_take(uint64_t *skip_delta, uint64_t *swap_delta, uint64_t *dirty_mark_delta, int *force_remaining) {
    (void)force_remaining;
    (void)dirty_mark_delta;
    (void)swap_delta;
    (void)skip_delta;
    if (skip_delta) memset(skip_delta, 0, sizeof *skip_delta);
    if (swap_delta) memset(swap_delta, 0, sizeof *swap_delta);
    if (dirty_mark_delta) memset(dirty_mark_delta, 0, sizeof *dirty_mark_delta);
    if (force_remaining) memset(force_remaining, 0, sizeof *force_remaining);
}

int gl_renderer_present_rect_dirty(int disp_x, int disp_y, int w, int h) {
    (void)h;
    (void)w;
    (void)disp_y;
    (void)disp_x;
    return 0;
}

int gl_renderer_projective_supported(void) {
    return 0;
}

int gl_renderer_read_display_hires(int x, int y, int w, int h, uint32_t *out, int cap_px, int *ow, int *oh) {
    (void)oh;
    (void)ow;
    (void)cap_px;
    (void)out;
    (void)h;
    (void)w;
    (void)y;
    (void)x;
    if (out) memset(out, 0, sizeof *out);
    if (ow) memset(ow, 0, sizeof *ow);
    if (oh) memset(oh, 0, sizeof *oh);
    return 0;
}

void gl_renderer_render_thread_costs(GlRthCosts *out) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

int gl_renderer_render_thread_json(char *out, size_t cap) {
    (void)cap;
    (void)out;
    if (out) out[0] = 0;
    return 0;
}

void gl_renderer_render_thread_measure(int on) {
    (void)on;
}

void gl_renderer_restage_vram_after_savestate(void) {
    if (gr_backend() != GR_BACKEND_OPENGL) return;
    gr_vram_transfer_in(0, 0, 1024, 512, gpu_get_vram());
    gl_renderer_invalidate_present();
}

int gl_renderer_scale_info(GlScaleInfo *out) {
    if (out) memset(out, 0, sizeof *out);
    return 0;
}

void gl_renderer_scene_prim_counts(uint64_t *prims, uint64_t *textured) {
    (void)textured;
    (void)prims;
    if (prims) memset(prims, 0, sizeof *prims);
    if (textured) memset(textured, 0, sizeof *textured);
}

int gl_renderer_select_texture_bank(uint16_t id) {
    (void)id;
    return 0;
}

int gl_renderer_select_texture_bank_live_clut(uint16_t id) {
    (void)id;
    return 0;
}

void gl_renderer_set_accurate_blending(int on) {
    (void)on;
}

int gl_renderer_set_bezel(const void *rgba, int w, int h) {
    (void)h;
    (void)w;
    (void)rgba;
    return 0;
}

void gl_renderer_set_bloom(float strength) {
    (void)strength;
}

void gl_renderer_set_cpu_auth_dual(int on) {
    (void)on;
}

void gl_renderer_set_dithering(int mode) {
    (void)mode;
}

void gl_renderer_set_dynamic_resolution(int on) {
    (void)on;
}

void gl_renderer_set_fmv_chroma_smoothing(int on) {
    (void)on;
}

void gl_renderer_set_fmv_filter(int cfg_value) {
    (void)cfg_value;
}

void gl_renderer_set_frame_generation(int on) {
    (void)on;
}

void gl_renderer_set_frame_generation_method(int method) {
    (void)method;
}

void gl_renderer_set_hd_texture_mode(int enabled) {
    (void)enabled;
}

int gl_renderer_set_internal_scale_live(int scale) {
    if (scale < 1) return 0;
    gr_set_scale(scale);
    gl_renderer_invalidate_present();
    return gr_scale();
}

void gl_renderer_set_interpolation_blend(int blend_mode) {
    (void)blend_mode;
}

void gl_renderer_set_interpolation_source(int source) {
    (void)source;
}

void gl_renderer_set_pgxp_color_perspective(int on) {
    (void)on;
}

void gl_renderer_set_pgxp_depth(int on) {
    (void)on;
}

void gl_renderer_set_pgxp_depth_threshold(float sz) {
    (void)sz;
}

void gl_renderer_set_pgxp_seam(int mode) {
    (void)mode;
}

int gl_renderer_set_post_aa(int mode) {
    (void)mode;
    return 0;
}

void gl_renderer_set_post_shader(const char *path) {
    (void)path;
}

void gl_renderer_set_present_thread(int on, int slots) {
    (void)slots;
    (void)on;
}

void gl_renderer_set_scanlines(int on, float strength) {
    (void)strength;
    (void)on;
}

void gl_renderer_set_texture_lod(int mode, int aniso) {
    (void)aniso;
    (void)mode;
}

void gl_renderer_set_texture_window_batching(int on) {
    (void)on;
}

int gl_renderer_step_internal_scale_now(int scale) {
    if (scale < 1) return 0;
    gr_set_scale(scale);
    gl_renderer_invalidate_present();
    return gr_scale();
}

int gl_renderer_stereo_begin(int x, int y, int w, int h, int reuse_backup) {
    (void)reuse_backup;
    (void)h;
    (void)w;
    (void)y;
    (void)x;
    return 0;
}

uint32_t gl_renderer_stereo_capture_records(GLRenderStereoCapture *out, uint32_t capacity) {
    (void)capacity;
    (void)out;
    if (out) memset(out, 0, sizeof *out);
    return 0;
}

void gl_renderer_stereo_diag(GLRenderStereoDiag *out) {
    (void)out;
    if (out) memset(out, 0, sizeof *out);
}

void gl_renderer_stereo_dump_arm(const char *dir, int pairs) {
    (void)pairs;
    (void)dir;
}

int gl_renderer_stereo_end(uint32_t eye, int keep) {
    (void)keep;
    (void)eye;
    return 0;
}

int gl_renderer_stereo_publish(uint64_t pair_id, uint64_t guest_cycle, const int32_t eye_view[2][3]) {
    (void)eye_view;
    (void)guest_cycle;
    (void)pair_id;
    return 0;
}

void gl_renderer_stereo_reset(void) {
}

int gl_renderer_stereo_set_presentation(uint32_t mode) {
    (void)mode;
    return 0;
}

int gl_renderer_stereo_stage_readback_pair(uint32_t width,uint32_t height,uint8_t *rgba) {
    (void)rgba;
    (void)height;
    (void)width;
    if (rgba) memset(rgba, 0, sizeof *rgba);
    return 0;
}

void gl_renderer_stereo_stage_reset(void) {
}

uint32_t gl_renderer_stereo_unavailable(void) {
    return 1;
}

void gl_renderer_swap_timing_json(char *out, int cap) {
    (void)cap;
    (void)out;
    if (out) out[0] = 0;
}

int gl_renderer_texture_banks_supported(void) {
    return 0;
}

int gl_renderer_texture_lod(void) {
    return 0;
}

int gl_renderer_vendor_is_nvidia(void) {
    return 0;
}

uint64_t gl_renderer_wide_triangle_recovery_count(void) {
    return 0;
}

int psx_mod_openxr_enable(int enabled) {
    (void)enabled;
    return 0;
}

void psx_mod_openxr_recenter(void) {
}
