#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "map_view.h"

/*
 * The flight radar's data (spec §11.3): the aircraft adsb.fi reports around the radar's centre,
 * filtered to its map, and the nearest one's route from adsb.lol. Pure C, host-buildable; the
 * polling is in adsb_task.c.
 */

#define ADSB_MAX 100                /* aircraft kept at most: radar.flights.max's ceiling */
#define ADSB_REPLY_MAX (128 * 1024) /* a longer reply is refused */
#define ADSB_ALT_GROUND INT32_MIN   /* alt_baro "ground" */
#define ADSB_ALT_UNKNOWN (INT32_MIN + 1)

typedef struct {
    char hex[8];      /* the ICAO address, as sent: "49d67d", or "~…" for one that isn't */
    char callsign[9]; /* trimmed; "" when the aircraft sends none */
    char type[5];     /* the ICAO type designator: "B38M"; "" when unknown */
    double lat, lon;
    int32_t alt_ft;   /* barometric, or ADSB_ALT_GROUND or ADSB_ALT_UNKNOWN */
    int16_t speed_kt; /* ground speed; -1 when unknown */
    int16_t track;    /* degrees 0–359, 0 = north; -1 when unknown */
    uint32_t dist_m;  /* from the radar's centre */
    uint16_t bearing; /* from the radar's centre, degrees 0–359 */
} adsb_aircraft_t;

typedef struct {
    map_view_t view;    /* the Flights map: aircraft off it are left out */
    double lat, lon;    /* its centre, which distances and bearings are from */
    int32_t min_alt_ft; /* radar.flights.min_alt_ft: lower and unknown altitudes are left out */
    bool ground;        /* radar.flights.ground: keep the aircraft on the ground */
    int max;            /* radar.flights.max, clamped to 1..ADSB_MAX */
} adsb_filter_t;

typedef struct {
    int64_t now_ms; /* the reply's own time */
    int count;
    adsb_aircraft_t ac[ADSB_MAX]; /* the nearest first */
} adsb_list_t;

typedef enum {
    ADSB_OK,
    ADSB_ERR_TOO_BIG, /* over ADSB_REPLY_MAX */
    ADSB_ERR_DEPTH,   /* nested deeper than any reply (AGENTS.md gotcha 30) */
    ADSB_ERR_FORMAT,  /* not JSON, or no `ac` array */
} adsb_err_t;

/* adsb.fi's readsb reply, `len` bytes with a NUL after them, into `out`. */
adsb_err_t adsb_parse(const char *json, size_t len, const adsb_filter_t *f, adsb_list_t *out);
const char *adsb_err_name(adsb_err_t err);
/* adsb.fi's query: a circle of whole nautical miles around the centre that reaches the map's
 * corners. Returns what snprintf returns. */
int adsb_url(char *out, size_t size, const adsb_filter_t *f);
/* Seconds to the next poll: 5 up to a 25 km range, 10 up to 50 km, 15 beyond, doubled after each
 * failure in a row up to 60. */
int adsb_poll_s(int range_km, int failures);

/* Routes from adsb.lol, looked up when the nearest aircraft changes (D27). */

#define ADSB_ROUTES_MAX 64
#define ADSB_ROUTE_KEEP_S (24 * 3600) /* a known route */
#define ADSB_ROUTE_RETRY_S 3600       /* an unknown route, or a lookup that failed */

typedef struct {
    char iata[4]; /* "" when it has none */
    char icao[5];
    char place[24]; /* adsb.lol's "location", the town, cut at a character */
} adsb_airport_t;

typedef struct {
    char callsign[9];
    uint32_t looked_up; /* UTC seconds */
    bool known;         /* false: unknown or failed */
    adsb_airport_t from, to;
} adsb_route_t;

typedef struct {
    adsb_route_t r[ADSB_ROUTES_MAX];
    int count;
} adsb_routes_t;

/* adsb.lol's route query; false for a callsign that isn't 1–8 letters and digits. */
bool adsb_route_url(char *out, size_t size, const char *callsign, double lat, double lon);
/* Its reply (NUL after `len` bytes): true with the first and last airports of a plausible route;
 * false, with out->known false, for "unknown", an implausible route or a reply that isn't one. */
bool adsb_route_parse(const char *json, size_t len, adsb_route_t *out);
void adsb_routes_init(adsb_routes_t *c);
/* The entry for `callsign` while it is good at `now`: a known route for ADSB_ROUTE_KEEP_S, an
 * unknown one for ADSB_ROUTE_RETRY_S. NULL: look it up. */
const adsb_route_t *adsb_routes_find(const adsb_routes_t *c, const char *callsign, uint32_t now);
/* Keeps a lookup's result: in place of the same callsign's, else in a free entry, else in place of
 * the oldest. */
void adsb_routes_put(adsb_routes_t *c, const adsb_route_t *r);
