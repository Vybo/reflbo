#include <stdio.h>
#include <string.h>

#include "storage_backup.h"
#include "unity.h"

static char s_bundle[4096], s_buf[4096], s_err[96];
static backup_file_t s_files[4];

void setUp(void)
{
    s_err[0] = '\0';
}

void tearDown(void) {}

static const backup_file_t k_files[] = {
    { "settings.json", "{\"schema\": 1, \"language\": \"cs\", \"location\": {\"name\": \"Kraków\"}}" },
    { "presets.json", "{\"schema\": 1, \"active\": \"home\", \"presets\": []}" },
};

/* GET /api/backup, then POST /api/restore (spec §14.4): the same files come back. */
static void test_a_bundle_round_trips_its_files(void)
{
    size_t n = backup_build(k_files, 2, "reflbo-bb94", "0.4.0", s_bundle, sizeof(s_bundle));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(s_bundle, "\"reflbo_backup\":1"));
    TEST_ASSERT_NOT_NULL(strstr(s_bundle, "\"device\":\"reflbo-bb94\""));
    TEST_ASSERT_NOT_NULL(strstr(s_bundle, "\"firmware\":\"0.4.0\""));
    int count = backup_split(s_bundle, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err));
    TEST_ASSERT_EQUAL_INT_MESSAGE(2, count, s_err);
    TEST_ASSERT_EQUAL_STRING("settings.json", s_files[0].name);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"language\":\"cs\",\"location\":{\"name\":\"Kraków\"}}", s_files[0].text);
    TEST_ASSERT_EQUAL_STRING("presets.json", s_files[1].name);
    TEST_ASSERT_EQUAL_STRING("{\"schema\":1,\"active\":\"home\",\"presets\":[]}", s_files[1].text);
}

/* A file that doesn't parse (it would be ignored at boot anyway) stays out of the bundle. */
static void test_a_broken_file_is_left_out(void)
{
    const backup_file_t files[] = { { "settings.json", "{\"schema\": 1" }, k_files[1] };
    TEST_ASSERT_TRUE(backup_build(files, 2, "reflbo-bb94", "0.4.0", s_bundle, sizeof(s_bundle)) > 0);
    TEST_ASSERT_EQUAL_INT(1, backup_split(s_bundle, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("presets.json", s_files[0].name);
    TEST_ASSERT_EQUAL_UINT(0, backup_build(k_files, 2, "reflbo-bb94", "0.4.0", s_bundle, 32)); /* no room */
}

static void test_restore_refuses_anything_but_a_backup(void)
{
    const char *bad[] = {
        "not json",
        "[]",
        "{\"files\": {}}",                                        /* not a backup */
        "{\"reflbo_backup\": 2, \"files\": {}}",                  /* a later format */
        "{\"reflbo_backup\": 1, \"files\": []}",                  /* files isn't an object */
        "{\"reflbo_backup\": 1, \"files\": {\"a.json\": \"x\"}}", /* a file isn't an object */
        "{\"reflbo_backup\": 1, \"files\": {\"../x.json\": {}}}", /* not a plain name */
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, backup_split(bad[i], s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err)),
                                      bad[i]);
        TEST_ASSERT_TRUE(s_err[0] != '\0');
    }
    TEST_ASSERT_TRUE(backup_build(k_files, 2, "reflbo-bb94", "0.4.0", s_bundle, sizeof(s_bundle)) > 0);
    TEST_ASSERT_EQUAL_INT(-1, backup_split(s_bundle, s_files, 1, s_buf, sizeof(s_buf), s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_INT(-1, backup_split(s_bundle, s_files, 4, s_buf, 40, s_err, sizeof(s_err)));
}

static void test_deep_nesting_is_refused_before_parsing(void)
{
    static char deep[256];
    size_t n = (size_t)snprintf(deep, sizeof(deep), "{\"reflbo_backup\": 1, \"files\": {\"a.json\": ");
    for (int i = 0; i < 40; i++) {
        deep[n++] = '[';
    }
    for (int i = 0; i < 40; i++) {
        deep[n++] = ']';
    }
    snprintf(deep + n, sizeof(deep) - n, "}}");
    TEST_ASSERT_EQUAL_INT(-1, backup_split(deep, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err)));
    TEST_ASSERT_NOT_NULL(strstr(s_err, "nested"));
}

/* A file nested as deep as presets.json may be (UI_JSON_MAX_DEPTH, two levels inside the bundle) restores; a level
 * more is refused before anything parses it. */
static void test_the_deepest_file_a_bundle_holds(void)
{
    static char deep[256];
    for (int more = 0; more < 2; more++) {
        int arrays = BACKUP_MAX_DEPTH - 2 - 1 + more; /* inside the file's own object */
        size_t n = (size_t)snprintf(deep, sizeof(deep), "{\"reflbo_backup\": 1, \"files\": {\"a.json\": {\"x\": ");
        for (int i = 0; i < arrays; i++) {
            deep[n++] = '[';
        }
        for (int i = 0; i < arrays; i++) {
            deep[n++] = ']';
        }
        snprintf(deep + n, sizeof(deep) - n, "}}}");
        int got = backup_split(deep, s_files, 4, s_buf, sizeof(s_buf), s_err, sizeof(s_err));
        TEST_ASSERT_EQUAL_INT_MESSAGE(more ? -1 : 1, got, s_err);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_bundle_round_trips_its_files);
    RUN_TEST(test_a_broken_file_is_left_out);
    RUN_TEST(test_restore_refuses_anything_but_a_backup);
    RUN_TEST(test_deep_nesting_is_refused_before_parsing);
    RUN_TEST(test_the_deepest_file_a_bundle_holds);
    return UNITY_END();
}
