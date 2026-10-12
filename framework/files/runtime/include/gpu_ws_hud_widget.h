#ifndef PSXRECOMP_GPU_WS_HUD_WIDGET_H
#define PSXRECOMP_GPU_WS_HUD_WIDGET_H

#include <stddef.h>
#include <stdint.h>

#include "gpu_ws_hud_scale.h"

#define PSX_WS_HUD_WIDGET_MAX 4

/* A HUD widget a game draws as several primitives over more than one third
 * of the screen. The thirds rule gives each primitive the pivot of its own
 * centre, which pulls such a widget apart. Whatever lies wholly inside the
 * box [x0, x1) x [y0, y1) takes the widget's pivot instead. */
typedef struct {
    int32_t x0, y0, x1, y1;
    int32_t pivot_x, pivot_y;
} PsxWsHudWidget;

/* The widgets of a game that links its HUD to `layers` nodes just below its
 * ordering table, the last two tables its table word named, and where the
 * drawing of the frame stands. */
typedef struct {
    PsxWsHudWidget widgets[PSX_WS_HUD_WIDGET_MAX];
    int count, layers;
    uint32_t tables[2];
    int enabled, in_hud_layer;
    unsigned shown;
} PsxWsHudWidgets;

static inline const PsxWsHudWidget *psx_ws_hud_widget_find(const PsxWsHudWidget *widgets, int count,
                                                           int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x1 <= x0 || y1 <= y0) return NULL;
    for (int i = 0; i < count; ++i)
        if (x0 >= widgets[i].x0 && x1 <= widgets[i].x1 && y0 >= widgets[i].y0 && y1 <= widgets[i].y1)
            return &widgets[i];
    return NULL;
}

/* One corner of a widget's primitive: toward the pivot by the squash and the
 * HUD size across, by the HUD size down. Corners that coincided before
 * coincide afterwards. */
static inline void psx_ws_hud_widget_corner(const PsxWsHudWidget *widget, int32_t *x, int32_t *y,
                                            int32_t squash_num, int32_t squash_den, int percent) {
    *x = psx_ws_hud_scale_edge(*x, widget->pivot_x, (int64_t)squash_num * percent, (int64_t)squash_den * 100);
    *y = psx_ws_hud_scale_edge(*y, widget->pivot_y, percent, 100);
}

/* A widget's rectangle, both corners through one mapping. It is never thinner
 * or lower than a pixel, so rectangles that shared an edge share it afterwards
 * unless one of them is held at that pixel. */
static inline void psx_ws_hud_widget_rect(const PsxWsHudWidget *widget, int32_t *x, int32_t *y, int32_t *w, int32_t *h,
                                          int32_t squash_num, int32_t squash_den, int percent) {
    int32_t x1 = *x + *w, y1 = *y + *h;
    psx_ws_hud_widget_corner(widget, x, y, squash_num, squash_den, percent);
    psx_ws_hud_widget_corner(widget, &x1, &y1, squash_num, squash_den, percent);
    *w = x1 > *x ? x1 - *x : 1;
    *h = y1 > *y ? y1 - *y : 1;
}

/* Whether the empty node at `node` opens a HUD layer, for a game that keeps
 * `layers` such nodes in the words just below its ordering table at `table`.
 * Both must be main RAM, in any segment or mirror. No table, no HUD layer. */
static inline int psx_ws_hud_node(uint32_t node, uint32_t table, int layers) {
    const uint32_t node_at = node & 0x1FFFFFFFu, table_at = table & 0x1FFFFFFFu;
    const uint32_t at = node_at & 0x1FFFFCu, base = table_at & 0x1FFFFCu;
    return layers > 0 && node_at < 0x800000u && table_at < 0x800000u && at < base &&
           base - at <= 4u * (uint32_t)layers;
}

/* The game set a new drawing area: the next frame's primitives follow, and
 * what the last frame showed is forgotten. Nothing else forgets it, so a
 * game has to set one every frame to have widgets. */
static inline void psx_ws_hud_widgets_frame(PsxWsHudWidgets *hud) {
    hud->shown = 0u;
    hud->in_hud_layer = 0;
}

/* A linked list begins. `table` is what the game's table word names now, or
 * 0 when widgets do not apply to this list. A double-buffered game alternates
 * two tables and can move the word on before the list it built is drawn, so
 * the word alone does not say which table a list comes from: the last two
 * tables it named are both the game's. */
static inline void psx_ws_hud_widgets_list(PsxWsHudWidgets *hud, uint32_t table) {
    hud->in_hud_layer = 0;
    hud->enabled = hud->count > 0 && table != 0u;
    if (hud->enabled && table != hud->tables[0]) {
        hud->tables[1] = hud->tables[0];
        hud->tables[0] = table;
    }
}

/* The list reached an empty node, which opens a layer. */
static inline void psx_ws_hud_widgets_node(PsxWsHudWidgets *hud, uint32_t node) {
    hud->in_hud_layer = hud->enabled && (psx_ws_hud_node(node, hud->tables[0], hud->layers) ||
                                         psx_ws_hud_node(node, hud->tables[1], hud->layers));
}

static inline void psx_ws_hud_widgets_list_end(PsxWsHudWidgets *hud) {
    hud->in_hud_layer = 0;
}

/* The widget a primitive with these bounds belongs to. In a HUD layer that is
 * the widget it lies wholly inside, which is then shown. Outside a HUD layer
 * only a rectangle may join, and only a widget shown since the last new
 * drawing area: a game can print a widget's text as a list of its own. */
static inline const PsxWsHudWidget *psx_ws_hud_widgets_take(PsxWsHudWidgets *hud, int32_t x0, int32_t y0,
                                                            int32_t x1, int32_t y1, int rectangle) {
    const PsxWsHudWidget *widget;
    unsigned bit;
    if (!hud->enabled || (!hud->in_hud_layer && !(rectangle && hud->shown))) return NULL;
    widget = psx_ws_hud_widget_find(hud->widgets, hud->count, x0, y0, x1, y1);
    if (!widget) return NULL;
    bit = 1u << (unsigned)(widget - hud->widgets);
    if (hud->in_hud_layer) hud->shown |= bit;
    return (hud->shown & bit) ? widget : NULL;
}

#endif /* PSXRECOMP_GPU_WS_HUD_WIDGET_H */
