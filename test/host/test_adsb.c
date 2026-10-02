#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adsb.h"
#include "unity.h"

/* The flight radar's data (spec §11.3). adsb_fi.json is adsb.fi's reply for 105 NM around Brno on
 * 2026-10-01 at 22:52 UTC, cut to 13 of its 32 aircraft; the routes are adsb.lol's replies of the
 * same minute. The expected distances come from Python over the same coordinates. */

#define BRNO_LAT 49.1951
#define BRNO_LON 16.6068

static adsb_list_t s_list;

void setUp(void)
{
    memset(&s_list, 0, sizeof(s_list));
}

void tearDown(void) {}

static char *load(const char *name, size_t *len)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", FIXTURE_DIR, name);
    FILE *f = fopen(path, "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(f, path);
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *data = malloc(*len + 1);
    TEST_ASSERT_EQUAL_size_t(*len, fread(data, 1, *len, f));
    data[*len] = '\0';
    fclose(f);
    return data;
}

/* The Flights map (400 x 238) around Brno with its range to the top edge. */
static adsb_filter_t filter(double range_km)
{
    adsb_filter_t f = { .lat = BRNO_LAT, .lon = BRNO_LON, .min_alt_ft = 0, .ground = false, .max = ADSB_MAX };
    map_view_init(&f.view, 491951, 166068, map_zoom_for_range(491951, range_km * 1000.0, 119), 400, 238);
    return f;
}

static void test_a_real_reply_keeps_the_aircraft_on_the_map_nearest_first(void)
{
    size_t len;
    char *json = load("adsb_fi.json", &len);
    adsb_filter_t f = filter(100);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT64(1790895156001LL, s_list.now_ms);
    TEST_ASSERT_EQUAL_INT(8, s_list.count); /* 13 in the reply: 2 on the ground and 3 more off the map */
    static const char *const k_order[] = { "TVS7UZ", "BAW15", "SIA321", "BLX204", "SXS6WN", "TVS56K", "RYR3698",
                                           "CAI1HA" };
    for (int i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_STRING(k_order[i], s_list.ac[i].callsign);
    }
    const adsb_aircraft_t *a = &s_list.ac[0];
    TEST_ASSERT_EQUAL_STRING("49d67d", a->hex);
    TEST_ASSERT_EQUAL_STRING("B38M", a->type);
    TEST_ASSERT_EQUAL_INT32(3675, a->alt_ft);
    TEST_ASSERT_EQUAL_INT16(248, a->speed_kt); /* 247.8 */
    TEST_ASSERT_EQUAL_INT16(122, a->track);    /* 121.64 */
    TEST_ASSERT_UINT32_WITHIN(2, 21726, a->dist_m);
    TEST_ASSERT_EQUAL_UINT16(119, a->bearing); /* 118.7: east-south-east, to the airport's east */
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 49.100973, a->lat);
    TEST_ASSERT_UINT32_WITHIN(2, 160803, s_list.ac[7].dist_m);
    free(json);
}

static void test_the_filters_and_the_cap_apply(void)
{
    size_t len;
    char *json = load("adsb_fi.json", &len);
    adsb_filter_t f = filter(100);
    f.max = 3;
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(3, s_list.count); /* the nearest three */
    TEST_ASSERT_EQUAL_STRING("SIA321", s_list.ac[2].callsign);

    f = filter(100);
    f.min_alt_ft = 20000;
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(6, s_list.count); /* TVS7UZ at 3675 ft and TVS56K at 19000 ft are left out */
    TEST_ASSERT_EQUAL_STRING("BAW15", s_list.ac[0].callsign);

    f = filter(50);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(4, s_list.count); /* a closer map holds fewer */
    TEST_ASSERT_EQUAL_STRING("BLX204", s_list.ac[3].callsign);

    f = filter(100);
    f.max = 0; /* clamped to 1 */
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(json, len, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(1, s_list.count);
    free(json);
}

static const char k_odd[] =
    "{\"now\":1790895156001,\"total\":6,\"ac\":["
    "{\"hex\":\"aaaaa1\",\"flight\":\"GND1    \",\"t\":\"A320\",\"alt_baro\":\"ground\",\"gs\":12.0,"
    "\"lat\":49.20,\"lon\":16.61},"
    "{\"hex\":\"aaaaa2\",\"flight\":\"LOW1\",\"t\":\"C172\",\"alt_baro\":1500,\"gs\":95,\"track\":45,"
    "\"lat\":49.21,\"lon\":16.62},"
    "{\"hex\":\"aaaaa3\",\"flight\":\"NOPOS\",\"alt_baro\":30000},"
    "{\"hex\":\"~bbbbb4\",\"alt_baro\":12000,\"gs\":300,\"track\":359.9,\"lat\":49.25,\"lon\":16.70},"
    "{\"hex\":\"aaaaa5\",\"flight\":\"NOALT\",\"lat\":49.30,\"lon\":16.60},"
    "{\"hex\":\"aaaaa6\",\"flight\":\"FAR\",\"alt_baro\":30000,\"lat\":10.0,\"lon\":10.0},"
    "\"junk\",17,"
    "{\"hex\":\"aaaaa7\",\"flight\":\"BADLAT\",\"alt_baro\":30000,\"lat\":\"49.2\",\"lon\":16.6}"
    "]}";

static void test_odd_aircraft_are_kept_or_left_out_as_they_should(void)
{
    adsb_filter_t f = filter(25);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(k_odd, strlen(k_odd), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(3, s_list.count); /* not on the ground, not without a position, not off the map */
    TEST_ASSERT_EQUAL_STRING("LOW1", s_list.ac[0].callsign);
    TEST_ASSERT_UINT32_WITHIN(2, 1914, s_list.ac[0].dist_m);
    TEST_ASSERT_EQUAL_STRING("", s_list.ac[1].callsign); /* no callsign: the view shows the hex */
    TEST_ASSERT_EQUAL_STRING("~bbbbb4", s_list.ac[1].hex);
    TEST_ASSERT_EQUAL_INT16(0, s_list.ac[1].track); /* 359.9 rounds to north */
    TEST_ASSERT_EQUAL_STRING("", s_list.ac[1].type);
    TEST_ASSERT_EQUAL_STRING("NOALT", s_list.ac[2].callsign);
    TEST_ASSERT_EQUAL_INT32(ADSB_ALT_UNKNOWN, s_list.ac[2].alt_ft);
    TEST_ASSERT_EQUAL_INT16(-1, s_list.ac[2].speed_kt);
    TEST_ASSERT_EQUAL_INT16(-1, s_list.ac[2].track);

    f.ground = true;
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(k_odd, strlen(k_odd), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(4, s_list.count);
    TEST_ASSERT_EQUAL_STRING("GND1", s_list.ac[0].callsign);
    TEST_ASSERT_EQUAL_INT32(ADSB_ALT_GROUND, s_list.ac[0].alt_ft);

    f.min_alt_ft = 5000; /* no unknown or lower altitudes; the ground stays as asked */
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse(k_odd, strlen(k_odd), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(2, s_list.count);
    TEST_ASSERT_EQUAL_STRING("GND1", s_list.ac[0].callsign);
    TEST_ASSERT_EQUAL_STRING("~bbbbb4", s_list.ac[1].hex);
}

static void test_bad_replies_are_refused(void)
{
    adsb_filter_t f = filter(50);
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_TOO_BIG, adsb_parse("{\"ac\":[]}", ADSB_REPLY_MAX + 1, &f, &s_list));
    char deep[64] = "{\"ac\":[";
    for (int i = 0; i < 20; i++) {
        strcat(deep, "[");
    }
    strcat(deep, "1");
    for (int i = 0; i < 20; i++) {
        strcat(deep, "]");
    }
    strcat(deep, "]}");
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_DEPTH, adsb_parse(deep, strlen(deep), &f, &s_list));
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_FORMAT, adsb_parse("{}", 2, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_FORMAT, adsb_parse("[]", 2, &f, &s_list));
    TEST_ASSERT_EQUAL_INT(ADSB_ERR_FORMAT, adsb_parse("{\"ac\":[", 7, &f, &s_list)); /* cut short */
    TEST_ASSERT_EQUAL_INT(0, s_list.count);
    TEST_ASSERT_EQUAL_INT(ADSB_OK, adsb_parse("{\"ac\":[],\"now\":5}", 18, &f, &s_list)); /* an empty sky */
    TEST_ASSERT_EQUAL_INT(0, s_list.count);
    TEST_ASSERT_EQUAL_STRING("bad reply", adsb_err_name(ADSB_ERR_FORMAT));
}

static void test_the_query_reaches_the_corners_of_the_map(void)
{
    char url[128];
    adsb_filter_t f = filter(50);
    adsb_url(url, sizeof(url), &f);
    TEST_ASSERT_EQUAL_STRING("https://opendata.adsb.fi/api/v3/lat/49.1951/lon/16.6068/dist/53", url);
    f = filter(100);
    adsb_url(url, sizeof(url), &f); /* 104.5 NM to the north corners, 106.4 to the south ones */
    TEST_ASSERT_EQUAL_STRING("https://opendata.adsb.fi/api/v3/lat/49.1951/lon/16.6068/dist/107", url);
    f = filter(25);
    adsb_url(url, sizeof(url), &f);
    TEST_ASSERT_EQUAL_STRING("https://opendata.adsb.fi/api/v3/lat/49.1951/lon/16.6068/dist/27", url);
}

static void test_polls_slow_down_with_range_and_failures(void)
{
    TEST_ASSERT_EQUAL_INT(5, adsb_poll_s(10, 0));
    TEST_ASSERT_EQUAL_INT(5, adsb_poll_s(25, 0));
    TEST_ASSERT_EQUAL_INT(10, adsb_poll_s(26, 0));
    TEST_ASSERT_EQUAL_INT(10, adsb_poll_s(50, 0));
    TEST_ASSERT_EQUAL_INT(15, adsb_poll_s(51, 0));
    TEST_ASSERT_EQUAL_INT(15, adsb_poll_s(100, 0));
    TEST_ASSERT_EQUAL_INT(10, adsb_poll_s(25, 1)); /* doubled after each failure */
    TEST_ASSERT_EQUAL_INT(40, adsb_poll_s(25, 3));
    TEST_ASSERT_EQUAL_INT(60, adsb_poll_s(25, 4)); /* up to a minute */
    TEST_ASSERT_EQUAL_INT(60, adsb_poll_s(100, 2));
    TEST_ASSERT_EQUAL_INT(60, adsb_poll_s(100, 1000));
}

static void test_a_route_names_its_airports(void)
{
    size_t len;
    char *json = load("route_tvs7uz.json", &len);
    adsb_route_t r;
    TEST_ASSERT_TRUE(adsb_route_parse(json, len, &r));
    TEST_ASSERT_TRUE(r.known);
    TEST_ASSERT_EQUAL_STRING("TVS7UZ", r.callsign);
    TEST_ASSERT_EQUAL_STRING("BRQ", r.from.iata);
    TEST_ASSERT_EQUAL_STRING("LKTB", r.from.icao);
    TEST_ASSERT_EQUAL_STRING("Brno", r.from.place);
    TEST_ASSERT_EQUAL_STRING("AYT", r.to.iata);
    TEST_ASSERT_EQUAL_STRING("Antalya", r.to.place);
    free(json);

    json = load("route_unknown.json", &len);
    TEST_ASSERT_FALSE(adsb_route_parse(json, len, &r)); /* adsb.lol doesn't know it */
    TEST_ASSERT_FALSE(r.known);
    free(json);
    TEST_ASSERT_FALSE(adsb_route_parse("Internal Server Error", 21, &r)); /* its 500 */

    static const char k_legs[] =
        "{\"callsign\":\"DLH1\",\"plausible\":true,\"_airports\":["
        "{\"iata\":\"FRA\",\"icao\":\"EDDF\",\"location\":\"Frankfurt am Main\"},"
        "{\"iata\":\"PRG\",\"icao\":\"LKPR\",\"location\":\"Praha\"},"
        "{\"iata\":\"\",\"icao\":\"LKKV\",\"location\":\"Karlovy Vary – Ruzyně Mezinárodní Letiště\"}]}";
    TEST_ASSERT_TRUE(adsb_route_parse(k_legs, strlen(k_legs), &r));
    TEST_ASSERT_EQUAL_STRING("FRA", r.from.iata); /* a route of legs: its first and last airports */
    TEST_ASSERT_EQUAL_STRING("", r.to.iata);
    TEST_ASSERT_EQUAL_STRING("LKKV", r.to.icao);
    TEST_ASSERT_EQUAL_STRING("Karlovy Vary – Ruzyn", r.to.place); /* cut at a character, not inside "ě" */

    static const char k_implausible[] =
        "{\"callsign\":\"TVS7UZ\",\"plausible\":false,\"_airports\":["
        "{\"iata\":\"BRQ\",\"icao\":\"LKTB\",\"location\":\"Brno\"},"
        "{\"iata\":\"AYT\",\"icao\":\"LTAI\",\"location\":\"Antalya\"}]}";
    TEST_ASSERT_FALSE(adsb_route_parse(k_implausible, strlen(k_implausible), &r));
}

static void test_route_urls_take_only_callsigns(void)
{
    char url[128];
    TEST_ASSERT_TRUE(adsb_route_url(url, sizeof(url), "TVS7UZ", 49.100973, 16.868567));
    TEST_ASSERT_EQUAL_STRING("https://api.adsb.lol/api/0/route/TVS7UZ/49.1010/16.8686", url);
    TEST_ASSERT_FALSE(adsb_route_url(url, sizeof(url), "", 49.1, 16.8));
    TEST_ASSERT_FALSE(adsb_route_url(url, sizeof(url), "AB/../C", 49.1, 16.8));
    TEST_ASSERT_FALSE(adsb_route_url(url, sizeof(url), "AB C", 49.1, 16.8));
}

static adsb_route_t route(const char *callsign, uint32_t at, bool known)
{
    adsb_route_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.callsign, sizeof(r.callsign), "%s", callsign);
    r.looked_up = at;
    r.known = known;
    return r;
}

static void test_the_route_cache_keeps_a_day_and_retries_after_an_hour(void)
{
    static adsb_routes_t c;
    adsb_routes_init(&c);
    const uint32_t t0 = 1790895156;
    adsb_route_t r = route("TVS7UZ", t0, true);
    adsb_routes_put(&c, &r);
    r = route("BAW15", t0, false); /* its lookup failed */
    adsb_routes_put(&c, &r);
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "TVS7UZ", t0 + 23 * 3600));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "TVS7UZ", t0 + 24 * 3600)); /* look it up again */
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "BAW15", t0 + 3599));
    TEST_ASSERT_FALSE(adsb_routes_find(&c, "BAW15", t0 + 3599)->known);
    TEST_ASSERT_NULL(adsb_routes_find(&c, "BAW15", t0 + 3600));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "SIA321", t0));

    r = route("BAW15", t0 + 3600, true); /* the retry found it: the entry is replaced */
    adsb_routes_put(&c, &r);
    TEST_ASSERT_TRUE(adsb_routes_find(&c, "BAW15", t0 + 3600)->known);
    TEST_ASSERT_EQUAL_INT(2, c.count);

    for (int i = 0; i < ADSB_ROUTES_MAX; i++) { /* a full cache drops the oldest */
        char name[9];
        snprintf(name, sizeof(name), "X%d", i);
        r = route(name, t0 + 7200 + (uint32_t)i, true);
        adsb_routes_put(&c, &r);
    }
    TEST_ASSERT_EQUAL_INT(ADSB_ROUTES_MAX, c.count);
    TEST_ASSERT_NULL(adsb_routes_find(&c, "TVS7UZ", t0 + 7300));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "BAW15", t0 + 7300));
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "X0", t0 + 7300));
    TEST_ASSERT_NOT_NULL(adsb_routes_find(&c, "X63", t0 + 7300));
    TEST_ASSERT_NULL(adsb_routes_find(&c, "X63", t0 + 7200)); /* asked before it was looked up: the clock went back */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_real_reply_keeps_the_aircraft_on_the_map_nearest_first);
    RUN_TEST(test_the_filters_and_the_cap_apply);
    RUN_TEST(test_odd_aircraft_are_kept_or_left_out_as_they_should);
    RUN_TEST(test_bad_replies_are_refused);
    RUN_TEST(test_the_query_reaches_the_corners_of_the_map);
    RUN_TEST(test_polls_slow_down_with_range_and_failures);
    RUN_TEST(test_a_route_names_its_airports);
    RUN_TEST(test_route_urls_take_only_callsigns);
    RUN_TEST(test_the_route_cache_keeps_a_day_and_retries_after_an_hour);
    return UNITY_END();
}
