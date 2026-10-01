#include <math.h>
#include <stdio.h>
#include <string.h>

#include "adsb.h"
#include "cJSON.h"
#include "util_json.h"

#define JSON_MAX_DEPTH 8 /* a readsb reply nests 4 deep: its arrays of strings in each aircraft */
#define QUERY_MAX_NM 250 /* adsb.fi's limit */
#define M_PER_NM 1852.0

static const cJSON *get(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(o, key);
}

/* A string without its trailing blanks (readsb pads callsigns to 8), cut to fit. */
static void copy_trimmed(char *out, size_t size, const cJSON *s)
{
    out[0] = '\0';
    if (!cJSON_IsString(s)) {
        return;
    }
    size_t n = strlen(s->valuestring);
    while (n > 0 && s->valuestring[n - 1] == ' ') {
        n--;
    }
    n = n < size ? n : size - 1;
    memcpy(out, s->valuestring, n);
    out[n] = '\0';
}

/* A number within [lo, hi], rounded; -1 for anything else. */
static int number(const cJSON *v, double lo, double hi)
{
    return cJSON_IsNumber(v) && v->valuedouble >= lo && v->valuedouble <= hi ? (int)lround(v->valuedouble) : -1;
}

static int32_t altitude(const cJSON *alt)
{
    if (cJSON_IsNumber(alt) && alt->valuedouble > -2000 && alt->valuedouble < 200000) {
        return (int32_t)lround(alt->valuedouble);
    }
    if (cJSON_IsString(alt) && strcmp(alt->valuestring, "ground") == 0) {
        return ADSB_ALT_GROUND;
    }
    return ADSB_ALT_UNKNOWN;
}

static bool altitude_kept(int32_t alt, const adsb_filter_t *f)
{
    if (alt == ADSB_ALT_GROUND) {
        return f->ground;
    }
    if (alt == ADSB_ALT_UNKNOWN) {
        return f->min_alt_ft <= 0;
    }
    return alt >= f->min_alt_ft;
}

/* One entry of `ac` that passes the filter. */
static bool aircraft(const cJSON *item, const adsb_filter_t *f, adsb_aircraft_t *a)
{
    const cJSON *lat = get(item, "lat"), *lon = get(item, "lon");
    if (!cJSON_IsObject(item) || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) || fabs(lat->valuedouble) > 90 ||
        fabs(lon->valuedouble) > 180) {
        return false; /* no position: nothing to draw */
    }
    memset(a, 0, sizeof(*a));
    a->lat = lat->valuedouble;
    a->lon = lon->valuedouble;
    a->alt_ft = altitude(get(item, "alt_baro"));
    if (!altitude_kept(a->alt_ft, f)) {
        return false;
    }
    double x, y;
    map_project(&f->view, a->lat, a->lon, &x, &y);
    if (x < 0 || y < 0 || x >= f->view.w || y >= f->view.h) {
        return false;
    }
    copy_trimmed(a->hex, sizeof(a->hex), get(item, "hex"));
    copy_trimmed(a->callsign, sizeof(a->callsign), get(item, "flight"));
    copy_trimmed(a->type, sizeof(a->type), get(item, "t"));
    a->speed_kt = (int16_t)number(get(item, "gs"), 0, 2000);
    int track = number(get(item, "track"), 0, 360);
    a->track = (int16_t)(track < 0 ? -1 : track % 360);
    a->dist_m = (uint32_t)lround(map_distance_m(f->lat, f->lon, a->lat, a->lon));
    a->bearing = (uint16_t)(lround(map_bearing_deg(f->lat, f->lon, a->lat, a->lon)) % 360);
    return true;
}

/* Into the list, nearest first, keeping at most `max`. */
static void insert(adsb_list_t *out, int max, const adsb_aircraft_t *a)
{
    int at = out->count;
    while (at > 0 && out->ac[at - 1].dist_m > a->dist_m) {
        at--;
    }
    if (at >= max) {
        return; /* farther than every one kept, and the list is full */
    }
    int end = out->count < max ? out->count : max - 1; /* the entries that move down */
    memmove(&out->ac[at + 1], &out->ac[at], (size_t)(end - at) * sizeof(out->ac[0]));
    out->ac[at] = *a;
    if (out->count < max) {
        out->count++;
    }
}

adsb_err_t adsb_parse(const char *json, size_t len, const adsb_filter_t *f, adsb_list_t *out)
{
    out->count = 0;
    out->now_ms = 0;
    if (len > ADSB_REPLY_MAX) {
        return ADSB_ERR_TOO_BIG;
    }
    if (util_json_depth(json) > JSON_MAX_DEPTH) {
        return ADSB_ERR_DEPTH;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    const cJSON *ac = get(root, "ac");
    if (!cJSON_IsArray(ac)) {
        cJSON_Delete(root);
        return ADSB_ERR_FORMAT;
    }
    const cJSON *now = get(root, "now");
    out->now_ms = cJSON_IsNumber(now) ? (int64_t)now->valuedouble : 0;
    int max = f->max < 1 ? 1 : f->max > ADSB_MAX ? ADSB_MAX : f->max;
    const cJSON *item;
    cJSON_ArrayForEach(item, ac)
    {
        adsb_aircraft_t a;
        if (aircraft(item, f, &a)) {
            insert(out, max, &a);
        }
    }
    cJSON_Delete(root);
    return ADSB_OK;
}

const char *adsb_err_name(adsb_err_t err)
{
    switch (err) {
    case ADSB_OK:
        return "ok";
    case ADSB_ERR_TOO_BIG:
        return "too big";
    case ADSB_ERR_DEPTH:
        return "too deep";
    default:
        return "bad reply";
    }
}

int adsb_url(char *out, size_t size, const adsb_filter_t *f)
{
    double far = 0;
    for (int i = 0; i < 4; i++) { /* the corners; the southern ones are the farther on the ground */
        double lat, lon;
        map_unproject(&f->view, i & 1 ? f->view.w : 0, i & 2 ? f->view.h : 0, &lat, &lon);
        double d = map_distance_m(f->lat, f->lon, lat, lon);
        far = d > far ? d : far;
    }
    int nm = (int)ceil(far / M_PER_NM);
    nm = nm < 1 ? 1 : nm > QUERY_MAX_NM ? QUERY_MAX_NM : nm;
    return snprintf(out, size, "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d", f->lat, f->lon, nm);
}

int adsb_poll_s(int range_km, int failures)
{
    int s = range_km <= 25 ? 5 : range_km <= 50 ? 10 : 15;
    for (int i = 0; i < failures && s < 60; i++) {
        s *= 2;
    }
    return s > 60 ? 60 : s;
}
