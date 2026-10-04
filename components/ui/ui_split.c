#include "ui_split.h"

#include <string.h>

/* The split layout's geometry and its cells' sizes (spec §5.2, D31). */

#define PANEL_W 400
#define PANEL_H 300

static const char *const k_ratio_names[] = { [UI_RATIO_1_4] = "1/4", [UI_RATIO_1_3] = "1/3", [UI_RATIO_1_2] = "1/2",
                                             [UI_RATIO_2_3] = "2/3", [UI_RATIO_3_4] = "3/4" };
static const uint8_t k_ratio_num[] = { [UI_RATIO_1_4] = 1, [UI_RATIO_1_3] = 1, [UI_RATIO_1_2] = 1,
                                       [UI_RATIO_2_3] = 2, [UI_RATIO_3_4] = 3 };
static const uint8_t k_ratio_den[] = { [UI_RATIO_1_4] = 4, [UI_RATIO_1_3] = 3, [UI_RATIO_1_2] = 2,
                                       [UI_RATIO_2_3] = 3, [UI_RATIO_3_4] = 4 };

/* Each size's least cell (spec §5.2), and the kinds it takes (spec §5.1). */
static const struct {
    int16_t min_w, narrow_h, wide_h;
    uint32_t kinds;
} k_sizes[] = {
    [UI_SIZE_XS] = { 40, 20, 20, UI_KINDS_XS },
    [UI_SIZE_S] = { 90, 40, 40, UI_KINDS_S }, /* M6c: under 150×80 the icon goes beside the value */
    [UI_SIZE_M] = { 130, 80, 80, UI_KINDS_M },
    [UI_SIZE_L] = { 200, 150, 150, UI_KINDS_L },
    [UI_SIZE_XL] = { 400, 120, 120, UI_KINDS_XL },
};

/* Where a kind's widget needs more height than its size's least cell: the least height at which it
 * stays clear of the cell's edges, measured on host renders of every field with Czech text and
 * stale marks; test_ui_widget_fit.c checks each one. Numbers, times and the battery fit their
 * digits to the height (ui_widget.c), so they need no more than the size's own. */
static const struct {
    uint8_t size, kind;
    uint8_t narrow_h, wide_h;
} k_needs[] = {
    { UI_SIZE_S, UI_FK_SUN, 49, 49 }, /* M6c: S's other kinds fit its own 40 px, the sky beside the value */
    { UI_SIZE_S, UI_FK_POLLEN, 42, 42 },
    { UI_SIZE_M, UI_FK_WEATHER_NOW, 98, 80 },
    { UI_SIZE_M, UI_FK_WEATHER_DAY, 94, 80 },
    { UI_SIZE_M, UI_FK_SUN, 94, 94 },
    { UI_SIZE_M, UI_FK_LEVEL, 93, 93 },
    { UI_SIZE_M, UI_FK_POLLEN, 105, 105 },
};

gfx_rect_t ui_split_area(void)
{
    return (gfx_rect_t){ 0, UI_STATUS_H + 1, PANEL_W, PANEL_H - UI_STATUS_H - 1 };
}

int ui_split_first(int length, int ratio)
{
    if (ratio < UI_RATIO_1_4 || ratio > UI_RATIO_3_4) {
        return 0;
    }
    return length * k_ratio_num[ratio] / k_ratio_den[ratio];
}

typedef struct {
    const uint8_t *tree;
    int at; /* the next node */
    ui_split_geometry_t *out;
    bool ok;
} walk_t;

/* One node and everything under it, laid over r. Each call takes a node, so it recurses at most
 * UI_SPLIT_NODES deep. */
static void walk(walk_t *w, gfx_rect_t r)
{
    if (!w->ok || w->at >= UI_SPLIT_NODES || r.w < UI_SPLIT_MIN_W || r.h < UI_SPLIT_MIN_H) {
        w->ok = false;
        return;
    }
    uint8_t node = w->tree[w->at++];
    int ratio = node & UI_SPLIT_RATIO;
    if (ratio == 0) {
        if (node != 0 || w->out->cells >= UI_SPLIT_CELLS) {
            w->ok = false;
            return;
        }
        w->out->cell[w->out->cells++] = r;
        return;
    }
    if (ratio > UI_RATIO_3_4 || (node & ~(UI_SPLIT_RATIO | UI_SPLIT_COLUMNS | UI_SPLIT_NO_LINE)) ||
        w->out->lines >= UI_SPLIT_CELLS - 1) {
        w->ok = false;
        return;
    }
    bool columns = node & UI_SPLIT_COLUMNS;
    int first = ui_split_first(columns ? r.w : r.h, ratio);
    gfx_rect_t a = r, b = r;
    if (columns) {
        a.w = (int16_t)first;
        b.x = (int16_t)(r.x + first + 1);
        b.w = (int16_t)(r.w - first - 1);
    } else {
        a.h = (int16_t)first;
        b.y = (int16_t)(r.y + first + 1);
        b.h = (int16_t)(r.h - first - 1);
    }
    w->out->line[w->out->lines++] =
        (ui_split_line_t){ .rect = r, .node = node, .at = (int16_t)(columns ? r.x + first : r.y + first) };
    walk(w, a);
    walk(w, b);
}

bool ui_split_layout(const uint8_t tree[UI_SPLIT_NODES], gfx_rect_t area, ui_split_geometry_t *out)
{
    memset(out, 0, sizeof(*out));
    walk_t w = { .tree = tree, .out = out, .ok = true };
    walk(&w, area);
    return w.ok;
}

/* The nodes a tree uses, from its shape alone: each split needs two more. */
int ui_split_nodes(const uint8_t tree[UI_SPLIT_NODES])
{
    int open = 1;
    for (int i = 0; i < UI_SPLIT_NODES; i++) {
        open += (tree[i] & UI_SPLIT_RATIO) ? 1 : -1;
        if (open == 0) {
            return i + 1;
        }
    }
    return 0;
}

int ui_split_min_w(ui_size_t size)
{
    return (unsigned)size <= UI_SIZE_XL ? k_sizes[size].min_w : -1;
}

int ui_split_min_h(ui_size_t size, bool narrow)
{
    if ((unsigned)size > UI_SIZE_XL) {
        return -1;
    }
    return narrow ? k_sizes[size].narrow_h : k_sizes[size].wide_h;
}

int ui_split_need(ui_size_t size, ui_field_kind_t kind, bool narrow)
{
    if ((unsigned)size > UI_SIZE_XL || (unsigned)kind >= UI_FK_COUNT || !(k_sizes[size].kinds & UI_KIND(kind))) {
        return -1;
    }
    int need = ui_split_min_h(size, narrow);
    for (size_t i = 0; i < sizeof(k_needs) / sizeof(k_needs[0]); i++) {
        if (k_needs[i].size == size && k_needs[i].kind == kind) {
            int h = narrow ? k_needs[i].narrow_h : k_needs[i].wide_h;
            need = h > need ? h : need;
        }
    }
    return need;
}

int ui_split_cell_size(int w, int h)
{
    for (int size = UI_SIZE_XL; size >= UI_SIZE_XS; size--) {
        if (w >= k_sizes[size].min_w && h >= ui_split_min_h((ui_size_t)size, w < UI_SPLIT_NARROW_W)) {
            return size;
        }
    }
    return -1;
}

int ui_split_field_size(ui_field_kind_t kind, int w, int h)
{
    for (int size = UI_SIZE_XL; size >= UI_SIZE_XS; size--) {
        int need = ui_split_need((ui_size_t)size, kind, w < UI_SPLIT_NARROW_W);
        if (w >= k_sizes[size].min_w && need >= 0 && h >= need) {
            return size;
        }
    }
    return -1;
}

const char *ui_split_ratio_name(int ratio)
{
    return ratio >= UI_RATIO_1_4 && ratio <= UI_RATIO_3_4 ? k_ratio_names[ratio] : NULL;
}

int ui_split_ratio_by_name(const char *name)
{
    for (int r = UI_RATIO_1_4; name != NULL && r <= UI_RATIO_3_4; r++) {
        if (strcmp(k_ratio_names[r], name) == 0) {
            return r;
        }
    }
    return 0;
}
