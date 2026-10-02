#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map_data.h"
#include "unity.h"

/* The real assets/map/map.bin, as tools/gen_map.py packs it (spec §11.1). */

static uint8_t *s_blob;
static size_t s_len;
static map_data_t s_map;

void setUp(void) {}
void tearDown(void) {}

static void load(void)
{
    FILE *f = fopen(MAP_BIN, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, MAP_BIN);
    fseek(f, 0, SEEK_END);
    s_len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    s_blob = malloc(s_len);
    TEST_ASSERT_EQUAL_size_t(s_len, fread(s_blob, 1, s_len, f));
    fclose(f);
}

static void test_the_blob_opens_and_holds_the_world(void)
{
    TEST_ASSERT_TRUE(map_data_open(&s_map, s_blob, s_len));
    TEST_ASSERT_TRUE(map_data_verify(&s_map)); /* the file as read from disk */
    TEST_ASSERT_TRUE(s_map.lines > 2000);
    TEST_ASSERT_TRUE(s_map.towns > 11000); /* Natural Earth's 7342 and GeoNames' (D29) */
    TEST_ASSERT_TRUE(s_map.airports > 5000);
}

typedef struct {
    char names[64][32]; /* the first 64 */
    int count;
    uint32_t last_population;
    bool rising; /* a town larger than the one before it */
} towns_t;

static bool on_town(void *ctx, const map_town_t *t)
{
    towns_t *c = ctx;
    if (c->count < 64) {
        snprintf(c->names[c->count], sizeof(c->names[0]), "%s", t->name);
    }
    c->rising |= c->count > 0 && t->population > c->last_population;
    c->last_population = t->population;
    c->count++;
    return true;
}

static int index_of(const towns_t *c, const char *name)
{
    for (int i = 0; i < c->count && i < 64; i++) {
        if (strcmp(c->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static void test_towns_around_brno_come_largest_first(void)
{
    map_view_t v;
    map_view_init(&v, 491951, 166068, 6.5, 400, 280); /* the weather radar's default view */
    map_bounds_t b;
    map_view_bounds(&v, &b);
    towns_t c = { 0 };
    map_data_towns(&s_map, &b, on_town, &c);
    TEST_ASSERT_FALSE(c.rising);
    int wien = index_of(&c, "Wien"), praha = index_of(&c, "Praha"); /* Natural Earth's local names */
    TEST_ASSERT_TRUE(wien >= 0 && praha > wien);
    TEST_ASSERT_TRUE(index_of(&c, "Brno") > praha);
    TEST_ASSERT_TRUE(index_of(&c, "Olomouc") > index_of(&c, "Brno"));
    TEST_ASSERT_EQUAL_INT(-1, index_of(&c, "Plzeň")); /* west of the view */

    b = (map_bounds_t){ 49.0, 50.5, 12.5, 14.0 };
    towns_t west = { 0 };
    map_data_towns(&s_map, &b, on_town, &west);
    TEST_ASSERT_TRUE(index_of(&west, "Plzeň") >= 0); /* corrected from "Pizen" */
}

/* GeoNames' places inside ČHMÚ's radar area (D29): the smaller towns, but no districts and no second
 * name for a town Natural Earth has. */
static void test_geonames_adds_the_smaller_towns_but_no_districts(void)
{
    map_bounds_t b = { 49.0, 49.45, 16.2, 17.1 }; /* around Brno */
    towns_t c = { 0 };
    map_data_towns(&s_map, &b, on_town, &c);
    TEST_ASSERT_EQUAL_INT(0, index_of(&c, "Brno"));
    TEST_ASSERT_TRUE(index_of(&c, "Vyškov") > 0);
    TEST_ASSERT_TRUE(index_of(&c, "Blansko") > 0);
    TEST_ASSERT_TRUE(index_of(&c, "Kuřim") > 0);
    TEST_ASSERT_EQUAL_INT(-1, index_of(&c, "Brno střed"));
    TEST_ASSERT_EQUAL_INT(-1, index_of(&c, "Líšeň"));

    b = (map_bounds_t){ 49.9, 50.3, 14.1, 14.8 }; /* around Prague */
    towns_t p = { 0 };
    map_data_towns(&s_map, &b, on_town, &p);
    TEST_ASSERT_EQUAL_INT(0, index_of(&p, "Praha"));
    TEST_ASSERT_EQUAL_INT(-1, index_of(&p, "Prague"));
    TEST_ASSERT_EQUAL_INT(-1, index_of(&p, "Stodůlky"));
    TEST_ASSERT_TRUE(index_of(&p, "Kladno") > 0);
}

static bool stop_after_one(void *ctx, const map_town_t *t)
{
    (void)t;
    (*(int *)ctx)++;
    return false;
}

static void test_a_callback_can_stop_the_walk(void)
{
    map_bounds_t world = { -90, 90, -180, 180 };
    int n = 0;
    map_data_towns(&s_map, &world, stop_after_one, &n);
    TEST_ASSERT_EQUAL_INT(1, n);
}

typedef struct {
    char iata[8][4];
    bool large[8];
    int count;
} airports_t;

static bool on_airport(void *ctx, const map_airport_t *a)
{
    airports_t *c = ctx;
    if (c->count < 8) {
        memcpy(c->iata[c->count], a->iata, 4);
        c->large[c->count] = a->large;
    }
    c->count++;
    return true;
}

static void test_airports_near_brno_come_large_first(void)
{
    map_bounds_t b = { 48.0, 49.5, 16.0, 17.5 };
    airports_t c = { 0 };
    map_data_airports(&s_map, &b, on_airport, &c);
    TEST_ASSERT_TRUE(c.count >= 2);
    TEST_ASSERT_EQUAL_STRING("VIE", c.iata[0]);
    TEST_ASSERT_TRUE(c.large[0]);
    bool brq = false;
    for (int i = 1; i < c.count && i < 8; i++) {
        brq |= strcmp(c.iata[i], "BRQ") == 0 && !c.large[i];
    }
    TEST_ASSERT_TRUE(brq);
}

typedef struct {
    map_bounds_t box;
    int borders, coasts, outside;
} segments_t;

static void on_segment(void *ctx, map_line_kind_t kind, double lat0, double lon0, double lat1, double lon1)
{
    segments_t *s = ctx;
    if (kind == MAP_LINE_BORDER) {
        s->borders++;
    } else {
        s->coasts++;
    }
    if (fmax(lat0, lat1) < s->box.lat_min || fmin(lat0, lat1) > s->box.lat_max || fmax(lon0, lon1) < s->box.lon_min ||
        fmin(lon0, lon1) > s->box.lon_max) {
        s->outside++;
    }
}

static void test_segments_near_mikulov_are_the_border_only(void)
{
    segments_t s = { .box = { 48.7, 48.9, 16.5, 16.8 } }; /* the Czech-Austrian border, far from any sea */
    map_data_segments(&s_map, &s.box, on_segment, &s);
    TEST_ASSERT_TRUE(s.borders > 0);
    TEST_ASSERT_EQUAL_INT(0, s.coasts);
    TEST_ASSERT_EQUAL_INT(0, s.outside);

    segments_t sea = { .box = { 53.5, 54.5, 9.5, 11.0 } }; /* Kiel: the Baltic coast */
    map_data_segments(&s_map, &sea.box, on_segment, &sea);
    TEST_ASSERT_TRUE(sea.coasts > 0);
}

static void test_a_damaged_blob_is_refused(void)
{
    uint8_t *copy = malloc(s_len);
    memcpy(copy, s_blob, s_len);
    map_data_t d;
    copy[s_len / 2] ^= 0x10; /* the CRC no longer matches: it opens, as the device opens it, but fails the check */
    TEST_ASSERT_TRUE(map_data_open(&d, copy, s_len));
    TEST_ASSERT_FALSE(map_data_verify(&d));
    copy[s_len / 2] ^= 0x10;
    TEST_ASSERT_TRUE(map_data_open(&d, copy, s_len));
    TEST_ASSERT_TRUE(map_data_verify(&d));
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len - 1) && map_data_verify(&d)); /* cut short: one refuses it */
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len / 2));                       /* cut into its sections */
    TEST_ASSERT_FALSE(map_data_open(&d, copy, 20));
    copy[4] = 2; /* a version this firmware doesn't read */
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len));
    copy[4] = 1;
    copy[0] = 'X';
    TEST_ASSERT_FALSE(map_data_open(&d, copy, s_len));
    free(copy);
}

int main(void)
{
    load();
    UNITY_BEGIN();
    RUN_TEST(test_the_blob_opens_and_holds_the_world);
    RUN_TEST(test_towns_around_brno_come_largest_first);
    RUN_TEST(test_geonames_adds_the_smaller_towns_but_no_districts);
    RUN_TEST(test_a_callback_can_stop_the_walk);
    RUN_TEST(test_airports_near_brno_come_large_first);
    RUN_TEST(test_segments_near_mikulov_are_the_border_only);
    RUN_TEST(test_a_damaged_blob_is_refused);
    int failures = UNITY_END();
    free(s_blob);
    return failures;
}
