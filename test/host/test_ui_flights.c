#define _POSIX_C_SOURCE 200809L /* setenv() in fixture_zone() */

#include <string.h>

#include "context_fixtures.h"
#include "radar_fixtures.h"
#include "ui_radar.h"
#include "unity.h"

/* The Flights panel's words (spec §11.3) in each state; the goldens show where they go. */

static ui_context_t s_ctx;
static char s_line1[96], s_line2[96], s_credit[32];

void setUp(void)
{
    s_ctx = fixture_context();
}

void tearDown(void) {}

static void panel(void)
{
    ui_flights_panel_text(&s_ctx, s_line1, s_line2, s_credit, sizeof(s_line1));
}

static void test_the_nearest_aircraft_with_its_route(void)
{
    s_ctx.radar = fixture_flights(50, fixture_aircraft(50), fixture_route(), s_ctx.now);
    panel();
    TEST_ASSERT_EQUAL_STRING("TVS7UZ \xC2\xB7 B38M \xC2\xB7 3675 ft \xC2\xB7 459 km/h", s_line1); /* 247.8 kt */
    TEST_ASSERT_EQUAL_STRING("22 km SE \xC2\xB7 BRQ Brno \xE2\x86\x92 AYT Antalya", s_line2);
    TEST_ASSERT_EQUAL_STRING("adsb.fi \xC2\xB7 adsb.lol", s_credit);
    s_ctx.lang = lang_get("cs");
    panel();
    TEST_ASSERT_EQUAL_STRING("22 km JV \xC2\xB7 BRQ Brno \xE2\x86\x92 AYT Antalya", s_line2);
}

static void test_flight_levels_from_ten_thousand_feet(void)
{
    char text[16];
    ui_flight_altitude(33800, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("FL338", text);
    ui_flight_altitude(36975, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("FL370", text); /* to the nearest hundred feet */
    ui_flight_altitude(10000, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("FL100", text);
    ui_flight_altitude(9975, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("9975 ft", text);
    ui_flight_altitude(ADSB_ALT_GROUND, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("GND", text);
    ui_flight_altitude(ADSB_ALT_UNKNOWN, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING("", text);
}

static void test_an_aircraft_without_a_callsign_type_or_speed(void)
{
    static adsb_list_t list;
    list = (adsb_list_t){ .count = 1 };
    list.ac[0] = (adsb_aircraft_t){ .hex = "~bbbbb4", .alt_ft = ADSB_ALT_UNKNOWN, .speed_kt = -1, .track = -1,
                                    .dist_m = 1400, .bearing = 359 };
    s_ctx.radar = fixture_flights(50, &list, NULL, s_ctx.now);
    panel();
    TEST_ASSERT_EQUAL_STRING("~BBBBB4", s_line1); /* its address, as nothing else is known */
    TEST_ASSERT_EQUAL_STRING("1 km N", s_line2);
    TEST_ASSERT_EQUAL_STRING("adsb.fi", s_credit);
}

static void test_the_messages_when_there_is_nothing_to_show(void)
{
    static const adsb_list_t k_empty;
    ui_radar_t *r = fixture_flights(50, &k_empty, NULL, s_ctx.now);
    s_ctx.radar = r;
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft within 50 km", s_line1);
    TEST_ASSERT_EQUAL_STRING("", s_line2);

    r->aircraft = fixture_aircraft(50);
    r->fl_failed = true; /* a failure after a poll 4 s ago: its aircraft still show */
    panel();
    TEST_ASSERT_EQUAL_STRING("TVS7UZ", strtok(s_line1, " "));
    r->fl_updated = s_ctx.now - 8 * 60; /* the last good poll at 20:40 */
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft data (20:40)", s_line1);
    r->fl_failed = false; /* no failure, but nothing new for 2 min: the same */
    r->fl_updated = s_ctx.now - UI_FLIGHTS_OLD_S - 1;
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft data (20:45)", s_line1);
    r->fl_updated = 0; /* not polled yet */
    panel();
    TEST_ASSERT_EQUAL_STRING("No aircraft data", s_line1);
    r->fl_always = false;
    panel();
    TEST_ASSERT_EQUAL_STRING("Flights need sync mode Always on", s_line1);
    s_ctx.radar = NULL;
    panel();
    TEST_ASSERT_EQUAL_STRING("Flights need sync mode Always on", s_line1);
    s_ctx.lang = lang_get("cs");
    panel();
    TEST_ASSERT_EQUAL_STRING("Lety jen v synchronizaci Stále", s_line1);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_nearest_aircraft_with_its_route);
    RUN_TEST(test_flight_levels_from_ten_thousand_feet);
    RUN_TEST(test_an_aircraft_without_a_callsign_type_or_speed);
    RUN_TEST(test_the_messages_when_there_is_nothing_to_show);
    return UNITY_END();
}
