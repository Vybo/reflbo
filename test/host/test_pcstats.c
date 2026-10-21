#include <stdio.h>
#include <string.h>

#include "pc_msg.h"
#include "unity.h"

/* The PC app's messages (spec §12a, D44): the board's hello, the app's catalogue, readings and status, and
 * /v1/info, as reflbo-stats sends them (its server tests pin the same shapes). */

static pc_catalog_t s_cat;
static pc_values_t s_vals;
static char s_err[96];
static char s_buf[9000];

void setUp(void)
{
    memset(&s_cat, 0, sizeof(s_cat));
    memset(&s_vals, 0, sizeof(s_vals));
    s_err[0] = '\0';
}

void tearDown(void) {}

static pc_msg_kind_t parse(const char *json, pc_hwinfo_t *hw)
{
    pc_hwinfo_t dummy;
    return pc_parse(json, strlen(json), &s_cat, &s_vals, hw ? hw : &dummy, s_err, sizeof(s_err));
}

static const char k_catalog[] =
    "{\"type\":\"catalog\",\"readings\":["
    "{\"id\":\"cpu_load\",\"label\":\"CPU load\",\"unit\":\"%\",\"decimals\":0,\"min\":0,\"max\":100},"
    "{\"id\":\"cpu_temp\",\"label\":\"CPU\",\"unit\":\"\\u00b0C\",\"decimals\":1},"
    "{\"id\":\"ram_used\",\"label\":\"RAM\",\"unit\":\"GB\",\"decimals\":1,\"min\":0,\"max\":64,\"extra\":true}]}";

static void test_the_hello_names_the_protocol_and_the_interval(void)
{
    char out[96];
    size_t n = pc_hello(out, sizeof(out), 1000);
    TEST_ASSERT_EQUAL_STRING("{\"type\":\"hello\",\"protocol\":1,\"interval_ms\":1000}", out);
    TEST_ASSERT_EQUAL(strlen(out), n);
    TEST_ASSERT_EQUAL(0, pc_hello(out, 10, 1000)); /* too small: nothing */
}

static void test_the_catalogue_is_read_with_its_ranges(void)
{
    TEST_ASSERT_EQUAL(PC_MSG_CATALOG, parse(k_catalog, NULL));
    TEST_ASSERT_EQUAL(3, s_cat.count);
    TEST_ASSERT_EQUAL_STRING("cpu_load", s_cat.r[0].id);
    TEST_ASSERT_EQUAL_STRING("CPU load", s_cat.r[0].label);
    TEST_ASSERT_EQUAL_STRING("%", s_cat.r[0].unit);
    TEST_ASSERT_TRUE(s_cat.r[0].has_min && s_cat.r[0].has_max);
    TEST_ASSERT_EQUAL_FLOAT(100.0f, s_cat.r[0].max);
    TEST_ASSERT_EQUAL_STRING("\xc2\xb0" "C", s_cat.r[1].unit);
    TEST_ASSERT_EQUAL(1, s_cat.r[1].decimals);
    TEST_ASSERT_FALSE(s_cat.r[1].has_min || s_cat.r[1].has_max);
    TEST_ASSERT_EQUAL(2, pc_catalog_find(&s_cat, "ram_used"));
    TEST_ASSERT_EQUAL(-1, pc_catalog_find(&s_cat, "gone"));
}

static void test_readings_go_by_id_and_null_is_no_value(void)
{
    parse(k_catalog, NULL);
    TEST_ASSERT_EQUAL(PC_MSG_READINGS,
                      parse("{\"type\":\"readings\",\"seq\":812,\"values\":{\"ram_used\":21.3,\"cpu_load\":12,"
                            "\"cpu_temp\":null,\"unknown\":5}}", NULL));
    TEST_ASSERT_EQUAL_UINT32(812, s_vals.seq);
    TEST_ASSERT_TRUE(s_vals.has[0]);
    TEST_ASSERT_EQUAL_FLOAT(12.0f, s_vals.v[0]);
    TEST_ASSERT_FALSE(s_vals.has[1]);
    TEST_ASSERT_TRUE(s_vals.has[2]);
    TEST_ASSERT_EQUAL_FLOAT(21.3f, s_vals.v[2]);
    /* a reading left out of a message is no value either */
    parse("{\"type\":\"readings\",\"seq\":813,\"values\":{\"cpu_load\":13}}", NULL);
    TEST_ASSERT_FALSE(s_vals.has[2]);
}

static void test_hwinfos_states_by_name(void)
{
    pc_hwinfo_t hw = PC_HW_OK;
    TEST_ASSERT_EQUAL(PC_MSG_STATUS, parse("{\"type\":\"status\",\"hwinfo\":\"stale\"}", &hw));
    TEST_ASSERT_EQUAL(PC_HW_STALE, hw);
    parse("{\"type\":\"status\",\"hwinfo\":\"shm off\"}", &hw);
    TEST_ASSERT_EQUAL(PC_HW_SHM_OFF, hw);
    parse("{\"type\":\"status\",\"hwinfo\":\"something new\"}", &hw);
    TEST_ASSERT_EQUAL(PC_HW_UNKNOWN, hw);
    TEST_ASSERT_EQUAL_STRING("not running", pc_hwinfo_name(PC_HW_NOT_RUNNING));
}

static void test_what_isnt_a_message_is_refused_or_ignored(void)
{
    TEST_ASSERT_EQUAL(PC_MSG_INVALID, parse("not json", NULL));
    TEST_ASSERT_EQUAL_STRING("not JSON", s_err);
    TEST_ASSERT_EQUAL(PC_MSG_INVALID, parse("{\"type\":\"readings\",\"a\":{\"b\":{\"c\":{\"d\":{}}}}}", NULL));
    TEST_ASSERT_EQUAL_STRING("nested too deeply", s_err);
    TEST_ASSERT_EQUAL(PC_MSG_NONE, parse("{\"type\":\"news\",\"text\":\"ignored\"}", NULL)); /* unknown: ignored */
    memset(s_buf, ' ', sizeof(s_buf) - 1);
    s_buf[sizeof(s_buf) - 1] = '\0';
    memcpy(s_buf, "{\"type\":\"status\",\"hwinfo\":\"ok\"}", 31);
    TEST_ASSERT_EQUAL(PC_MSG_INVALID, pc_parse(s_buf, strlen(s_buf), &s_cat, &s_vals, &(pc_hwinfo_t){ 0 }, s_err,
                                               sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("a frame over 8 KB", s_err);
}

static void test_a_catalogue_beyond_the_rules_is_refused_whole(void)
{
    static const struct {
        const char *json, *why;
    } k_cases[] = {
        { "{\"type\":\"catalog\",\"readings\":[{\"id\":\"CPU\",\"label\":\"x\",\"unit\":\"\"}]}",
          "reading 1: an id is 1-15 characters of a-z, 0-9 and _" },
        { "{\"type\":\"catalog\",\"readings\":[{\"id\":\"a_very_long_identifier\",\"label\":\"x\"}]}",
          "reading 1: an id is 1-15 characters of a-z, 0-9 and _" },
        { "{\"type\":\"catalog\",\"readings\":[{\"id\":\"a\"},{\"id\":\"a\"}]}", "reading 2: id \"a\" is used twice" },
        { "{\"type\":\"catalog\",\"readings\":{}}", "a catalogue is a list of readings" },
        { "{\"type\":\"catalog\",\"readings\":[{\"id\":\"a\",\"decimals\":4}]}", "reading 1: decimals are 0-3" },
    };
    for (size_t i = 0; i < sizeof(k_cases) / sizeof(k_cases[0]); i++) {
        parse(k_catalog, NULL);
        TEST_ASSERT_EQUAL_MESSAGE(PC_MSG_INVALID, parse(k_cases[i].json, NULL), k_cases[i].json);
        TEST_ASSERT_EQUAL_STRING(k_cases[i].why, s_err);
        TEST_ASSERT_EQUAL(3, s_cat.count); /* the last good one stays */
    }
    char *const end = s_buf + sizeof(s_buf);
    char *p = s_buf + snprintf(s_buf, sizeof(s_buf), "{\"type\":\"catalog\",\"readings\":[");
    for (int i = 0; i < 33; i++) {
        p += snprintf(p, (size_t)(end - p), "%s{\"id\":\"r%d\"}", i ? "," : "", i);
    }
    strcpy(p, "]}");
    TEST_ASSERT_EQUAL(PC_MSG_INVALID, parse(s_buf, NULL));
    TEST_ASSERT_EQUAL_STRING("32 readings at most", s_err);
}

static void test_long_labels_and_units_are_cut_at_a_character(void)
{
    parse("{\"type\":\"catalog\",\"readings\":[{\"id\":\"a\",\"label\":\"Teplota procesoru \\u010d\\u00e1st 2\","
          "\"unit\":\"m\\u00b3/h\\u00b2xx\"}]}", NULL);
    TEST_ASSERT_EQUAL_STRING("Teplota procesoru \xc4\x8d\xc3\xa1s", s_cat.r[0].label); /* 23 bytes */
    TEST_ASSERT_EQUAL_STRING("m\xc2\xb3/h\xc2\xb2", s_cat.r[0].unit);                  /* 7 bytes */
}

static void test_info_names_the_app_its_protocol_and_hwinfo(void)
{
    pc_info_t info;
    const char *ok = "{\"app\":\"reflbo-stats\",\"protocol\":1,\"version\":\"0.1.0\",\"name\":\"DESKTOP-TEST\",\"hwinfo\":\"ok\"}";
    TEST_ASSERT_TRUE(pc_parse_info(ok, strlen(ok), &info, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL(1, info.protocol);
    TEST_ASSERT_EQUAL_STRING("0.1.0", info.version);
    TEST_ASSERT_EQUAL_STRING("DESKTOP-TEST", info.name);
    TEST_ASSERT_EQUAL(PC_HW_OK, info.hwinfo);
    const char *v2 = "{\"app\":\"reflbo-stats\",\"protocol\":2,\"hwinfo\":\"ok\"}";
    TEST_ASSERT_FALSE(pc_parse_info(v2, strlen(v2), &info, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("protocol 2", s_err);
    const char *other = "{\"app\":\"something else\"}";
    TEST_ASSERT_FALSE(pc_parse_info(other, strlen(other), &info, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("not reflbo-stats", s_err);
}

/* A number no float holds is no value; a sequence or a protocol past an int is clamped, never cast blindly. */
static void test_a_number_no_float_holds_is_no_value(void)
{
    parse(k_catalog, NULL);
    parse("{\"type\":\"readings\",\"seq\":1e20,\"values\":{\"cpu_load\":1e39,\"cpu_temp\":-1e39,"
          "\"ram_used\":3.4e38}}", NULL);
    TEST_ASSERT_FALSE(s_vals.has[0]);
    TEST_ASSERT_FALSE(s_vals.has[1]);
    TEST_ASSERT_TRUE(s_vals.has[2]);
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, s_vals.seq);
    parse("{\"type\":\"catalog\",\"readings\":[{\"id\":\"a\",\"min\":-1e39,\"max\":1e39}]}", NULL);
    TEST_ASSERT_FALSE(s_cat.r[0].has_min || s_cat.r[0].has_max);
    pc_info_t info;
    const char *big = "{\"app\":\"reflbo-stats\",\"protocol\":1e20}";
    TEST_ASSERT_FALSE(pc_parse_info(big, strlen(big), &info, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_STRING("protocol 2147483647", s_err);
}

/* The PC step's answer (spec §9.3, §12a.2): found, asleep, or refused with the reason. */
static void test_the_probe_reads_the_pcs_answer(void)
{
    pc_info_t info;
    char detail[32];
    TEST_ASSERT_EQUAL(PC_PROBE_OFF, pc_probe_result(0, "", 0, &info, detail, sizeof(detail))); /* no answer */
    TEST_ASSERT_EQUAL_STRING("off", detail);
    TEST_ASSERT_EQUAL(PC_PROBE_REFUSED, pc_probe_result(401, "", 0, &info, detail, sizeof(detail)));
    TEST_ASSERT_EQUAL_STRING("wrong token", detail);
    TEST_ASSERT_EQUAL(PC_PROBE_REFUSED, pc_probe_result(503, "", 0, &info, detail, sizeof(detail)));
    TEST_ASSERT_EQUAL_STRING("HTTP 503", detail);
    const char *ok = "{\"app\":\"reflbo-stats\",\"protocol\":1,\"name\":\"DESKTOP-PC\",\"hwinfo\":\"ok\"}";
    TEST_ASSERT_EQUAL(PC_PROBE_FOUND, pc_probe_result(200, ok, strlen(ok), &info, detail, sizeof(detail)));
    TEST_ASSERT_EQUAL_STRING("", detail);
    TEST_ASSERT_EQUAL_STRING("DESKTOP-PC", info.name);
    const char *stale = "{\"app\":\"reflbo-stats\",\"protocol\":1,\"name\":\"PC\",\"hwinfo\":\"not running\"}";
    TEST_ASSERT_EQUAL(PC_PROBE_FOUND, pc_probe_result(200, stale, strlen(stale), &info, detail, sizeof(detail)));
    TEST_ASSERT_EQUAL_STRING("HWiNFO not running", detail); /* a session starts and says why its values are missing */
    const char *v2 = "{\"app\":\"reflbo-stats\",\"protocol\":2}";
    TEST_ASSERT_EQUAL(PC_PROBE_REFUSED, pc_probe_result(200, v2, strlen(v2), &info, detail, sizeof(detail)));
    TEST_ASSERT_EQUAL_STRING("protocol 2", detail);
    const char *other = "<html>router</html>";
    TEST_ASSERT_EQUAL(PC_PROBE_REFUSED, pc_probe_result(200, other, strlen(other), &info, detail, sizeof(detail)));
    TEST_ASSERT_EQUAL_STRING("not reflbo-stats", detail);
}

/* A stream that closes (spec §12a.3, the PC app's §6): the app quitting or turning the board away ends the session at
 * once, as a refused upgrade does; a lost connection waits for the silence rule. */
static void test_a_closed_stream_ends_its_session_or_waits(void)
{
    char why[48];
    TEST_ASSERT_EQUAL(PC_CLOSE_END, pc_close_action(1001, "quit", 0, why, sizeof(why)));
    TEST_ASSERT_EQUAL_STRING("the PC app quit", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_END, pc_close_action(1008, "new token", 0, why, sizeof(why)));
    TEST_ASSERT_EQUAL_STRING("new token", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_END, pc_close_action(1008, "protocol 2", 0, why, sizeof(why)));
    TEST_ASSERT_EQUAL_STRING("protocol 2", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_END, pc_close_action(1008, "", 0, why, sizeof(why)));
    TEST_ASSERT_EQUAL_STRING("refused", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_END, pc_close_action(0, "", 401, why, sizeof(why)));
    TEST_ASSERT_EQUAL_STRING("wrong token", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_END, pc_close_action(0, "", 503, why, sizeof(why))); /* eight boards already */
    TEST_ASSERT_EQUAL_STRING("HTTP 503", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_WAIT, pc_close_action(1001, "no pong", 0, why, sizeof(why))); /* its pings lost */
    TEST_ASSERT_EQUAL_STRING("no pong", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_WAIT, pc_close_action(1006, "", 0, why, sizeof(why))); /* lost */
    TEST_ASSERT_EQUAL_STRING("closed", why);
    TEST_ASSERT_EQUAL(PC_CLOSE_WAIT, pc_close_action(0, "", 0, why, sizeof(why)));
}

/* A refused upgrade's status as esp_websocket_client 1.8.0 reports it: with the error, whatever the error's type
 * (it never sets WEBSOCKET_ERROR_TYPE_HANDSHAKE). 0 for a plain TCP failure, 101 for a stream that was accepted. */
static void test_a_refused_upgrade_reports_its_status(void)
{
    TEST_ASSERT_EQUAL_INT(401, pc_upgrade_status(401));
    TEST_ASSERT_EQUAL_INT(503, pc_upgrade_status(503));
    TEST_ASSERT_EQUAL_INT(0, pc_upgrade_status(0));
    TEST_ASSERT_EQUAL_INT(0, pc_upgrade_status(101));
    TEST_ASSERT_EQUAL_INT(0, pc_upgrade_status(-1));
    TEST_ASSERT_EQUAL_INT(0, pc_upgrade_status(1000));
}

static void test_the_catalogue_survives_its_file(void)
{
    parse(k_catalog, NULL);
    char out[2048];
    size_t n = pc_catalog_json(&s_cat, out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    pc_catalog_t back;
    memset(&back, 0, sizeof(back));
    TEST_ASSERT_EQUAL(PC_MSG_CATALOG, pc_parse(out, n, &back, &s_vals, &(pc_hwinfo_t){ 0 }, s_err, sizeof(s_err)));
    TEST_ASSERT_EQUAL_MEMORY(&s_cat, &back, sizeof(back));
    TEST_ASSERT_EQUAL(0, pc_catalog_json(&s_cat, out, 40)); /* too small: nothing */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_hello_names_the_protocol_and_the_interval);
    RUN_TEST(test_the_catalogue_is_read_with_its_ranges);
    RUN_TEST(test_readings_go_by_id_and_null_is_no_value);
    RUN_TEST(test_hwinfos_states_by_name);
    RUN_TEST(test_what_isnt_a_message_is_refused_or_ignored);
    RUN_TEST(test_a_catalogue_beyond_the_rules_is_refused_whole);
    RUN_TEST(test_long_labels_and_units_are_cut_at_a_character);
    RUN_TEST(test_info_names_the_app_its_protocol_and_hwinfo);
    RUN_TEST(test_a_number_no_float_holds_is_no_value);
    RUN_TEST(test_the_probe_reads_the_pcs_answer);
    RUN_TEST(test_a_closed_stream_ends_its_session_or_waits);
    RUN_TEST(test_a_refused_upgrade_reports_its_status);
    RUN_TEST(test_the_catalogue_survives_its_file);
    return UNITY_END();
}
