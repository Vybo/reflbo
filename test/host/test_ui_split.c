#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "dashboard_fixtures.h"
#include "ui_split.h"
#include "unity.h"

/* The split layout (spec §5.2, D31): its geometry, and the sizes its cells give their fields. */

#define ROWS(r) ((uint8_t)(r))
#define COLS(r) ((uint8_t)((r) | UI_SPLIT_COLUMNS))
#define NO_LINE UI_SPLIT_NO_LINE
#define CELL 0

static ui_split_geometry_t s_g;

void setUp(void)
{
    memset(&s_g, 0xA5, sizeof(s_g));
}

void tearDown(void) {}

static void check_rect(int x, int y, int w, int h, gfx_rect_t r)
{
    TEST_ASSERT_EQUAL_INT(x, r.x);
    TEST_ASSERT_EQUAL_INT(y, r.y);
    TEST_ASSERT_EQUAL_INT(w, r.w);
    TEST_ASSERT_EQUAL_INT(h, r.h);
}

static bool lay(const uint8_t *nodes, int count)
{
    uint8_t tree[UI_SPLIT_NODES] = { 0 };
    memcpy(tree, nodes, (size_t)count);
    return ui_split_layout(tree, ui_split_area(), &s_g);
}

static void test_the_area_is_everything_under_the_status_bar(void)
{
    check_rect(0, 21, 400, 279, ui_split_area());
}

static void test_each_ratio_rounds_its_first_part_down(void)
{
    static const int k_279[] = { 69, 93, 139, 186, 209 }, k_400[] = { 100, 133, 200, 266, 300 };
    for (int r = UI_RATIO_1_4; r <= UI_RATIO_3_4; r++) {
        TEST_ASSERT_EQUAL_INT(k_279[r - 1], ui_split_first(279, r));
        TEST_ASSERT_EQUAL_INT(k_400[r - 1], ui_split_first(400, r));
    }
}

static void test_a_single_cell_fills_the_area(void)
{
    static const uint8_t k_tree[] = { CELL };
    TEST_ASSERT_TRUE(lay(k_tree, 1));
    TEST_ASSERT_EQUAL_INT(1, s_g.cells);
    TEST_ASSERT_EQUAL_INT(0, s_g.lines);
    check_rect(0, 21, 400, 279, s_g.cell[0]);
    TEST_ASSERT_EQUAL_INT(1, ui_split_nodes((const uint8_t[UI_SPLIT_NODES]){ CELL }));
}

/* Spec §5.2's example: Weather with smaller bottom cells. */
static void test_the_spec_example_lays_out_as_the_spec_says(void)
{
    static const uint8_t k_tree[] = { ROWS(UI_RATIO_3_4),
                                      COLS(UI_RATIO_1_2), CELL, ROWS(UI_RATIO_1_2), CELL, CELL,
                                      COLS(UI_RATIO_1_2) | NO_LINE, CELL, CELL };
    TEST_ASSERT_TRUE(lay(k_tree, sizeof(k_tree)));
    TEST_ASSERT_EQUAL_INT(9, ui_split_nodes((const uint8_t[UI_SPLIT_NODES]){ ROWS(UI_RATIO_3_4), COLS(UI_RATIO_1_2),
        CELL, ROWS(UI_RATIO_1_2), CELL, CELL, COLS(UI_RATIO_1_2) | NO_LINE, CELL, CELL }));
    TEST_ASSERT_EQUAL_INT(5, s_g.cells);
    check_rect(0, 21, 200, 209, s_g.cell[0]);   /* L */
    check_rect(201, 21, 199, 104, s_g.cell[1]); /* M */
    check_rect(201, 126, 199, 104, s_g.cell[2]);
    check_rect(0, 231, 200, 69, s_g.cell[3]);   /* S, drawn wide */
    check_rect(201, 231, 199, 69, s_g.cell[4]);
    TEST_ASSERT_EQUAL_INT(4, s_g.lines); /* every split, in preorder: the gap after the first part */
    check_rect(0, 21, 400, 279, s_g.line[0].rect);
    TEST_ASSERT_EQUAL_INT(230, s_g.line[0].at);
    TEST_ASSERT_EQUAL_INT(200, s_g.line[1].at);
    TEST_ASSERT_EQUAL_INT(125, s_g.line[2].at);
    check_rect(201, 21, 199, 209, s_g.line[2].rect);
    TEST_ASSERT_EQUAL_INT(200, s_g.line[3].at);
    TEST_ASSERT_TRUE(s_g.line[3].node & UI_SPLIT_NO_LINE);
    TEST_ASSERT_FALSE(s_g.line[0].node & UI_SPLIT_NO_LINE);
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_cell_size(s_g.cell[0].w, s_g.cell[0].h));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(s_g.cell[1].w, s_g.cell[1].h));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(s_g.cell[3].w, s_g.cell[3].h));
}

/* Eight cells, each at least 90×40: two rows of four columns. */
static void test_eight_cells_fill_the_tree(void)
{
    static const uint8_t k_tree[UI_SPLIT_NODES] = {
        ROWS(UI_RATIO_1_2),
        COLS(UI_RATIO_1_4), CELL, COLS(UI_RATIO_1_3), CELL, COLS(UI_RATIO_1_2), CELL, CELL,
        COLS(UI_RATIO_1_4), CELL, COLS(UI_RATIO_1_3), CELL, COLS(UI_RATIO_1_2), CELL, CELL,
    };
    TEST_ASSERT_TRUE(ui_split_layout(k_tree, ui_split_area(), &s_g));
    TEST_ASSERT_EQUAL_INT(UI_SPLIT_NODES, ui_split_nodes(k_tree));
    TEST_ASSERT_EQUAL_INT(8, s_g.cells);
    TEST_ASSERT_EQUAL_INT(7, s_g.lines);
    check_rect(0, 21, 100, 139, s_g.cell[0]);
    check_rect(101, 21, 99, 139, s_g.cell[1]);
    check_rect(201, 21, 99, 139, s_g.cell[2]);
    check_rect(301, 21, 99, 139, s_g.cell[3]);
    check_rect(301, 161, 99, 139, s_g.cell[7]);
}

/* A tree whose splits run past the 15 nodes has no end: there is no room for its cells. */
static void test_a_tree_cut_short_is_refused(void)
{
    uint8_t tree[UI_SPLIT_NODES];
    memset(tree, COLS(UI_RATIO_1_2), sizeof(tree));
    TEST_ASSERT_FALSE(ui_split_layout(tree, ui_split_area(), &s_g));
    TEST_ASSERT_EQUAL_INT(0, ui_split_nodes(tree));
}

static void test_a_part_under_90_by_40_is_refused(void)
{
    static const uint8_t k_narrow[] = { COLS(UI_RATIO_1_4), COLS(UI_RATIO_1_2), CELL, CELL, CELL }; /* 50 px */
    static const uint8_t k_short[] = { ROWS(UI_RATIO_1_4), ROWS(UI_RATIO_1_2), CELL, CELL, CELL };  /* 34 px */
    static const uint8_t k_just[] = { ROWS(UI_RATIO_1_3), ROWS(UI_RATIO_1_2), CELL, CELL, CELL };   /* 46 px */
    TEST_ASSERT_FALSE(lay(k_narrow, sizeof(k_narrow)));
    TEST_ASSERT_FALSE(lay(k_short, sizeof(k_short)));
    TEST_ASSERT_TRUE(lay(k_just, sizeof(k_just)));
    check_rect(0, 21, 400, 46, s_g.cell[0]);
    check_rect(0, 68, 400, 46, s_g.cell[1]);
}

static void test_a_bad_node_is_refused(void)
{
    static const uint8_t k_ratio[] = { 6, CELL, CELL };
    static const uint8_t k_flag[] = { ROWS(UI_RATIO_1_2) | 0x20, CELL, CELL };
    TEST_ASSERT_FALSE(lay(k_ratio, sizeof(k_ratio)));
    TEST_ASSERT_FALSE(lay(k_flag, sizeof(k_flag)));
}

/* XL 400 wide and at least 120 tall; L at least 200×150; M at least 130×80; S otherwise: wide from
 * 150 px with 40 px of height, narrower with 80 (spec §5.2). */
static void test_a_cells_size_follows_its_dimensions(void)
{
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_cell_size(400, 120));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(400, 119));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_cell_size(399, 279));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_cell_size(200, 150));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(199, 150));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(200, 149));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(129, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 40));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_cell_size(149, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_cell_size(150, 79));
    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(149, 79)); /* narrow S needs 80 */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(90, 40));
    TEST_ASSERT_EQUAL_INT(-1, ui_split_cell_size(150, 39));
}

/* A field draws at the largest size its cell allows that takes its kind and has room for it. */
static void test_a_field_draws_at_the_largest_size_with_room_for_it(void)
{
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_field_size(UI_FK_TIME, 400, 279));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_WEATHER_NOW, 400, 279)); /* XL takes no weather */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_DATE, 400, 279));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_XL, ui_split_field_size(UI_FK_NUMBER, 400, 120)); /* its digits fit the height */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_NUMBER, 400, 119));  /* L needs 150 */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_BATTERY, 130, 80));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_SERIES, 200, 209));  /* L takes no series */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_L, ui_split_field_size(UI_FK_RAIN_MAP, 200, 209));
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_RAIN_MAP, 199, 69)); /* series and maps need M */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_SERIES, 199, 69));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_POLLEN, 199, 104)); /* M pollen needs 105 */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_POLLEN, 199, 105));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 199, 69)); /* wide S needs 51 */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 98));        /* narrow S needs 99 */
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_WEATHER_DAY, 129, 99));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_M, ui_split_field_size(UI_FK_WEATHER_DAY, 149, 94)); /* narrow M needs 94 */
    TEST_ASSERT_EQUAL_INT(-1, ui_split_field_size(UI_FK_NUMBER, 99, 69));
    TEST_ASSERT_EQUAL_INT(UI_SIZE_S, ui_split_field_size(UI_FK_NUMBER, 99, 80));
}

/* A larger cell never takes a field that a smaller one inside it takes: Join keeps a field. */
static void test_more_room_never_loses_a_field(void)
{
    for (int k = 0; k < UI_FK_COUNT; k++) {
        for (int w = 90; w <= 400; w += 1) {
            for (int h = 40; h <= 279; h += 7) {
                if (ui_split_field_size((ui_field_kind_t)k, w, h) >= 0) {
                    TEST_ASSERT_TRUE(ui_split_field_size((ui_field_kind_t)k, w + 1 > 400 ? 400 : w + 1, h) >=
                                     ui_split_field_size((ui_field_kind_t)k, w, h));
                    TEST_ASSERT_TRUE(ui_split_field_size((ui_field_kind_t)k, w, h + 7 > 279 ? 279 : h + 7) >=
                                     ui_split_field_size((ui_field_kind_t)k, w, h));
                }
            }
        }
    }
}

static void test_ratios_have_names(void)
{
    static const char *const k_names[] = { "1/4", "1/3", "1/2", "2/3", "3/4" };
    for (int r = UI_RATIO_1_4; r <= UI_RATIO_3_4; r++) {
        TEST_ASSERT_EQUAL_STRING(k_names[r - 1], ui_split_ratio_name(r));
        TEST_ASSERT_EQUAL_INT(r, ui_split_ratio_by_name(k_names[r - 1]));
    }
    TEST_ASSERT_EQUAL_INT(0, ui_split_ratio_by_name("2/4"));
    TEST_ASSERT_EQUAL_INT(0, ui_split_ratio_by_name(NULL));
    TEST_ASSERT_NULL(ui_split_ratio_name(0));
    TEST_ASSERT_NULL(ui_split_ratio_name(6));
}

/* A separator is a 1 px line in its split's gap, 8 px short of each end; a hidden one leaves the gap
 * white, and the cells don't move (spec §5.2). */
static void test_separators_are_drawn_unless_hidden(void)
{
    static uint8_t buf[400 * 300 / 8];
    ui_context_t ctx = fixture_context();
    ui_preset_t p = { .layout = UI_LAYOUT_SPLIT, .split = { ROWS(UI_RATIO_1_2), COLS(UI_RATIO_1_2) | NO_LINE, CELL,
                                                            CELL, COLS(UI_RATIO_1_3), CELL, CELL } };
    gfx_fb_t fb;
    gfx_fb_init(&fb, buf, 400, 300);
    ui_draw_dashboard(&fb, &ctx, &p); /* every cell empty: only the lines below the status bar */
    for (int x = 0; x < 400; x++) { /* the rows' gap at y 160 */
        TEST_ASSERT_EQUAL_INT_MESSAGE(x >= 8 && x < 392, gfx_get_pixel(&fb, x, 160), "the rows' line");
    }
    for (int y = 21; y < 160; y++) { /* the top columns' gap at x 200: hidden */
        TEST_ASSERT_FALSE_MESSAGE(gfx_get_pixel(&fb, 200, y), "a hidden line");
    }
    for (int y = 161; y < 300; y++) { /* the bottom columns' gap at x 133, over 139 px */
        TEST_ASSERT_EQUAL_INT_MESSAGE(y >= 169 && y < 292, gfx_get_pixel(&fb, 133, y), "the columns' line");
    }
    int ink = 0;
    for (int y = 21; y < 300; y++) {
        for (int x = 0; x < 400; x++) {
            ink += gfx_get_pixel(&fb, x, y);
        }
    }
    TEST_ASSERT_EQUAL_INT(384 + 123, ink); /* the two lines and nothing else */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_area_is_everything_under_the_status_bar);
    RUN_TEST(test_each_ratio_rounds_its_first_part_down);
    RUN_TEST(test_a_single_cell_fills_the_area);
    RUN_TEST(test_the_spec_example_lays_out_as_the_spec_says);
    RUN_TEST(test_eight_cells_fill_the_tree);
    RUN_TEST(test_a_tree_cut_short_is_refused);
    RUN_TEST(test_a_part_under_90_by_40_is_refused);
    RUN_TEST(test_a_bad_node_is_refused);
    RUN_TEST(test_a_cells_size_follows_its_dimensions);
    RUN_TEST(test_a_field_draws_at_the_largest_size_with_room_for_it);
    RUN_TEST(test_more_room_never_loses_a_field);
    RUN_TEST(test_ratios_have_names);
    RUN_TEST(test_separators_are_drawn_unless_hidden);
    return UNITY_END();
}
