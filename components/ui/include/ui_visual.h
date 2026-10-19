#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gfx.h"
#include "lang.h"
#include "ui_fields.h"
#include "ui_history.h"

/*
 * Show as (spec §5.3, M7, D44): a numeric field drawn as its value, or as a history chart, a bar, a ring or an arc
 * gauge between a minimum and a maximum. The looks are the spike's, approved on 2026-10-19. Pure C, host-buildable.
 */

typedef enum {
    UI_VIS_VALUE, /* the field's own widget */
    UI_VIS_CHART,
    UI_VIS_BAR,
    UI_VIS_RING,
    UI_VIS_GAUGE,
    UI_VIS_COUNT,
} ui_vis_as_t;

typedef enum {
    UI_LIMIT_FIELD, /* the field's own range; automatic without one */
    UI_LIMIT_VALUE, /* the visual's own number */
    UI_LIMIT_AUTO,  /* a chart's: from the values in view */
} ui_vis_limit_t;

#define UI_SPAN_MIN_S 10
#define UI_SPAN_VALUES_MAX_S 3600 /* from each value the field takes */
#define UI_SPAN_HOURS_MIN_S 7200  /* from the minutes, in whole hours */
#define UI_SPAN_MAX_S 86400
#define UI_SPAN_DEFAULT_S 60

typedef struct {
    uint8_t as;                 /* ui_vis_as_t */
    uint8_t min_mode, max_mode; /* ui_vis_limit_t */
    bool numbers;               /* a chart's latest value over its top left, its range's ends and span in the corners */
    uint32_t span_s;            /* a chart's span (ui_visual_span_ok()) */
    float min, max;             /* for UI_LIMIT_VALUE, in the field's unit */
} ui_visual_t;

/* Whether `as` has room in w×h (spec §5.3): about 80×40 for a chart, 60×40 for a gauge, 40×40 for a ring and
 * 60×16 for a bar; the value always has. */
bool ui_visual_fits(ui_vis_as_t as, int w, int h);
/* A chart's span: 10-3 600 s, or whole hours from 2 to 24. */
bool ui_visual_span_ok(uint32_t span_s);
/* The span as the chart writes it: "60 s", "5 min", "1 h". */
void ui_visual_span_text(uint32_t span_s, char *out, size_t size);
/* The range `vis` draws `v` between (spec §5.3): each bound its own number, or the field's range, or automatic: a
 * chart's from the values in view, `view_lo`..`view_hi` (`have_view` false: none), with a tenth's room, rounded; a
 * bar's, a ring's or a gauge's from 0 to a round number past the value. */
void ui_visual_range(const ui_visual_t *vis, const ui_value_t *v, bool have_view, float view_lo, float view_hi,
                     float *lo, float *hi);
/* `v` as `vis` in `r`, nothing outside it; a chart draws `series` up to `now_ms` (NULL: no history yet). A value
 * missing draws the visual empty with "—". */
void ui_visual_draw(gfx_fb_t *fb, gfx_rect_t r, const ui_value_t *v, const ui_visual_t *vis, const ui_series_t *series,
                    uint32_t now_ms, const lang_t *lang);
