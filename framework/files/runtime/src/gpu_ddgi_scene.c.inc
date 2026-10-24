/* Shadow casters and lights of a world draw, included once by gpu_ddgi.c.inc: textures read from the CPU copy of VRAM, the
 * level's emitting faces kept across frames by their world place. */

static int s_ddgi_hide_blobs = 1, s_ddgi_shadowing;
void gpu_ddgi_set_hide_blobs(int enabled) { s_ddgi_hide_blobs = enabled != 0; }
int gpu_ddgi_hide_blobs(void) { return s_ddgi_hide_blobs; }
void gpu_ddgi_set_shadowing(int enabled) { s_ddgi_shadowing = enabled != 0; }

static uint16_t ddgi_texel(int u, int v, uint16_t texpage, uint16_t clut) { return psx_ddgi_texel(vram, u, v, texpage, clut); }

static void ddgi_colour5(uint16_t raw, float out[3]) {
    out[0] = (float)(raw & 31) / 31.0f;
    out[1] = (float)((raw >> 5) & 31) / 31.0f;
    out[2] = (float)((raw >> 10) & 31) / 31.0f;
}

static float ddgi_lum(const float c[3]) { return 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2]; }
static float ddgi_linear(float c) { return powf(c > 0.0f ? c : 0.0f, 2.2f); }

/* An emitting face of the level, kept by its world place: the brightest it was seen (a far one shows a faded palette row). */
typedef struct DdgiEmitter {
    uint64_t key;
    float centre[3], normal[3], area, colour[3], lum;
    uint32_t seen;
} DdgiEmitter;

#define DDGI_EMITTERS 8192
#define DDGI_EMITTER_PROBES 8
#define DDGI_EMITTER_KEPT 3600u
static DdgiEmitter s_ddgi_emitters[DDGI_EMITTERS];

static void ddgi_keep_emitter(const float centre[3], const float normal[3], float area, const float colour[3]) {
    const int64_t q[3] = {(int64_t)floorf(centre[0] / 8.0f), (int64_t)floorf(centre[1] / 8.0f), (int64_t)floorf(centre[2] / 8.0f)};
    const uint64_t key = 1u + ((uint64_t)(q[0] & 0x1FFFFF) | (uint64_t)(q[1] & 0x1FFFFF) << 21 | (uint64_t)(q[2] & 0x1FFFFF) << 42);
    const uint32_t now = (uint32_t)s_frame_count, first = (uint32_t)((key * 0x9E3779B97F4A7C15ull) >> 51);
    DdgiEmitter *slot = NULL;
    for (uint32_t i = 0; i < DDGI_EMITTER_PROBES; ++i) {
        DdgiEmitter *e = &s_ddgi_emitters[(first + i) & (DDGI_EMITTERS - 1)];
        if (e->key == key) {
            slot = e;
            break;
        }
        if (!slot || !e->key || now - e->seen > now - slot->seen) slot = e;
    }
    const float lum = ddgi_lum(colour);
    if (slot->key != key || now - slot->seen > DDGI_EMITTER_KEPT) {
        slot->key = key;
        slot->lum = -1.0f;
    }
    memcpy(slot->centre, centre, sizeof slot->centre);
    memcpy(slot->normal, normal, sizeof slot->normal);
    slot->area = area;
    slot->seen = now;
    if (lum > slot->lum) {
        memcpy(slot->colour, colour, sizeof slot->colour);
        slot->lum = lum;
    }
}

/* What a world face gives off of its own, linear, kept by its picture: the same face is drawn again every frame. */
typedef struct DdgiFace {
    uint64_t key;
    uint32_t stamp;
    float emission[3];
} DdgiFace;

#define DDGI_FACES 8192
#define DDGI_FACE_STEPS 8
static DdgiFace s_ddgi_faces[DDGI_FACES];

/* An added blend's colour, or the bright texels' share of the face: a lamp is often a spot in a larger picture. 0 for none. */
static int ddgi_emission(const GpuDdgiTriangle *t, float out[3]) {
    uint64_t key = 1469598103934665603ull;
    const uint8_t *bytes[4] = {(const uint8_t *)t->uv, (const uint8_t *)t->colour, (const uint8_t *)&t->clut,
                               (const uint8_t *)&t->texpage};
    const size_t sizes[4] = {sizeof t->uv, sizeof t->colour, sizeof t->clut, sizeof t->texpage};
    for (int part = 0; part < 4; ++part)
        for (size_t b = 0; b < sizes[part]; ++b) key = (key ^ bytes[part][b]) * 1099511628211ull;
    key = (key ^ t->flags) * 1099511628211ull;
    const uint32_t now = (uint32_t)s_frame_count;
    DdgiFace *face = &s_ddgi_faces[(key >> 7) & (DDGI_FACES - 1)];
    if (face->key != key || now - face->stamp > 600u) {
        face->key = key;
        face->stamp = now;
        const int textured = t->flags & 1, raw = t->flags & 4, abr = (t->texpage >> 5) & 3;
        const int added = textured && (t->flags & 2) && (abr == 1 || abr == 3);
        float sum[3] = {0.0f, 0.0f, 0.0f}, glow[3] = {0.0f, 0.0f, 0.0f};
        int seen = 0;
        /* No glow anywhere when its brightest vertex times its palette's brightest colour stays under the threshold. */
        float most = 0.0f;
        for (int v = 0; v < 3; ++v)
            for (int k = 0; k < 3; ++k)
                if (t->colour[v][k] > most) most = t->colour[v][k];
        most /= 255.0f;
        if (textured) {
            float palette = 1.0f;
            if (((t->texpage >> 7) & 3) == 0) {
                const int cx = (t->clut & 63) * 16, cy = (t->clut >> 6) & 511;
                int top = 0;
                for (int e = 0; e < 16; ++e) {
                    const uint16_t c = vram[cy * 1024 + ((cx + e) & 1023)];
                    const int r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31, m = r > g ? (r > b ? r : b) : (g > b ? g : b);
                    if (c && m > top) top = m;
                }
                palette = (float)top / 31.0f;
            }
            most = raw ? palette : palette * most * 2.0f;
        }
        if (!added && most < 0.749f) {
            memset(face->emission, 0, sizeof face->emission);
            memset(out, 0, sizeof face->emission);
            return 0;
        }
        for (int a = 0; a < DDGI_FACE_STEPS; ++a)
            for (int b = 0; a + b < DDGI_FACE_STEPS; ++b) {
                const float w1 = ((float)a + 0.334f) / (float)DDGI_FACE_STEPS, w2 = ((float)b + 0.333f) / (float)DDGI_FACE_STEPS;
                const float bary[3] = {1.0f - w1 - w2, w1, w2};
                float vc[3], lit[3];
                for (int k = 0; k < 3; ++k)
                    vc[k] = (bary[0] * t->colour[0][k] + bary[1] * t->colour[1][k] + bary[2] * t->colour[2][k]) / 255.0f;
                if (textured) {
                    const float u = bary[0] * t->uv[0][0] + bary[1] * t->uv[1][0] + bary[2] * t->uv[2][0];
                    const float v = bary[0] * t->uv[0][1] + bary[1] * t->uv[1][1] + bary[2] * t->uv[2][1];
                    const uint16_t texel = ddgi_texel((int)u, (int)v, t->texpage, t->clut);
                    if (!texel) continue;
                    ddgi_colour5(texel, lit);
                    if (!raw)
                        for (int k = 0; k < 3; ++k) lit[k] *= vc[k] * 2.0f;
                } else {
                    memcpy(lit, vc, sizeof lit);
                }
                ++seen;
                if (added) {
                    for (int k = 0; k < 3; ++k) sum[k] += ddgi_linear(fminf(lit[k], 1.0f));
                    continue;
                }
                const float bright = fmaxf(lit[0], fmaxf(lit[1], lit[2]));
                const float x = fminf(1.0f, fmaxf(0.0f, (bright - 0.75f) / 0.2f)), share = x * x * (3.0f - 2.0f * x);
                if (share > 0.0f)
                    for (int k = 0; k < 3; ++k) glow[k] += ddgi_linear(fminf(lit[k], 1.0f)) * share;
            }
        for (int k = 0; k < 3; ++k)
            face->emission[k] = !seen ? 0.0f : added ? sum[k] / (float)seen * (abr == 3 ? 0.25f : 1.0f) : glow[k] / (float)seen;
    }
    memcpy(out, face->emission, sizeof face->emission);
    return ddgi_lum(out) > 0.01f;
}

typedef struct DdgiCluster {
    uint64_t key;
    float place[3], colour[3], normal[3], lo[3], hi[3], power, area;
} DdgiCluster;

#define DDGI_CLUSTERS 512
/* World units: an enemy stands about 1400 tall. */
#define DDGI_CLUSTER_SIZE 600.0f
#define DDGI_LAMP_RANGE 12000.0f
#define DDGI_DYNAMIC_GROUPS 32

static GpuDdgiScene s_ddgi_scene_before;

void gpu_ddgi_forget_level(void) {
    memset(s_ddgi_emitters, 0, sizeof s_ddgi_emitters);
    memset(s_ddgi_faces, 0, sizeof s_ddgi_faces);
    memset(&s_ddgi_scene_before, 0, sizeof s_ddgi_scene_before);
}

static float ddgi_distance2(const float a[3], const float b[3]) {
    const float d[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    return d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
}

static void ddgi_scene_casters(const DdgiList *list, const float eye[3], GpuDdgiScene *scene) {
    int order[512], kept = 0;
    for (int i = 0; i < list->card_count && kept < 512; ++i)
        if (!(list->cards[i].flags & 20)) order[kept++] = i;
    /* Nearest first: a crowd keeps the casters that throw shadows where they are seen. */
    for (int i = 1; i < kept; ++i)
        for (int j = i; j > 0 && list->cards[order[j]].depth < list->cards[order[j - 1]].depth; --j) {
            const int swap = order[j];
            order[j] = order[j - 1];
            order[j - 1] = swap;
        }
    int used[GPU_DDGI_CASTERS] = {0};
    for (int n = 0; n < kept && scene->casters < GPU_DDGI_CASTERS; ++n) {
        const GpuDdgiCard *card = &list->cards[order[n]];
        if (psx_ddgi_card_blocking(card) < 0.02f || card->bounds[2] <= card->bounds[0] || card->bounds[3] <= card->bounds[1])
            continue;
        const float x0 = 2.0f * card->bounds[0] - 1.0f, x1 = 2.0f * card->bounds[2] - 1.0f;
        const float y0 = 2.0f * card->bounds[1] - 1.0f, y1 = 2.0f * card->bounds[3] - 1.0f;
        const float wide = sqrtf(card->across[0] * card->across[0] + card->across[1] * card->across[1] +
                                 card->across[2] * card->across[2]);
        const float tall = sqrtf(card->down[0] * card->down[0] + card->down[1] * card->down[1] + card->down[2] * card->down[2]);
        GpuDdgiCaster *c = &scene->caster[scene->casters];
        for (int a = 0; a < 3; ++a)
            c->centre[a] = card->centre[a] + card->across[a] * (x0 + x1) * 0.5f + card->down[a] * (y0 + y1) * 0.5f;
        /* A body is narrower through than the picture is wide. */
        c->across = wide * (x1 - x0) * 0.5f * 0.7f;
        c->tall = tall * (y1 - y0) * 0.5f * 0.95f;
        if (c->tall < 30.0f || c->across < 15.0f || ddgi_distance2(c->centre, eye) > 20000.0f * 20000.0f) continue;
        psx_ddgi_box_picture(card, wide, tall, c->picture, &c->half_width, &c->half_height);
        const float level = sqrtf(card->across[0] * card->across[0] + card->across[2] * card->across[2]);
        c->side[0] = level > 1e-3f ? card->across[0] / level : 1.0f;
        c->side[1] = level > 1e-3f ? card->across[2] / level : 0.0f;
        memcpy(c->mask, card->mask, sizeof c->mask);
        c->previous = -1;
        float best = fmaxf(200.0f, 1.5f * c->across);
        best *= best;
        for (int p = 0; p < s_ddgi_scene_before.casters; ++p) {
            const float d2 = ddgi_distance2(c->centre, s_ddgi_scene_before.caster[p].centre);
            if (!used[p] && d2 < best) {
                best = d2;
                c->previous = p;
            }
        }
        /* An animation's frames differ in size: the shape follows them slowly, the place at once. */
        if (c->previous >= 0) {
            const GpuDdgiCaster *was = &s_ddgi_scene_before.caster[c->previous];
            used[c->previous] = 1;
            c->across = 0.65f * was->across + 0.35f * c->across;
            c->tall = 0.65f * was->tall + 0.35f * c->tall;
        }
        ++scene->casters;
    }
}

static void ddgi_scene_static_lamps(const DdgiList *list, const GteCameraPose *camera, const float eye[3], GpuDdgiScene *scene,
                                    int room) {
    for (int i = 0; i < list->count; ++i) {
        const GpuDdgiTriangle *t = &list->triangles[i];
        float colour[3];
        if (!ddgi_emission(t, colour)) continue;
        float e1[3], e2[3], n[3], centre[3];
        for (int a = 0; a < 3; ++a) {
            e1[a] = t->world[1][a] - t->world[0][a];
            e2[a] = t->world[2][a] - t->world[0][a];
            centre[a] = (t->world[0][a] + t->world[1][a] + t->world[2][a]) / 3.0f;
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        const float twice = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (twice < 1e-3f) continue;
        const float area = twice * 0.5f;
        if (!(t->flags & 2) && area > 1.5e6f) continue;
        float toward = 0.0f;
        for (int a = 0; a < 3; ++a) {
            n[a] /= twice;
            toward += n[a] * (eye[a] - t->world[0][a]);
        }
        if (toward < 0.0f)
            for (int a = 0; a < 3; ++a) n[a] = -n[a];
        ddgi_keep_emitter(centre, n, area, colour);
    }
    (void)camera;
    static DdgiCluster clusters[DDGI_CLUSTERS];
    int count = 0;
    const uint32_t now = (uint32_t)s_frame_count;
    for (int i = 0; i < DDGI_EMITTERS; ++i) {
        const DdgiEmitter *e = &s_ddgi_emitters[i];
        if (!e->key || now - e->seen > DDGI_EMITTER_KEPT || ddgi_distance2(e->centre, eye) > DDGI_LAMP_RANGE * DDGI_LAMP_RANGE)
            continue;
        const int64_t q[3] = {(int64_t)floorf(e->centre[0] / DDGI_CLUSTER_SIZE), (int64_t)floorf(e->centre[1] / DDGI_CLUSTER_SIZE),
                              (int64_t)floorf(e->centre[2] / DDGI_CLUSTER_SIZE)};
        const uint64_t key = 1u + ((uint64_t)(q[0] & 0x1FFFFF) | (uint64_t)(q[1] & 0x1FFFFF) << 21 | (uint64_t)(q[2] & 0x1FFFFF) << 42);
        DdgiCluster *c = NULL;
        for (int k = 0; k < count; ++k)
            if (clusters[k].key == key) {
                c = &clusters[k];
                break;
            }
        if (!c) {
            if (count == DDGI_CLUSTERS) continue;
            c = &clusters[count++];
            memset(c, 0, sizeof *c);
            c->key = key;
            for (int a = 0; a < 3; ++a) c->lo[a] = c->hi[a] = e->centre[a];
        }
        const float power = e->lum * e->area;
        for (int a = 0; a < 3; ++a) {
            c->place[a] += e->centre[a] * power;
            c->colour[a] += e->colour[a] * e->area;
            c->normal[a] += e->normal[a] * power;
            c->lo[a] = fminf(c->lo[a], e->centre[a]);
            c->hi[a] = fmaxf(c->hi[a], e->centre[a]);
        }
        c->power += power;
        c->area += e->area;
    }
    /* The strongest where the camera is, a light already shown kept a while longer so the set does not flicker. */
    float score[DDGI_CLUSTERS];
    for (int k = 0; k < count; ++k) {
        const DdgiCluster *c = &clusters[k];
        float place[3];
        for (int a = 0; a < 3; ++a) place[a] = c->place[a] / fmaxf(c->power, 1e-6f);
        score[k] = c->power / (ddgi_distance2(place, eye) + 2000.0f * 2000.0f);
        for (int p = 0; p < s_ddgi_scene_before.lamps; ++p)
            if (!s_ddgi_scene_before.lamp[p].dynamic && s_ddgi_scene_before.lamp[p].key == (uint32_t)(c->key ^ (c->key >> 32)))
                score[k] *= 1.5f;
    }
    for (int taken = 0; taken < room && taken < count; ++taken) {
        int best = -1;
        for (int k = 0; k < count; ++k)
            if (score[k] > 0.0f && (best < 0 || score[k] > score[best])) best = k;
        if (best < 0 || score[best] < 1e-4f) break;
        const DdgiCluster *c = &clusters[best];
        score[best] = 0.0f;
        GpuDdgiLamp *lamp = &scene->lamp[scene->lamps++];
        memset(lamp, 0, sizeof *lamp);
        float spread = 0.0f, normal = 0.0f;
        for (int a = 0; a < 3; ++a) {
            lamp->position[a] = c->place[a] / fmaxf(c->power, 1e-6f);
            lamp->colour[a] = c->colour[a] / fmaxf(c->area, 1e-6f);
            spread += (c->hi[a] - c->lo[a]) * (c->hi[a] - c->lo[a]);
            normal += c->normal[a] * c->normal[a];
        }
        normal = sqrtf(normal);
        /* Faces turned every way, a lamp seen round: no side it lights alone. */
        for (int a = 0; a < 3; ++a) lamp->normal[a] = normal > 0.5f * c->power ? c->normal[a] / normal : 0.0f;
        lamp->size = fmaxf(60.0f, 0.5f * sqrtf(spread) + 0.5f * sqrtf(c->area));
        lamp->power = c->power;
        lamp->key = (uint32_t)(c->key ^ (c->key >> 32));
        lamp->previous = -1;
        lamp->weight = 0.25f;
        for (int p = 0; p < s_ddgi_scene_before.lamps; ++p)
            if (!s_ddgi_scene_before.lamp[p].dynamic && s_ddgi_scene_before.lamp[p].key == lamp->key) {
                lamp->previous = p;
                lamp->weight = fminf(1.0f, s_ddgi_scene_before.lamp[p].weight + 0.25f);
            }
    }
    /* A light that left the set fades out. */
    for (int p = 0; p < s_ddgi_scene_before.lamps && scene->lamps < room; ++p) {
        const GpuDdgiLamp *was = &s_ddgi_scene_before.lamp[p];
        if (was->dynamic || was->weight <= 0.25f) continue;
        int kept = 0;
        for (int k = 0; k < scene->lamps; ++k) kept |= scene->lamp[k].key == was->key;
        if (kept) continue;
        GpuDdgiLamp *lamp = &scene->lamp[scene->lamps++];
        *lamp = *was;
        lamp->previous = p;
        lamp->weight = was->weight - 0.25f;
    }
}

/* The muzzle's light: along the ray through the glow's middle, a short way in front of the camera. */
#define DDGI_MUZZLE_DEPTH 260.0f
#define DDGI_MUZZLE_POWER 60000.0f

static void ddgi_scene_dynamic_lamps(const DdgiList *list, const GteCameraPose *camera, const float eye[3], GpuDdgiScene *scene) {
    typedef struct {
        float place[3], colour[3], power, area;
    } Group;
    Group groups[DDGI_DYNAMIC_GROUPS];
    int count = 0;
    if (list->flash_strength > 40.0f && camera->focal && camera->squash_num > 0 && camera->squash_den > 0) {
        const double fx = (double)camera->focal * camera->squash_num / camera->squash_den, fy = camera->focal;
        const double c[3] = {(list->flash_place[0] / list->flash_strength - camera->centre_x16 / 65536.0) / fx * DDGI_MUZZLE_DEPTH,
                             (list->flash_place[1] / list->flash_strength - camera->centre_y16 / 65536.0) / fy * DDGI_MUZZLE_DEPTH,
                             DDGI_MUZZLE_DEPTH};
        Group *g = &groups[count++];
        memset(g, 0, sizeof *g);
        g->power = DDGI_MUZZLE_POWER * fminf(1.0f, list->flash_strength / 150.0f);
        g->area = list->flash_strength;
        float back[3][3];
        psx_ddgi_pose_back(camera->rotation, back);
        for (int a = 0; a < 3; ++a) {
            double v = 0.0;
            for (int r = 0; r < 3; ++r) v += back[r][a] * (c[r] - camera->translation[r]);
            g->place[a] = (float)(v + camera->offset[a]) * g->power;
            g->colour[a] = list->flash_colour[a];
        }
    }
    for (int i = 0; i < list->card_count; ++i) {
        const GpuDdgiCard *card = &list->cards[i];
        const int abr = (card->texpage >> 5) & 3;
        if ((card->flags & 12) || !(card->flags & 1) || (abr != 1 && abr != 3)) continue;
        if (card->cover < 0.02f || card->blended < 0.3f) continue;
        float lit[3], shown[3];
        for (int k = 0; k < 3; ++k) {
            shown[k] = fminf(card->texels[k] * ((card->flags & 2) ? 1.0f : card->colour[k] * 2.0f / 255.0f), 1.0f);
            lit[k] = ddgi_linear(shown[k]);
        }
        const float wide = sqrtf(card->across[0] * card->across[0] + card->across[1] * card->across[1] +
                                 card->across[2] * card->across[2]);
        const float tall = sqrtf(card->down[0] * card->down[0] + card->down[1] * card->down[1] + card->down[2] * card->down[2]);
        /* Added pictures add up as the screen shows them: a blast of many faint layers lights as brightly as it looks. */
        const float area = 4.0f * wide * tall * card->cover,
                    power = 0.75f * ddgi_lum(shown) * (abr == 3 ? 0.25f : 1.0f) * area * card->light;
        if (power < 1.0f) continue;
        Group *g = NULL;
        for (int k = 0; k < count; ++k) {
            float place[3];
            for (int a = 0; a < 3; ++a) place[a] = groups[k].place[a] / groups[k].power;
            if (ddgi_distance2(place, card->centre) < 400.0f * 400.0f) {
                g = &groups[k];
                break;
            }
        }
        if (!g) {
            if (count == DDGI_DYNAMIC_GROUPS) continue;
            g = &groups[count++];
            memset(g, 0, sizeof *g);
        }
        for (int a = 0; a < 3; ++a) {
            g->place[a] += card->centre[a] * power;
            g->colour[a] += lit[a] * area;
        }
        g->power += power;
        g->area += area;
    }
    int used[GPU_DDGI_LAMPS] = {0};
    for (int taken = 0; taken < GPU_DDGI_DYNAMIC_LAMPS && scene->lamps < GPU_DDGI_LAMPS; ++taken) {
        int best = -1;
        float best_score = 0.0f;
        for (int k = 0; k < count; ++k) {
            if (groups[k].power <= 0.0f) continue;
            float place[3];
            for (int a = 0; a < 3; ++a) place[a] = groups[k].place[a] / groups[k].power;
            const float s = groups[k].power / (ddgi_distance2(place, eye) + 1000.0f * 1000.0f);
            if (s > best_score) {
                best_score = s;
                best = k;
            }
        }
        if (best < 0) break;
        Group *g = &groups[best];
        GpuDdgiLamp *lamp = &scene->lamp[scene->lamps++];
        memset(lamp, 0, sizeof *lamp);
        for (int a = 0; a < 3; ++a) {
            lamp->position[a] = g->place[a] / g->power;
            lamp->colour[a] = g->colour[a] / fmaxf(g->area, 1e-6f);
        }
        /* An impact is drawn on the wall it hit: its light stands a little toward the camera, in front of that wall. */
        const float away = sqrtf(ddgi_distance2(lamp->position, eye));
        const float toward = fminf(120.0f, 0.5f * away);
        for (int a = 0; a < 3; ++a) lamp->position[a] += (eye[a] - lamp->position[a]) / fmaxf(away, 1e-3f) * toward;
        lamp->power = g->power;
        lamp->size = fminf(3000.0f, 600.0f + 12.0f * sqrtf(g->power));
        lamp->dynamic = 1;
        lamp->weight = 1.0f;
        lamp->previous = -1;
        float nearest = 1200.0f * 1200.0f;
        for (int p = 0; p < s_ddgi_scene_before.lamps; ++p) {
            const GpuDdgiLamp *was = &s_ddgi_scene_before.lamp[p];
            const float d2 = ddgi_distance2(was->position, lamp->position);
            if (was->dynamic && !used[p] && d2 < nearest) {
                nearest = d2;
                lamp->previous = p;
            }
        }
        if (lamp->previous >= 0) used[lamp->previous] = 1;
        g->power = 0.0f;
    }
    /* A shot gone this frame leaves its light a moment longer: no flicker between its packets. */
    for (int p = 0; p < s_ddgi_scene_before.lamps && scene->lamps < GPU_DDGI_LAMPS; ++p) {
        const GpuDdgiLamp *was = &s_ddgi_scene_before.lamp[p];
        if (!was->dynamic || used[p] || was->weight < 0.3f) continue;
        GpuDdgiLamp *lamp = &scene->lamp[scene->lamps++];
        *lamp = *was;
        lamp->previous = p;
        lamp->weight = was->weight * 0.5f;
    }
}

int gpu_ddgi_scene(uint32_t pose, GpuDdgiScene *scene) {
    const DdgiList *list = NULL;
    for (int i = 0; i < 2; ++i)
        if (pose && s_ddgi_lists[i].pose == pose) list = &s_ddgi_lists[i];
    GteCameraPose camera;
    if (!list || !scene || !gte_pose_get(pose, &camera)) return 0;
    memset(scene, 0, sizeof *scene);
    scene->pose = pose;
    scene->before = s_ddgi_scene_before.pose;
    float eye[3], back[3][3];
    psx_ddgi_pose_back(camera.rotation, back);
    for (int a = 0; a < 3; ++a) {
        double v = 0.0;
        for (int r = 0; r < 3; ++r) v -= back[r][a] * camera.translation[r];
        eye[a] = (float)(v + camera.offset[a]);
    }
    ddgi_scene_casters(list, eye, scene);
    ddgi_scene_static_lamps(list, &camera, eye, scene, GPU_DDGI_LAMPS - GPU_DDGI_DYNAMIC_LAMPS);
    ddgi_scene_dynamic_lamps(list, &camera, eye, scene);
    s_ddgi_scene_before = *scene;
    if (ddgi_logging() && s_ddgi_stats.frames % 60u == 2u) {
        int dynamic = 0, emitters = 0;
        for (int i = 0; i < scene->lamps; ++i) dynamic += scene->lamp[i].dynamic;
        for (int i = 0; i < DDGI_EMITTERS; ++i) emitters += s_ddgi_emitters[i].key != 0;
        fprintf(stderr, "ddgi scene: pose %u cards %d casters %d lamps %d (dynamic %d) emitters kept %d flash %.0f\n", pose,
                list->card_count, scene->casters, scene->lamps, dynamic, emitters, list->flash_strength);
    }
    return 1;
}

/* With dynamic shadows the game's blob is not drawn: 1 to leave the sprite out. */
static int ddgi_blob_hidden(const WsTag *tag) { return s_ddgi_shadowing && s_ddgi_hide_blobs && tag && tag->shadow; }
