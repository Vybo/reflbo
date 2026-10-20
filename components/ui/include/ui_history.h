#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The history behind the charts (spec §5.3, M7, D44): for each field a chart on any preset shows, its last values
 * at their own rate and 24 h of one-minute minimum, mean and maximum. RAM only: the app keeps the series in PSRAM,
 * and a restart or a deep sleep loses them. Times are the caller's monotonic clock in ms, which may wrap. Pure C,
 * host-buildable.
 */

#define UI_HIST_SERIES 16
#define UI_HIST_VALUES 3600 /* an hour at 1 s, 15 min at 250 ms */
#define UI_HIST_MINUTES 1440
#define UI_HIST_NAME_LEN 20 /* a field's name in presets.json: "pc." and a 15-character id */

typedef struct {
    char name[UI_HIST_NAME_LEN]; /* "" for a free series */
    uint16_t head, count;        /* the values' ring: the next goes at head */
    uint16_t mhead, mcount;      /* the finished minutes' ring, the same way */
    bool gathering;              /* a minute is being gathered, since minute_ms */
    bool cur_break;
    uint32_t minute_ms;
    uint32_t cur_n;
    float cur_min, cur_max, cur_sum;
    float v[UI_HIST_VALUES]; /* NaN: a break, where the line stops */
    uint32_t t_ms[UI_HIST_VALUES];
    float mmin[UI_HIST_MINUTES], mmean[UI_HIST_MINUTES], mmax[UI_HIST_MINUTES]; /* NaN mean: no value that minute */
    uint8_t mbreak[UI_HIST_MINUTES];
} ui_series_t;

typedef struct {
    ui_series_t *series; /* UI_HIST_SERIES of them */
} ui_history_t;

/* A chart's pixel column. */
typedef struct {
    uint16_t n;            /* the values in it, or the minutes; 0: none */
    bool brk;              /* a break since the column before with values: the line doesn't join them */
    float min, mean, max;
} ui_hist_column_t;

/* `mem` holds UI_HIST_SERIES series; all of them free. */
void ui_history_init(ui_history_t *h, ui_series_t *mem);
/* The series to keep, by field name: those already kept stay with their values, the others are freed, and new
 * names take free series, from nothing, until all 16 are taken. "" and repeats count for nothing. Returns how many
 * series are kept. */
int ui_history_track(ui_history_t *h, const char *const names[], int count);
/* The series kept for `name`, or NULL. */
ui_series_t *ui_history_find(const ui_history_t *h, const char *name);
/* A value at `now_ms`; one no number holds (NaN, an infinity) is a break. */
void ui_series_push(ui_series_t *s, uint32_t now_ms, float v);
/* A break: the line stops here, as when a session reconnects. */
void ui_series_break(ui_series_t *s, uint32_t now_ms);
/* The last `span_ms` before `now_ms` in `width` columns, oldest at the left: from the values while they cover the
 * span (their oldest's age and one interval), else from the minutes. `cols` gets the `count` columns from `first`,
 * so a chart can take them a few at a time. Returns how many of those have values. */
int ui_series_columns(const ui_series_t *s, uint32_t now_ms, uint32_t span_ms, int width, int first, int count,
                      ui_hist_column_t *cols);
