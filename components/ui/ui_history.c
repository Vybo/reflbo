#include "ui_history.h"

#include <math.h>
#include <string.h>

/* The history behind the charts (spec §5.3, D44). */

#define MINUTE_MS 60000u

static void clear(ui_series_t *s, const char *name)
{
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, sizeof(s->name) - 1);
}

void ui_history_init(ui_history_t *h, ui_series_t *mem)
{
    h->series = mem;
    for (int i = 0; i < UI_HIST_SERIES; i++) {
        clear(&mem[i], "");
    }
}

static bool named(const char *const names[], int count, const char *name)
{
    for (int i = 0; i < count; i++) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return true;
        }
    }
    return false;
}

ui_series_t *ui_history_find(const ui_history_t *h, const char *name)
{
    for (int i = 0; name != NULL && name[0] && i < UI_HIST_SERIES; i++) {
        if (strcmp(h->series[i].name, name) == 0) {
            return &h->series[i];
        }
    }
    return NULL;
}

int ui_history_track(ui_history_t *h, const char *const names[], int count)
{
    for (int i = 0; i < UI_HIST_SERIES; i++) {
        if (h->series[i].name[0] && !named(names, count, h->series[i].name)) {
            h->series[i].name[0] = '\0'; /* freed: cleared when it is taken again */
        }
    }
    for (int k = 0; k < count; k++) {
        const char *name = names[k];
        if (name == NULL || !name[0] || strlen(name) >= UI_HIST_NAME_LEN || ui_history_find(h, name) != NULL) {
            continue;
        }
        for (int i = 0; i < UI_HIST_SERIES; i++) {
            if (!h->series[i].name[0]) {
                clear(&h->series[i], name);
                break;
            }
        }
    }
    int kept = 0;
    for (int i = 0; i < UI_HIST_SERIES; i++) {
        kept += h->series[i].name[0] != '\0';
    }
    return kept;
}

/* The minute being gathered goes into the ring. */
static void finish_minute(ui_series_t *s)
{
    int i = s->mhead;
    bool any = s->cur_n > 0;
    s->mmin[i] = any ? s->cur_min : NAN;
    s->mmean[i] = any ? s->cur_sum / (float)s->cur_n : NAN;
    s->mmax[i] = any ? s->cur_max : NAN;
    s->mbreak[i] = s->cur_break;
    s->mhead = (uint16_t)((s->mhead + 1) % UI_HIST_MINUTES);
    if (s->mcount < UI_HIST_MINUTES) {
        s->mcount++;
    }
    s->cur_n = 0;
    s->cur_sum = 0;
    s->cur_break = false;
}

/* Brings the minutes up to `now_ms`: each minute passed is finished, those without values empty; past a day of
 * them, they all go. */
static void advance(ui_series_t *s, uint32_t now_ms)
{
    if (!s->gathering) {
        s->gathering = true;
        s->minute_ms = now_ms;
        return;
    }
    uint32_t steps = (now_ms - s->minute_ms) / MINUTE_MS;
    if (steps > UI_HIST_MINUTES) {
        s->mhead = s->mcount = 0;
        s->cur_n = 0;
        s->cur_sum = 0;
        s->cur_break = false;
        s->minute_ms = now_ms;
        return;
    }
    for (uint32_t k = 0; k < steps; k++) {
        finish_minute(s);
        s->minute_ms += MINUTE_MS;
    }
}

static void store(ui_series_t *s, uint32_t now_ms, float v)
{
    s->v[s->head] = v;
    s->t_ms[s->head] = now_ms;
    s->head = (uint16_t)((s->head + 1) % UI_HIST_VALUES);
    if (s->count < UI_HIST_VALUES) {
        s->count++;
    }
}

void ui_series_break(ui_series_t *s, uint32_t now_ms)
{
    advance(s, now_ms);
    s->cur_break = true;
    store(s, now_ms, NAN);
}

void ui_series_push(ui_series_t *s, uint32_t now_ms, float v)
{
    if (!isfinite(v)) {
        ui_series_break(s, now_ms);
        return;
    }
    advance(s, now_ms);
    if (s->cur_n == 0 || v < s->cur_min) {
        s->cur_min = v;
    }
    if (s->cur_n == 0 || v > s->cur_max) {
        s->cur_max = v;
    }
    s->cur_sum += v;
    s->cur_n++;
    store(s, now_ms, v);
}

/* How long before `now_ms` `t_ms` was, the clock wrapping: a time up to a minute ahead is now (a clock read a moment
 * before the value), anything else that far back, weeks included, until the 49.7 days of a 32-bit clock. */
static uint32_t age(uint32_t now_ms, uint32_t t_ms)
{
    uint32_t a = now_ms - t_ms;
    return a > 0u - 60000u ? 0 : a;
}

typedef struct {
    ui_hist_column_t *cols; /* columns first .. first + count - 1 */
    int width, first, count;
    uint32_t span_ms;
    int last; /* the oldest column with values so far, walking from the newest, kept or not; -1: none */
    int filled;
} walk_t;

static void add(walk_t *w, uint32_t a, float lo, float mean, float hi)
{
    int c = w->width - 1 - (int)((uint64_t)a * (uint64_t)w->width / w->span_ms);
    w->last = c;
    if (c < w->first || c >= w->first + w->count) {
        return;
    }
    ui_hist_column_t *col = &w->cols[c - w->first];
    if (col->n == 0) {
        col->min = lo;
        col->max = hi;
        col->mean = 0;
        w->filled++;
    }
    col->min = lo < col->min ? lo : col->min;
    col->max = hi > col->max ? hi : col->max;
    col->mean += mean; /* a sum until the end */
    col->n++;
}

/* A break older than the values walked so far: the oldest of them doesn't join what comes before it. */
static void mark_break(walk_t *w)
{
    if (w->last >= w->first && w->last < w->first + w->count) {
        w->cols[w->last - w->first].brk = true;
    }
}

static void from_values(const ui_series_t *s, uint32_t now_ms, walk_t *w)
{
    for (int k = 0; k < s->count; k++) {
        int i = (s->head + UI_HIST_VALUES - 1 - k) % UI_HIST_VALUES;
        uint32_t a = age(now_ms, s->t_ms[i]);
        if (a >= w->span_ms) {
            break;
        }
        if (isnan(s->v[i])) {
            mark_break(w);
        } else {
            add(w, a, s->v[i], s->v[i], s->v[i]);
        }
    }
}

static void from_minutes(const ui_series_t *s, uint32_t now_ms, walk_t *w)
{
    if (!s->gathering) {
        return;
    }
    uint32_t a = age(now_ms, s->minute_ms + MINUTE_MS / 2); /* the minute being gathered, at its middle */
    if (a < w->span_ms) {
        if (s->cur_n > 0) {
            add(w, a, s->cur_min, s->cur_sum / (float)s->cur_n, s->cur_max);
        }
        if (s->cur_break) {
            mark_break(w);
        }
    }
    for (int k = 0; k < s->mcount; k++) {
        int i = (s->mhead + UI_HIST_MINUTES - 1 - k) % UI_HIST_MINUTES;
        a = age(now_ms, s->minute_ms - (uint32_t)(k + 1) * MINUTE_MS + MINUTE_MS / 2);
        if (a >= w->span_ms) {
            break;
        }
        if (!isnan(s->mmean[i])) {
            add(w, a, s->mmin[i], s->mmean[i], s->mmax[i]);
        }
        if (s->mbreak[i]) {
            mark_break(w);
        }
    }
}

/* Whether the values cover `span_ms`: all of them since the first, or their oldest's age and one interval. */
static bool values_cover(const ui_series_t *s, uint32_t now_ms, uint32_t span_ms)
{
    if (s->count < UI_HIST_VALUES) {
        return true;
    }
    int oldest = s->head, newest = (s->head + UI_HIST_VALUES - 1) % UI_HIST_VALUES;
    uint32_t interval = (s->t_ms[newest] - s->t_ms[oldest]) / (UI_HIST_VALUES - 1);
    return (uint64_t)age(now_ms, s->t_ms[oldest]) + interval >= span_ms;
}

int ui_series_columns(const ui_series_t *s, uint32_t now_ms, uint32_t span_ms, int width, int first, int count,
                      ui_hist_column_t *cols)
{
    if (count <= 0) {
        return 0;
    }
    memset(cols, 0, (size_t)count * sizeof(cols[0]));
    if (width <= 0 || span_ms == 0) {
        return 0;
    }
    walk_t w = { .cols = cols, .width = width, .first = first, .count = count, .span_ms = span_ms, .last = -1 };
    if (values_cover(s, now_ms, span_ms)) {
        from_values(s, now_ms, &w);
    } else {
        from_minutes(s, now_ms, &w);
    }
    for (int c = 0; c < count; c++) {
        if (cols[c].n > 0) {
            cols[c].mean /= (float)cols[c].n;
        }
    }
    return w.filled;
}
