/* Probe lighting prototype, included once by gpu.c: every exact world polygon becomes world triangles of its world draw. */
#include "gpu_ddgi.h"
#include "gpu_ddgi_mask.h"
#include "gpu_ddgi_turn.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct DdgiList {
    GpuDdgiTriangle *triangles;
    int count, capacity;
    GpuDdgiCard *cards;
    int card_count, card_capacity;
    /* The added-blend glow over the held weapon: its covered pixels, their colour and place summed by area. */
    float flash_strength, flash_colour[3], flash_place[2]; /* strength: shown brightness times pixels */
    uint32_t pose;
    uint32_t area[4];
    int32_t offset[2];
    double turn; /* the presentation's part of a game step its polygons were drawn turned by */
} DdgiList;

static DdgiList s_ddgi_lists[2];
static int s_ddgi_now; /* the list the polygons go to */
static int s_ddgi_done = -1;
static int s_ddgi_view, s_ddgi_capture;
static int s_ddgi_env = -1;
static GpuDdgiStats s_ddgi_stats;

static int ddgi_logging(void) {
    if (s_ddgi_env < 0) {
        const char *asked = getenv("PSX_DDGI");
        s_ddgi_env = asked && asked[0] == '1';
    }
    return s_ddgi_env;
}

void gpu_ddgi_set_view(int enabled) { s_ddgi_view = enabled != 0; }
int gpu_ddgi_view(void) { return s_ddgi_view; }
void gpu_ddgi_set_capture(int enabled) { s_ddgi_capture = enabled != 0; }
static int ddgi_capturing(void) { return s_ddgi_capture || s_ddgi_view || ddgi_logging(); }

static int ddgi_project(const GteCameraPose *pose, const float world[3], double *sx, double *sy, double *depth) {
    const double v[3] = {world[0] - pose->offset[0], world[1] - pose->offset[1], world[2] - pose->offset[2]};
    double c[3];
    for (int r = 0; r < 3; ++r)
        c[r] = pose->translation[r] +
               (pose->rotation[r * 3] * v[0] + pose->rotation[r * 3 + 1] * v[1] + pose->rotation[r * 3 + 2] * v[2]) / 4096.0;
    if (c[2] < 1.0 || fabs(c[0]) > 32767.0 || fabs(c[1]) > 32767.0) return 0;
    *sx = pose->centre_x16 / 65536.0 + c[0] * pose->focal / c[2] * pose->squash_num / pose->squash_den;
    *sy = pose->centre_y16 / 65536.0 + c[1] * pose->focal / c[2];
    *depth = c[2];
    return 1;
}

static void ddgi_finish(const DdgiList *list) {
    GteCameraPose pose;
    s_ddgi_stats.pose = list->pose;
    s_ddgi_stats.triangles = list->count;
    s_ddgi_stats.corners = s_ddgi_stats.within_pixel = 0;
    s_ddgi_stats.worst = 0.0;
    ++s_ddgi_stats.frames;
    if (!ddgi_logging() || !gte_pose_get(list->pose, &pose)) return;
    for (int i = 0; i < list->count; ++i)
        for (int k = 0; k < 3; ++k) {
            const GpuDdgiTriangle *t = &list->triangles[i];
            double sx, sy, depth;
            if (!ddgi_project(&pose, t->world[k], &sx, &sy, &depth) || depth * 2.0 <= pose.focal ||
                abs(t->screen[k][0]) >= 1023 || abs(t->screen[k][1]) >= 1023)
                continue;
            const double off = fmax(fabs(sx - t->screen[k][0] - 0.5), fabs(sy - t->screen[k][1] - 0.5));
            ++s_ddgi_stats.corners;
            if (off <= 0.5) ++s_ddgi_stats.within_pixel;
            if (off > s_ddgi_stats.worst) s_ddgi_stats.worst = off;
        }
    if (s_ddgi_stats.frames % 60u == 1u)
        fprintf(stderr, "ddgi: pose %u triangles %d corners %d within a pixel %d worst %.2f stale %u\n", s_ddgi_stats.pose,
                s_ddgi_stats.triangles, s_ddgi_stats.corners, s_ddgi_stats.within_pixel, s_ddgi_stats.worst,
                s_ddgi_stats.stale);
}

static GpuDdgiTriangle *ddgi_push(DdgiList *list) {
    if (list->count == list->capacity) {
        const int capacity = list->capacity ? list->capacity * 2 : 4096;
        GpuDdgiTriangle *grown = (GpuDdgiTriangle *)realloc(list->triangles, (size_t)capacity * sizeof *grown);
        if (!grown) return NULL;
        list->triangles = grown;
        list->capacity = capacity;
    }
    return &list->triangles[list->count++];
}

/* The list a primitive of this pose goes to: a newer pose finishes the one being filled. NULL for a pose already passed. */
static DdgiList *ddgi_list_for(uint32_t pose) {
    DdgiList *list = &s_ddgi_lists[s_ddgi_now];
    if (pose == list->pose) return list;
    if (list->pose && pose < list->pose) {
        ++s_ddgi_stats.stale;
        return NULL;
    }
    if (list->count || list->card_count) {
        ddgi_finish(list);
        s_ddgi_done = s_ddgi_now;
        s_ddgi_now ^= 1;
        list = &s_ddgi_lists[s_ddgi_now];
    }
    list->count = list->card_count = 0;
    list->flash_strength = list->flash_colour[0] = list->flash_colour[1] = list->flash_colour[2] = 0.0f;
    list->flash_place[0] = list->flash_place[1] = 0.0f;
    list->turn = 0.0;
    list->pose = pose;
    return list;
}

static void ddgi_note_polygon(const int *indices, int count) {
    if (!ddgi_capturing() || gp0_cmd_source_addr == 0xFFFFFFFFu) return;
    int16_t input[4][3];
    uint32_t pose = 0;
    for (int i = 0; i < count; ++i) {
        uint32_t addr, sequence = 0;
        uint64_t identity = 0;
        if (!gp0_source_word_address(indices[i], &addr) ||
            gte_precision_load_pose(addr, gp0_cmd_buf[indices[i]], &sequence, &identity) != GTE_PRECISION_LOOKUP_ACCEPTED ||
            !sequence || !(identity >> 63) || (pose && sequence != pose))
            return;
        pose = sequence;
        input[i][0] = (int16_t)(identity & 0xFFFFu);
        input[i][1] = (int16_t)((identity >> 16) & 0xFFFFu);
        input[i][2] = (int16_t)((identity >> 32) & 0xFFFFu);
    }
    DdgiList *list = ddgi_list_for(pose);
    if (!list) return;
    GteCameraPose camera;
    if (!gte_pose_get(pose, &camera)) return;
    list->area[0] = draw_area_left;
    list->area[1] = draw_area_top;
    list->area[2] = draw_area_right;
    list->area[3] = draw_area_bottom;
    list->offset[0] = draw_offset_x;
    list->offset[1] = draw_offset_y;
    list->turn = geometry_camera_presentation_yaw();
    const uint32_t op = gp0_cmd_buf[0] >> 24;
    const int textured = (op & 4u) != 0, shaded = (op & 0x10u) != 0;
    static const int halves[2][3] = {{0, 1, 2}, {2, 1, 3}};
    for (int half = 0; half < (count == 4 ? 2 : 1); ++half) {
        GpuDdgiTriangle *t = ddgi_push(list);
        if (!t) return;
        for (int k = 0; k < 3; ++k) {
            const int corner = halves[half][k], word = indices[corner];
            int32_t x, y;
            parse_vertex(gp0_cmd_buf[word], &x, &y);
            for (int axis = 0; axis < 3; ++axis) t->world[k][axis] = (float)(input[corner][axis] + camera.offset[axis]);
            t->screen[k][0] = (int16_t)x;
            t->screen[k][1] = (int16_t)y;
            const uint32_t uv = textured ? gp0_cmd_buf[word + 1] : 0u;
            t->uv[k][0] = (uint8_t)(uv & 0xFFu);
            t->uv[k][1] = (uint8_t)((uv >> 8) & 0xFFu);
            const uint32_t colour = gp0_cmd_buf[shaded ? word - 1 : 0];
            t->colour[k][0] = (uint8_t)(colour & 0xFFu);
            t->colour[k][1] = (uint8_t)((colour >> 8) & 0xFFu);
            t->colour[k][2] = (uint8_t)((colour >> 16) & 0xFFu);
        }
        t->clut = textured ? (uint16_t)(gp0_cmd_buf[indices[0] + 1] >> 16) : 0u;
        t->texpage = textured ? (uint16_t)(gp0_cmd_buf[indices[1] + 1] >> 16) : 0u;
        t->flags = (uint8_t)((textured ? 1 : 0) | ((op & 2u) ? 2 : 0) | ((op & 1u) ? 4 : 0));
    }
}

#include "gpu_ddgi_scene.c.inc"

#ifndef PSX_DISABLE_FRAME_INTERPOLATION
/* A world sprite's rectangle as drawn (x, y before the draw offset, sides its sub-pixel edges) at camera depth z, turned:
 * the game turned it (an impact, a blast), an effect and never a shadow caster; facing: the renderer drew it as a card
 * facing the camera, and its caster turns the same. 1: the game's blob shadow, left out. */
static int ddgi_note_card(int x, int y, int w, int h, const float sides[4], float z, int left_u, int top_v, int right_u,
                          int bottom_v, uint16_t clut, uint16_t texpage, uint32_t colour, int semi, int raw, int turned,
                          int facing) {
    if (z <= 0.0f || w <= 0 || h <= 0) return 0;
    const WsTag *tag = temporal_sprite_tag();
    if (ddgi_blob_hidden(tag)) return 1;
    if (!ddgi_capturing()) return 0;
    /* The world draw its polygons are in: the game has begun the next one by the time the GPU draws this. */
    DdgiList *list = &s_ddgi_lists[s_ddgi_now];
    GteCameraPose camera;
    if (!tag || !list->pose || list->area[1] != draw_area_top || !gte_pose_get(list->pose, &camera) || !camera.focal ||
        camera.squash_num <= 0 || camera.squash_den <= 0)
        return 0;
    if (list->card_count == list->card_capacity) {
        const int capacity = list->card_capacity ? list->card_capacity * 2 : 256;
        GpuDdgiCard *grown = (GpuDdgiCard *)realloc(list->cards, (size_t)capacity * sizeof *grown);
        if (!grown) return 0;
        list->cards = grown;
        list->card_capacity = capacity;
    }
    const double left = x + sides[0], right = x + w + sides[2], top = y + sides[1], bottom = y + h + sides[3];
    const double fx = (double)camera.focal * camera.squash_num / camera.squash_den, fy = camera.focal;
    const double c[3] = {((left + right) * 0.5 - camera.centre_x16 / 65536.0) * z / fx,
                         ((top + bottom) * 0.5 - camera.centre_y16 / 65536.0) * z / fy, z};
    const double across = (right - left) * 0.5 * z / fx, down = (bottom - top) * 0.5 * z / fy;
    double width[2] = {across, 0.0};
    if (facing && !turned) gpu_sprite_card_across(c[0], c[2], across, width);
    GpuDdgiCard *card = &list->cards[list->card_count++];
    float back[3][3];
    psx_ddgi_pose_back(camera.rotation, back);
    for (int axis = 0; axis < 3; ++axis) {
        double v = 0.0;
        for (int row = 0; row < 3; ++row) v += back[row][axis] * (c[row] - camera.translation[row]);
        card->centre[axis] = (float)(v + camera.offset[axis]);
        card->across[axis] = (float)(back[0][axis] * width[0] + back[2][axis] * width[1]);
        card->down[axis] = (float)(back[1][axis] * down);
    }
    card->depth = z;
    card->uv[0] = (int16_t)left_u;
    card->uv[1] = (int16_t)top_v;
    card->uv[2] = (int16_t)right_u;
    card->uv[3] = (int16_t)bottom_v;
    card->colour[0] = (uint8_t)(colour & 0xFFu);
    card->colour[1] = (uint8_t)((colour >> 8) & 0xFFu);
    card->colour[2] = (uint8_t)((colour >> 16) & 0xFFu);
    card->clut = clut;
    card->texpage = texpage;
    card->flags = (uint8_t)((semi ? 1 : 0) | (raw ? 2 : 0) | (tag->shadow ? 4 : 0) | (tag->whole ? 8 : 0) | (tag->effect || turned ? 16 : 0));
    card->light = tag->light > 0.0f ? tag->light : 1.0f;
    psx_ddgi_card_look(vram, card, semi, !(card->flags & 20));
    return 0;
}

static int ddgi_note_sprite(int x, int y, int w, int h, const float sides[4], float z, int left_u, int top_v, int right_u,
                            int bottom_v, uint16_t clut, uint16_t texpage, uint32_t colour, int semi, int raw, int facing) {
    return ddgi_note_card(x, y, w, h, sides, z, left_u, top_v, right_u, bottom_v, clut, texpage, colour, semi, raw, 0, facing);
}
#endif


/* A flat added-blend picture in the lower middle of the frame, x, y before the draw offset: the weapon's muzzle glow. */
static void ddgi_note_flash(int x, int y, int w, int h, int left_u, int top_v, int right_u, int bottom_v, uint16_t clut,
                            uint16_t texpage, uint32_t colour, int raw, float share) {
    const int abr = (texpage >> 5) & 3;
    if (!ddgi_capturing() || w <= 0 || h <= 0 || (abr != 1 && abr != 3)) return;
    DdgiList *list = &s_ddgi_lists[s_ddgi_now];
    const int rows = (int)list->area[3] - (int)list->area[1] + 1, columns = (int)list->area[2] - (int)list->area[0] + 1;
    if (!list->pose || list->area[1] != draw_area_top || rows <= 0 || columns <= 0) return;
    const float cx = (float)x + (float)w * 0.5f, cy = (float)y + (float)h * 0.5f;
    if (cy < 0.45f * (float)rows || cx < 0.15f * (float)columns || cx > 0.85f * (float)columns || w > columns * 4 / 5) return;
    GpuDdgiCard card;
    memset(&card, 0, sizeof card);
    card.uv[0] = (int16_t)left_u;
    card.uv[1] = (int16_t)top_v;
    card.uv[2] = (int16_t)right_u;
    card.uv[3] = (int16_t)bottom_v;
    card.clut = clut;
    card.texpage = texpage;
    psx_ddgi_card_look(vram, &card, 1, 0);
    if (card.cover < 0.02f || card.blended < 0.3f) return;
    const float area = (float)(w * h) * share * card.cover;
    float shown[3];
    for (int k = 0; k < 3; ++k) {
        shown[k] = fminf(card.texels[k] * (raw ? 1.0f : (float)((colour >> (8 * k)) & 0xFFu) * 2.0f / 255.0f), 1.0f);
        list->flash_colour[k] += ddgi_linear(shown[k]) * area;
    }
    /* Added pictures add up as the screen shows them: many faint layers of a glow make a bright one. */
    const float strength = ddgi_lum(shown) * (abr == 3 ? 0.25f : 1.0f) * area;
    list->flash_place[0] += cx * strength;
    list->flash_place[1] += cy * strength;
    list->flash_strength += strength;
}

#ifndef PSX_DISABLE_FRAME_INTERPOLATION
/* A turned textured quad that blends: a world sprite the game turned (an impact, a blast) or, flat, part of the weapon's glow. */
static uint32_t ddgi_mean_colour(const uint32_t colour[4]) {
    uint32_t mean = 0;
    for (int k = 0; k < 3; ++k) {
        uint32_t sum = 0;
        for (int i = 0; i < 4; ++i) sum += (colour[i] >> (8 * k)) & 0xFFu;
        mean |= (sum / 4u) << (8 * k);
    }
    return mean;
}

static double ddgi_triangle_area(const int32_t vx[4], const int32_t vy[4], int a, int b, int c) {
    return 0.5 * fabs((double)(vx[b] - vx[a]) * (vy[c] - vy[a]) - (double)(vx[c] - vx[a]) * (vy[b] - vy[a]));
}

static void ddgi_note_turned(const int32_t vx[4], const int32_t vy[4], const int u[4], const int v[4], uint16_t clut, uint16_t texpage,
                             uint32_t colour, int raw, int precise, int drawn_a, int drawn_b) {
    if (precise || !ddgi_capturing()) return;
    int x0 = vx[0], y0 = vy[0], x1 = vx[0], y1 = vy[0], u0 = u[0], v0 = v[0], u1 = u[0], v1 = v[0];
    for (int i = 1; i < 4; ++i) {
        x0 = vx[i] < x0 ? vx[i] : x0;
        x1 = vx[i] > x1 ? vx[i] : x1;
        y0 = vy[i] < y0 ? vy[i] : y0;
        y1 = vy[i] > y1 ? vy[i] : y1;
        u0 = u[i] < u0 ? u[i] : u0;
        u1 = u[i] > u1 ? u[i] : u1;
        v0 = v[i] < v0 ? v[i] : v0;
        v1 = v[i] > v1 ? v[i] : v1;
    }
    x0 -= draw_offset_x, x1 -= draw_offset_x, y0 -= draw_offset_y, y1 -= draw_offset_y;
    if (x1 <= x0 || y1 <= y0) return;
    /* The pixels of the two triangles the GPU draws, 0 1 2 and 2 1 3, against the box's: a diamond fills half of it. */
    const double turned = (drawn_a ? ddgi_triangle_area(vx, vy, 0, 1, 2) : 0.0) + (drawn_b ? ddgi_triangle_area(vx, vy, 2, 1, 3) : 0.0);
    const float share = (float)fmin(1.0, turned / ((double)(x1 - x0) * (y1 - y0)));
    const WsTag *tag = temporal_sprite_tag();
    if (tag) {
        static const float flat[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        DdgiList *list = &s_ddgi_lists[s_ddgi_now];
        const int before = list->card_count;
        (void)ddgi_note_card(x0, y0, x1 - x0, y1 - y0, flat, (float)tag->anchor_x, u0, v0, u1, v1, clut, texpage, colour, 1, raw, 1, 0);
        if (list->card_count > before) {
            GpuDdgiCard *card = &list->cards[before];
            const float side = sqrtf(share);
            for (int a = 0; a < 3; ++a) card->across[a] *= side, card->down[a] *= side;
        }
    } else {
        ddgi_note_flash(x0, y0, x1 - x0, y1 - y0, u0, v0, u1, v1, clut, texpage, colour, raw, share);
    }
}
#endif

int gpu_ddgi_cards(uint32_t pose, const GpuDdgiCard **cards, int *count) {
    for (int i = 0; i < 2; ++i)
        if (pose && s_ddgi_lists[i].pose == pose) {
            *cards = s_ddgi_lists[i].cards;
            *count = s_ddgi_lists[i].card_count;
            return 1;
        }
    return 0;
}

float gpu_ddgi_turn(uint32_t pose) {
    for (int i = 0; i < 2; ++i)
        if (pose && s_ddgi_lists[i].pose == pose)
            return (float)(s_ddgi_lists[i].turn * (6.28318530717958647692 / GEOMETRY_CAMERA_FULL_TURN));
    return 0.0f;
}

int gpu_ddgi_frame(const GpuDdgiTriangle **triangles, int *count, GteCameraPose *camera) {
    if (s_ddgi_done < 0) return 0;
    const DdgiList *list = &s_ddgi_lists[s_ddgi_done];
    if (triangles) *triangles = list->triangles;
    if (count) *count = list->count;
    return camera ? gte_pose_get(list->pose, camera) : 1;
}

int gpu_ddgi_shown(int y, const GpuDdgiTriangle **triangles, int *count, GteCameraPose *camera) {
    const int order[2] = {s_ddgi_now, s_ddgi_now ^ 1};
    for (int i = 0; i < 2; ++i) {
        const DdgiList *list = &s_ddgi_lists[order[i]];
        if (!list->count || y < (int)list->area[1] || y > (int)list->area[3] || !gte_pose_get(list->pose, camera)) continue;
        *triangles = list->triangles;
        *count = list->count;
        return 1;
    }
    return gpu_ddgi_latest(triangles, count, camera);
}

int gpu_ddgi_latest(const GpuDdgiTriangle **triangles, int *count, GteCameraPose *camera) {
    const DdgiList *list = &s_ddgi_lists[s_ddgi_now];
    if (!list->count) {
        if (s_ddgi_done < 0) return 0;
        list = &s_ddgi_lists[s_ddgi_done];
    }
    if (!list->count || !gte_pose_get(list->pose, camera)) return 0;
    *triangles = list->triangles;
    *count = list->count;
    return 1;
}

void gpu_ddgi_stats(GpuDdgiStats *stats) {
    if (stats) *stats = s_ddgi_stats;
}

void gpu_ddgi_draw_probes(void) {
    if (!gpu_ddgi_view()) return;
    const DdgiList *list = &s_ddgi_lists[s_ddgi_now];
    GteCameraPose camera;
    if (!list->count || !gte_pose_get(list->pose, &camera)) return;
    const int spacing = 400;
    int base[3];
    for (int axis = 0; axis < 3; ++axis) base[axis] = (int)floor((double)camera.offset[axis] / spacing);
    gr_set_draw_area((int)list->area[0], (int)list->area[1], (int)list->area[2], (int)list->area[3]);
    gr_set_draw_offset(list->offset[0], list->offset[1]);
    gr_set_semi_transparency(0, (int)semi_transparency);
    for (int i = -4; i <= 4; ++i)
        for (int j = -1; j <= 1; ++j)
            for (int k = -4; k <= 4; ++k) {
                const int cell[3] = {base[0] + i, base[1] + j, base[2] + k};
                const float world[3] = {(float)(cell[0] * spacing), (float)(cell[1] * spacing), (float)(cell[2] * spacing)};
                double sx, sy, depth;
                if (!ddgi_project(&camera, world, &sx, &sy, &depth) || depth < 64.0 || fabs(sx) > 1000.0 || fabs(sy) > 1000.0)
                    continue;
                const int odd = (cell[0] + cell[1] + cell[2]) & 1;
                const uint16_t level = (uint16_t)(31 - (j + 1) * 6);
                const uint16_t colour = odd ? (uint16_t)(level | (level << 5)) : (uint16_t)((level << 5) | (level << 10));
                const int size = depth < 1024.0 ? 3 : 2;
                gr_draw_flat_rect((int)floor(sx) + list->offset[0] - size / 2, (int)floor(sy) + list->offset[1] - size / 2, size, size,
                                  colour);
            }
    gr_set_draw_area((int)draw_area_left, (int)draw_area_top, (int)draw_area_right, (int)draw_area_bottom);
    gr_set_draw_offset(draw_offset_x, draw_offset_y);
}
