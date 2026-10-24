/* The BVHs probe lighting traces: the frame's by median splits, the level's by binned SAH, and smooth corner normals. */
#ifndef PSXRECOMP_GPU_DDGI_BVH_H
#define PSXRECOMP_GPU_DDGI_BVH_H

#include "gpu_ddgi.h"

#include <math.h>
#include <stdint.h>

typedef struct {
    float lo[3], hi[3];
    int link, count; /* inner: first of two children; leaf: first triangle and how many */
} DdgiNode;

/* A face as a BVH build moves it: its box, its centre and its place in the list it came from. */
typedef struct {
    float lo[3], hi[3], centre[3];
    int index;
} DdgiBuildFace;

/* A build's nodes and faces; node_count grows as it splits, node 0 the root. */
typedef struct {
    DdgiNode *nodes;
    DdgiBuildFace *faces;
    int node_count;
} DdgiBvhBuild;

#define DDGI_LEAF_FACES 4
#define DDGI_LEVEL_BINS 16
#define DDGI_LEVEL_DEPTH 28 /* the rays' stack holds 32 */
#define DDGI_MIN(a, b) ((a) < (b) ? (a) : (b)) /* fminf and fmaxf are library calls here: half of the SAH build */
#define DDGI_MAX(a, b) ((a) > (b) ? (a) : (b))

/* A median split's halves without sorting them: qsort at every level was most of the frame's build. */
static inline void psx_ddgi_select(DdgiBuildFace *faces, int lo, int hi, int nth, int axis) {
    while (hi > lo) {
        const float a = faces[lo].centre[axis], b = faces[(lo + hi) / 2].centre[axis], c = faces[hi].centre[axis];
        const float pivot = a < b ? (b < c ? b : a < c ? c : a) : (a < c ? a : b < c ? c : b);
        int i = lo, j = hi;
        while (i <= j) {
            while (faces[i].centre[axis] < pivot) ++i;
            while (faces[j].centre[axis] > pivot) --j;
            if (i <= j) {
                const DdgiBuildFace swap = faces[i];
                faces[i++] = faces[j];
                faces[j--] = swap;
            }
        }
        if (nth <= j) hi = j;
        else if (nth >= i) lo = i;
        else return;
    }
}

static inline void psx_ddgi_face_of(DdgiBuildFace *f, const GpuDdgiTriangle *t, int index) {
    for (int a = 0; a < 3; ++a) {
        f->centre[a] = (t->world[0][a] + t->world[1][a] + t->world[2][a]) / 3.0f;
        f->lo[a] = f->hi[a] = t->world[0][a];
        for (int k = 1; k < 3; ++k) {
            if (t->world[k][a] < f->lo[a]) f->lo[a] = t->world[k][a];
            if (t->world[k][a] > f->hi[a]) f->hi[a] = t->world[k][a];
        }
    }
    f->index = index;
}

static inline void psx_ddgi_leaf(DdgiNode *n, const DdgiBuildFace *faces, int first, int count) {
    for (int a = 0; a < 3; ++a) n->lo[a] = 1e30f, n->hi[a] = -1e30f;
    for (int i = first; i < first + count; ++i)
        for (int a = 0; a < 3; ++a) {
            if (faces[i].lo[a] < n->lo[a]) n->lo[a] = faces[i].lo[a];
            if (faces[i].hi[a] > n->hi[a]) n->hi[a] = faces[i].hi[a];
        }
    n->link = first;
    n->count = count;
}

static inline void psx_ddgi_centre_box(const DdgiBuildFace *faces, int first, int count, float clo[3], float chi[3]) {
    for (int a = 0; a < 3; ++a) clo[a] = 1e30f, chi[a] = -1e30f;
    for (int i = first; i < first + count; ++i)
        for (int a = 0; a < 3; ++a) {
            const float c = faces[i].centre[a];
            if (c < clo[a]) clo[a] = c;
            if (c > chi[a]) chi[a] = c;
        }
}

/* The frame's BVH: halves at the median of the widest spread of centres. */
static inline void psx_ddgi_median_split(DdgiBvhBuild *build, int node, int first, int count) {
    DdgiNode *n = &build->nodes[node];
    float clo[3], chi[3];
    psx_ddgi_centre_box(build->faces, first, count, clo, chi);
    int axis = 0;
    for (int a = 1; a < 3; ++a)
        if (chi[a] - clo[a] > chi[axis] - clo[axis]) axis = a;
    if (count <= DDGI_LEAF_FACES || chi[axis] - clo[axis] < 1e-3f) {
        psx_ddgi_leaf(n, build->faces, first, count);
        return;
    }
    const int left = build->node_count, half = count / 2;
    psx_ddgi_select(build->faces, first, first + count - 1, first + half, axis);
    build->node_count += 2;
    n->link = left;
    n->count = 0;
    psx_ddgi_median_split(build, left, first, half);
    psx_ddgi_median_split(build, left + 1, first + half, count - half);
    const DdgiNode *l = &build->nodes[left], *r = &build->nodes[left + 1];
    for (int a = 0; a < 3; ++a) {
        n->lo[a] = l->lo[a] < r->lo[a] ? l->lo[a] : r->lo[a];
        n->hi[a] = l->hi[a] > r->hi[a] ? l->hi[a] : r->hi[a];
    }
}

static inline float psx_ddgi_half_area(const float lo[3], const float hi[3]) {
    const float x = hi[0] - lo[0], y = hi[1] - lo[1], z = hi[2] - lo[2];
    return x * y + y * z + z * x;
}

static inline int psx_ddgi_level_bin(float centre, float lo, float scale, int bins) {
    const int b = (int)((centre - lo) * scale);
    return b < 0 ? 0 : b >= bins ? bins - 1 : b;
}

/* The level's BVH: binned SAH, 17% off mission 2's lamp pass against Morton order; the median where a leaf would outgrow the
 * rays' stack. */
static inline void psx_ddgi_sah_split(DdgiBvhBuild *build, int node, int first, int count, int depth) {
    DdgiNode *n = &build->nodes[node];
    DdgiBuildFace *faces = build->faces;
    if (count <= DDGI_LEAF_FACES) {
        psx_ddgi_leaf(n, faces, first, count);
        return;
    }
    float clo[3], chi[3];
    psx_ddgi_centre_box(faces, first, count, clo, chi);
    int medians = 0;
    for (int c = count; c > DDGI_LEAF_FACES; c = (c + 1) / 2) ++medians;
    /* Fewer bins for a small node: 12.6 to 10.9 ms on mission 2. */
    int axis = -1, cut = 0, bins = count / 2 < DDGI_LEVEL_BINS ? count / 2 : DDGI_LEVEL_BINS;
    float scale[3];
    if (count > 8 && depth + 1 + medians <= DDGI_LEVEL_DEPTH) {
        int bin_count[3][DDGI_LEVEL_BINS] = {{0}};
        float bin_lo[3][DDGI_LEVEL_BINS][3], bin_hi[3][DDGI_LEVEL_BINS][3];
        for (int a = 0; a < 3; ++a) {
            scale[a] = chi[a] - clo[a] < 1e-3f ? 0.0f : (float)bins / (chi[a] - clo[a]) * 0.99999f;
            for (int b = 0; b < bins; ++b)
                for (int k = 0; k < 3; ++k) bin_lo[a][b][k] = 1e30f, bin_hi[a][b][k] = -1e30f;
        }
        for (int i = first; i < first + count; ++i) {
            const DdgiBuildFace *f = &faces[i];
            for (int a = 0; a < 3; ++a) {
                const int b = psx_ddgi_level_bin(f->centre[a], clo[a], scale[a], bins);
                ++bin_count[a][b];
                for (int k = 0; k < 3; ++k) {
                    if (f->lo[k] < bin_lo[a][b][k]) bin_lo[a][b][k] = f->lo[k];
                    if (f->hi[k] > bin_hi[a][b][k]) bin_hi[a][b][k] = f->hi[k];
                }
            }
        }
        float best = 1e30f;
        for (int a = 0; a < 3; ++a) {
            if (scale[a] <= 0.0f) continue;
            int right_count[DDGI_LEVEL_BINS], seen = 0;
            float right_area[DDGI_LEVEL_BINS], lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
            for (int b = bins - 1; b > 0; --b) {
                seen += bin_count[a][b];
                for (int k = 0; k < 3; ++k) lo[k] = DDGI_MIN(lo[k], bin_lo[a][b][k]), hi[k] = DDGI_MAX(hi[k], bin_hi[a][b][k]);
                right_count[b] = seen;
                right_area[b] = seen ? psx_ddgi_half_area(lo, hi) : 0.0f;
            }
            for (int k = 0; k < 3; ++k) lo[k] = 1e30f, hi[k] = -1e30f;
            seen = 0;
            for (int b = 0; b < bins - 1; ++b) {
                seen += bin_count[a][b];
                for (int k = 0; k < 3; ++k) lo[k] = DDGI_MIN(lo[k], bin_lo[a][b][k]), hi[k] = DDGI_MAX(hi[k], bin_hi[a][b][k]);
                if (!seen || !right_count[b + 1]) continue;
                const float cost = psx_ddgi_half_area(lo, hi) * (float)seen + right_area[b + 1] * (float)right_count[b + 1];
                if (cost < best) best = cost, axis = a, cut = b;
            }
        }
    }
    int half = 0;
    if (axis >= 0) {
        int i = first, j = first + count - 1;
        while (i <= j) {
            if (psx_ddgi_level_bin(faces[i].centre[axis], clo[axis], scale[axis], bins) <= cut) {
                ++i;
                continue;
            }
            const DdgiBuildFace swap = faces[i];
            faces[i] = faces[j];
            faces[j--] = swap;
        }
        half = i - first;
    }
    if (half <= 0 || half >= count) {
        int widest = 0;
        for (int a = 1; a < 3; ++a)
            if (chi[a] - clo[a] > chi[widest] - clo[widest]) widest = a;
        half = count / 2;
        psx_ddgi_select(faces, first, first + count - 1, first + half, widest);
    }
    const int left = build->node_count;
    build->node_count += 2;
    n->link = left;
    n->count = 0;
    psx_ddgi_sah_split(build, left, first, half, depth + 1);
    psx_ddgi_sah_split(build, left + 1, first + half, count - half, depth + 1);
    const DdgiNode *l = &build->nodes[left], *r = &build->nodes[left + 1];
    for (int k = 0; k < 3; ++k) {
        n->lo[k] = DDGI_MIN(l->lo[k], r->lo[k]);
        n->hi[k] = DDGI_MAX(l->hi[k], r->hi[k]);
    }
}

/* lroundf's value without the library call, for the callers' values (|v| < 2^24) under the default rounding. */
static inline int64_t psx_ddgi_round(float v) { return (int64_t)((double)v + (v < 0.0f ? -0.5 : 0.5)); }

/* A unit normal in 16 bits, octahedral: u in the low byte, v in the high, as the shaders' oct16 reads it. */
static inline uint16_t psx_ddgi_oct16(const float n[3]) {
    const float s = fabsf(n[0]) + fabsf(n[1]) + fabsf(n[2]);
    float u = n[0] / s, v = n[1] / s;
    if (n[2] < 0.0f) {
        const float pu = (1.0f - fabsf(v)) * (u >= 0.0f ? 1.0f : -1.0f), pv = (1.0f - fabsf(u)) * (v >= 0.0f ? 1.0f : -1.0f);
        u = pu;
        v = pv;
    }
    const int qu = (int)psx_ddgi_round((u * 0.5f + 0.5f) * 255.0f), qv = (int)psx_ddgi_round((v * 0.5f + 0.5f) * 255.0f);
    return (uint16_t)((qu < 0 ? 0 : qu > 255 ? 255 : qu) | (qv < 0 ? 0 : qv > 255 ? 255 : qv) << 8);
}

/* A corner by its world place rounded to whole units, 21 bits an axis, bit 63 set. */
static inline uint64_t psx_ddgi_corner_key(const float w[3]) {
    return (uint64_t)(psx_ddgi_round(w[0]) & 0x1FFFFF) | (uint64_t)(psx_ddgi_round(w[1]) & 0x1FFFFF) << 21 |
           (uint64_t)(psx_ddgi_round(w[2]) & 0x1FFFFF) << 42 | 1ull << 63;
}

#define DDGI_CORNER_SLOTS 65536

/* Room for smooth normals of up to the caller's count of faces: face and normal a face, next and slot a corner, head and
 * key DDGI_CORNER_SLOTS. */
typedef struct {
    float (*face)[4]; /* the face's normal toward the eye, its length twice the area; w its unit length */
    int *head, *next, *slot;
    uint64_t *key;
    uint16_t (*normal)[3];
} DdgiCorners;

/* A gouraud-lit terrain has no edges between its faces: a corner's normal is the area-weighted mean of the faces that meet
 * there at less than 45 degrees to its own, so walls keep their corners and the ground is lit smoothly. */
static inline void psx_ddgi_corner_normals(const DdgiCorners *work, const GpuDdgiTriangle *tris, int count, const float eye[3]) {
    uint32_t slots = 1024;
    while (slots < (uint32_t)count * 6u && slots < DDGI_CORNER_SLOTS) slots <<= 1;
    const uint32_t mask = slots - 1;
    for (uint32_t i = 0; i < slots; ++i) work->head[i] = -1;
    for (int i = 0; i < count; ++i) {
        const float *w0 = tris[i].world[0], *w1 = tris[i].world[1], *w2 = tris[i].world[2];
        const float e1[3] = {w1[0] - w0[0], w1[1] - w0[1], w1[2] - w0[2]}, e2[3] = {w2[0] - w0[0], w2[1] - w0[1], w2[2] - w0[2]};
        float *f = work->face[i];
        f[0] = e1[1] * e2[2] - e1[2] * e2[1];
        f[1] = e1[2] * e2[0] - e1[0] * e2[2];
        f[2] = e1[0] * e2[1] - e1[1] * e2[0];
        if ((eye[0] - w0[0]) * f[0] + (eye[1] - w0[1]) * f[1] + (eye[2] - w0[2]) * f[2] < 0.0f) f[0] = -f[0], f[1] = -f[1], f[2] = -f[2];
        f[3] = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        for (int k = 0; k < 3; ++k) {
            const uint64_t key = psx_ddgi_corner_key(tris[i].world[k]);
            uint32_t at = (uint32_t)(key ^ (key >> 23) ^ (key >> 41)) & mask;
            while (work->head[at] >= 0 && work->key[at] != key) at = (at + 1) & mask;
            if (work->head[at] < 0) work->key[at] = key;
            work->next[i * 3 + k] = work->head[at];
            work->head[at] = i * 3 + k;
            work->slot[i * 3 + k] = (int)at;
        }
    }
    for (int i = 0; i < count; ++i) {
        const float *f = work->face[i];
        for (int k = 0; k < 3; ++k) {
            if (f[3] <= 0.0f) {
                work->normal[i][k] = 0;
                continue;
            }
            float sum[3] = {0.0f, 0.0f, 0.0f};
            for (int c = work->head[work->slot[i * 3 + k]]; c >= 0; c = work->next[c]) {
                const float *g = work->face[c / 3];
                if (g[3] <= 0.0f || f[0] * g[0] + f[1] * g[1] + f[2] * g[2] < 0.7071f * f[3] * g[3]) continue;
                sum[0] += g[0], sum[1] += g[1], sum[2] += g[2];
            }
            const float length = sqrtf(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
            if (length <= 0.0f) {
                work->normal[i][k] = 0;
                continue;
            }
            sum[0] /= length, sum[1] /= length, sum[2] /= length;
            work->normal[i][k] = psx_ddgi_oct16(sum);
        }
    }
}

#endif
