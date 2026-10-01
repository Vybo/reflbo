#include "settings.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"

#define SCHEMA 1
#define SETTINGS_JSON_MAX_DEPTH 16 /* the sketch nests 3 levels */
#define BAT_SPAN_MIN_MV 300 /* BATTERY_CAL_SPAN_MIN in sensors' battery_model.h */

static bool fail(char *err, size_t size, const char *fmt, ...)
{
    if (size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return false;
}

static const cJSON *child(const cJSON *obj, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

static void read_string(const cJSON *obj, const char *key, char *out, size_t size)
{
    const cJSON *item = child(obj, key);
    if (cJSON_IsString(item) && strlen(item->valuestring) < size && item->valuestring[0] != '\0') {
        snprintf(out, size, "%s", item->valuestring);
    }
}

static void read_bool(const cJSON *obj, const char *key, bool *out)
{
    const cJSON *item = child(obj, key);
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item);
    }
}

/* A number scaled by `scale`, rounded and clamped to [lo, hi]. */
static long read_scaled(const cJSON *obj, const char *key, long fallback, double scale, long lo, long hi)
{
    const cJSON *item = child(obj, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return fallback;
    }
    long v = lround(item->valuedouble * scale);
    return v < lo ? lo : v > hi ? hi : v;
}

static uint8_t lpm_from_hz(const cJSON *display, uint8_t fallback)
{
    const cJSON *item = child(display, "lpm_hz");
    if (!cJSON_IsNumber(item)) {
        return fallback;
    }
    for (int q = 1; q <= 32; q *= 2) {
        if (fabs(item->valuedouble - q / 4.0) < 0.01) {
            return (uint8_t)q;
        }
    }
    return fallback;
}

/* battery.learned_mv into `out` if it is a usable curve: 21 rising voltages within 3.0-4.4 V and at
 * least 0.3 V from 0 % to 100 %, as battery_cal_valid() in sensors wants. */
static bool read_curve(const cJSON *arr, uint16_t out[SETTINGS_BAT_CURVE_POINTS])
{
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != SETTINGS_BAT_CURVE_POINTS) {
        return false;
    }
    uint16_t c[SETTINGS_BAT_CURVE_POINTS];
    for (int i = 0; i < SETTINGS_BAT_CURVE_POINTS; i++) {
        const cJSON *v = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsNumber(v) || v->valuedouble < 3000 || v->valuedouble > 4400 ||
            (i > 0 && lround(v->valuedouble) <= c[i - 1])) {
            return false;
        }
        c[i] = (uint16_t)lround(v->valuedouble);
    }
    if (c[SETTINGS_BAT_CURVE_POINTS - 1] - c[0] < BAT_SPAN_MIN_MV) {
        return false;
    }
    memcpy(out, c, sizeof(c));
    return true;
}

/* "HH:MM" to minutes after midnight; -1 for anything else. */
static int hhmm(const cJSON *item)
{
    const char *s = cJSON_IsString(item) ? item->valuestring : "";
    if (strlen(s) != 5 || s[2] != ':') {
        return -1;
    }
    for (int i = 0; i < 5; i++) {
        if (i != 2 && (s[i] < '0' || s[i] > '9')) {
            return -1;
        }
    }
    int h = (s[0] - '0') * 10 + (s[1] - '0'), m = (s[3] - '0') * 10 + (s[4] - '0');
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

static cJSON *hhmm_json(int minutes)
{
    char text[12]; /* room for any int, so GCC can see nothing is cut */
    snprintf(text, sizeof(text), "%02d:%02d", minutes / 60 % 24, minutes % 60);
    return cJSON_CreateString(text);
}

static const char *const k_sync_modes[] = { [SETTINGS_SYNC_TIMES] = "times", [SETTINGS_SYNC_INTERVAL] = "interval",
                                            [SETTINGS_SYNC_ALWAYS] = "always", [SETTINGS_SYNC_MANUAL] = "manual" };

/* sync.times: the valid ones, sorted and without repeats, at most 8 (the first of the day); none
 * valid keeps the default. */
static void read_times(const cJSON *arr, settings_t *out)
{
    bool minute[24 * 60] = { false };
    const cJSON *list = cJSON_IsArray(arr) ? arr : NULL;
    const cJSON *item;
    cJSON_ArrayForEach(item, list)
    {
        int m = hhmm(item);
        if (m >= 0) {
            minute[m] = true;
        }
    }
    uint8_t n = 0;
    for (int m = 0; m < 24 * 60 && n < SETTINGS_SYNC_TIMES_MAX; m++) {
        if (minute[m]) {
            out->sync_times[n++] = (uint16_t)m;
        }
    }
    if (n > 0) {
        for (uint8_t i = n; i < SETTINGS_SYNC_TIMES_MAX; i++) {
            out->sync_times[i] = 0;
        }
        out->sync_time_count = n;
    }
}

/* A host name: letters, digits, dots and hyphens. */
static bool host_name(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= SETTINGS_HOST_LEN) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

/* time.ntp: up to two usable host names; none usable keeps the default. */
static void read_ntp(const cJSON *arr, settings_t *out)
{
    char hosts[SETTINGS_NTP_MAX][SETTINGS_HOST_LEN] = { "" };
    int n = 0;
    const cJSON *list = cJSON_IsArray(arr) ? arr : NULL;
    const cJSON *item;
    cJSON_ArrayForEach(item, list)
    {
        if (n < SETTINGS_NTP_MAX && cJSON_IsString(item) && host_name(item->valuestring)) {
            snprintf(hosts[n++], SETTINGS_HOST_LEN, "%s", item->valuestring);
        }
    }
    if (n > 0) {
        memcpy(out->ntp, hosts, sizeof(hosts));
    }
}

static void read_sync(const cJSON *sync, settings_t *out)
{
    const cJSON *mode = child(sync, "mode");
    for (size_t i = 0; cJSON_IsString(mode) && i < sizeof(k_sync_modes) / sizeof(k_sync_modes[0]); i++) {
        if (strcmp(mode->valuestring, k_sync_modes[i]) == 0) {
            out->sync_mode = (uint8_t)i;
        }
    }
    read_times(child(sync, "times"), out);
    out->sync_interval_min = (uint16_t)read_scaled(sync, "interval_min", out->sync_interval_min, 1, 15, 1440);
    const cJSON *quiet = child(sync, "quiet");
    read_bool(quiet, "enabled", &out->quiet);
    int from = hhmm(child(quiet, "from")), to = hhmm(child(quiet, "to"));
    out->quiet_from = from >= 0 ? (uint16_t)from : out->quiet_from;
    out->quiet_to = to >= 0 ? (uint16_t)to : out->quiet_to;
}

/* A radar's centre: both coordinates as numbers, or else the location's, which it then follows. */
static void read_centre(const cJSON *obj, const settings_t *s, bool *set, int32_t *lat, int32_t *lon)
{
    const cJSON *la = child(obj, "lat"), *lo = child(obj, "lon");
    *set = cJSON_IsNumber(la) && cJSON_IsNumber(lo) && isfinite(la->valuedouble) && isfinite(lo->valuedouble);
    *lat = *set ? (int32_t)read_scaled(obj, "lat", s->lat_e4, 1e4, -850000, 850000) : s->lat_e4; /* web Mercator */
    *lon = *set ? (int32_t)read_scaled(obj, "lon", s->lon_e4, 1e4, -1800000, 1800000) : s->lon_e4;
}

static void read_radar(const cJSON *radar, settings_t *out)
{
    const cJSON *wx = child(radar, "weather"), *fl = child(radar, "flights");
    read_centre(wx, out, &out->wx_centre_set, &out->wx_lat_e4, &out->wx_lon_e4);
    out->wx_zoom_q = (uint8_t)read_scaled(wx, "zoom", out->wx_zoom_q, 4, SETTINGS_ZOOM_Q_MIN, SETTINGS_ZOOM_Q_MAX);
    read_centre(fl, out, &out->fl_centre_set, &out->fl_lat_e4, &out->fl_lon_e4);
    out->fl_range_km = (uint8_t)read_scaled(fl, "range_km", out->fl_range_km, 1, 10, 100);
    out->fl_min_alt_ft = (uint16_t)read_scaled(fl, "min_alt_ft", out->fl_min_alt_ft, 1, 0, 60000);
    read_bool(fl, "ground", &out->fl_ground);
    out->fl_max = (uint8_t)read_scaled(fl, "max", out->fl_max, 1, 1, 100);
}

void settings_radar_defaults(settings_t *out)
{
    out->wx_centre_set = false;
    out->wx_lat_e4 = out->lat_e4;
    out->wx_lon_e4 = out->lon_e4;
    out->wx_zoom_q = 26;
    out->fl_centre_set = false;
    out->fl_lat_e4 = out->lat_e4;
    out->fl_lon_e4 = out->lon_e4;
    out->fl_range_km = 50;
    out->fl_min_alt_ft = 0;
    out->fl_ground = false;
    out->fl_max = 100;
}

void settings_sync_defaults(settings_t *out)
{
    out->sync_mode = SETTINGS_SYNC_TIMES;
    out->sync_time_count = 1;
    memset(out->sync_times, 0, sizeof(out->sync_times));
    out->sync_times[0] = 5 * 60 + 30;
    out->sync_interval_min = 60;
    out->quiet = false;
    out->quiet_from = 23 * 60;
    out->quiet_to = 6 * 60;
    memset(out->ntp, 0, sizeof(out->ntp));
    snprintf(out->ntp[0], SETTINGS_HOST_LEN, "%s", "cz.pool.ntp.org");
    snprintf(out->ntp[1], SETTINGS_HOST_LEN, "%s", "pool.ntp.org");
}

bool settings_from_json(const char *json, const settings_t *defaults, settings_t *out, char *err, size_t err_size)
{
    if (util_json_depth(json) > SETTINGS_JSON_MAX_DEPTH) {
        return fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
    }
    cJSON *root = json != NULL ? cJSON_Parse(json) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return fail(err, err_size, "not a JSON object");
    }
    const cJSON *schema = child(root, "schema");
    if (!cJSON_IsNumber(schema) || schema->valueint != SCHEMA) {
        cJSON_Delete(root);
        return fail(err, err_size, "schema must be %d", SCHEMA);
    }
    *out = *defaults;
    read_string(root, "language", out->language, sizeof(out->language));
    const cJSON *time = child(root, "time");
    read_string(time, "tz_posix", out->tz_posix, sizeof(out->tz_posix));
    read_string(time, "tz_iana", out->tz_iana, sizeof(out->tz_iana));
    read_bool(time, "clock_24h", &out->clock_24h);
    const cJSON *temp_unit = child(child(root, "units"), "temp");
    if (cJSON_IsString(temp_unit)) {
        out->fahrenheit = strcmp(temp_unit->valuestring, "F") == 0;
    }
    const cJSON *sensors = child(root, "sensors");
    out->sensors_every_min = (uint8_t)read_scaled(sensors, "interval_min", out->sensors_every_min, 1, 1, 30);
    out->temp_offset_c100 = (int16_t)read_scaled(sensors, "temp_offset_c", out->temp_offset_c100, 100, -1000, 1000);
    out->hum_offset_pct100 = (int16_t)read_scaled(sensors, "hum_offset_pct", out->hum_offset_pct100, 100, -2000, 2000);
    const cJSON *display = child(root, "display");
    out->display_every_min = (uint8_t)read_scaled(display, "update_min", out->display_every_min, 1, 1, 15);
    out->lpm_quarter_hz = lpm_from_hz(display, out->lpm_quarter_hz);
    const cJSON *location = child(root, "location");
    read_string(location, "name", out->place, sizeof(out->place));
    out->lat_e4 = (int32_t)read_scaled(location, "lat", out->lat_e4, 1e4, -900000, 900000);
    out->lon_e4 = (int32_t)read_scaled(location, "lon", out->lon_e4, 1e4, -1800000, 1800000);
    const cJSON *battery = child(root, "battery");
    long empty = read_scaled(battery, "empty_v", out->bat_empty_mv, 1000, 3000, 4000);
    long full = read_scaled(battery, "full_v", out->bat_full_mv, 1000, 3600, 4400);
    bool room = full - empty >= BAT_SPAN_MIN_MV; /* else neither voltage is taken */
    if (room) {
        out->bat_empty_mv = (uint16_t)empty;
        out->bat_full_mv = (uint16_t)full;
    }
    bool learned = read_curve(child(battery, "learned_mv"), out->bat_learned_mv);
    out->bat_learned_at = (uint32_t)read_scaled(battery, "learned_at", out->bat_learned_at, 1, 0, UINT32_MAX);
    const cJSON *from = child(battery, "level_from");
    if (cJSON_IsString(from)) {
        out->bat_cal = strcmp(from->valuestring, "manual") == 0 && room     ? SETTINGS_BAT_MANUAL
                       : strcmp(from->valuestring, "learned") == 0 && learned ? SETTINGS_BAT_LEARNED
                                                                              : SETTINGS_BAT_CURVE;
    }
    read_ntp(child(time, "ntp"), out);
    read_sync(child(root, "sync"), out);
    read_radar(child(root, "radar"), out); /* after the location, which its centres may follow */
    cJSON_Delete(root);
    return true;
}

/* obj[key], created as an empty object if it is missing or not an object. */
static cJSON *object_at(cJSON *obj, const char *key)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsObject(item)) {
        cJSON_DeleteItemFromObjectCaseSensitive(obj, key);
        item = cJSON_AddObjectToObject(obj, key);
    }
    return item;
}

static void put(cJSON *obj, const char *key, cJSON *value)
{
    if (cJSON_GetObjectItemCaseSensitive(obj, key) != NULL) {
        cJSON_ReplaceItemInObjectCaseSensitive(obj, key, value);
    } else {
        cJSON_AddItemToObject(obj, key, value);
    }
}

/* A centre that follows the location isn't saved, so a new location still moves it. */
static void put_centre(cJSON *obj, bool set, int32_t lat_e4, int32_t lon_e4)
{
    if (set) {
        put(obj, "lat", cJSON_CreateNumber(lat_e4 / 1e4));
        put(obj, "lon", cJSON_CreateNumber(lon_e4 / 1e4));
    } else {
        cJSON_DeleteItemFromObjectCaseSensitive(obj, "lat");
        cJSON_DeleteItemFromObjectCaseSensitive(obj, "lon");
    }
}

/* The file a save or patch merges into: NULL for none, or one nested past the cap, which the loader
 * rejects too and whose parse could overrun the app task's stack. */
static cJSON *parse_base(const char *base_json)
{
    return base_json != NULL && util_json_depth(base_json) <= SETTINGS_JSON_MAX_DEPTH ? cJSON_Parse(base_json) : NULL;
}

size_t settings_to_json(const settings_t *s, const char *base_json, char *out, size_t size)
{
    cJSON *root = parse_base(base_json);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        root = cJSON_CreateObject();
    }
    put(root, "schema", cJSON_CreateNumber(SCHEMA));
    put(root, "language", cJSON_CreateString(s->language));
    cJSON *time = object_at(root, "time");
    put(time, "tz_iana", cJSON_CreateString(s->tz_iana));
    put(time, "tz_posix", cJSON_CreateString(s->tz_posix));
    put(time, "clock_24h", cJSON_CreateBool(s->clock_24h));
    put(object_at(root, "units"), "temp", cJSON_CreateString(s->fahrenheit ? "F" : "C"));
    cJSON *sensors = object_at(root, "sensors");
    put(sensors, "interval_min", cJSON_CreateNumber(s->sensors_every_min));
    put(sensors, "temp_offset_c", cJSON_CreateNumber(s->temp_offset_c100 / 100.0));
    put(sensors, "hum_offset_pct", cJSON_CreateNumber(s->hum_offset_pct100 / 100.0));
    cJSON *display = object_at(root, "display");
    put(display, "update_min", cJSON_CreateNumber(s->display_every_min));
    put(display, "lpm_hz", cJSON_CreateNumber(s->lpm_quarter_hz / 4.0));
    cJSON *location = object_at(root, "location");
    put(location, "name", cJSON_CreateString(s->place));
    put(location, "lat", cJSON_CreateNumber(s->lat_e4 / 1e4));
    put(location, "lon", cJSON_CreateNumber(s->lon_e4 / 1e4));
    cJSON *battery = object_at(root, "battery");
    put(battery, "level_from", cJSON_CreateString(s->bat_cal == SETTINGS_BAT_MANUAL    ? "manual"
                                                  : s->bat_cal == SETTINGS_BAT_LEARNED ? "learned"
                                                                                       : "curve"));
    put(battery, "empty_v", cJSON_CreateNumber(s->bat_empty_mv / 1000.0));
    put(battery, "full_v", cJSON_CreateNumber(s->bat_full_mv / 1000.0));
    if (s->bat_learned_mv[SETTINGS_BAT_CURVE_POINTS - 1] != 0) {
        cJSON *curve = cJSON_CreateArray();
        for (int i = 0; i < SETTINGS_BAT_CURVE_POINTS; i++) {
            cJSON_AddItemToArray(curve, cJSON_CreateNumber(s->bat_learned_mv[i]));
        }
        put(battery, "learned_mv", curve);
        put(battery, "learned_at", cJSON_CreateNumber(s->bat_learned_at));
    }
    cJSON *ntp = cJSON_CreateArray();
    for (int i = 0; i < SETTINGS_NTP_MAX; i++) {
        if (s->ntp[i][0] != '\0') {
            cJSON_AddItemToArray(ntp, cJSON_CreateString(s->ntp[i]));
        }
    }
    put(time, "ntp", ntp);
    cJSON *sync = object_at(root, "sync");
    put(sync, "mode", cJSON_CreateString(k_sync_modes[s->sync_mode < SETTINGS_SYNC_MANUAL ? s->sync_mode
                                                                                         : SETTINGS_SYNC_MANUAL]));
    cJSON *times = cJSON_CreateArray();
    for (int i = 0; i < s->sync_time_count && i < SETTINGS_SYNC_TIMES_MAX; i++) {
        cJSON_AddItemToArray(times, hhmm_json(s->sync_times[i]));
    }
    put(sync, "times", times);
    put(sync, "interval_min", cJSON_CreateNumber(s->sync_interval_min));
    cJSON *quiet = object_at(sync, "quiet");
    put(quiet, "enabled", cJSON_CreateBool(s->quiet));
    put(quiet, "from", hhmm_json(s->quiet_from));
    put(quiet, "to", hhmm_json(s->quiet_to));
    cJSON *radar = object_at(root, "radar");
    cJSON *wx = object_at(radar, "weather");
    put_centre(wx, s->wx_centre_set, s->wx_lat_e4, s->wx_lon_e4);
    put(wx, "zoom", cJSON_CreateNumber(s->wx_zoom_q / 4.0));
    cJSON *fl = object_at(radar, "flights");
    put_centre(fl, s->fl_centre_set, s->fl_lat_e4, s->fl_lon_e4);
    put(fl, "range_km", cJSON_CreateNumber(s->fl_range_km));
    put(fl, "min_alt_ft", cJSON_CreateNumber(s->fl_min_alt_ft));
    put(fl, "ground", cJSON_CreateBool(s->fl_ground));
    put(fl, "max", cJSON_CreateNumber(s->fl_max));
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, true);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

/* RFC 7396 on cJSON trees: `patch` is an object; its members merge into `target`. */
static void merge(cJSON *target, const cJSON *patch)
{
    for (const cJSON *p = patch->child; p != NULL; p = p->next) {
        if (cJSON_IsNull(p)) {
            cJSON_DeleteItemFromObjectCaseSensitive(target, p->string);
        } else if (cJSON_IsObject(p)) {
            merge(object_at(target, p->string), p);
        } else {
            put(target, p->string, cJSON_Duplicate(p, true));
        }
    }
}

size_t settings_patch(const char *base_json, const char *patch, char *out, size_t size, char *err, size_t err_size)
{
    if (util_json_depth(patch) > SETTINGS_JSON_MAX_DEPTH) {
        fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
        return 0;
    }
    cJSON *p = patch != NULL ? cJSON_Parse(patch) : NULL;
    if (!cJSON_IsObject(p)) {
        cJSON_Delete(p);
        fail(err, err_size, "the patch must be a JSON object");
        return 0;
    }
    cJSON *root = parse_base(base_json);
    if (!cJSON_IsObject(root)) { /* no file yet, or one the loader rejected */
        cJSON_Delete(root);
        root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "schema", SCHEMA);
    }
    merge(root, p);
    cJSON_Delete(p);
    const cJSON *schema = child(root, "schema");
    size_t n = 0;
    if (!cJSON_IsNumber(schema) || schema->valuedouble != SCHEMA) {
        fail(err, err_size, "schema must be %d", SCHEMA);
    } else if (size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false)) {
        n = strlen(out);
    } else {
        fail(err, err_size, "the settings don't fit %u bytes", (unsigned)size);
    }
    cJSON_Delete(root);
    return n;
}
