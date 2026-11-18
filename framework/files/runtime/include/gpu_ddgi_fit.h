/* A level's lights fitted to its baked vertex light by non-negative least squares over its faces. */
#ifndef PSXRECOMP_GPU_DDGI_FIT_H
#define PSXRECOMP_GPU_DDGI_FIT_H

#include "gpu_ddgi_bvh.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define DDGI_FIT_LAMPS 256
#define DDGI_FIT_CELL 600.0f      /* an enemy stands about 1400 tall */
#define DDGI_FIT_REACH 12000.0f
#define DDGI_FIT_SAMPLES 16384
#define DDGI_FIT_SWEEPS 400
#define DDGI_FIT_HOT_CELL 2000.0f   /* with the brightest quarter: 0.27 of mission 1's baked light explained, 0.15 at 1200 */
#define DDGI_FIT_ROW 48             /* lamps a face keeps: the strongest in reach that see it */
#define DDGI_FIT_SUN_TRIES 96       /* directions over the sky's half */
#define DDGI_FIT_SUN_EVERY 4        /* a direction is judged on one face in four */
#define DDGI_FIT_SUN_GAIN 0.03f     /* the share of the baked light's variance a sun must explain to be taken */

typedef struct {
    float position[3], normal[3], size, colour[3], power;
} DdgiLevelLamp;

typedef struct {
    int lamps;
    DdgiLevelLamp lamp[DDGI_FIT_LAMPS];
    float ambient, sun; /* in the lamp pass's units: its share denominator's floor, the sun's weight */
    float sun_dir[3], sun_gain; /* toward the sun found, zero for none; the share of the variance it explained alone */
    int sun_asked;              /* the game asked for this sun: the level's light did not show one */
    int samples, emitting; /* faces fitted; lamps from emitting faces, the rest stand before the brightest baked light */
    double residual, variance; /* the mean squared error left and the baked light's own, linear light */
} DdgiFit;

/* A face as the fit samples it: what lights it is fixed once, its residual follows each solve. */
typedef struct {
    float centre[3], normal[3], weight, baked, residual;
    int count;
    uint16_t lamp[DDGI_FIT_ROW];
    float share[DDGI_FIT_ROW];
} DdgiFitSample;

/* The fit's normal equations (unknowns: the ambient, each lamp, the sun) and its samples. */
#define DDGI_FIT_UNKNOWNS (DDGI_FIT_LAMPS + 2)
typedef struct {
    double ata[DDGI_FIT_UNKNOWNS * DDGI_FIT_UNKNOWNS], atb[DDGI_FIT_UNKNOWNS], x[DDGI_FIT_UNKNOWNS];
    DdgiFitSample sample[DDGI_FIT_SAMPLES];
    float sun[DDGI_FIT_SAMPLES];
} DdgiFitWork;

static inline float psx_ddgi_dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

/* Twice the triangle's area along its winding's normal, times front (+1 or -1, the game's front side). */
static inline float psx_ddgi_face_normal(const GpuDdgiTriangle *t, float front, float n[3]) {
    const float e1[3] = {t->world[1][0] - t->world[0][0], t->world[1][1] - t->world[0][1], t->world[1][2] - t->world[0][2]};
    const float e2[3] = {t->world[2][0] - t->world[0][0], t->world[2][1] - t->world[0][1], t->world[2][2] - t->world[0][2]};
    n[0] = front * (e1[1] * e2[2] - e1[2] * e2[1]);
    n[1] = front * (e1[2] * e2[0] - e1[0] * e2[2]);
    n[2] = front * (e1[0] * e2[1] - e1[1] * e2[0]);
    const float twice = sqrtf(psx_ddgi_dot3(n, n));
    if (twice > 1e-6f)
        for (int a = 0; a < 3; ++a) n[a] /= twice;
    return twice;
}

/* Either side of t between t_min and t_max along o + d s. */
static inline int psx_ddgi_ray_triangle(const float o[3], const float d[3], const GpuDdgiTriangle *t, float t_min, float t_max) {
    const float e1[3] = {t->world[1][0] - t->world[0][0], t->world[1][1] - t->world[0][1], t->world[1][2] - t->world[0][2]};
    const float e2[3] = {t->world[2][0] - t->world[0][0], t->world[2][1] - t->world[0][1], t->world[2][2] - t->world[0][2]};
    const float p[3] = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
    const float det = psx_ddgi_dot3(e1, p);
    if (fabsf(det) < 1e-9f) return 0;
    const float inv = 1.0f / det, s[3] = {o[0] - t->world[0][0], o[1] - t->world[0][1], o[2] - t->world[0][2]};
    const float u = psx_ddgi_dot3(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return 0;
    const float q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
    const float v = psx_ddgi_dot3(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return 0;
    const float along = psx_ddgi_dot3(e2, q) * inv;
    return along > t_min && along < t_max;
}

/* Any face of a BVH between t_min and t_max along o + d s, d of unit length; order is the faces as the build left them. */
static inline int psx_ddgi_bvh_blocked(const DdgiNode *nodes, const DdgiBuildFace *order, const GpuDdgiTriangle *tris,
                                       const float o[3], const float d[3], float t_min, float t_max) {
    float inv[3];
    for (int a = 0; a < 3; ++a) inv[a] = 1.0f / (fabsf(d[a]) < 1e-7f ? (d[a] < 0.0f ? -1e-7f : 1e-7f) : d[a]);
    int stack[64], sp = 0;
    stack[sp++] = 0;
    while (sp) {
        const DdgiNode *n = &nodes[stack[--sp]];
        float lo = 0.0f, hi = t_max;
        for (int a = 0; a < 3; ++a) {
            float t0 = (n->lo[a] - o[a]) * inv[a], t1 = (n->hi[a] - o[a]) * inv[a];
            if (t0 > t1) {
                const float swap = t0;
                t0 = t1;
                t1 = swap;
            }
            lo = t0 > lo ? t0 : lo;
            hi = t1 < hi ? t1 : hi;
        }
        if (lo > hi) continue;
        if (!n->count) {
            if (sp <= 62) {
                stack[sp++] = n->link;
                stack[sp++] = n->link + 1;
            }
            continue;
        }
        for (int k = n->link; k < n->link + n->count; ++k)
            if (psx_ddgi_ray_triangle(o, d, &tris[order[k].index], t_min, t_max)) return 1;
    }
    return 0;
}

/* What one unit of a lamp's power gives a point p of normal n: DDGI_LAMP_FS's share of the lamp, weight 1. */
static inline float psx_ddgi_lamp_share(const float p[3], const float n[3], const DdgiLevelLamp *lamp) {
    float l[3] = {lamp->position[0] - p[0], lamp->position[1] - p[1], lamp->position[2] - p[2]};
    const float dist = sqrtf(psx_ddgi_dot3(l, l));
    for (int a = 0; a < 3; ++a) l[a] /= dist > 1e-3f ? dist : 1e-3f;
    const float ndl = psx_ddgi_dot3(n, l);
    if (ndl <= 0.0f) return 0.0f;
    const float facing = -psx_ddgi_dot3(lamp->normal, l);
    const float side = psx_ddgi_dot3(lamp->normal, lamp->normal) > 0.25f ? (facing > 0.15f ? facing : 0.15f) : 1.0f;
    return ndl * side / (dist * dist + lamp->size * lamp->size);
}

/* Emitting faces gathered by cell into lamps: power-weighted places, a normal where the faces agree. emission is linear. */
static inline int psx_ddgi_cluster_lamps(const GpuDdgiTriangle *tris, const float (*emission)[3], int count, float front,
                                         DdgiLevelLamp *lamps, int most) {
    float weight[DDGI_FIT_LAMPS], area[DDGI_FIT_LAMPS], lo[DDGI_FIT_LAMPS][3], hi[DDGI_FIT_LAMPS][3];
    int64_t key[DDGI_FIT_LAMPS];
    int made = 0;
    if (most > DDGI_FIT_LAMPS) most = DDGI_FIT_LAMPS;
    for (int i = 0; i < count; ++i) {
        const float lum = 0.2126f * emission[i][0] + 0.7152f * emission[i][1] + 0.0722f * emission[i][2];
        if (lum <= 0.01f) continue;
        float n[3], centre[3];
        const float face = 0.5f * psx_ddgi_face_normal(&tris[i], front, n);
        if (face < 1e-3f) continue;
        for (int a = 0; a < 3; ++a) centre[a] = (tris[i].world[0][a] + tris[i].world[1][a] + tris[i].world[2][a]) / 3.0f;
        const int64_t q[3] = {(int64_t)floorf(centre[0] / DDGI_FIT_CELL), (int64_t)floorf(centre[1] / DDGI_FIT_CELL),
                              (int64_t)floorf(centre[2] / DDGI_FIT_CELL)};
        const int64_t cell = (q[0] & 0x1FFFFF) | (q[1] & 0x1FFFFF) << 21 | (q[2] & 0x1FFFFF) << 42;
        int k = 0;
        while (k < made && key[k] != cell) ++k;
        if (k == made) {
            if (made == most) continue;
            memset(&lamps[k], 0, sizeof lamps[k]);
            key[k] = cell;
            weight[k] = area[k] = 0.0f;
            for (int a = 0; a < 3; ++a) lo[k][a] = hi[k][a] = centre[a];
            ++made;
        }
        const float power = lum * face;
        for (int a = 0; a < 3; ++a) {
            lamps[k].position[a] += centre[a] * power;
            lamps[k].normal[a] += n[a] * power;
            lamps[k].colour[a] += emission[i][a] * face;
            lo[k][a] = centre[a] < lo[k][a] ? centre[a] : lo[k][a];
            hi[k][a] = centre[a] > hi[k][a] ? centre[a] : hi[k][a];
        }
        weight[k] += power;
        area[k] += face;
    }
    for (int k = 0; k < made; ++k) {
        float spread = 0.0f;
        for (int a = 0; a < 3; ++a) {
            lamps[k].position[a] /= weight[k];
            lamps[k].colour[a] /= area[k];
            spread += (hi[k][a] - lo[k][a]) * (hi[k][a] - lo[k][a]);
        }
        const float agree = sqrtf(psx_ddgi_dot3(lamps[k].normal, lamps[k].normal));
        /* Faces turned every way light round about: no normal. */
        for (int a = 0; a < 3; ++a) lamps[k].normal[a] = agree > 0.5f * weight[k] ? lamps[k].normal[a] / agree : 0.0f;
        lamps[k].size = fmaxf(60.0f, 0.5f * sqrtf(spread) + 0.5f * sqrtf(area[k]));
    }
    return made;
}

/* The baked light of a face in linear light, each channel taken out of its gamma before it is weighed; 1 for neutral 128. */
static inline float psx_ddgi_baked(const GpuDdgiTriangle *t) {
    static const float weight[3] = {0.2126f, 0.7152f, 0.0722f};
    float light = 0.0f;
    for (int k = 0; k < 3; ++k)
        for (int c = 0; c < 3; ++c) light += weight[c] * powf((float)t->colour[k][c] / 128.0f, 2.2f);
    return light / 3.0f;
}

/* Lamps a hand-lit level hides: the brightest quarter of its baked light gathered by cell, each lamp out in front of it,
 * most of the way to what the face looks at. */
static inline int psx_ddgi_hot_lamps(const DdgiNode *nodes, const DdgiBuildFace *order, const GpuDdgiTriangle *tris, int count,
                                     float front, DdgiLevelLamp *lamps, int most) {
    static const float kStandoffs[] = {3000.0f, 2000.0f, 1400.0f, 1000.0f, 700.0f, 500.0f, 300.0f};
    enum { kBins = 256 };
    int histogram[kBins] = {0}, total = 0;
    for (int i = 0; i < count; ++i) {
        const float b = psx_ddgi_baked(&tris[i]);
        ++histogram[b >= 5.0f ? kBins - 1 : (int)(b / 5.0f * (float)kBins)];
        ++total;
    }
    int above = 0, bin = kBins - 1;
    while (bin > 0 && (above + histogram[bin]) * 4 <= total) above += histogram[bin--];
    const float threshold = 5.0f * (float)bin / (float)kBins;
    float weight[DDGI_FIT_LAMPS];
    int64_t key[DDGI_FIT_LAMPS];
    int made = 0;
    if (most > DDGI_FIT_LAMPS) most = DDGI_FIT_LAMPS;
    for (int i = 0; i < count && most > 0; ++i) {
        const float b = psx_ddgi_baked(&tris[i]);
        if (b < threshold) continue;
        float n[3], centre[3];
        const float face = 0.5f * psx_ddgi_face_normal(&tris[i], front, n);
        if (face < 1.0f) continue;
        for (int a = 0; a < 3; ++a) centre[a] = (tris[i].world[0][a] + tris[i].world[1][a] + tris[i].world[2][a]) / 3.0f;
        const int64_t q[3] = {(int64_t)floorf(centre[0] / DDGI_FIT_HOT_CELL), (int64_t)floorf(centre[1] / DDGI_FIT_HOT_CELL),
                              (int64_t)floorf(centre[2] / DDGI_FIT_HOT_CELL)};
        const int64_t cell = (q[0] & 0x1FFFFF) | (q[1] & 0x1FFFFF) << 21 | (q[2] & 0x1FFFFF) << 42;
        int k = 0;
        while (k < made && key[k] != cell) ++k;
        if (k == made) {
            if (made == most) continue;
            memset(&lamps[k], 0, sizeof lamps[k]);
            key[k] = cell;
            weight[k] = 0.0f;
            ++made;
        }
        float standoff = 150.0f;
        for (int s = 0; s < (int)(sizeof kStandoffs / sizeof kStandoffs[0]); ++s) {
            const float from[3] = {centre[0] + n[0] * 8.0f, centre[1] + n[1] * 8.0f, centre[2] + n[2] * 8.0f};
            if (!psx_ddgi_bvh_blocked(nodes, order, tris, from, n, 1.0f, kStandoffs[s])) {
                standoff = 0.85f * kStandoffs[s];
                break;
            }
        }
        const float w = b * face;
        for (int a = 0; a < 3; ++a) {
            lamps[k].position[a] += (centre[a] + n[a] * standoff) * w;
            lamps[k].colour[a] += w * (float)(tris[i].colour[0][a] + tris[i].colour[1][a] + tris[i].colour[2][a]) / (3.0f * 255.0f);
        }
        weight[k] += w;
    }
    for (int k = 0; k < made; ++k) {
        float top = 0.0f;
        for (int a = 0; a < 3; ++a) {
            lamps[k].position[a] /= weight[k];
            lamps[k].colour[a] /= weight[k];
            top = lamps[k].colour[a] > top ? lamps[k].colour[a] : top;
        }
        for (int a = 0; a < 3; ++a) lamps[k].colour[a] = top > 0.0f ? lamps[k].colour[a] / top : 1.0f;
        lamps[k].size = 300.0f;
    }
    return made;
}

/* Non-negative x minimising |A x - b|^2 from its normal equations, by coordinate descent from 0. */
static inline void psx_ddgi_nnls(const double *ata, const double *atb, int n, double *x, int sweeps) {
    for (int i = 0; i < n; ++i) x[i] = 0.0;
    for (int s = 0; s < sweeps; ++s)
        for (int j = 0; j < n; ++j) {
            const double diagonal = ata[j * n + j];
            if (diagonal <= 0.0) continue;
            double gradient = -atb[j];
            for (int k = 0; k < n; ++k) gradient += ata[j * n + k] * x[k];
            const double next = x[j] - gradient / diagonal;
            x[j] = next > 0.0 ? next : 0.0;
        }
}

/* The faces sampled once: place, front, weight, baked light, and the strongest lamps in reach that see them. */
static inline int psx_ddgi_fit_sample(const DdgiNode *nodes, const DdgiBuildFace *order, const GpuDdgiTriangle *tris, int count,
                                      float front, const DdgiFit *fit, DdgiFitWork *work) {
    const int stride = count > DDGI_FIT_SAMPLES ? (count + DDGI_FIT_SAMPLES - 1) / DDGI_FIT_SAMPLES : 1;
    int samples = 0;
    for (int i = 0; i < count && samples < DDGI_FIT_SAMPLES; i += stride) {
        const GpuDdgiTriangle *t = &tris[i];
        DdgiFitSample *s = &work->sample[samples];
        const float area = 0.5f * psx_ddgi_face_normal(t, front, s->normal);
        if (area < 1.0f) continue;
        float from[3];
        for (int a = 0; a < 3; ++a) {
            s->centre[a] = (t->world[0][a] + t->world[1][a] + t->world[2][a]) / 3.0f;
            from[a] = s->centre[a] + s->normal[a] * 8.0f;
        }
        s->weight = area;
        s->baked = psx_ddgi_baked(t);
        s->count = 0;
        for (int k = 0; k < fit->lamps; ++k) {
            const DdgiLevelLamp *lamp = &fit->lamp[k];
            float to[3] = {lamp->position[0] - from[0], lamp->position[1] - from[1], lamp->position[2] - from[2]};
            const float dist = sqrtf(psx_ddgi_dot3(to, to));
            if (dist > DDGI_FIT_REACH || dist < 40.0f) continue;
            const float g = psx_ddgi_lamp_share(s->centre, s->normal, lamp);
            int slot = s->count;
            if (slot == DDGI_FIT_ROW) { /* full: the weakest gives way */
                slot = 0;
                for (int j = 1; j < DDGI_FIT_ROW; ++j)
                    if (s->share[j] < s->share[slot]) slot = j;
                if (g <= s->share[slot]) continue;
            }
            if (g <= 0.0f) continue;
            for (int a = 0; a < 3; ++a) to[a] /= dist;
            if (psx_ddgi_bvh_blocked(nodes, order, tris, from, to, 2.0f, dist * 0.95f - 16.0f)) continue;
            s->lamp[slot] = (uint16_t)k;
            s->share[slot] = g;
            if (slot == s->count) ++s->count;
        }
        ++samples;
    }
    return samples;
}

/* The ambient, the lamps' powers and, with sun given (each sample's share), the sun's, by non-negative least squares over
 * the samples; each sample's residual left behind. */
static inline void psx_ddgi_fit_solve(DdgiFit *fit, DdgiFitWork *work, int samples, const float *sun) {
    const int n = fit->lamps + 2, sun_at = fit->lamps + 1;
    memset(work->ata, 0, sizeof(double) * (size_t)n * (size_t)n);
    memset(work->atb, 0, sizeof(double) * (size_t)n);
    double sum_b = 0.0, sum_w = 0.0, sum_mean = 0.0;
    for (int i = 0; i < samples; ++i) {
        const DdgiFitSample *s = &work->sample[i];
        int at[DDGI_FIT_ROW + 2], used = 0;
        double row[DDGI_FIT_ROW + 2];
        at[used] = 0;
        row[used++] = 1.0;
        for (int j = 0; j < s->count; ++j) {
            at[used] = s->lamp[j] + 1;
            row[used++] = s->share[j];
        }
        if (sun && sun[i] > 0.0f) {
            at[used] = sun_at;
            row[used++] = sun[i];
        }
        const double b = s->baked, w = s->weight;
        for (int p = 0; p < used; ++p) {
            work->atb[at[p]] += w * row[p] * b;
            for (int q = 0; q < used; ++q) work->ata[at[p] * n + at[q]] += w * row[p] * row[q];
        }
        sum_b += w * b * b;
        sum_mean += w * b;
        sum_w += w;
    }
    psx_ddgi_nnls(work->ata, work->atb, n, work->x, DDGI_FIT_SWEEPS);
    double error = 0.0;
    for (int i = 0; i < samples; ++i) {
        DdgiFitSample *s = &work->sample[i];
        double model = work->x[0] + (sun ? work->x[sun_at] * sun[i] : 0.0);
        for (int j = 0; j < s->count; ++j) model += work->x[s->lamp[j] + 1] * s->share[j];
        s->residual = (float)(s->baked - model);
        error += s->weight * (double)s->residual * s->residual;
    }
    fit->samples = samples;
    fit->residual = sum_w > 0.0 ? error / sum_w : 0.0;
    fit->variance = sum_w > 0.0 ? sum_b / sum_w - (sum_mean / sum_w) * (sum_mean / sum_w) : 0.0;
    fit->ambient = (float)work->x[0];
    for (int k = 0; k < fit->lamps; ++k) fit->lamp[k].power = (float)work->x[k + 1];
    fit->sun = sun ? (float)work->x[sun_at] : 0.0f;
}

/* The sun's share of a sample along dir (unit, toward the sun), as the lamp pass takes it: its facing where nothing hides it. */
static inline float psx_ddgi_sun_share(const DdgiNode *nodes, const DdgiBuildFace *order, const GpuDdgiTriangle *tris,
                                       const DdgiFitSample *s, const float dir[3]) {
    const float facing = psx_ddgi_dot3(s->normal, dir);
    if (facing <= 0.0f) return 0.0f;
    const float from[3] = {s->centre[0] + s->normal[0] * 8.0f, s->centre[1] + s->normal[1] * 8.0f, s->centre[2] + s->normal[2] * 8.0f};
    return psx_ddgi_bvh_blocked(nodes, order, tris, from, dir, 1.0f, 40000.0f) ? 0.0f : facing;
}

/* Direction i of tries spread evenly over the sky from 10 degrees up to straight up; world y grows downward. */
static inline void psx_ddgi_sun_try(int i, int tries, float dir[3]) {
    const float low = 0.17364818f, up = low + (1.0f - low) * ((float)i + 0.5f) / (float)tries;
    const float round = sqrtf(1.0f - up * up), turn = 2.3999632f * (float)i;
    dir[0] = round * sinf(turn);
    dir[1] = -up;
    dir[2] = round * cosf(turn);
}

/* Fits the lamps' powers and the ambient to the faces' baked light; with find_sun, also the sun whose light explains most of
 * the baked light, kept when it explains DDGI_FIT_SUN_GAIN of its variance. asked (unit, toward the sun, or NULL): the game wants
 * a sun on this level whatever the light shows, along asked where none was found (mission 1's weak find put a 12 degree sun in
 * the player's face), at least as strong as the light every surface has. */
static inline void psx_ddgi_fit_level(const DdgiNode *nodes, const DdgiBuildFace *order, const GpuDdgiTriangle *tris, int count,
                                      float front, int find_sun, const float *asked, DdgiFit *fit, DdgiFitWork *work) {
    const int samples = psx_ddgi_fit_sample(nodes, order, tris, count, front, fit, work);
    psx_ddgi_fit_solve(fit, work, samples, NULL);
    fit->sun_dir[0] = fit->sun_dir[1] = fit->sun_dir[2] = 0.0f;
    fit->sun_gain = 0.0f;
    fit->sun_asked = 0;
    if (!find_sun || samples == 0) return;
    /* Each direction's share of the baked light's variance alone, cov(g, b)^2 / var(g). Taken after the lamps, the lamps standing
     * before sunlit ground had already explained it (mission 1's roof gave 0.004); a sun only lights faces open to the sky, so an
     * indoor level finds none either way. */
    double best = 0.0, weights = 0.0, sum_b = 0.0;
    float chosen[3] = {0.0f, 0.0f, 0.0f};
    for (int i = 0; i < samples; i += DDGI_FIT_SUN_EVERY) {
        weights += work->sample[i].weight;
        sum_b += (double)work->sample[i].weight * work->sample[i].baked;
    }
    for (int t = 0; t < DDGI_FIT_SUN_TRIES; ++t) {
        float dir[3];
        psx_ddgi_sun_try(t, DDGI_FIT_SUN_TRIES, dir);
        double sg = 0.0, sgg = 0.0, sgb = 0.0;
        for (int i = 0; i < samples; i += DDGI_FIT_SUN_EVERY) {
            const DdgiFitSample *s = &work->sample[i];
            const double g = psx_ddgi_sun_share(nodes, order, tris, s, dir);
            sg += s->weight * g;
            sgg += s->weight * g * g;
            sgb += s->weight * g * s->baked;
        }
        const double cov = sgb - sg * sum_b / weights, var = sgg - sg * sg / weights;
        const double gain = cov > 0.0 && var > 0.0 ? cov * cov / var : 0.0;
        if (gain > best) {
            best = gain;
            memcpy(chosen, dir, sizeof chosen);
        }
    }
    fit->sun_gain = fit->variance > 0.0 && weights > 0.0 ? (float)(best / (fit->variance * weights)) : 0.0f;
    const int found = fit->sun_gain >= DDGI_FIT_SUN_GAIN;
    if (!found && !asked) return;
    if (!found) memcpy(chosen, asked, sizeof chosen);
    for (int i = 0; i < samples; ++i) work->sun[i] = psx_ddgi_sun_share(nodes, order, tris, &work->sample[i], chosen);
    psx_ddgi_fit_solve(fit, work, samples, work->sun);
    if (!found) {
        fit->sun_asked = 1;
        if (fit->sun < fit->ambient) fit->sun = fit->ambient;
    }
    if (fit->sun > 0.0f) memcpy(fit->sun_dir, chosen, sizeof chosen);
}

#endif
