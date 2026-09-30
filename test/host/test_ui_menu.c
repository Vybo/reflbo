#include <string.h>

#include "ui_menu.h"
#include "unity.h"

static ui_menu_t s_m;
static ui_menu_model_t s_model;
static const char *const k_presets[] = { "Home", "Indoor", "Weather", "Focus clock" };

void setUp(void)
{
    memset(&s_model, 0, sizeof(s_model));
    s_model.choices[UI_MI_ACTIVE_PRESET] = k_presets;
    s_model.choice_count[UI_MI_ACTIVE_PRESET] = 4;
    s_model.value[UI_MI_ACTIVE_PRESET] = 1;
    s_model.value[UI_MI_UPDATE_INTERVAL] = 1;
    s_model.local = (struct tm){ .tm_year = 126, .tm_mon = 8, .tm_mday = 25, .tm_hour = 20, .tm_min = 48 };
    ui_menu_open(&s_m);
}

void tearDown(void) {}

static ui_menu_intent_t press(ui_menu_key_t key)
{
    return ui_menu_input(&s_m, &s_model, key);
}

/* Moves the cursor to `item` in the current list and selects it. */
static ui_menu_intent_t open_item(ui_menu_item_t item)
{
    for (int i = 0; i < UI_MI_COUNT && ui_menu_current(&s_m, &s_model) != item; i++) {
        press(UI_MENU_KEY_NEXT);
    }
    TEST_ASSERT_EQUAL_INT(item, ui_menu_current(&s_m, &s_model));
    return press(UI_MENU_KEY_SELECT);
}

static void test_the_root_lists_the_sections_in_order(void)
{
    const ui_menu_item_t expected[] = { UI_MI_PRESETS, UI_MI_WIFI, UI_MI_TIME, UI_MI_DISPLAY, UI_MI_SENSORS,
                                        UI_MI_INFO, UI_MI_SYSTEM };
    ui_menu_item_t items[UI_MI_COUNT];
    int n = ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT);
    TEST_ASSERT_EQUAL_INT(7, n);
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 7);
    TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, ui_menu_current(&s_m, &s_model));
}

static void test_next_wraps_select_enters_and_back_returns_to_the_section(void)
{
    for (int i = 0; i < 7; i++) {
        press(UI_MENU_KEY_NEXT);
    }
    TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, ui_menu_current(&s_m, &s_model)); /* wrapped */
    open_item(UI_MI_SENSORS);
    TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, s_m.section);
    TEST_ASSERT_EQUAL_INT(UI_MI_TEMP_OFFSET, ui_menu_current(&s_m, &s_model));
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_BACK).kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_ROOT, s_m.section);
    TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, ui_menu_current(&s_m, &s_model)); /* the cursor stays on it */
}

static void test_back_at_the_root_and_exit_anywhere_close_the_menu(void)
{
    TEST_ASSERT_EQUAL(UI_MENU_CLOSE, press(UI_MENU_KEY_BACK).kind);
    ui_menu_open(&s_m);
    open_item(UI_MI_TIME);
    TEST_ASSERT_EQUAL(UI_MENU_CLOSE, press(UI_MENU_KEY_EXIT).kind);
}

static void test_a_toggle_flips_at_once(void)
{
    open_item(UI_MI_PRESETS);
    ui_menu_intent_t in = open_item(UI_MI_AUTO_CYCLE);
    TEST_ASSERT_EQUAL(UI_MENU_SET, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_AUTO_CYCLE, in.item);
    TEST_ASSERT_EQUAL_INT(1, in.value);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
}

static void test_a_choice_is_saved_with_select_or_dropped_with_exit(void)
{
    open_item(UI_MI_PRESETS);
    open_item(UI_MI_ACTIVE_PRESET);
    TEST_ASSERT_EQUAL(UI_MENU_EDIT, s_m.mode);
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT); /* 1 -> 2 -> 3 -> 0: wraps */
    TEST_ASSERT_EQUAL_INT(0, s_m.edit);
    press(UI_MENU_KEY_BACK); /* back to 3 */
    ui_menu_intent_t in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_SET, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_ACTIVE_PRESET, in.item);
    TEST_ASSERT_EQUAL_INT(3, in.value);
    press(UI_MENU_KEY_SELECT); /* edit again, then cancel */
    press(UI_MENU_KEY_NEXT);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_EXIT).kind);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
    TEST_ASSERT_EQUAL_INT(UI_MI_PRESETS, s_m.section); /* cancelling an edit doesn't leave the section */
}

static void test_numbers_step_and_stop_at_their_limits(void)
{
    open_item(UI_MI_DISPLAY);
    open_item(UI_MI_UPDATE_INTERVAL); /* 1-15 min */
    press(UI_MENU_KEY_BACK);          /* already at the minimum */
    TEST_ASSERT_EQUAL_INT(1, s_m.edit);
    for (int i = 0; i < 20; i++) {
        press(UI_MENU_KEY_NEXT);
    }
    TEST_ASSERT_EQUAL_INT(15, s_m.edit);
    TEST_ASSERT_EQUAL_INT(15, press(UI_MENU_KEY_SELECT).value);
    press(UI_MENU_KEY_EXIT);
    ui_menu_open(&s_m);
    open_item(UI_MI_SENSORS);
    open_item(UI_MI_TEMP_OFFSET); /* 0.1 degree steps */
    press(UI_MENU_KEY_BACK);
    press(UI_MENU_KEY_BACK);
    TEST_ASSERT_EQUAL_INT(-2, press(UI_MENU_KEY_SELECT).value);
}

static void test_the_date_time_editor_walks_its_fields_and_keeps_the_day_valid(void)
{
    s_model.local.tm_mday = 31;
    s_model.local.tm_mon = 0; /* 31 January */
    open_item(UI_MI_TIME);
    open_item(UI_MI_SET_DATETIME);
    TEST_ASSERT_EQUAL(UI_MENU_DATETIME, s_m.mode);
    TEST_ASSERT_EQUAL_INT(0, s_m.dt_field);        /* the day first */
    press(UI_MENU_KEY_NEXT);                       /* 31 wraps to 1 */
    TEST_ASSERT_EQUAL_INT(1, s_m.dt.tm_mday);
    press(UI_MENU_KEY_BACK);                       /* and back to 31 */
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_SELECT).kind);
    press(UI_MENU_KEY_NEXT);                       /* February: the day drops to 28 */
    TEST_ASSERT_EQUAL_INT(1, s_m.dt.tm_mon);
    TEST_ASSERT_EQUAL_INT(28, s_m.dt.tm_mday);
    press(UI_MENU_KEY_SELECT);                     /* the year */
    press(UI_MENU_KEY_SELECT);                     /* the hour */
    press(UI_MENU_KEY_BACK);                       /* 20 -> 19 */
    press(UI_MENU_KEY_SELECT);                     /* the minute */
    press(UI_MENU_KEY_NEXT);                       /* 48 -> 49 */
    ui_menu_intent_t in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_SET_TIME, in.kind);
    TEST_ASSERT_EQUAL_INT(126, in.local.tm_year);
    TEST_ASSERT_EQUAL_INT(1, in.local.tm_mon);
    TEST_ASSERT_EQUAL_INT(28, in.local.tm_mday);
    TEST_ASSERT_EQUAL_INT(19, in.local.tm_hour);
    TEST_ASSERT_EQUAL_INT(49, in.local.tm_min);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
}

/* Without a backup cell (D9) a power-off resets the RTC to 1 January 2000; the editor then starts
 * in 2026, so setting the time doesn't take 26 presses on the year. */
static void test_the_date_time_editor_starts_in_2026_after_the_clock_was_lost(void)
{
    s_model.local = (struct tm){ .tm_year = 100, .tm_mon = 0, .tm_mday = 1 };
    open_item(UI_MI_TIME);
    open_item(UI_MI_SET_DATETIME);
    TEST_ASSERT_EQUAL_INT(126, s_m.dt.tm_year);
    TEST_ASSERT_EQUAL_INT(0, s_m.dt.tm_mon);
    TEST_ASSERT_EQUAL_INT(1, s_m.dt.tm_mday);
}

static void test_factory_reset_asks_first(void)
{
    open_item(UI_MI_SYSTEM);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_FACTORY_RESET).kind);
    TEST_ASSERT_EQUAL(UI_MENU_CONFIRM, s_m.mode);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_BACK).kind); /* cancelled */
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
    press(UI_MENU_KEY_SELECT);
    ui_menu_intent_t in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_FACTORY_RESET, in.item);
    ui_menu_open(&s_m);
    open_item(UI_MI_SYSTEM);
    in = open_item(UI_MI_REBOOT); /* rebooting loses nothing: no question */
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_REBOOT, in.item);
}

static void test_hidden_items_are_skipped_and_info_does_nothing(void)
{
    s_model.hidden[UI_MI_DISPLAY] = true;
    ui_menu_item_t items[UI_MI_COUNT];
    TEST_ASSERT_EQUAL_INT(6, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT);
    press(UI_MENU_KEY_NEXT);
    TEST_ASSERT_EQUAL_INT(UI_MI_SENSORS, ui_menu_current(&s_m, &s_model));
    open_item(UI_MI_INFO);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_INFO_UPTIME).kind);
    TEST_ASSERT_EQUAL(UI_MENU_BROWSE, s_m.mode);
}

/* Spec §5.7, D18: config mode starts at once; forgetting the networks or the web password asks. */
static void test_the_wifi_section_asks_before_it_forgets_anything(void)
{
    open_item(UI_MI_WIFI);
    ui_menu_item_t items[UI_MI_COUNT];
    const ui_menu_item_t expected[] = { UI_MI_CONFIG_MODE, UI_MI_FORGET_NETWORKS, UI_MI_RESET_PASSWORD };
    TEST_ASSERT_EQUAL_INT(3, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 3);
    ui_menu_intent_t in = open_item(UI_MI_CONFIG_MODE);
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_CONFIG_MODE, in.item);
    const lang_t *en = lang_get("en");
    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_FORGET_NETWORKS).kind);
    TEST_ASSERT_EQUAL(UI_MENU_CONFIRM, s_m.mode);
    TEST_ASSERT_EQUAL_STRING(lang_str(en, LS_CONFIRM_FORGET_NETWORKS), ui_menu_question(UI_MI_FORGET_NETWORKS, en));
    in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_FORGET_NETWORKS, in.item);
    TEST_ASSERT_EQUAL(UI_MENU_NONE, open_item(UI_MI_RESET_PASSWORD).kind);
    TEST_ASSERT_EQUAL(UI_MENU_CONFIRM, s_m.mode);
    TEST_ASSERT_EQUAL_STRING(lang_str(en, LS_CONFIRM_RESET_PASSWORD), ui_menu_question(UI_MI_RESET_PASSWORD, en));
    TEST_ASSERT_EQUAL(UI_MENU_NONE, press(UI_MENU_KEY_NEXT).kind); /* only a long press confirms */
    in = press(UI_MENU_KEY_SELECT);
    TEST_ASSERT_EQUAL(UI_MENU_ACTION, in.kind);
    TEST_ASSERT_EQUAL_INT(UI_MI_RESET_PASSWORD, in.item);
    TEST_ASSERT_EQUAL_STRING(lang_str(en, LS_CONFIRM_FACTORY_RESET), ui_menu_question(UI_MI_FACTORY_RESET, en));
    TEST_ASSERT_NULL(ui_menu_question(UI_MI_REBOOT, en));
}

/* Spec §5.7: Info gains the IP address and the MAC with M4. */
static void test_info_shows_the_network_addresses(void)
{
    open_item(UI_MI_INFO);
    ui_menu_item_t items[UI_MI_COUNT];
    const ui_menu_item_t expected[] = { UI_MI_INFO_BATTERY, UI_MI_INFO_FIRMWARE, UI_MI_INFO_DEVICE, UI_MI_INFO_IP,
                                        UI_MI_INFO_MAC, UI_MI_INFO_UPTIME, UI_MI_INFO_MEMORY };
    TEST_ASSERT_EQUAL_INT(7, ui_menu_visible(&s_m, &s_model, items, UI_MI_COUNT));
    TEST_ASSERT_EQUAL_INT_ARRAY(expected, items, 7);
    TEST_ASSERT_TRUE(ui_menu_is_section(UI_MI_WIFI));
    TEST_ASSERT_FALSE(ui_menu_is_section(UI_MI_INFO_IP));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_root_lists_the_sections_in_order);
    RUN_TEST(test_next_wraps_select_enters_and_back_returns_to_the_section);
    RUN_TEST(test_back_at_the_root_and_exit_anywhere_close_the_menu);
    RUN_TEST(test_a_toggle_flips_at_once);
    RUN_TEST(test_a_choice_is_saved_with_select_or_dropped_with_exit);
    RUN_TEST(test_numbers_step_and_stop_at_their_limits);
    RUN_TEST(test_the_date_time_editor_walks_its_fields_and_keeps_the_day_valid);
    RUN_TEST(test_the_date_time_editor_starts_in_2026_after_the_clock_was_lost);
    RUN_TEST(test_factory_reset_asks_first);
    RUN_TEST(test_hidden_items_are_skipped_and_info_does_nothing);
    RUN_TEST(test_the_wifi_section_asks_before_it_forgets_anything);
    RUN_TEST(test_info_shows_the_network_addresses);
    return UNITY_END();
}
