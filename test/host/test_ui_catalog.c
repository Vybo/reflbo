#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "cJSON.h"
#include "context_fixtures.h"
#include "ui_catalog.h"
#include "unity.h"

static char s_out[8192];
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
    TEST_ASSERT_EQUAL_INT(6, cJSON_GetArraySize(layouts));
    const cJSON *radar = by_id(layouts, "radar"); /* M6: the radars draw their own map, without slots */
    TEST_ASSERT_NOT_NULL(radar);
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(radar, "slots")));
    TEST_ASSERT_NOT_NULL(by_id(layouts, "flights"));
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
    RUN_TEST(test_fields_carry_their_kind_label_and_current_value);
    RUN_TEST(test_a_buffer_too_small_gives_nothing);
    return UNITY_END();
}
