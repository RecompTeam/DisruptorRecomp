/* The render thread (render_thread.c, after upstream psxrecomp's docs/RENDER_THREAD.md) for this renderer: while it runs the
 * emulation thread records the backend's calls and the per-frame setters, and the render thread, which owns the main context,
 * replays them in order. Whatever else reaches the renderer takes the context back first (GL_RT_SYNC) and runs as before. */


#define RTHF_PRIM    0x0001u
#define RTHF_TAGGED  0x0002u
#define RTHF_BD_SHIFT 2
#define RTHF_FLAT    0x0010u
#define RTHF_SKY     0x0020u

static uint16_t *s_rth_vram_pub = NULL, *s_rth_vram_priv = NULL;
static int s_rthm_area[4] = {0, 0, VRAM_W - 1, VRAM_H - 1};
static uint64_t s_rth_syncs = 0, s_rth_frames = 0;
static int s_rth_primsync_due = 0;
static const char *s_rth_last_sync = "";
/* The thread that started the render thread records: its start number in a thread-local, read on every record. */
static unsigned s_rth_start = 0;
#if defined(_MSC_VER)
static __declspec(thread) unsigned t_rth_start;
#else
static __thread unsigned t_rth_start;
#endif
static int rth_on_emu_thread(void) { return t_rth_start == s_rth_start; }

/* Backend calls are appended into one ring record (a group) until it is full or something else needs the ring. */
#define RTH_GROUP_BYTES 4096u
static uint8_t *s_rth_group = NULL;
static uint32_t s_rth_group_used = 0;
static void rth_group_close(void) {
    if (!s_rth_group) return;
    s_rth_group = NULL;
    rt_cmd_commit_bytes(s_rth_group_used);
}

static int ctx_prim_tagged(void) { return rth_replaying() ? s_rthx_tagged : psx_ws_prim_is_tagged(); }
static int ctx_prim_in_backdrop(void) { return rth_replaying() ? s_rthx_backdrop : psx_ws_prim_in_backdrop(); }
static int ctx_drawing_flat(void) { return rth_replaying() ? s_rthx_flat : gpu_ws_drawing_flat(); }
static int ctx_packet_fade(void) { return rth_replaying() ? s_rthx_fade : gpu_current_packet_fade(); }
static int ctx_hud_glyph(void) { return rth_replaying() ? s_rthx_glyph : gpu_current_hud_glyph(); }
static int ctx_sky_fogged(void) { return rth_replaying() ? s_rthx_sky : gpu_sky_fogged(); }
static int ctx_depth24(void) { return rth_replaying() ? 0 : gpu_display_is_depth24(); }
static void ctx_x_squash(int32_t *num, int32_t *den) {
    if (rth_replaying()) { *num = s_rthx_squash[0]; *den = s_rthx_squash[1]; return; }
    gte_ws_x_squash(num, den);
}

static int gl_rth_eligible(void) {
    extern int psx_netplay_active(void);
    return s_raster_ok && s_ctx && !gpu_display_is_depth24() && !psx_netplay_active() &&
           !psx_host_ui_gl_visible() && !s_ddgi_light && !s_ddgi_shadows && !s_ddgi_lamps && !ddgi_sun_active();
}

static void gl_rth_acquire(const char *reason) {
    if (!s_rth_on || !rth_on_emu_thread() || rt_held()) return;
    rth_group_close();
    rt_acquire(reason);
    /* The replayed uploads, a save state's whole VRAM among them, reached only the render thread's copy. */
    memcpy(s_rth_vram_pub, s_rth_vram_priv, (size_t)VRAM_W * VRAM_H * sizeof(uint16_t));
    s_vram = s_rth_vram_pub;
    sw_renderer_rebind_vram(s_rth_vram_pub);
    s_rth_syncs++;
    s_rth_last_sync = reason;
}

static void gl_rth_release(void) {
    if (!s_rth_on || !rt_held()) return;
    memcpy(s_rth_vram_priv, s_rth_vram_pub, (size_t)VRAM_W * VRAM_H * sizeof(uint16_t));
    s_vram = s_rth_vram_priv;
    sw_renderer_rebind_vram(s_rth_vram_priv);
    sw_get_draw_area(&s_rthm_area[0], &s_rthm_area[1], &s_rthm_area[2], &s_rthm_area[3]);
    rt_release();
}

/* Record (1) or run directly (0): directly with no render thread, on it, or while the emulation thread holds the context. */
static int rth_record_mode(void) {
    if (!s_rth_on || !rth_on_emu_thread() || rt_held()) return 0;
    if (gpu_display_is_depth24()) { gl_rth_acquire("depth24"); return 0; }
    return 1;
}

static int rth_put(uint16_t op, uint16_t flags, const void *payload, uint32_t bytes) {
    const uint32_t need = (uint32_t)sizeof(RtCmd) + ((bytes + (RT_ALIGN - 1u)) & ~(RT_ALIGN - 1u));
    if (need > RTH_GROUP_BYTES / 2u) {
        rth_group_close();
        void *p = rt_cmd_begin(op, flags, bytes);
        if (!p) { gl_rth_acquire("oversize"); return 0; }
        if (bytes) memcpy(p, payload, bytes);
        rt_cmd_commit();
        return 1;
    }
    if (s_rth_group && s_rth_group_used + need > RTH_GROUP_BYTES) rth_group_close();
    if (!s_rth_group) {
        s_rth_group = (uint8_t *)rt_cmd_begin(RTH_GROUP, 0, RTH_GROUP_BYTES);
        if (!s_rth_group) { gl_rth_acquire("oversize"); return 0; }
        s_rth_group_used = 0;
    }
    RtCmd *sub = (RtCmd *)(s_rth_group + s_rth_group_used);
    sub->size = need;
    sub->op = op;
    sub->flags = flags;
    sub->payload = bytes;
    sub->seq = 0;
    if (bytes) memcpy(sub + 1, payload, bytes);
    s_rth_group_used += need;
    return 1;
}
#define RTH_REC(op, flags, ...) do { const int32_t rth_v_[] = { __VA_ARGS__ }; \
    if (rth_put((op), (flags), rth_v_, (uint32_t)sizeof rth_v_)) return; } while (0)
static int32_t rth_f(float value) { int32_t bits; memcpy(&bits, &value, sizeof bits); return bits; }
static float rth_float(int32_t bits) { float value; memcpy(&value, &bits, sizeof value); return value; }

/* The guest's per-primitive tags the backend reads while drawing, taken as the primitive is recorded. */
static uint16_t rth_prim_flags(void) {
    uint16_t flags = RTHF_PRIM;
    {   /* PSX_RTH_PRIMSYNC=N: a sync at the next shaded textured triangle after N recorded primitives */
        static int n = 0, every = -1;
        if (every < 0) every = getenv("PSX_RTH_PRIMSYNC") ? atoi(getenv("PSX_RTH_PRIMSYNC")) : 0;
        if (every > 0 && ++n % every == 0) s_rth_primsync_due = 1;
    }
    if (psx_ws_prim_is_tagged()) flags |= RTHF_TAGGED;
    flags |= (uint16_t)((psx_ws_prim_in_backdrop() & 3) << RTHF_BD_SHIFT);
    if (gpu_ws_drawing_flat()) flags |= RTHF_FLAT;
    if (gpu_sky_fogged()) flags |= RTHF_SKY;
    return flags;
}

/* ---- the recording backend ------------------------------------------------ */
static void rtb_init(uint16_t *vram) { GL_RT_SYNC("init"); glb_init(vram); }
static void rtb_set_scale(int s) { GL_RT_SYNC("set_scale"); glb_set_scale(s); }
static int rtb_scale(void) { return glb_scale(); }
static void rtb_set_texture_filter(int b) { if (rth_record_mode()) RTH_REC(RTH_TEXTURE_FILTER, 0, b); glb_set_texture_filter(b); }
static int rtb_texture_filter(void) { GL_RT_SYNC("texture_filter"); return glb_texture_filter(); }
static void rtb_set_semi_transparency(int e, int m) { if (rth_record_mode()) RTH_REC(RTH_SEMI, 0, e, m); glb_set_semi_transparency(e, m); }
static void rtb_set_mask_bits(int s, int c) { if (rth_record_mode()) RTH_REC(RTH_MASK, 0, s, c); glb_set_mask_bits(s, c); }
static void rtb_set_texture_window(uint32_t r) { if (rth_record_mode()) RTH_REC(RTH_TWIN, 0, (int32_t)r); glb_set_texture_window(r); }
static void rtb_set_color_modulation(int r, int g, int b, int raw) {
    if (rth_record_mode()) RTH_REC(RTH_MOD, 0, r, g, b, raw);
    glb_set_color_modulation(r, g, b, raw);
}
static void rtb_set_geometry_correction(int enabled) {
    if (rth_record_mode()) RTH_REC(RTH_GEOMETRY, 0, enabled);
    glb_set_geometry_correction(enabled);
}
static void rtb_set_world_triangle(int world) { if (rth_record_mode()) RTH_REC(RTH_WORLD, 0, world); glb_set_world_triangle(world); }
static void rtb_set_sky_cells(int rows, int columns, int tall, int period, int focal, const int32_t *cells, int irregular) {
    if (rth_record_mode()) {
        int32_t payload[6 + 2 * SKY_CELLS] = {rows, columns, tall, period, focal, irregular};
        const int count = rows > 0 && columns > 0 && rows * columns <= SKY_CELLS ? rows * columns : 0;
        if (cells && count) memcpy(payload + 6, cells, (size_t)count * 2 * sizeof(int32_t));
        if (rth_put(RTH_SKY_CELLS, 0, payload, sizeof payload)) return;
    }
    glb_set_sky_cells(rows, columns, tall, period, focal, cells, irregular);
}
static void rtb_set_sky_extension(int width, int rows, int band, const uint8_t *indices) {
    if (rth_record_mode()) {
        const size_t count = width > 0 && rows > 0 && indices ? (size_t)width * (size_t)rows : 0;
        uint8_t *payload = (uint8_t *)malloc(12 + count);
        if (payload) {
            const int32_t head[3] = {count ? width : 0, count ? rows : 0, band};
            memcpy(payload, head, sizeof head);
            if (count) memcpy(payload + 12, indices, count);
            const int put = rth_put(RTH_SKY_EXTENSION, 0, payload, (uint32_t)(12 + count));
            free(payload);
            if (put) return;
        }
    }
    glb_set_sky_extension(width, rows, band, indices);
}
static void rtb_set_sky_column(float column) {
    if (rth_record_mode()) {
        int32_t bits;
        memcpy(&bits, &column, sizeof bits);
        RTH_REC(RTH_SKY_COLUMN, 0, bits);
    }
    glb_set_sky_column(column);
}
static void rtb_set_sky_look(int size_per_mille, int smooth) {
    if (rth_record_mode()) RTH_REC(RTH_SKY_LOOK, 0, size_per_mille, smooth);
    glb_set_sky_look(size_per_mille, smooth);
}
static void rtb_set_column_band(int low, int high, int canonical) {
    if (rth_record_mode()) RTH_REC(RTH_COLUMN_BAND, 0, low, high, canonical);
    glb_set_column_band(low, high, canonical);
}
static void rtb_set_precise_triangle(int enabled, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (rth_record_mode()) RTH_REC(RTH_PRECISE, 0, enabled, x0, y0, x1, y1, x2, y2);
    glb_set_precise_triangle(enabled, x0, y0, x1, y1, x2, y2);
}
static void rtb_set_unpinned_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    if (rth_record_mode()) RTH_REC(RTH_UNPINNED, 0, x0, y0, x1, y1, x2, y2);
    glb_set_unpinned_triangle(x0, y0, x1, y1, x2, y2);
}
static void rtb_set_perspective_triangle(int enabled, float q0, float q1, float q2) {
    if (rth_record_mode()) RTH_REC(RTH_PERSP, 0, enabled, rth_f(q0), rth_f(q1), rth_f(q2));
    glb_set_perspective_triangle(enabled, q0, q1, q2);
}
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
static void rtb_set_temporal_depth_triangle(int enabled, float z0, float z1, float z2) {
    if (rth_record_mode()) RTH_REC(RTH_TEMPORAL_DEPTH, 0, enabled, rth_f(z0), rth_f(z1), rth_f(z2));
    glb_set_temporal_depth_triangle(enabled, z0, z1, z2);
}
static void rtb_set_surface_depth_triangle(float z0, float z1, float z2) {
    if (rth_record_mode()) RTH_REC(RTH_SURFACE_DEPTH, 0, rth_f(z0), rth_f(z1), rth_f(z2));
    glb_set_surface_depth_triangle(z0, z1, z2);
}
static void rtb_set_temporal_identity_triangle(uint64_t id0, uint64_t id1, uint64_t id2) {
    if (rth_record_mode()) {
        const uint64_t ids[3] = {id0, id1, id2};
        if (rth_put(RTH_IDENTITY, 0, ids, sizeof ids)) return;
    }
    glb_set_temporal_identity_triangle(id0, id1, id2);
}
static void rtb_set_temporal_sprite(float depth, const float sides[4]) {
    if (rth_record_mode()) {
        float v[6] = {depth, 0.0f, 0.0f, 0.0f, 0.0f, sides ? 1.0f : 0.0f};
        if (sides) memcpy(&v[1], sides, 4 * sizeof(float));
        if (rth_put(RTH_SPRITE, 0, v, sizeof v)) return;
    }
    glb_set_temporal_sprite(depth, sides);
}
static void rtb_set_temporal_sprite_ground(float ground, float reach) {
    const float v[2] = {ground, reach};
    if (rth_record_mode() && rth_put(RTH_SPRITE_GROUND, 0, v, sizeof v)) return;
    glb_set_temporal_sprite_ground(ground, reach);
}
static void rtb_set_sprite_card(const float card[8]) {
    if (rth_record_mode()) {
        float v[9] = {0};
        if (card) memcpy(v, card, 8 * sizeof(float));
        v[8] = card ? 1.0f : 0.0f;
        if (rth_put(RTH_SPRITE_CARD, 0, v, sizeof v)) return;
    }
    glb_set_sprite_card(card);
}
#endif
static void rtb_set_screen_tile(const PsxWsPlacedTile *tile) {
    if (rth_record_mode()) {
        PsxWsPlacedTile copy;
        memset(&copy, 0, sizeof copy);
        if (tile) copy = *tile;
        const int32_t has = tile != NULL;
        uint8_t payload[sizeof(int32_t) + sizeof(PsxWsPlacedTile)];
        memcpy(payload, &has, sizeof has);
        memcpy(payload + sizeof has, &copy, sizeof copy);
        if (rth_put(RTH_TILE, 0, payload, sizeof payload)) return;
    }
    glb_set_screen_tile(tile);
}
static void rtb_set_flat_sides(float left, float right) {
    if (rth_record_mode()) RTH_REC(RTH_FLAT_SIDES, 0, rth_f(left), rth_f(right));
    glb_set_flat_sides(left, right);
}
static void rtb_set_near_triangle(const float a[3], const float b[3], const float c[3]) {
    if (rth_record_mode()) {
        float v[9];
        memcpy(&v[0], a, 3 * sizeof(float));
        memcpy(&v[3], b, 3 * sizeof(float));
        memcpy(&v[6], c, 3 * sizeof(float));
        if (rth_put(RTH_NEAR, 0, v, sizeof v)) return;
    }
    glb_set_near_triangle(a, b, c);
}
static void rtb_set_presentation_yaw(double yaw_units, double full_turn, double center_x, double center_y, double focal_x) {
    if (rth_record_mode()) {
        const double v[5] = {yaw_units, full_turn, center_x, center_y, focal_x};
        if (rth_put(RTH_YAW, 0, v, sizeof v)) return;
    }
    glb_set_presentation_yaw(yaw_units, full_turn, center_x, center_y, focal_x);
}
static void rtb_fill_rect(int x, int y, int w, int h, uint16_t c) {
    if (rth_record_mode()) RTH_REC(RTH_FILL, rth_prim_flags(), x, y, w, h, c);
    glb_fill_rect(x, y, w, h, c);
}
static void rtb_copy_rect(int sx, int sy, int dx, int dy, int w, int h) {
    if (rth_record_mode()) RTH_REC(RTH_COPY, rth_prim_flags(), sx, sy, dx, dy, w, h);
    glb_copy_rect(sx, sy, dx, dy, w, h);
}
static void rtb_draw_flat_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col) {
    if (rth_record_mode()) RTH_REC(RTH_FLAT_TRI, rth_prim_flags(), x0, y0, x1, y1, x2, y2, col);
    glb_draw_flat_triangle(x0, y0, x1, y1, x2, y2, col);
}
static void rtb_draw_gouraud_triangle(int x0, int y0, uint16_t c0, int x1, int y1, uint16_t c1, int x2, int y2, uint16_t c2) {
    if (rth_record_mode()) RTH_REC(RTH_GOURAUD_TRI, rth_prim_flags(), x0, y0, c0, x1, y1, c1, x2, y2, c2);
    glb_draw_gouraud_triangle(x0, y0, c0, x1, y1, c1, x2, y2, c2);
}
static void rtb_draw_textured_triangle(int x0, int y0, int u0, int v0, int x1, int y1, int u1, int v1, int x2, int y2, int u2,
                                       int v2, uint16_t cx, uint16_t cy, uint16_t tp) {
    if (rth_record_mode())
        RTH_REC(RTH_TEX_TRI, rth_prim_flags(), x0, y0, u0, v0, x1, y1, u1, v1, x2, y2, u2, v2, cx, cy, tp,
                gpu_current_packet_fade(), gpu_current_hud_glyph());
    glb_draw_textured_triangle(x0, y0, u0, v0, x1, y1, u1, v1, x2, y2, u2, v2, cx, cy, tp);
}
static void rth_primsync(void) {
    if (!s_rth_primsync_due) return;
    s_rth_primsync_due = 0;
    GL_RT_SYNC("primsync");
    uint16_t px[4];
    glb_vram_transfer_out(0, 0, 2, 2, px);
}
static void rtb_draw_shaded_textured_triangle(int x0, int y0, int u0, int v0, uint32_t c0, int x1, int y1, int u1, int v1,
                                              uint32_t c1, int x2, int y2, int u2, int v2, uint32_t c2, uint16_t cx,
                                              uint16_t cy, uint16_t tp, int raw) {
    rth_primsync();
    if (rth_record_mode())
        RTH_REC(RTH_SHADED_TEX_TRI, rth_prim_flags(), x0, y0, u0, v0, (int32_t)c0, x1, y1, u1, v1, (int32_t)c1, x2, y2, u2,
                v2, (int32_t)c2, cx, cy, tp, raw, gpu_current_packet_fade(), gpu_current_hud_glyph());
    glb_draw_shaded_textured_triangle(x0, y0, u0, v0, c0, x1, y1, u1, v1, c1, x2, y2, u2, v2, c2, cx, cy, tp, raw);
}
static void rtb_draw_flat_rect(int x, int y, int w, int h, uint16_t c) {
    if (rth_record_mode()) RTH_REC(RTH_FLAT_RECT, rth_prim_flags(), x, y, w, h, c);
    glb_draw_flat_rect(x, y, w, h, c);
}
static void rtb_draw_textured_rect(int x, int y, int w, int h, int u, int v, uint16_t cx, uint16_t cy, uint16_t tp) {
    if (rth_record_mode())
        RTH_REC(RTH_TEX_RECT, rth_prim_flags(), x, y, w, h, u, v, cx, cy, tp, gpu_current_packet_fade(), gpu_current_hud_glyph());
    glb_draw_textured_rect(x, y, w, h, u, v, cx, cy, tp);
}
static void rtb_draw_textured_rect_scaled(int x, int y, int w, int h, int u0, int v0, int u1, int v1, uint16_t cx, uint16_t cy,
                                          uint16_t tp) {
    if (rth_record_mode())
        RTH_REC(RTH_TEX_RECT_SCALED, rth_prim_flags(), x, y, w, h, u0, v0, u1, v1, cx, cy, tp, gpu_current_packet_fade(),
                gpu_current_hud_glyph());
    glb_draw_textured_rect_scaled(x, y, w, h, u0, v0, u1, v1, cx, cy, tp);
}
static void rtb_draw_line(int x0, int y0, int x1, int y1, uint16_t c) {
    if (rth_record_mode()) RTH_REC(RTH_LINE, rth_prim_flags(), x0, y0, x1, y1, c);
    glb_draw_line(x0, y0, x1, y1, c);
}
static void rtb_draw_shaded_line(int x0, int y0, uint16_t c0, int x1, int y1, uint16_t c1) {
    if (rth_record_mode()) RTH_REC(RTH_SHADED_LINE, rth_prim_flags(), x0, y0, c0, x1, y1, c1);
    glb_draw_shaded_line(x0, y0, c0, x1, y1, c1);
}
static int rtb_render_display(uint32_t *o, int p, int dx, int dy, int dw, int dh) {
    GL_RT_SYNC("render_display");
    return glb_render_display(o, p, dx, dy, dw, dh);
}
static int rtb_render_display_hires(uint32_t *o, int p, int dx, int dy, int dw, int dh) {
    GL_RT_SYNC("render_display_hires");
    return glb_render_display_hires(o, p, dx, dy, dw, dh);
}
static void rtb_vram_write(int x, int y, uint16_t px) { if (rth_record_mode()) RTH_REC(RTH_VRAM_WRITE, 0, x, y, px); glb_vram_write(x, y, px); }
static uint16_t rtb_vram_read(int x, int y) { GL_RT_SYNC("vram_read"); return glb_vram_read(x, y); }
static void rtb_vram_transfer_in(int x, int y, int w, int h, const uint16_t *d) {
    if (rth_record_mode()) {
        const size_t n = w > 0 && h > 0 ? (size_t)w * (size_t)h : 0;
        rth_group_close();
        int32_t *p = (int32_t *)rt_cmd_begin(RTH_XFER_IN, 0, (uint32_t)(16u + n * 2u));
        if (p) {
            p[0] = x; p[1] = y; p[2] = w; p[3] = h;
            if (n) memcpy(p + 4, d, n * 2u);
            rt_cmd_commit();
            return;
        }
        gl_rth_acquire("oversize_upload");
    }
    glb_vram_transfer_in(x, y, w, h, d);
}
static void rtb_vram_transfer_out(int x, int y, int w, int h, uint16_t *d) {
    GL_RT_SYNC("vram_transfer_out");
    glb_vram_transfer_out(x, y, w, h, d);
}
static void rtb_set_draw_area(int x1, int y1, int x2, int y2) {
    if (rth_record_mode()) {
        s_rthm_area[0] = x1; s_rthm_area[1] = y1; s_rthm_area[2] = x2; s_rthm_area[3] = y2;
        RTH_REC(RTH_AREA, 0, x1, y1, x2, y2);
    }
    glb_set_draw_area(x1, y1, x2, y2);
}
static void rtb_get_draw_area(int *x1, int *y1, int *x2, int *y2) {
    if (s_rth_on && !rt_on_render_thread() && !rt_held()) {
        *x1 = s_rthm_area[0]; *y1 = s_rthm_area[1]; *x2 = s_rthm_area[2]; *y2 = s_rthm_area[3];
        return;
    }
    glb_get_draw_area(x1, y1, x2, y2);
}
static void rtb_set_draw_offset(int x, int y) { if (rth_record_mode()) RTH_REC(RTH_OFFSET, 0, x, y); glb_set_draw_offset(x, y); }
static void rtb_wide_configure(int wide_w, int offset) {
    if (rth_record_mode()) RTH_REC(RTH_WIDE_CONFIGURE, 0, wide_w, offset);
    glb_wide_configure(wide_w, offset);
}
static void rtb_wide_set_target(int base_x) { if (rth_record_mode()) RTH_REC(RTH_WIDE_TARGET, 0, base_x); glb_wide_set_target(base_x); }
static void rtb_wide_disable_target(void) { if (rth_record_mode()) RTH_REC(RTH_WIDE_DISABLE, 0, 0); glb_wide_disable_target(); }
static void rtb_wide_clear(int base_x, int y, int h, uint16_t color) {
    if (rth_record_mode()) RTH_REC(RTH_WIDE_CLEAR, 0, base_x, y, h, color);
    glb_wide_clear(base_x, y, h, color);
}
static void rtb_wide_clear_margins(int base_x, int y, int h, uint16_t color, int sides) {
    if (rth_record_mode()) RTH_REC(RTH_WIDE_CLEAR_MARGINS, 0, base_x, y, h, color, sides);
    glb_wide_clear_margins(base_x, y, h, color, sides);
}
static void rtb_wide_copy_frame(int src_base_x, int src_y, int dst_base_x, int dst_y, int h) {
    if (rth_record_mode()) RTH_REC(RTH_WIDE_COPY_FRAME, 0, src_base_x, src_y, dst_base_x, dst_y, h);
    glb_wide_copy_frame(src_base_x, src_y, dst_base_x, dst_y, h);
}
static void rtb_wide_copy_rows(int base_x, int src_x, int src_y, int dst_x, int dst_y, int w, int h) {
    if (rth_record_mode()) RTH_REC(RTH_WIDE_COPY_ROWS, 0, base_x, src_x, src_y, dst_x, dst_y, w, h);
    glb_wide_copy_rows(base_x, src_x, src_y, dst_x, dst_y, w, h);
}
static int rtb_render_wide_display(uint32_t *out, int pitch, int base_x, int disp_y, int disp_h) {
    GL_RT_SYNC("render_wide_display");
    return glb_render_wide_display(out, pitch, base_x, disp_y, disp_h);
}
static int rtb_wide_dump_full(uint32_t *out, int cap, int *ow, int *oh, int base_x) {
    GL_RT_SYNC("wide_dump_full");
    return glb_wide_dump_full(out, cap, ow, oh, base_x);
}

static const GpuRenderBackend GL_RT_BACKEND = {
    .name = "opengl",
    .init = rtb_init, .set_scale = rtb_set_scale, .scale = rtb_scale,
    .set_texture_filter = rtb_set_texture_filter, .texture_filter = rtb_texture_filter,
    .set_semi_transparency = rtb_set_semi_transparency, .set_mask_bits = rtb_set_mask_bits,
    .set_texture_window = rtb_set_texture_window, .set_color_modulation = rtb_set_color_modulation,
    .set_geometry_correction = rtb_set_geometry_correction,
    .set_world_triangle = rtb_set_world_triangle,
    .set_column_band = rtb_set_column_band,
    .set_sky_cells = rtb_set_sky_cells,
    .set_sky_extension = rtb_set_sky_extension,
    .set_sky_column = rtb_set_sky_column,
    .set_sky_look = rtb_set_sky_look,
    .set_precise_triangle = rtb_set_precise_triangle,
    .set_unpinned_triangle = rtb_set_unpinned_triangle,
    .set_perspective_triangle = rtb_set_perspective_triangle,
    .set_near_triangle = rtb_set_near_triangle,
    .set_screen_tile = rtb_set_screen_tile,
    .set_flat_sides = rtb_set_flat_sides,
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    .set_temporal_depth_triangle = rtb_set_temporal_depth_triangle,
    .set_surface_depth_triangle = rtb_set_surface_depth_triangle,
    .set_temporal_identity_triangle = rtb_set_temporal_identity_triangle,
    .set_temporal_sprite = rtb_set_temporal_sprite,
    .set_temporal_sprite_ground = rtb_set_temporal_sprite_ground,
    .set_sprite_card = rtb_set_sprite_card,
#else
    .set_temporal_depth_triangle = NULL,
    .set_temporal_identity_triangle = NULL,
#endif
    .set_presentation_yaw = rtb_set_presentation_yaw,
    .fill_rect = rtb_fill_rect, .copy_rect = rtb_copy_rect,
    .draw_flat_triangle = rtb_draw_flat_triangle, .draw_gouraud_triangle = rtb_draw_gouraud_triangle,
    .draw_textured_triangle = rtb_draw_textured_triangle,
    .draw_shaded_textured_triangle = rtb_draw_shaded_textured_triangle,
    .draw_flat_rect = rtb_draw_flat_rect, .draw_textured_rect = rtb_draw_textured_rect,
    .draw_textured_rect_scaled = rtb_draw_textured_rect_scaled,
    .draw_line = rtb_draw_line, .draw_shaded_line = rtb_draw_shaded_line,
    .render_display = rtb_render_display, .render_display_hires = rtb_render_display_hires,
    .vram_write = rtb_vram_write, .vram_read = rtb_vram_read,
    .vram_transfer_in = rtb_vram_transfer_in, .vram_transfer_out = rtb_vram_transfer_out,
    .set_draw_area = rtb_set_draw_area, .get_draw_area = rtb_get_draw_area,
    .set_draw_offset = rtb_set_draw_offset,
    .wide_configure = rtb_wide_configure,
    .wide_set_target = rtb_wide_set_target,
    .wide_disable_target = rtb_wide_disable_target,
    .wide_clear = rtb_wide_clear,
    .wide_clear_margins = rtb_wide_clear_margins,
    .wide_copy_frame = rtb_wide_copy_frame,
    .wide_copy_rows = rtb_wide_copy_rows,
    .render_wide_display = rtb_render_wide_display,
    .wide_dump_full = rtb_wide_dump_full,
};

/* ---- replay on the render thread --------------------------------------------- */
static void rth_prim_begin(uint16_t flags, int fade, int glyph) {
    s_rthx_tagged = (flags & RTHF_TAGGED) != 0;
    s_rthx_backdrop = (flags >> RTHF_BD_SHIFT) & 3;
    s_rthx_flat = (flags & RTHF_FLAT) != 0;
    s_rthx_sky = (flags & RTHF_SKY) != 0;
    s_rthx_fade = fade;
    s_rthx_glyph = glyph;
}

static void gl_rth_exec(void *user, const RtCmd *c, const void *payload) {
    (void)user;
    const int32_t *v = (const int32_t *)payload;
    const float *fv = (const float *)payload;
    if (c->flags & RTHF_PRIM) rth_prim_begin(c->flags, -1, 0);
    switch (c->op) {
    case RTH_TEXTURE_FILTER: glb_set_texture_filter(v[0]); break;
    case RTH_SEMI: glb_set_semi_transparency(v[0], v[1]); break;
    case RTH_MASK: glb_set_mask_bits(v[0], v[1]); break;
    case RTH_TWIN: glb_set_texture_window((uint32_t)v[0]); break;
    case RTH_MOD: glb_set_color_modulation(v[0], v[1], v[2], v[3]); break;
    case RTH_GEOMETRY: glb_set_geometry_correction(v[0]); break;
    case RTH_WORLD: glb_set_world_triangle(v[0]); break;
    case RTH_COLUMN_BAND: glb_set_column_band(v[0], v[1], v[2]); break;
    case RTH_SKY_CELLS: {
        int32_t sky[6 + 2 * SKY_CELLS];
        memcpy(sky, payload, sizeof sky);
        glb_set_sky_cells(sky[0], sky[1], sky[2], sky[3], sky[4], sky + 6, sky[5]);
        break;
    }
    case RTH_SKY_EXTENSION: {
        int32_t head[3];
        memcpy(head, payload, sizeof head);
        glb_set_sky_extension(head[0], head[1], head[2], (const uint8_t *)payload + 12);
        break;
    }
    case RTH_SKY_COLUMN: {
        float column;
        memcpy(&column, &v[0], sizeof column);
        glb_set_sky_column(column);
        break;
    }
    case RTH_SKY_LOOK: glb_set_sky_look(v[0], v[1]); break;
    case RTH_PRECISE: glb_set_precise_triangle(v[0], v[1], v[2], v[3], v[4], v[5], v[6]); break;
    case RTH_UNPINNED: glb_set_unpinned_triangle(v[0], v[1], v[2], v[3], v[4], v[5]); break;
    case RTH_PERSP: glb_set_perspective_triangle(v[0], rth_float(v[1]), rth_float(v[2]), rth_float(v[3])); break;
#ifndef PSX_DISABLE_FRAME_INTERPOLATION
    case RTH_TEMPORAL_DEPTH: glb_set_temporal_depth_triangle(v[0], rth_float(v[1]), rth_float(v[2]), rth_float(v[3])); break;
    case RTH_SURFACE_DEPTH: glb_set_surface_depth_triangle(fv[0], fv[1], fv[2]); break;
    case RTH_IDENTITY: {
        const uint64_t *ids = (const uint64_t *)payload;
        glb_set_temporal_identity_triangle(ids[0], ids[1], ids[2]);
        break;
    }
    case RTH_SPRITE: glb_set_temporal_sprite(fv[0], fv[5] != 0.0f ? &fv[1] : NULL); break;
    case RTH_SPRITE_CARD: glb_set_sprite_card(fv[8] != 0.0f ? fv : NULL); break;
    case RTH_SPRITE_GROUND: glb_set_temporal_sprite_ground(fv[0], fv[1]); break;
#endif
    case RTH_TILE: {
        int32_t has;
        PsxWsPlacedTile tile;
        memcpy(&has, payload, sizeof has);
        memcpy(&tile, (const uint8_t *)payload + sizeof has, sizeof tile);
        glb_set_screen_tile(has ? &tile : NULL);
        break;
    }
    case RTH_FLAT_SIDES: glb_set_flat_sides(fv[0], fv[1]); break;
    case RTH_NEAR: glb_set_near_triangle(&fv[0], &fv[3], &fv[6]); break;
    case RTH_YAW: {
        const double *d = (const double *)payload;
        glb_set_presentation_yaw(d[0], d[1], d[2], d[3], d[4]);
        break;
    }
    case RTH_FILL: glb_fill_rect(v[0], v[1], v[2], v[3], (uint16_t)v[4]); break;
    case RTH_COPY: glb_copy_rect(v[0], v[1], v[2], v[3], v[4], v[5]); break;
    case RTH_FLAT_TRI: glb_draw_flat_triangle(v[0], v[1], v[2], v[3], v[4], v[5], (uint16_t)v[6]); break;
    case RTH_GOURAUD_TRI:
        glb_draw_gouraud_triangle(v[0], v[1], (uint16_t)v[2], v[3], v[4], (uint16_t)v[5], v[6], v[7], (uint16_t)v[8]);
        break;
    case RTH_TEX_TRI:
        rth_prim_begin(c->flags, v[15], v[16]);
        glb_draw_textured_triangle(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], (uint16_t)v[12],
                                   (uint16_t)v[13], (uint16_t)v[14]);
        break;
    case RTH_SHADED_TEX_TRI:
        rth_prim_begin(c->flags, v[19], v[20]);
        glb_draw_shaded_textured_triangle(v[0], v[1], v[2], v[3], (uint32_t)v[4], v[5], v[6], v[7], v[8], (uint32_t)v[9], v[10],
                                          v[11], v[12], v[13], (uint32_t)v[14], (uint16_t)v[15], (uint16_t)v[16],
                                          (uint16_t)v[17], v[18]);
        break;
    case RTH_FLAT_RECT: glb_draw_flat_rect(v[0], v[1], v[2], v[3], (uint16_t)v[4]); break;
    case RTH_TEX_RECT:
        rth_prim_begin(c->flags, v[9], v[10]);
        glb_draw_textured_rect(v[0], v[1], v[2], v[3], v[4], v[5], (uint16_t)v[6], (uint16_t)v[7], (uint16_t)v[8]);
        break;
    case RTH_TEX_RECT_SCALED:
        rth_prim_begin(c->flags, v[11], v[12]);
        glb_draw_textured_rect_scaled(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], (uint16_t)v[8], (uint16_t)v[9],
                                      (uint16_t)v[10]);
        break;
    case RTH_LINE: glb_draw_line(v[0], v[1], v[2], v[3], (uint16_t)v[4]); break;
    case RTH_SHADED_LINE: glb_draw_shaded_line(v[0], v[1], (uint16_t)v[2], v[3], v[4], (uint16_t)v[5]); break;
    case RTH_VRAM_WRITE: glb_vram_write(v[0], v[1], (uint16_t)v[2]); break;
    case RTH_XFER_IN: glb_vram_transfer_in(v[0], v[1], v[2], v[3], (const uint16_t *)(v + 4)); break;
    case RTH_AREA: glb_set_draw_area(v[0], v[1], v[2], v[3]); break;
    case RTH_OFFSET: glb_set_draw_offset(v[0], v[1]); break;
    case RTH_WIDE_CONFIGURE: glb_wide_configure(v[0], v[1]); break;
    case RTH_WIDE_TARGET: glb_wide_set_target(v[0]); break;
    case RTH_WIDE_DISABLE: glb_wide_disable_target(); break;
    case RTH_WIDE_CLEAR: glb_wide_clear(v[0], v[1], v[2], (uint16_t)v[3]); break;
    case RTH_WIDE_CLEAR_MARGINS: glb_wide_clear_margins(v[0], v[1], v[2], (uint16_t)v[3], v[4]); break;
    case RTH_WIDE_COPY_FRAME: glb_wide_copy_frame(v[0], v[1], v[2], v[3], v[4]); break;
    case RTH_WIDE_COPY_ROWS: glb_wide_copy_rows(v[0], v[1], v[2], v[3], v[4], v[5], v[6]); break;
    case RTH_PRESENT_WIDE:
        s_rthx_squash[0] = v[7]; s_rthx_squash[1] = v[8];
        s_rthx_presented = (uint64_t)(uint32_t)v[9] | (uint64_t)(uint32_t)v[10] << 32;
        s_rthx_cycle = (uint64_t)(uint32_t)v[11] | (uint64_t)(uint32_t)v[12] << 32;
        gl_renderer_present_wide_or_vram(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[13]);
        break;
    case RTH_PRESENT_VRAM:
        s_rthx_squash[0] = v[6]; s_rthx_squash[1] = v[7];
        s_rthx_presented = (uint64_t)(uint32_t)v[8] | (uint64_t)(uint32_t)v[9] << 32;
        s_rthx_cycle = (uint64_t)(uint32_t)v[10] | (uint64_t)(uint32_t)v[11] << 32;
        gl_renderer_present_vram(v[0], v[1], v[2], v[3], v[4], v[5]);
        break;
    case RTH_PRESENT_BLANK: gl_renderer_present_blank(); break;
    case RTH_EDGE_SKY: gl_renderer_set_edge_sky(v[0]); break;
    case RTH_EDGE_DEPTH: gl_renderer_set_edge_depth(fv[0]); break;
    case RTH_FADE: gl_renderer_set_palette_fade(v[0], rth_float(v[1]), rth_float(v[2]), rth_float(v[3])); break;
    case RTH_FADE_POWER: gl_renderer_set_palette_fade_power(v[0]); break;
    case RTH_FADE_BLOCKS: gl_renderer_set_palette_blocks(v + 1, v[0]); break;
    case RTH_SSAO_FADE: gl_renderer_set_ssao_fade(fv[0], fv[1]); break;
    case RTH_HIDDEN_ROWS: gl_renderer_set_hidden_rows(v[0]); break;
    case RTH_INTERP_SUSPENDED: gl_renderer_set_interpolation_suspended(v[0]); break;
    case RTH_UI_SUSPENDED: gl_renderer_set_host_ui_suspended(v[0]); break;
    case RTH_DDGI_MISSION: gl_renderer_set_ddgi_mission(v[0]); break;
    case RTH_INVALIDATE_PRESENT: gl_renderer_invalidate_present(); break;
    case RTH_GROUP:
        for (uint32_t at = 0; at + sizeof(RtCmd) <= c->payload;) {
            const RtCmd *sub = (const RtCmd *)((const uint8_t *)payload + at);
            gl_rth_exec(user, sub, sub + 1);
            at += sub->size;
        }
        break;
    default: break;
    }
    if (c->flags & RTHF_PRIM) rth_prim_begin(0, -1, 0);
}

static void gl_rth_ctx(void *user, int current) {
    (void)user;
    if (current) {
        SDL_GL_MakeCurrent(s_win, s_ctx);
        return;
    }
    glFlush();
    SDL_GL_MakeCurrent(s_win, NULL);
}

/* A per-frame setter's call recorded in command order; 0 when it runs here. */
static int rth_rec_call(uint16_t op, const int32_t *v, int n) { return rth_record_mode() && rth_put(op, 0, v, (uint32_t)n * 4u); }

int gl_renderer_render_thread_start(int max_frames) {
    if (s_rth_on || !s_ctx || !s_raster_ok || !s_vram) return 0;
    s_rth_vram_pub = s_vram;
    s_rth_vram_priv = (uint16_t *)malloc((size_t)VRAM_W * VRAM_H * sizeof(uint16_t));
    if (!s_rth_vram_priv) return 0;
    RtConfig config;
    memset(&config, 0, sizeof config);
    config.ring_bytes = (size_t)32 << 20;
    config.max_frames = max_frames > 0 ? max_frames : 2;
    config.exec = gl_rth_exec;
    config.ctx = gl_rth_ctx;
    flush_flat_batch();
    flush_tex_batch();
    flush_cpu_upload();
    glFlush();
    memcpy(s_rth_vram_priv, s_rth_vram_pub, (size_t)VRAM_W * VRAM_H * sizeof(uint16_t));
    sw_get_draw_area(&s_rthm_area[0], &s_rthm_area[1], &s_rthm_area[2], &s_rthm_area[3]);
    s_vram = s_rth_vram_priv;
    sw_renderer_rebind_vram(s_rth_vram_priv);
    if (!rt_start(&config)) {
        s_vram = s_rth_vram_pub;
        sw_renderer_rebind_vram(s_rth_vram_pub);
        free(s_rth_vram_priv);
        s_rth_vram_priv = NULL;
        return 0;
    }
    t_rth_start = ++s_rth_start;
    s_rth_on = 1;
    gr_refresh_backend();
    fprintf(stdout, "psxrecomp: render thread on, %d frames in flight at most\n", config.max_frames);
    return 1;
}

void gl_renderer_render_thread_stop(void) {
    if (!s_rth_on) return;
    gl_rth_acquire("stop");
    rt_stop();
    s_rth_on = 0;
    s_vram = s_rth_vram_pub;
    sw_renderer_rebind_vram(s_rth_vram_pub);
    free(s_rth_vram_priv);
    s_rth_vram_priv = NULL;
    gr_refresh_backend();
}

int gl_renderer_render_thread_active(void) { return s_rth_on; }

/* The framework's main loop asks for these: the swap interval it set, and the GL context before it touches GL itself. */
int gl_renderer_get_swap_interval(void) { return s_swap_interval; }
void gl_renderer_render_thread_sync(const char *reason) { gl_rth_acquire(reason ? reason : "framework"); }

static int s_rth_want = -1;  /* asked: 1 on, 0 off, -1 not yet read from PSX_RENDER_THREAD */
/* The thread starts or stops at the next frame boundary, where the emulation thread holds the context. */
void gl_renderer_render_thread_request(int on) { s_rth_want = on != 0; }

/* Each vblank before the pacer: a frame that may run apart hands the context to the render thread, one that may not keeps it,
 * then the frame is closed (at most the configured frames wait). */
void gl_renderer_render_thread_frame_boundary(void) {
    if (s_rth_want < 0) {
        const char *env = getenv("PSX_RENDER_THREAD");
        s_rth_want = env && env[0] == '1';
    }
    if (s_rth_want && !s_rth_on) (void)gl_renderer_render_thread_start(2);
    else if (!s_rth_want && s_rth_on) gl_renderer_render_thread_stop();
    if (!s_rth_on) return;
    if (gl_rth_eligible()) gl_rth_release();
    else gl_rth_acquire("ineligible");
    rth_group_close();
    if (!rt_held()) rt_frame_end();
    s_rth_frames++;
}

void gl_renderer_render_thread_stats(uint64_t *frames, uint64_t *syncs, const char **last_sync) {
    if (frames) *frames = s_rth_frames;
    if (syncs) *syncs = s_rth_syncs;
    if (last_sync) *last_sync = s_rth_last_sync;
}
