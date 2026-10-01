#include <stdio.h>
#include <string.h>

#include "adsb.h"
#include "cJSON.h"
#include "util_json.h"

#define JSON_MAX_DEPTH 8 /* adsb.lol's route nests 3 deep */

static const cJSON *get(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(o, key);
}

static bool callsign_ok(const char *s)
{
    size_t n = strlen(s);
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return n >= 1 && n <= 8;
}

bool adsb_route_url(char *out, size_t size, const char *callsign, double lat, double lon)
{
    if (!callsign_ok(callsign)) {
        return false; /* it goes into the path */
    }
    int n = snprintf(out, size, "https://api.adsb.lol/api/0/route/%s/%.4f/%.4f", callsign, lat, lon);
    return n > 0 && (size_t)n < size;
}

/* A copy cut to fit at a character boundary (a limit inside "ě" would split it). */
static void copy_utf8(char *out, size_t size, const char *s)
{
    size_t n = strlen(s);
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) {
            n--; /* s[n] continues a character: cut before its first byte */
        }
    }
    memcpy(out, s, n);
    out[n] = '\0';
}

static void copy_code(char *out, size_t size, const cJSON *s)
{
    out[0] = '\0';
    if (cJSON_IsString(s) && strlen(s->valuestring) < size) {
        memcpy(out, s->valuestring, strlen(s->valuestring) + 1);
    }
}

static bool airport(const cJSON *a, adsb_airport_t *out)
{
    memset(out, 0, sizeof(*out));
    copy_code(out->iata, sizeof(out->iata), get(a, "iata"));
    copy_code(out->icao, sizeof(out->icao), get(a, "icao"));
    const cJSON *place = get(a, "location");
    if (cJSON_IsString(place)) {
        copy_utf8(out->place, sizeof(out->place), place->valuestring);
    }
    return out->iata[0] != '\0' || out->icao[0] != '\0';
}

bool adsb_route_parse(const char *json, size_t len, adsb_route_t *out)
{
    memset(out, 0, sizeof(*out));
    if (util_json_depth(json) > JSON_MAX_DEPTH) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    copy_code(out->callsign, sizeof(out->callsign), get(root, "callsign"));
    const cJSON *airports = get(root, "_airports");
    int n = cJSON_IsArray(airports) ? cJSON_GetArraySize(airports) : 0;
    if (n >= 2 && !cJSON_IsFalse(get(root, "plausible"))) { /* the first and last airports of its legs */
        bool from = airport(cJSON_GetArrayItem(airports, 0), &out->from);
        out->known = airport(cJSON_GetArrayItem(airports, n - 1), &out->to) && from;
    }
    cJSON_Delete(root);
    return out->known;
}

void adsb_routes_init(adsb_routes_t *c)
{
    memset(c, 0, sizeof(*c));
}

const adsb_route_t *adsb_routes_find(const adsb_routes_t *c, const char *callsign, uint32_t now)
{
    for (int i = 0; i < c->count; i++) {
        const adsb_route_t *r = &c->r[i];
        if (strcmp(r->callsign, callsign) == 0) {
            uint32_t keep = r->known ? ADSB_ROUTE_KEEP_S : ADSB_ROUTE_RETRY_S;
            return now >= r->looked_up && now - r->looked_up < keep ? r : NULL; /* a clock set back: ask again */
        }
    }
    return NULL;
}

void adsb_routes_put(adsb_routes_t *c, const adsb_route_t *r)
{
    int at = -1;
    for (int i = 0; i < c->count && at < 0; i++) {
        at = strcmp(c->r[i].callsign, r->callsign) == 0 ? i : -1;
    }
    if (at < 0 && c->count < ADSB_ROUTES_MAX) {
        at = c->count++;
    }
    if (at < 0) {
        at = 0;
        for (int i = 1; i < c->count; i++) {
            at = c->r[i].looked_up < c->r[at].looked_up ? i : at;
        }
    }
    c->r[at] = *r;
}
