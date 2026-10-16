#ifndef PSXRECOMP_GPU_WS_HUD_SCALE_H
#define PSXRECOMP_GPU_WS_HUD_SCALE_H

#include <stdint.h>

#define PSX_WS_HUD_SCALE_MIN 50
#define PSX_WS_HUD_SCALE_MAX 100

static inline int psx_ws_hud_scale_clamp(int percent) {
    if (percent < PSX_WS_HUD_SCALE_MIN) return PSX_WS_HUD_SCALE_MIN;
    if (percent > PSX_WS_HUD_SCALE_MAX) return PSX_WS_HUD_SCALE_MAX;
    return percent;
}

/* One edge of a widget, moved toward `pivot` by numerator/denominator and
 * rounded to nearest. Two widgets with one pivot that share an edge share it
 * afterwards, unless one of them is held at its one pixel minimum. */
static inline int32_t psx_ws_hud_scale_edge(int32_t value, int32_t pivot,
                                            int64_t numerator,
                                            int64_t denominator) {
    const int64_t scaled = (int64_t)(value - pivot) * numerator;
    const int64_t half = denominator / 2;
    return pivot + (int32_t)((scaled >= 0 ? scaled + half : scaled - half) /
                             denominator);
}

/* Twice the column the thirds rule takes for the middle of [x, x + w). A game's line of text, one picture `strip` wide
 * with its words at the left end, is not in the right third until it begins there. */
static inline int32_t psx_ws_hud_thirds_centre(int32_t x, int32_t w, int32_t strip, int32_t screen_w) {
    const int32_t centre = 2 * x + w;
    return w == strip && 3 * centre > 4 * screen_w && 3 * x <= 2 * screen_w ? screen_w : centre;
}

/* The column a held weapon's sprite [x, x + w) is squashed about: the centre in the middle third, sliding toward the edge in
 * an outer third and on it when the sprite touches it, so a cropped picture stays cropped. One over five twelfths wide
 * slides from the centre on. */
/* Where a held weapon's sprite at x, w wide, squashed about pivot by numerator/denominator, is drawn: whole edges from
 * both squashed ends, and how far each unrounded end stands off its whole edge. 0 when no width or no squash is left. */
typedef struct {
    int32_t left, width;
    float sides[2];
} PsxWsHeldPlace;
static inline int psx_ws_hud_held_place(int32_t x, int32_t w, int32_t pivot, int32_t numerator, int32_t denominator,
                                        PsxWsHeldPlace *place) {
    const int64_t ends[2] = {x, (int64_t)x + w};
    int64_t whole[2];
    place->left = x;
    place->width = w;
    place->sides[0] = place->sides[1] = 0.0f;
    if (w <= 0 || numerator <= 0 || denominator <= 0) return 0;
    for (int end = 0; end < 2; ++end) {
        const int64_t offset = ends[end] - pivot;
        whole[end] = pivot + (offset * numerator + (offset >= 0 ? denominator / 2 : -(denominator / 2))) / denominator;
        place->sides[end] = (float)((double)pivot + (double)offset * numerator / denominator - (double)whole[end]);
    }
    place->left = (int32_t)whole[0];
    place->width = whole[1] > whole[0] ? (int32_t)(whole[1] - whole[0]) : 1;
    if (whole[1] > whole[0]) return 1;
    place->sides[0] = place->sides[1] = 0.0f;
    return 0;
}

static inline int32_t psx_ws_hud_held_pivot(int32_t x, int32_t w, int32_t screen_w) {
    int64_t centre, start, reach, gone;
    int mirrored;
    if (w <= 0 || screen_w <= 0 || w >= screen_w) return screen_w / 2;
    centre = 2 * (int64_t)x + w;
    mirrored = centre < screen_w;
    if (mirrored) centre = 2 * (int64_t)screen_w - centre;
    start = 12 * (int64_t)w <= 5 * (int64_t)screen_w ? 4 * (int64_t)screen_w : 3 * (int64_t)screen_w; /* in sixths of a pixel */
    gone = 3 * centre - start;
    if (gone <= 0) return screen_w / 2;
    reach = 3 * (2 * (int64_t)screen_w - w) - start;
    if (gone > reach) gone = reach;
    centre = ((int64_t)screen_w * (reach + gone) + reach) / (2 * reach);
    return (int32_t)(mirrored ? screen_w - centre : centre);
}

/* Shrinks a screen-space widget toward the edge or corner it sits at: thirds of width and height pick the pivot, as the
 * stock horizontal squash does. Pieces in different thirds are pulled apart. The horizontal factor includes the squash.
 * The middle column below the top third is left alone, the first-person weapon is drawn there. Returns 0 when nothing
 * changed. `strip` is the game's text line width where one may be meant, else 0. */
static inline int psx_ws_hud_scale_rect_strip(int32_t *x, int32_t *y,
                                              int32_t *w, int32_t *h,
                                              int32_t screen_w, int32_t screen_h,
                                              int32_t squash_num, int32_t squash_den,
                                              int percent, int32_t strip) {
    int32_t centre_x, centre_y, pivot_x, pivot_y, x0, x1, y0, y1;
    int left, right, top, bottom;
    if (percent >= PSX_WS_HUD_SCALE_MAX || percent < PSX_WS_HUD_SCALE_MIN ||
        *w <= 0 || *h <= 0 || screen_w <= 0 || screen_h <= 0 ||
        squash_num <= 0 || squash_den <= 0)
        return 0;
    centre_x = psx_ws_hud_thirds_centre(*x, *w, strip, screen_w);
    centre_y = 2 * *y + *h;
    left = 3 * centre_x < 2 * screen_w;
    right = 3 * centre_x > 4 * screen_w;
    top = 3 * centre_y < 2 * screen_h;
    bottom = 3 * centre_y > 4 * screen_h;
    if (!left && !right && !top) return 0;
    pivot_x = left ? 0 : right ? screen_w : screen_w / 2;
    pivot_y = top ? 0 : bottom ? screen_h : screen_h / 2;
    x0 = psx_ws_hud_scale_edge(*x, pivot_x, (int64_t)squash_num * percent,
                               (int64_t)squash_den * 100);
    x1 = psx_ws_hud_scale_edge(*x + *w, pivot_x, (int64_t)squash_num * percent,
                               (int64_t)squash_den * 100);
    y0 = psx_ws_hud_scale_edge(*y, pivot_y, percent, 100);
    y1 = psx_ws_hud_scale_edge(*y + *h, pivot_y, percent, 100);
    *x = x0;
    *w = x1 > x0 ? x1 - x0 : 1;
    *y = y0;
    *h = y1 > y0 ? y1 - y0 : 1;
    return 1;
}

static inline int psx_ws_hud_scale_rect(int32_t *x, int32_t *y,
                                        int32_t *w, int32_t *h,
                                        int32_t screen_w, int32_t screen_h,
                                        int32_t squash_num, int32_t squash_den,
                                        int percent) {
    return psx_ws_hud_scale_rect_strip(x, y, w, h, screen_w, screen_h, squash_num, squash_den, percent, 0);
}

#endif /* PSXRECOMP_GPU_WS_HUD_SCALE_H */
