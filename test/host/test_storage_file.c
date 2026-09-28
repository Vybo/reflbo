#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "storage_file.h"
#include "unity.h"

/* Every test runs in an empty TEST_TMP_DIR (in the build directory), with a path as short as the
 * device's: the build directory's own path may not fit STORAGE_PATH_MAX. */
#define PATH "cfg.json"

static int s_parse_calls;
static char s_parsed[64];

/* Accepts text that starts like a JSON object, and keeps it. */
static bool parse_object(const char *text, void *ctx)
{
    (void)ctx;
    s_parse_calls++;
    if (text[0] != '{') {
        return false;
    }
    snprintf(s_parsed, sizeof(s_parsed), "%s", text);
    return true;
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    fputs(text, f);
    fclose(f);
}

static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static const char *contents(const char *path)
{
    static char text[64];
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL(f);
    size_t n = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[n] = '\0';
    return text;
}

static storage_file_result_t load(char *buf, size_t size, bool *from_backup, unsigned *rejected)
{
    return storage_file_load(PATH, buf, size, parse_object, NULL, from_backup, rejected);
}

void setUp(void)
{
    mkdir(TEST_TMP_DIR, 0775);
    TEST_ASSERT_EQUAL(0, chdir(TEST_TMP_DIR));
    unlink(PATH);
    unlink(PATH ".bak");
    unlink(PATH ".tmp");
    rmdir(PATH ".tmp");
    s_parse_calls = 0;
    s_parsed[0] = '\0';
}

void tearDown(void) {}

static void test_nothing_there_is_missing(void)
{
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 99;
    TEST_ASSERT_EQUAL(STORAGE_FILE_MISSING, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL(0, s_parse_calls);
    TEST_ASSERT_EQUAL(0, rejected);
}

static void test_the_first_write_leaves_no_backup(void)
{
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, storage_file_write_atomic(PATH, "{\"v\":1}", 7));
    TEST_ASSERT_EQUAL_STRING("{\"v\":1}", contents(PATH));
    TEST_ASSERT_FALSE(exists(PATH ".bak"));
    TEST_ASSERT_FALSE(exists(PATH ".tmp"));
}

static void test_each_write_keeps_the_previous_file_as_the_backup(void)
{
    storage_file_write_atomic(PATH, "{\"v\":1}", 7);
    storage_file_write_atomic(PATH, "{\"v\":2}", 7);
    TEST_ASSERT_EQUAL_STRING("{\"v\":2}", contents(PATH));
    TEST_ASSERT_EQUAL_STRING("{\"v\":1}", contents(PATH ".bak"));
    storage_file_write_atomic(PATH, "{\"v\":3}", 7);
    TEST_ASSERT_EQUAL_STRING("{\"v\":3}", contents(PATH));
    TEST_ASSERT_EQUAL_STRING("{\"v\":2}", contents(PATH ".bak"));
    TEST_ASSERT_FALSE(exists(PATH ".tmp"));
}

static void test_a_good_file_is_used_without_reading_the_backup(void)
{
    put(PATH, "{\"main\":1}");
    put(PATH ".bak", "{\"backup\":1}");
    char buf[64];
    bool from_backup = true;
    unsigned rejected = 99;
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_FALSE(from_backup);
    TEST_ASSERT_EQUAL_STRING("{\"main\":1}", s_parsed);
    TEST_ASSERT_EQUAL(1, s_parse_calls);
    TEST_ASSERT_EQUAL(0, rejected);
}

static void test_a_rejected_file_falls_back_to_the_backup(void)
{
    storage_file_write_atomic(PATH, "{\"good\":1}", 10);
    storage_file_write_atomic(PATH, "garbage", 7);
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 0;
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_TRUE(from_backup);
    TEST_ASSERT_EQUAL_STRING("{\"good\":1}", s_parsed);
    TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN, rejected);
}

static void test_power_lost_between_the_renames_loads_the_previous_file(void)
{
    /* <path> already became <path>.bak; the new file is still <path>.tmp. */
    put(PATH ".bak", "{\"previous\":1}");
    put(PATH ".tmp", "{\"next\":1}");
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 99;
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_TRUE(from_backup);
    TEST_ASSERT_EQUAL_STRING("{\"previous\":1}", s_parsed);
    TEST_ASSERT_EQUAL(0, rejected); /* a missing file is not a rejected one */
}

static void test_a_file_too_big_for_the_buffer_is_rejected_unparsed(void)
{
    char buf[16];
    bool from_backup = false;
    unsigned rejected = 0;
    put(PATH, "{\"v\":\"01234567\"}"); /* 16 bytes: one too many */
    TEST_ASSERT_EQUAL(STORAGE_FILE_INVALID, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL(0, s_parse_calls);
    TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN, rejected);
    put(PATH, "{\"v\":\"0123456\"}"); /* 15 bytes fit */
    TEST_ASSERT_EQUAL(STORAGE_FILE_OK, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL_STRING("{\"v\":\"0123456\"}", s_parsed);
}

static void test_both_rejected_is_invalid(void)
{
    put(PATH, "nope");
    put(PATH ".bak", "nor this");
    char buf[64];
    bool from_backup = false;
    unsigned rejected = 0;
    TEST_ASSERT_EQUAL(STORAGE_FILE_INVALID, load(buf, sizeof(buf), &from_backup, &rejected));
    TEST_ASSERT_EQUAL(2, s_parse_calls);
    TEST_ASSERT_EQUAL(STORAGE_REJECTED_MAIN | STORAGE_REJECTED_BACKUP, rejected);
}

static void test_a_failed_write_changes_nothing(void)
{
    put(PATH, "{\"old\":1}");
    TEST_ASSERT_EQUAL(0, mkdir(PATH ".tmp", 0775)); /* now .tmp can't be opened for writing */
    TEST_ASSERT_EQUAL(STORAGE_FILE_IO_ERROR, storage_file_write_atomic(PATH, "{\"new\":1}", 9));
    TEST_ASSERT_EQUAL(EISDIR, errno);
    TEST_ASSERT_EQUAL_STRING("{\"old\":1}", contents(PATH));
    TEST_ASSERT_FALSE(exists(PATH ".bak"));
}

static void test_bad_arguments_are_refused(void)
{
    char path[STORAGE_PATH_MAX];
    memset(path, 'a', sizeof(path) - 4);
    path[sizeof(path) - 4] = '\0'; /* fits, but not with ".bak" */
    char buf[64];
    bool from_backup = false;
    TEST_ASSERT_EQUAL(STORAGE_FILE_BAD_ARG, storage_file_write_atomic(path, "{}", 2));
    TEST_ASSERT_EQUAL(STORAGE_FILE_BAD_ARG,
                      storage_file_load(path, buf, sizeof(buf), parse_object, NULL, &from_backup, NULL));
    TEST_ASSERT_EQUAL(STORAGE_FILE_BAD_ARG, storage_file_load(PATH, buf, 1, parse_object, NULL, &from_backup, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_nothing_there_is_missing);
    RUN_TEST(test_the_first_write_leaves_no_backup);
    RUN_TEST(test_each_write_keeps_the_previous_file_as_the_backup);
    RUN_TEST(test_a_good_file_is_used_without_reading_the_backup);
    RUN_TEST(test_a_rejected_file_falls_back_to_the_backup);
    RUN_TEST(test_power_lost_between_the_renames_loads_the_previous_file);
    RUN_TEST(test_a_file_too_big_for_the_buffer_is_rejected_unparsed);
    RUN_TEST(test_both_rejected_is_invalid);
    RUN_TEST(test_a_failed_write_changes_nothing);
    RUN_TEST(test_bad_arguments_are_refused);
    return UNITY_END();
}
