/* What the game's own picture shows at each pixel of a frame, known before the frame is drawn: a pass over the display
 * list fills it, so a choice about a face made while drawing reads the whole frame, not what is drawn so far. Free of
 * renderer state so tests run the same code the renderer does. */
#ifndef GPU_COVERAGE_H
#define GPU_COVERAGE_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_COVERAGE_WIDTH 1024
#define PSX_COVERAGE_HEIGHT 512
#define PSX_COVERAGE_REACH_PX 3 /* frame pixels: a tile is placed and a face is drawn apart from the game's integers */
#define PSX_COVERAGE_PIECES 12 /* past both edges at most two runs of ticks each: nine pieces */
#define PSX_COVERAGE_SKY_KEYS 256
#define PSX_COVERAGE_WORDS (PSX_COVERAGE_WIDTH / 64)

enum {
    PSX_COVER_NONE = 0,  /* nothing drawn: the clear colour shows */
    PSX_COVER_SKY = 1,   /* a sky tile */
    PSX_COVER_WORLD = 2, /* an opaque polygon of the game's own */
    PSX_COVER_REACH = 3, /* a polygon only the widened walk admits, where nothing else is */
    PSX_COVER_FLAT = 4   /* an opaque rectangle that is no sky tile */
};

typedef struct {
    int32_t left, top, right, bottom; /* inclusive, VRAM pixels */
} PsxCoverageArea;

/* The classes of one frame buffer, and for the questions below the columns where sky lies within the reach, row by row. */
typedef struct {
    uint8_t cls[PSX_COVERAGE_HEIGHT][PSX_COVERAGE_WIDTH];
    PsxCoverageArea area;
    int built, dirty;
    uint64_t sky[PSX_COVERAGE_HEIGHT][PSX_COVERAGE_WORDS];  /* a bit a column: sky within the reach in this row */
    uint64_t sky_band[PSX_COVERAGE_HEIGHT / 16][PSX_COVERAGE_WORDS]; /* the same for any of 16 rows */
} PsxCoverage;

static inline PsxCoverageArea psx_coverage_clip(PsxCoverageArea area) {
    if (area.left < 0) area.left = 0;
    if (area.top < 0) area.top = 0;
    if (area.right > PSX_COVERAGE_WIDTH - 1) area.right = PSX_COVERAGE_WIDTH - 1;
    if (area.bottom > PSX_COVERAGE_HEIGHT - 1) area.bottom = PSX_COVERAGE_HEIGHT - 1;
    return area;
}

/* A new frame buffer: its area is cleared and the answers forgotten. */
static inline void psx_coverage_begin(PsxCoverage *cov, PsxCoverageArea area) {
    area = psx_coverage_clip(area);
    cov->area = area;
    cov->built = 0;
    cov->dirty = 1;
    for (int32_t y = area.top; y <= area.bottom; ++y)
        if (area.right >= area.left) memset(&cov->cls[y][area.left], PSX_COVER_NONE, (size_t)(area.right - area.left + 1));
}

static inline void psx_coverage_put(PsxCoverage *cov, int32_t x, int32_t y, uint8_t cls) {
    uint8_t *at = &cov->cls[y][x];
    if (cls != PSX_COVER_REACH || *at == PSX_COVER_NONE) *at = cls; /* the game's own draws cover all; the widened walk's only the void */
}

/* A triangle in VRAM pixels, inside the area the frame draws in: a pixel is covered when its centre lies inside, or on a
 * top or left edge. Each row's run of columns is carried from the row above and filled at once: the pass runs over every frame's list. */
static inline void psx_coverage_triangle(PsxCoverage *cov, PsxCoverageArea draw, const int32_t x[3], const int32_t y[3], uint8_t cls) {
    draw = psx_coverage_clip(draw);
    const int64_t area2 = (int64_t)(x[1] - x[0]) * (y[2] - y[0]) - (int64_t)(y[1] - y[0]) * (x[2] - x[0]);
    if (area2 == 0) return;
    const int order[3] = {0, area2 > 0 ? 1 : 2, area2 > 0 ? 2 : 1};
    int32_t low_x = x[0], high_x = x[0], low_y = y[0], high_y = y[0];
    for (int i = 1; i < 3; ++i) {
        low_x = x[i] < low_x ? x[i] : low_x;
        high_x = x[i] > high_x ? x[i] : high_x;
        low_y = y[i] < low_y ? y[i] : low_y;
        high_y = y[i] > high_y ? y[i] : high_y;
    }
    if (low_x < draw.left) low_x = draw.left;
    if (low_y < draw.top) low_y = draw.top;
    if (high_x > draw.right) high_x = draw.right;
    if (high_y > draw.bottom) high_y = draw.bottom;
    if (low_x > high_x || low_y > high_y) return;
    /* Column x - low_x of a row passes edge e when step_x * (x - low_x) >= least - row, row = the edge's value at low_x.
     * u = row - least steps by step_y a row, and floor(u / |step_x|) with its remainder is carried instead of divided. */
    int64_t u[3], step_y[3], div[3], quot[3], rest[3], whole[3], part[3];
    int sign[3];
    for (int e = 0; e < 3; ++e) {
        const int32_t ax = x[order[e]], ay = y[order[e]], bx = x[order[(e + 1) % 3]], by = y[order[(e + 1) % 3]];
        const int64_t dx = bx - ax, dy = by - ay;
        const int64_t row = 2 * dx * ((int64_t)low_y * 2 + 1 - (int64_t)ay * 2) - 2 * dy * ((int64_t)low_x * 2 + 1 - (int64_t)ax * 2);
        const int64_t step_x = -4 * dy;
        u[e] = row - (dy < 0 || (dy == 0 && dx > 0) ? 0 : 1);
        step_y[e] = 4 * dx;
        sign[e] = step_x > 0 ? 1 : step_x < 0 ? -1 : 0;
        div[e] = step_x > 0 ? step_x : step_x < 0 ? -step_x : 1;
        quot[e] = u[e] / div[e] - (u[e] % div[e] < 0 ? 1 : 0);
        rest[e] = u[e] - quot[e] * div[e];
        whole[e] = step_y[e] / div[e] - (step_y[e] % div[e] < 0 ? 1 : 0);
        part[e] = step_y[e] - whole[e] * div[e];
    }
    cov->dirty = 1;
    for (int32_t py = low_y; py <= high_y; ++py) {
        int64_t from = low_x, to = high_x; /* the row's columns where all three edges hold */
        for (int e = 0; e < 3; ++e) {
            if (sign[e] == 0) {
                if (u[e] < 0) to = from - 1;
            } else if (sign[e] > 0) {
                if (quot[e] < 0 && low_x - quot[e] > from) from = low_x - quot[e]; /* x - low_x >= ceil(-u / s) = -floor(u / s) */
            } else if (low_x + quot[e] < to) {
                to = low_x + quot[e]; /* x - low_x <= floor(u / s) */
            }
        }
        if (from <= to) {
            uint8_t *line = cov->cls[py];
            if (cls != PSX_COVER_REACH)
                memset(&line[from], cls, (size_t)(to - from + 1));
            else
                for (int64_t px = from; px <= to; ++px)
                    if (line[px] == PSX_COVER_NONE) line[px] = cls;
        }
        for (int e = 0; e < 3; ++e) {
            u[e] += step_y[e];
            quot[e] += whole[e];
            rest[e] += part[e];
            if (rest[e] >= div[e]) {
                rest[e] -= div[e];
                ++quot[e];
            }
        }
    }
}
/* A rectangle [x, x + w) by [y, y + h) in VRAM pixels, inside the draw area. */
static inline void psx_coverage_rect(PsxCoverage *cov, PsxCoverageArea draw, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t cls) {
    draw = psx_coverage_clip(draw);
    const int32_t left = x > draw.left ? x : draw.left, top = y > draw.top ? y : draw.top;
    const int32_t right = x + w - 1 < draw.right ? x + w - 1 : draw.right, bottom = y + h - 1 < draw.bottom ? y + h - 1 : draw.bottom;
    if (left > right || top > bottom) return;
    cov->dirty = 1;
    for (int32_t py = top; py <= bottom; ++py) {
        if (cls != PSX_COVER_REACH) {
            memset(&cov->cls[py][left], cls, (size_t)(right - left + 1));
            continue;
        }
        for (int32_t px = left; px <= right; ++px) psx_coverage_put(cov, px, py, cls);
    }
}
/* 0x80 in each byte of `w` that is 0. Exact per byte: no carry crosses into the next. */
static inline uint64_t psx_coverage_zero_bytes(uint64_t w) {
    return ~(((w & 0x7F7F7F7F7F7F7F7FULL) + 0x7F7F7F7F7F7F7F7FULL) | w) & 0x8080808080808080ULL;
}

/* The eight 0x80 flags of a word as eight bits, the lowest byte's as bit 0. */
static inline uint64_t psx_coverage_pack(uint64_t flags) { return ((flags >> 7) * 0x0102040810204080ULL) >> 56; }

static inline int psx_coverage_bit(const uint64_t *row, int32_t x) { return (int)((row[x >> 6] >> (x & 63)) & 1u); }

/* The bits the questions below read, made once a frame buffer's list is in and again only after a draw: eight pixels a
 * step, the sky widened by the reach with shifts. */
static inline void psx_coverage_build(PsxCoverage *cov) {
    const PsxCoverageArea a = cov->area;
    if (!cov->dirty) return;
    cov->dirty = 0;
    cov->built = 1;
    if (a.right < a.left || a.bottom < a.top) return;
    const int32_t first = (a.left >> 6) - 1 < 0 ? 0 : (a.left >> 6) - 1;
    const int32_t last = (a.right >> 6) + 1 >= PSX_COVERAGE_WORDS ? PSX_COVERAGE_WORDS - 1 : (a.right >> 6) + 1;
    for (int32_t band = a.top >> 4; band <= a.bottom >> 4; ++band)
        for (int32_t i = first; i <= last; ++i) cov->sky_band[band][i] = 0;
    for (int32_t y = a.top; y <= a.bottom; ++y) {
        uint64_t sky[PSX_COVERAGE_WORDS];
        for (int32_t i = first; i <= last; ++i) sky[i] = 0;
        for (int32_t x = a.left & ~7; x <= a.right; x += 8) {
            uint64_t w;
            memcpy(&w, &cov->cls[y][x], 8);
            uint64_t keep = 0xFFu;
            if (x < a.left) keep &= 0xFFu << (a.left - x);
            if (x + 7 > a.right) keep &= 0xFFu >> (x + 7 - a.right);
            const uint64_t s = psx_coverage_pack(psx_coverage_zero_bytes(w ^ 0x0101010101010101ULL)) & keep;
            sky[x >> 6] |= s << (x & 63);
        }
        uint64_t *wide = cov->sky[y], *band = cov->sky_band[y >> 4];
        for (int32_t i = first; i <= last; ++i) {
            uint64_t d = sky[i];
            for (int k = 1; k <= PSX_COVERAGE_REACH_PX; ++k) {
                d |= sky[i] << k | sky[i] >> k;
                if (i > first) d |= sky[i - 1] >> (64 - k);
                if (i < last) d |= sky[i + 1] << (64 - k);
            }
            wide[i] = d;
            band[i] |= d;
        }
    }
}

/* Whether the game's picture shows sky within the reach of column `x`, in any of rows top..bottom. */
static inline int psx_coverage_sky_near(const PsxCoverage *cov, int32_t x, int32_t top, int32_t bottom) {
    const PsxCoverageArea a = cov->area;
    if (!cov->built || x < a.left || x > a.right) return 0;
    if (top < a.top) top = a.top;
    if (bottom > a.bottom) bottom = a.bottom;
    for (int32_t y = top; y <= bottom;) {
        if ((y & 15) == 0 && y + 15 <= bottom) {
            if (psx_coverage_bit(cov->sky_band[y >> 4], x)) return 1;
            y += 16;
        } else {
            if (psx_coverage_bit(cov->sky[y], x)) return 1;
            ++y;
        }
    }
    return 0;
}

/* Columns [low, high] of a face, and whether a piece is cut away over the sky (a tick) or drawn as the game draws it. */
typedef struct {
    int32_t low, high;
    int outside;
} PsxCoveragePiece;

static inline int psx_coverage_piece(PsxCoveragePiece *out, int count, int32_t low, int32_t high, int outside) {
    if (count > PSX_COVERAGE_PIECES) return count; /* already past the room */
    if (count > 0 && out[count - 1].outside == outside && out[count - 1].high == low) {
        out[count - 1].high = high;
        return count;
    }
    if (count == PSX_COVERAGE_PIECES) return PSX_COVERAGE_PIECES + 1; /* more than room: the face is drawn whole */
    out[count].low = low;
    out[count].high = high;
    out[count].outside = outside;
    return count + 1;
}

/* The one rule for a face of the game's own crossing an inner edge [left, right] of its room's span. The game draws it
 * whole; only the columns within the reach of an edge it overhangs, where the game's whole picture shows sky within the
 * reach in the face's rows, are a tick to cut away. A face over sky the game itself hides is drawn whole. Returns the
 * pieces left to right, 0 for the face whole; a column band [low, high] is cut on the boundaries the GPU fills to. */
static inline int psx_coverage_pieces(const PsxCoverage *cov, int32_t least, int32_t most, int32_t top, int32_t bottom, int32_t left,
                                      int32_t right, PsxCoveragePiece out[PSX_COVERAGE_PIECES]) {
    const int32_t far = 1 << 20;
    int count = 0, ticks = 0;
    int32_t at = -far;
    for (int side = 0; side < 2; ++side) {
        const int32_t edge = side == 0 ? left : right;
        if (edge >= far || edge <= -far || (side == 0 ? least >= left : most <= right)) continue;
        const int32_t from = side == 0 ? (least > left - PSX_COVERAGE_REACH_PX ? least : left - PSX_COVERAGE_REACH_PX) : right;
        const int32_t to = side == 0 ? left : (most < right + PSX_COVERAGE_REACH_PX ? most : right + PSX_COVERAGE_REACH_PX);
        for (int32_t x = from; x < to; ++x) {
            if (!psx_coverage_sky_near(cov, x, top, bottom)) continue;
            if (x > at) count = psx_coverage_piece(out, count, at, x, 0);
            count = psx_coverage_piece(out, count, x, x + 1, 1);
            at = x + 1;
            ++ticks;
        }
    }
    if (!ticks) return 0;
    count = psx_coverage_piece(out, count, at, far, 0);
    return count > PSX_COVERAGE_PIECES ? 0 : count;
}

/* The sky tiles drawn into the frame buffer so far, by packet: drawing a tile takes its record from the sky fog, and a list
 * read once a face asks may begin with tiles already drawn. */
typedef struct {
    uint32_t keys[PSX_COVERAGE_SKY_KEYS];
    uint32_t count;
} PsxCoverageSkyKeys;

static inline void psx_coverage_sky_keys_reset(PsxCoverageSkyKeys *drawn) { drawn->count = 0u; }

static inline void psx_coverage_sky_keys_add(PsxCoverageSkyKeys *drawn, uint32_t key) {
    if (drawn->count < PSX_COVERAGE_SKY_KEYS) drawn->keys[drawn->count++] = key; /* past the table a tile counts as no sky: no cut */
}

static inline int psx_coverage_sky_keys_has(const PsxCoverageSkyKeys *drawn, uint32_t key) {
    for (uint32_t i = 0; i < drawn->count; ++i)
        if (drawn->keys[i] == key) return 1;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* GPU_COVERAGE_H */
