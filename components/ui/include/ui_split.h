#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx.h"
#include "ui_fields.h"
#include "ui_layout.h"

/*
 * The split layout (spec §5.2, D31): the area under the status bar split into rows or columns, each
 * part split again, at most 24 cells (M6c, D34; 8 before). A tree is kept in preorder, a byte a node:
 * 0 is a cell; a split has its ratio in the low bits, and flags for columns and a hidden separator.
 * The cells' fields are the preset's slots, in the order the cells come. Pure C, host-buildable.
 */

#define UI_SPLIT_CELLS 24
#define UI_SPLIT_NODES (2 * UI_SPLIT_CELLS - 1)
#define UI_SPLIT_MIN_W 40     /* no part is smaller (spec §5.2; M6c, D34: 90×40 before) */
#define UI_SPLIT_MIN_H 20
#define UI_SPLIT_NARROW_W 150 /* narrower: a kind's narrow height; S stacks from 80 px tall (D34) */
#define UI_SPLIT_INSET 8      /* a separator stops this short of each end */

typedef enum {
    UI_RATIO_1_4 = 1,
    UI_RATIO_1_3,
    UI_RATIO_1_2,
    UI_RATIO_2_3,
    UI_RATIO_3_4,
} ui_ratio_t;

#define UI_SPLIT_RATIO 0x07   /* a split's ratio (ui_ratio_t); 0 makes the node a cell */
#define UI_SPLIT_COLUMNS 0x08 /* part a left of part b; without it, a over b */
#define UI_SPLIT_NO_LINE 0x10 /* the separator hidden; its 1 px gap stays */

typedef struct {
    gfx_rect_t rect; /* the split's own rectangle */
    uint8_t node;    /* its code */
    int16_t at;      /* the gap between its parts: an x for columns, a y for rows */
} ui_split_line_t;

typedef struct {
    uint8_t cells;
    gfx_rect_t cell[UI_SPLIT_CELLS]; /* in preorder, as their fields come in the slots */
    uint8_t lines;
    ui_split_line_t line[UI_SPLIT_CELLS - 1]; /* every split, in preorder, its separator shown or not */
} ui_split_geometry_t;

/* What a split preset divides: everything under the status bar and its line (400×279). */
gfx_rect_t ui_split_area(void);
/* Lays the tree out over `area`. False if it is cut short, has a node it doesn't know, or has a
 * part under 40×20; *out is then unspecified. */
bool ui_split_layout(const uint8_t tree[UI_SPLIT_NODES], gfx_rect_t area, ui_split_geometry_t *out);
/* How many of the tree's nodes it uses: 1 to UI_SPLIT_NODES, or 0 if it is cut short. */
int ui_split_nodes(const uint8_t tree[UI_SPLIT_NODES]);
/* The first part's length when a split of `length` px gives it `ratio`, rounded down. */
int ui_split_first(int length, int ratio);

/* The size class a cell of w×h has (spec §5.2), or -1 when it is too small for any field. */
int ui_split_cell_size(int w, int h);
/* The size a field of `kind` draws at in a cell of w×h: the largest size the cell is wide enough
 * for, that takes the kind, and whose height for the kind the cell has; -1 if there is none. */
int ui_split_field_size(ui_field_kind_t kind, int w, int h);
/* What GET /api/layouts publishes (ui_catalog.c): a size's least width and height, in a cell
 * narrower than 150 px or not; and the least height a cell needs to draw `kind` at `size`, at
 * least the size's own, or -1 when the size doesn't take the kind. */
int ui_split_min_w(ui_size_t size);
int ui_split_min_h(ui_size_t size, bool narrow);
int ui_split_need(ui_size_t size, ui_field_kind_t kind, bool narrow);

/* "3/4" for UI_RATIO_3_4 and back; NULL and 0 for anything else. */
const char *ui_split_ratio_name(int ratio);
int ui_split_ratio_by_name(const char *name);
