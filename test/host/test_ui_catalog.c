#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "cJSON.h"
#include "context_fixtures.h"
#include "ui_catalog.h"
#include "ui_split.h"
#include "unity.h"

static char s_out[8192]; /* the layouts take 4 473 bytes with the split rules */
static cJSON *s_root;

void setUp(void)
{
    s_root = NULL;
}

void tearDown(void)
{
    cJSON_Delete(s_root);
}

static const cJSON *by_id(const cJSON *array, const char *id)
{
    const cJSON *item;
    cJSON_ArrayForEach(item, array)
    {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, "id");
        if (cJSON_IsString(v) && strcmp(v->valuestring, id) == 0) {
            return item;
        }
    }
    return NULL;
}

static const char *str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static int num(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) ? v->valueint : -1;
}

/* GET /api/layouts (spec §5.2, §10.3): the preset editor offers only fields a slot can show. */
static void test_layouts_list_their_slots_with_rectangles_sizes_and_kinds(void)
{
    TEST_ASSERT_TRUE(ui_catalog_layouts_json(s_out, sizeof(s_out)) > 0);
    s_root = cJSON_Parse(s_out);
    TEST_ASSERT_NOT_NULL(s_root);
    TEST_ASSERT_EQUAL_INT(400, num(s_root, "width"));
    TEST_ASSERT_EQUAL_INT(300, num(s_root, "height"));
    const cJSON *layouts = cJSON_GetObjectItemCaseSensitive(s_root, "layouts");
    TEST_ASSERT_EQUAL_INT(7, cJSON_GetArraySize(layouts));
    const cJSON *radar = by_id(layouts, "radar"); /* M6: the radars draw their own map, without slots */
    TEST_ASSERT_NOT_NULL(radar);
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(radar, "slots")));
    TEST_ASSERT_NOT_NULL(by_id(layouts, "flights"));
    const cJSON *split = by_id(layouts, "split"); /* M6b: its cells come from each preset's tree */
    TEST_ASSERT_NOT_NULL(split);
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(split, "slots")));
    const cJSON *classic = by_id(layouts, "classic");
    TEST_ASSERT_NOT_NULL(classic);
    const cJSON *slots = cJSON_GetObjectItemCaseSensitive(classic, "slots");
    TEST_ASSERT_EQUAL_INT(6, cJSON_GetArraySize(slots));
    const cJSON *main_slot = by_id(slots, "main");
    TEST_ASSERT_NOT_NULL(main_slot);
    TEST_ASSERT_EQUAL_INT(21, num(main_slot, "y"));
    TEST_ASSERT_EQUAL_INT(400, num(main_slot, "w"));
    TEST_ASSERT_EQUAL_INT(125, num(main_slot, "h"));
    TEST_ASSERT_EQUAL_STRING("XL", str(main_slot, "size"));
    const cJSON *kinds = cJSON_GetObjectItemCaseSensitive(main_slot, "kinds");
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(kinds));
    TEST_ASSERT_EQUAL_STRING("time", cJSON_GetArrayItem(kinds, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("number", cJSON_GetArrayItem(kinds, 1)->valuestring);
    const cJSON *hourly = by_id(cJSON_GetObjectItemCaseSensitive(by_id(layouts, "weather"), "slots"), "hourly");
    TEST_ASSERT_EQUAL_STRING("M", str(hourly, "size"));
    const cJSON *hourly_kinds = cJSON_GetObjectItemCaseSensitive(hourly, "kinds");
    const cJSON *last = cJSON_GetArrayItem(hourly_kinds, cJSON_GetArraySize(hourly_kinds) - 1);
    TEST_ASSERT_EQUAL_STRING("rain_map", last->valuestring); /* a medium slot takes the rain map */
}

/* The size a field draws at by the published rules, as the editor reads them (web/app.js). */
static const char *rule_size(const cJSON *split, const char *kind, int w, int h)
{
    int wide = w >= num(split, "narrow_w");
    const cJSON *size;
    cJSON_ArrayForEach(size, cJSON_GetObjectItemCaseSensitive(split, "sizes"))
    {
        const cJSON *need = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(size, "kinds"), kind);
        if (w >= num(size, "min_w") && need != NULL && h >= cJSON_GetArrayItem(need, wide)->valueint) {
            return str(size, "size");
        }
    }
    return NULL;
}

/* M6b (spec §5.2, D31): the split layout's rules, so the editor offers each cell only the fields
 * that fit it. */
static void test_layouts_publish_the_split_rules(void)
{
    TEST_ASSERT_TRUE(ui_catalog_layouts_json(s_out, sizeof(s_out)) > 0);
    s_root = cJSON_Parse(s_out);
    const cJSON *split = cJSON_GetObjectItemCaseSensitive(s_root, "split");
    TEST_ASSERT_NOT_NULL(split);
    TEST_ASSERT_EQUAL_INT(0, num(split, "x"));
    TEST_ASSERT_EQUAL_INT(21, num(split, "y"));
    TEST_ASSERT_EQUAL_INT(400, num(split, "w"));
    TEST_ASSERT_EQUAL_INT(279, num(split, "h"));
    TEST_ASSERT_EQUAL_INT(8, num(split, "cells"));
    TEST_ASSERT_EQUAL_INT(90, num(split, "min_w"));
    TEST_ASSERT_EQUAL_INT(40, num(split, "min_h"));
    TEST_ASSERT_EQUAL_INT(150, num(split, "narrow_w"));
    TEST_ASSERT_EQUAL_INT(8, num(split, "inset"));
    const cJSON *ratios = cJSON_GetObjectItemCaseSensitive(split, "ratios");
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(ratios));
    TEST_ASSERT_EQUAL_STRING("1/4", cJSON_GetArrayItem(ratios, 0)->valuestring);
    TEST_ASSERT_EQUAL_STRING("3/4", cJSON_GetArrayItem(ratios, 4)->valuestring);
    const cJSON *sizes = cJSON_GetObjectItemCaseSensitive(split, "sizes");
    TEST_ASSERT_EQUAL_INT(5, cJSON_GetArraySize(sizes)); /* the largest first, as a cell takes the first that fits */
    const cJSON *xl = cJSON_GetArrayItem(sizes, 0);
    const cJSON *s = cJSON_GetArrayItem(sizes, 3);
    const cJSON *xs = cJSON_GetArrayItem(sizes, 4);
    TEST_ASSERT_EQUAL_STRING("XL", str(xl, "size"));
    TEST_ASSERT_EQUAL_STRING("S", str(s, "size"));
    TEST_ASSERT_EQUAL_STRING("XS", str(xs, "size")); /* M6c, D34 */
    TEST_ASSERT_EQUAL_INT(40, num(xs, "min_w"));
    const cJSON *xs_min = cJSON_GetObjectItemCaseSensitive(xs, "min_h");
    TEST_ASSERT_EQUAL_INT(20, cJSON_GetArrayItem(xs_min, 0)->valueint);
    TEST_ASSERT_EQUAL_INT(20, cJSON_GetArrayItem(xs_min, 1)->valueint);
    TEST_ASSERT_EQUAL_INT(11, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(xs, "kinds"))); /* S's kinds */
    TEST_ASSERT_EQUAL_INT(400, num(xl, "min_w"));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(xl, "kinds"))); /* time, number */
    const cJSON *s_min = cJSON_GetObjectItemCaseSensitive(s, "min_h");
    TEST_ASSERT_EQUAL_INT(80, cJSON_GetArrayItem(s_min, 0)->valueint); /* narrow, then wide */
    TEST_ASSERT_EQUAL_INT(40, cJSON_GetArrayItem(s_min, 1)->valueint);
    const cJSON *wx = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "weather_now");
    TEST_ASSERT_EQUAL_INT(86, cJSON_GetArrayItem(wx, 0)->valueint);
    TEST_ASSERT_EQUAL_INT(61, cJSON_GetArrayItem(wx, 1)->valueint);
    TEST_ASSERT_NULL(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(s, "kinds"), "series"));
    /* the rules give what the renderer does, everywhere */
    static const char *const k_kinds[UI_FK_COUNT] = { "time", "date", "number", "battery", "moon", "text",
                                                      "weather_now", "weather_day", "series", "sun", "level",
                                                      "pollen", "rain_map" };
    static const char *const k_names[] = { "XS", "S", "M", "L", "XL" };
    for (int k = 0; k < UI_FK_COUNT; k++) {
        for (int w = 40; w <= 400; w += 7) {
            for (int h = 20; h <= 279; h += 3) {
                int size = ui_split_field_size((ui_field_kind_t)k, w, h);
                const char *published = rule_size(split, k_kinds[k], w, h);
                TEST_ASSERT_EQUAL_STRING(size < 0 ? NULL : k_names[size], published);
            }
        }
    }
}

/* GET /api/fields: the catalogue with values right now, labels in English (spec §5.8). */
static void test_fields_carry_their_kind_label_and_current_value(void)
{
    ui_context_t ctx = fixture_context();
    ctx.lang = lang_get("cs"); /* values in the device's language, labels in the web UI's */
    TEST_ASSERT_TRUE(ui_catalog_fields_json(&ctx, s_out, sizeof(s_out)) > 0);
    s_root = cJSON_Parse(s_out);
    const cJSON *fields = cJSON_GetObjectItemCaseSensitive(s_root, "fields");
    TEST_ASSERT_EQUAL_INT(UI_FIELD_COUNT - 1, cJSON_GetArraySize(fields));
    const cJSON *temp = by_id(fields, "env.temp");
    TEST_ASSERT_EQUAL_STRING("number", str(temp, "kind"));
    TEST_ASSERT_EQUAL_STRING("Temperature", str(temp, "label"));
    TEST_ASSERT_EQUAL_STRING("23,4 °C", str(temp, "value"));
    TEST_ASSERT_EQUAL_STRING("fresh", str(temp, "state"));
    TEST_ASSERT_EQUAL_STRING("20:48", str(by_id(fields, "time.clock"), "value"));
    const cJSON *wx = by_id(fields, "wx.now");
    TEST_ASSERT_EQUAL_STRING("weather_now", str(wx, "kind"));
    TEST_ASSERT_EQUAL_STRING("missing", str(wx, "state"));
    TEST_ASSERT_EQUAL_STRING("", str(wx, "value"));
}

static void test_a_buffer_too_small_gives_nothing(void)
{
    TEST_ASSERT_EQUAL_UINT(0, ui_catalog_layouts_json(s_out, 64));
    ui_context_t ctx = fixture_context();
    TEST_ASSERT_EQUAL_UINT(0, ui_catalog_fields_json(&ctx, s_out, 64));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_layouts_list_their_slots_with_rectangles_sizes_and_kinds);
    RUN_TEST(test_layouts_publish_the_split_rules);
    RUN_TEST(test_fields_carry_their_kind_label_and_current_value);
    RUN_TEST(test_a_buffer_too_small_gives_nothing);
    return UNITY_END();
}
