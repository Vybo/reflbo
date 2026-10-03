#include <stdio.h>
#include <string.h>

#include "ha_store.h"
#include "unity.h"

#define NOW ((time_t)1790307012) /* 2026-09-25 03:30:12 UTC */

static ha_store_t s_store;
static ha_fields_t s_fields;

static ha_field_t mapping(const char *key, ha_kind_t kind, uint32_t ttl_s)
{
    ha_field_t f = { .kind = (uint8_t)kind, .precision = HA_PRECISION_AUTO, .ttl_s = ttl_s, .topic = "t" };
    snprintf(f.key, sizeof(f.key), "%s", key);
    snprintf(f.label, sizeof(f.label), "Label %s", key);
    snprintf(f.unit, sizeof(f.unit), "%s", kind == HA_KIND_NUMBER ? "°C" : "");
    return f;
}

void setUp(void)
{
    memset(&s_store, 0xAA, sizeof(s_store));
    ha_store_init(&s_store);
    s_fields = (ha_fields_t){ .count = 2 };
    s_fields.field[0] = mapping("outdoor", HA_KIND_NUMBER, 0);
    s_fields.field[1] = mapping("door", HA_KIND_TEXT, 600);
    ha_store_rebuild(&s_store, &s_fields);
}

void tearDown(void) {}

static ha_value_t number(int32_t n, uint8_t decimals)
{
    return (ha_value_t){ .kind = HA_KIND_NUMBER, .number = n, .decimals = decimals };
}

static ha_value_t text(const char *t)
{
    ha_value_t v = { .kind = HA_KIND_TEXT };
    snprintf(v.text, sizeof(v.text), "%s", t);
    return v;
}

/* The entries follow the mappings: their key, label, unit, kind and time to live, no value yet. */
static void test_entries_follow_the_mappings(void)
{
    TEST_ASSERT_EQUAL_UINT8(2, s_store.count);
    TEST_ASSERT_EQUAL_INT(0, ha_store_find(&s_store, "outdoor"));
    TEST_ASSERT_EQUAL_INT(1, ha_store_find(&s_store, "door"));
    TEST_ASSERT_EQUAL_INT(-1, ha_store_find(&s_store, "window"));
    TEST_ASSERT_EQUAL_STRING("Label outdoor", s_store.entry[0].label);
    TEST_ASSERT_EQUAL_STRING("°C", s_store.entry[0].unit);
    TEST_ASSERT_EQUAL_UINT8(HA_KIND_TEXT, s_store.entry[1].kind);
    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 0, NOW));
    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 5, NOW)); /* no such entry */
}

/* spec §12.5: stale after the mapping's ttl_s, or by default twice the expected interval; never in manual. */
static void test_values_go_stale_after_their_time_to_live(void)
{
    ha_store_set_default_ttl(&s_store, 7200);
    ha_value_t v = number(215, 1);
    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW));
    v = text("open");
    TEST_ASSERT_TRUE(ha_store_set(&s_store, 1, &v, NOW));
    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 0, NOW + 7200));
    TEST_ASSERT_EQUAL(HA_STALE, ha_store_freshness(&s_store, 0, NOW + 7201));
    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 1, NOW + 600)); /* its own 10 min */
    TEST_ASSERT_EQUAL(HA_STALE, ha_store_freshness(&s_store, 1, NOW + 601));
    ha_store_set_default_ttl(&s_store, 0);
    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 0, NOW + 30 * 86400)); /* manual: only its age */
    TEST_ASSERT_EQUAL_INT32(215, s_store.entry[0].number);
    TEST_ASSERT_EQUAL_UINT8(1, s_store.entry[0].decimals);
    TEST_ASSERT_EQUAL_STRING("open", s_store.entry[1].text);
}

/* A value that shows the same needs no redraw; a stale one coming back does. */
static void test_a_value_says_whether_the_screen_changes(void)
{
    ha_store_set_default_ttl(&s_store, 7200);
    ha_value_t v = number(215, 1);
    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW));
    TEST_ASSERT_FALSE(ha_store_set(&s_store, 0, &v, NOW + 60));
    v = number(216, 1);
    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW + 120));
    TEST_ASSERT_TRUE(ha_store_set(&s_store, 0, &v, NOW + 8000)); /* it had gone stale */
    v = text("open");
    TEST_ASSERT_FALSE(ha_store_set(&s_store, 0, &v, NOW + 8060)); /* the wrong kind: ignored */
    TEST_ASSERT_EQUAL_INT32(216, s_store.entry[0].number);
    TEST_ASSERT_FALSE(ha_store_set(&s_store, 9, &v, NOW));
}

/* Editing the mappings keeps the values of the keys that stay with their kind. */
static void test_a_rebuild_keeps_what_still_maps(void)
{
    ha_value_t v = number(215, 1);
    ha_store_set(&s_store, 0, &v, NOW);
    v = text("open");
    ha_store_set(&s_store, 1, &v, NOW);
    ha_fields_t next = { .count = 3 };
    next.field[0] = mapping("door", HA_KIND_TEXT, 0);       /* moved */
    next.field[1] = mapping("outdoor", HA_KIND_TEXT, 0);    /* now a text: its number goes */
    next.field[2] = mapping("window", HA_KIND_NUMBER, 0);   /* new */
    snprintf(next.field[0].label, sizeof(next.field[0].label), "%s", "Front door");
    ha_store_rebuild(&s_store, &next);
    TEST_ASSERT_EQUAL_UINT8(3, s_store.count);
    TEST_ASSERT_EQUAL_STRING("door", s_store.entry[0].key);
    TEST_ASSERT_EQUAL_STRING("open", s_store.entry[0].text);
    TEST_ASSERT_EQUAL_STRING("Front door", s_store.entry[0].label);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)NOW, s_store.entry[0].updated);
    TEST_ASSERT_EQUAL_UINT32(0, s_store.entry[1].updated);
    TEST_ASSERT_EQUAL_UINT32(0, s_store.entry[2].updated);
    ha_store_rebuild(&s_store, &(ha_fields_t){ .count = 0 });
    TEST_ASSERT_EQUAL_UINT8(0, s_store.count);
}

/* A rebuild moves the entries in place: values follow their keys through any reordering. */
static void test_a_rebuild_follows_the_keys_through_any_order(void)
{
    static const char *const k_keys[] = { "a", "b", "c", "d" };
    ha_fields_t f = { .count = 4 };
    for (int i = 0; i < 4; i++) {
        f.field[i] = mapping(k_keys[i], HA_KIND_NUMBER, 0);
    }
    ha_store_rebuild(&s_store, &f);
    for (int i = 0; i < 4; i++) {
        ha_value_t v = number(i + 1, 0);
        ha_store_set(&s_store, i, &v, NOW);
    }
    ha_fields_t next = { .count = 4 };
    next.field[0] = mapping("d", HA_KIND_NUMBER, 0);
    next.field[1] = mapping("a", HA_KIND_NUMBER, 0);
    next.field[2] = mapping("x", HA_KIND_NUMBER, 0); /* new */
    next.field[3] = mapping("c", HA_KIND_NUMBER, 0); /* b goes */
    ha_store_rebuild(&s_store, &next);
    static const int k_want[] = { 4, 1, 0, 3 };
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT_EQUAL_STRING(next.field[i].key, s_store.entry[i].key);
        TEST_ASSERT_EQUAL_INT32(k_want[i], s_store.entry[i].number);
        TEST_ASSERT_EQUAL(k_want[i] ? HA_FRESH : HA_MISSING, ha_store_freshness(&s_store, i, NOW));
    }
    /* 32 mappings reversed: every value travels the length of the block */
    ha_fields_t all = { .count = HA_FIELDS_MAX }, reversed = { .count = HA_FIELDS_MAX };
    for (int i = 0; i < HA_FIELDS_MAX; i++) {
        char key[8];
        snprintf(key, sizeof(key), "k%d", i);
        all.field[i] = mapping(key, HA_KIND_NUMBER, 0);
        reversed.field[HA_FIELDS_MAX - 1 - i] = all.field[i];
    }
    ha_store_rebuild(&s_store, &all);
    for (int i = 0; i < HA_FIELDS_MAX; i++) {
        ha_value_t v = number(100 + i, 0);
        ha_store_set(&s_store, i, &v, NOW);
    }
    ha_store_rebuild(&s_store, &reversed);
    for (int i = 0; i < HA_FIELDS_MAX; i++) {
        TEST_ASSERT_EQUAL_INT32(100 + HA_FIELDS_MAX - 1 - i, s_store.entry[i].number);
    }
}

/* The clock moved (a sync set it after a power-off, spec §7): every value and the message keep their age. */
static void test_a_clock_move_keeps_the_ages(void)
{
    const time_t y2000 = 946684800; /* where the RTC starts without its backup cell (D9) */
    ha_value_t v = number(215, 1);
    ha_store_set(&s_store, 0, &v, y2000 + 60);
    ha_store_set_message(&s_store, "Door open", y2000 + 120);
    ha_store_shift_time(&s_store, NOW - (y2000 + 180)); /* the sync's clock: a minute after the message */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(NOW - 120), s_store.entry[0].updated);
    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_freshness(&s_store, 0, NOW));
    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_freshness(&s_store, 1, NOW)); /* no value stays none */
    TEST_ASSERT_EQUAL_UINT32((uint32_t)(NOW - 60), s_store.message_at);
    TEST_ASSERT_TRUE(ha_store_banner(&s_store, NOW));
    ha_store_shift_time(&s_store, -(int64_t)NOW); /* back past 1970: very old, but still there */
    TEST_ASSERT_EQUAL_UINT32(1, s_store.entry[0].updated);
    TEST_ASSERT_EQUAL_UINT32(1, s_store.message_at);
}

/* spec §12.7: a banner until KEY dismisses it, a new message replaces it or 24 h pass; the field shows it
 * until it is replaced or cleared, stale after 24 h. */
static void test_the_message(void)
{
    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW));
    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_message_freshness(&s_store, NOW));
    ha_store_set_message(&s_store, "Washing machine done", NOW);
    TEST_ASSERT_TRUE(ha_store_banner(&s_store, NOW + HA_MESSAGE_FRESH_S));
    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW + HA_MESSAGE_FRESH_S + 1));
    TEST_ASSERT_EQUAL(HA_FRESH, ha_store_message_freshness(&s_store, NOW + HA_MESSAGE_FRESH_S));
    TEST_ASSERT_EQUAL(HA_STALE, ha_store_message_freshness(&s_store, NOW + HA_MESSAGE_FRESH_S + 1));
    ha_store_dismiss(&s_store);
    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW + 60));
    TEST_ASSERT_EQUAL_STRING("Washing machine done", s_store.message); /* the field keeps it */
    ha_store_set_message(&s_store, "Door open", NOW + 120);
    TEST_ASSERT_TRUE(ha_store_banner(&s_store, NOW + 180)); /* a new one shows again */
    ha_store_set_message(&s_store, "", NOW + 240);
    TEST_ASSERT_FALSE(ha_store_banner(&s_store, NOW + 240));
    TEST_ASSERT_EQUAL(HA_MISSING, ha_store_message_freshness(&s_store, NOW + 240));
    ha_store_set_message(&s_store, "kept", NOW + 300);
    ha_store_rebuild(&s_store, &s_fields);
    TEST_ASSERT_EQUAL_STRING("kept", s_store.message); /* new mappings leave the message */
}

/* The block goes into RTC RAM through deep sleep with its own magic, version and CRC (spec §12.5). */
static void test_the_block_is_sealed(void)
{
    TEST_ASSERT_FALSE(ha_store_valid(&s_store)); /* never sealed */
    ha_store_set_message(&s_store, "hello", NOW);
    ha_store_seal(&s_store);
    TEST_ASSERT_TRUE(ha_store_valid(&s_store));
    s_store.message[0] = 'j';
    TEST_ASSERT_FALSE(ha_store_valid(&s_store));
    printf("the store's block: %u bytes\n", (unsigned)sizeof(ha_store_t));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_entries_follow_the_mappings);
    RUN_TEST(test_values_go_stale_after_their_time_to_live);
    RUN_TEST(test_a_value_says_whether_the_screen_changes);
    RUN_TEST(test_a_rebuild_keeps_what_still_maps);
    RUN_TEST(test_a_rebuild_follows_the_keys_through_any_order);
    RUN_TEST(test_a_clock_move_keeps_the_ages);
    RUN_TEST(test_the_message);
    RUN_TEST(test_the_block_is_sealed);
    return UNITY_END();
}
