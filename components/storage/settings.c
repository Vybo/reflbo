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
    const cJSON *before = child(sync, "mode_before_always");
    for (size_t i = 0; cJSON_IsString(before) && i < sizeof(k_sync_modes) / sizeof(k_sync_modes[0]); i++) {
        if (i != SETTINGS_SYNC_ALWAYS && strcmp(before->valuestring, k_sync_modes[i]) == 0) {
            out->sync_mode_before_always = (uint8_t)i;
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

static const char *const k_steps[] = { "weather", "air", "radar", "solar", "energy" };
static const char *const k_solar_sources[] = { [SETTINGS_SOLAR_OFF] = "off", [SETTINGS_SOLAR_OPEN_METEO] = "open-meteo",
                                               [SETTINGS_SOLAR_FORECAST_SOLAR] = "forecast-solar",
                                               [SETTINGS_SOLAR_SOLCAST] = "solcast" };
static const char *const k_energy_sources[] = { [SETTINGS_ENERGY_OFF] = "off", [SETTINGS_ENERGY_SOLAX] = "solax",
                                                [SETTINGS_ENERGY_SOLAX_DEV] = "solax-dev" };
static const char *const k_regions[] = { [SETTINGS_REGION_EU] = "eu", [SETTINGS_REGION_CN] = "cn",
                                         [SETTINGS_REGION_IN] = "in" };
static const char *const k_batteries[] = { [SETTINGS_BATTERY_AUTO] = "auto", [SETTINGS_BATTERY_ON] = "on",
                                           [SETTINGS_BATTERY_OFF] = "off" };

/* The index of `item`'s string in `names`, or `fallback`. */
static uint8_t choice(const cJSON *item, const char *const *names, size_t count, uint8_t fallback)
{
    for (size_t i = 0; cJSON_IsString(item) && i < count; i++) {
        if (strcmp(item->valuestring, names[i]) == 0) {
            return (uint8_t)i;
        }
    }
    return fallback;
}

/* sync.steps: a missing list or one that isn't is all of them; an unknown name is left out. */
static void read_steps(const cJSON *steps, settings_t *out)
{
    if (!cJSON_IsArray(steps)) {
        return;
    }
    out->sync_steps = 0;
    const cJSON *item;
    cJSON_ArrayForEach(item, steps)
    {
        uint8_t i = choice(item, k_steps, sizeof(k_steps) / sizeof(k_steps[0]), UINT8_MAX);
        out->sync_steps |= i != UINT8_MAX ? (uint8_t)(1u << i) : 0;
    }
}

static void read_solar(const cJSON *solar, const cJSON *energy, settings_t *out)
{
    out->solar_source = choice(child(solar, "source"), k_solar_sources,
                               sizeof(k_solar_sources) / sizeof(k_solar_sources[0]), out->solar_source);
    const cJSON *planes = child(solar, "planes");
    int n = cJSON_IsArray(planes) ? cJSON_GetArraySize(planes) : 0;
    if (n > 0) {
        settings_plane_t fallback = out->solar_planes[0];
        out->solar_plane_count = (uint8_t)(n < SETTINGS_PLANES_MAX ? n : SETTINGS_PLANES_MAX);
        for (int i = 0; i < out->solar_plane_count; i++) {
            const cJSON *p = cJSON_GetArrayItem(planes, i);
            settings_plane_t *plane = &out->solar_planes[i];
            *plane = i < 1 || plane->kwp_e2 == 0 ? fallback : *plane;
            plane->kwp_e2 = (uint16_t)read_scaled(p, "kwp", plane->kwp_e2, 100, 10, 10000);
            plane->tilt = (uint8_t)read_scaled(p, "tilt", plane->tilt, 1, 0, 90);
            plane->azimuth = (int16_t)read_scaled(p, "azimuth", plane->azimuth, 1, -180, 180);
        }
    }
    out->solar_losses_pct = (uint8_t)read_scaled(solar, "losses_pct", out->solar_losses_pct, 1, 0, 50);
    out->solar_inverter_kw_e2 = (uint16_t)read_scaled(solar, "inverter_kw", out->solar_inverter_kw_e2, 100, 0, 10000);
    out->energy_source = choice(child(energy, "source"), k_energy_sources,
                                sizeof(k_energy_sources) / sizeof(k_energy_sources[0]), out->energy_source);
    out->energy_battery = choice(child(energy, "battery"), k_batteries, sizeof(k_batteries) / sizeof(k_batteries[0]),
                                 out->energy_battery);
    out->energy_region = choice(child(energy, "region"), k_regions, sizeof(k_regions) / sizeof(k_regions[0]),
                                out->energy_region);
}

/* Text without control characters (UTF-8 is fine): what a broker takes as a user name. */
static bool printable(const char *s)
{
    for (; *s != '\0'; s++) {
        if ((unsigned char)*s < ' ' || *s == 0x7F) {
            return false;
        }
    }
    return true;
}

/* mqtt.discovery_prefix: 1-31 bytes of printable ASCII without wildcards, a leading or trailing "/". */
static bool topic_prefix(const char *s)
{
    size_t n = strlen(s);
    if (n == 0 || n >= SETTINGS_MQTT_PREFIX_LEN || s[0] == '/' || s[n - 1] == '/') {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] <= ' ' || s[i] > '~' || s[i] == '+' || s[i] == '#') {
            return false;
        }
    }
    return true;
}

/* mqtt.* (spec §12.1, §14.3): each value on its own; "" empties the host and the user. */
static void read_mqtt(const cJSON *mqtt, settings_t *out)
{
    read_bool(mqtt, "enabled", &out->mqtt_enabled);
    read_bool(mqtt, "discovery", &out->mqtt_discovery);
    const cJSON *host = child(mqtt, "host"), *user = child(mqtt, "user"), *prefix = child(mqtt, "discovery_prefix");
    if (cJSON_IsString(host) && (host->valuestring[0] == '\0' || host_name(host->valuestring))) {
        snprintf(out->mqtt_host, sizeof(out->mqtt_host), "%s", host->valuestring);
    }
    if (cJSON_IsString(user) && strlen(user->valuestring) < sizeof(out->mqtt_user) && printable(user->valuestring)) {
        snprintf(out->mqtt_user, sizeof(out->mqtt_user), "%s", user->valuestring);
    }
    if (cJSON_IsString(prefix) && topic_prefix(prefix->valuestring)) {
        snprintf(out->mqtt_prefix, sizeof(out->mqtt_prefix), "%s", prefix->valuestring);
    }
    out->mqtt_port = (uint16_t)read_scaled(mqtt, "port", out->mqtt_port, 1, 1, 65535);
}

void settings_mqtt_defaults(settings_t *out)
{
    out->mqtt_enabled = false;
    out->mqtt_host[0] = '\0';
    out->mqtt_port = 1883;
    out->mqtt_user[0] = '\0';
    out->mqtt_discovery = true;
    snprintf(out->mqtt_prefix, sizeof(out->mqtt_prefix), "%s", "homeassistant");
}

void settings_solar_defaults(settings_t *out)
{
    out->sync_steps = SETTINGS_STEPS_ALL;
    out->solar_source = SETTINGS_SOLAR_OFF;
    out->solar_plane_count = 1;
    memset(out->solar_planes, 0, sizeof(out->solar_planes));
    out->solar_planes[0] = (settings_plane_t){ .kwp_e2 = 500, .tilt = 35, .azimuth = 0 };
    out->solar_losses_pct = 14;
    out->solar_inverter_kw_e2 = 0;
    out->energy_source = SETTINGS_ENERGY_OFF;
    out->energy_battery = SETTINGS_BATTERY_AUTO;
    out->energy_region = SETTINGS_REGION_EU;
}

const char *settings_step_name(settings_step_t step)
{
    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); i++) {
        if (step == (settings_step_t)(1u << i)) {
            return k_steps[i];
        }
    }
    return "";
}

bool settings_check_solar(const settings_t *s, bool fs_key_set, char *err, size_t err_size)
{
    if (s->solar_source == SETTINGS_SOLAR_FORECAST_SOLAR && s->solar_plane_count > 1 && !fs_key_set) {
        return fail(err, err_size, "a second plane needs a Forecast.Solar key");
    }
    return true;
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
    out->sync_mode_before_always = SETTINGS_SYNC_TIMES;
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

void settings_remember_mode(settings_t *s, uint8_t prev_mode)
{
    bool from_other = prev_mode != SETTINGS_SYNC_ALWAYS && prev_mode <= SETTINGS_SYNC_MANUAL;
    if (s->sync_mode == SETTINGS_SYNC_ALWAYS && from_other) {
        s->sync_mode_before_always = prev_mode;
    }
}

void settings_replaced(settings_t *s, const settings_t *before)
{
    if (s->sync_mode_before_always == before->sync_mode_before_always) {
        settings_remember_mode(s, before->sync_mode);
    }
}

void settings_toggle_always(settings_t *s)
{
    uint8_t prev = s->sync_mode;
    s->sync_mode = prev == SETTINGS_SYNC_ALWAYS ? s->sync_mode_before_always : SETTINGS_SYNC_ALWAYS;
    settings_remember_mode(s, prev);
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
    read_steps(child(child(root, "sync"), "steps"), out);
    read_solar(child(root, "solar"), child(root, "energy"), out);
    read_mqtt(child(root, "mqtt"), out);
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
    uint8_t before = s->sync_mode_before_always;
    bool known = before <= SETTINGS_SYNC_MANUAL && before != SETTINGS_SYNC_ALWAYS;
    put(sync, "mode_before_always", cJSON_CreateString(k_sync_modes[known ? before : SETTINGS_SYNC_TIMES]));
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
    cJSON *steps = cJSON_CreateArray();
    for (size_t i = 0; i < sizeof(k_steps) / sizeof(k_steps[0]); i++) {
        if (s->sync_steps & (1u << i)) {
            cJSON_AddItemToArray(steps, cJSON_CreateString(k_steps[i]));
        }
    }
    put(sync, "steps", steps);
    cJSON *solar = object_at(root, "solar");
    put(solar, "source", cJSON_CreateString(k_solar_sources[s->solar_source <= SETTINGS_SOLAR_SOLCAST ? s->solar_source
                                                                                                    : 0]));
    cJSON *planes = cJSON_CreateArray();
    for (int i = 0; i < s->solar_plane_count && i < SETTINGS_PLANES_MAX; i++) {
        cJSON *plane = cJSON_CreateObject();
        cJSON_AddNumberToObject(plane, "kwp", s->solar_planes[i].kwp_e2 / 100.0);
        cJSON_AddNumberToObject(plane, "tilt", s->solar_planes[i].tilt);
        cJSON_AddNumberToObject(plane, "azimuth", s->solar_planes[i].azimuth);
        cJSON_AddItemToArray(planes, plane);
    }
    put(solar, "planes", planes);
    put(solar, "losses_pct", cJSON_CreateNumber(s->solar_losses_pct));
    put(solar, "inverter_kw", cJSON_CreateNumber(s->solar_inverter_kw_e2 / 100.0));
    cJSON *energy = object_at(root, "energy");
    uint8_t energy_source = s->energy_source <= SETTINGS_ENERGY_SOLAX_DEV ? s->energy_source : 0;
    uint8_t energy_battery = s->energy_battery <= SETTINGS_BATTERY_OFF ? s->energy_battery : 0;
    uint8_t energy_region = s->energy_region <= SETTINGS_REGION_IN ? s->energy_region : 0;
    put(energy, "source", cJSON_CreateString(k_energy_sources[energy_source]));
    put(energy, "battery", cJSON_CreateString(k_batteries[energy_battery]));
    put(energy, "region", cJSON_CreateString(k_regions[energy_region]));
    cJSON *mqtt = object_at(root, "mqtt");
    put(mqtt, "enabled", cJSON_CreateBool(s->mqtt_enabled));
    put(mqtt, "host", cJSON_CreateString(s->mqtt_host));
    put(mqtt, "port", cJSON_CreateNumber(s->mqtt_port));
    put(mqtt, "user", cJSON_CreateString(s->mqtt_user));
    put(mqtt, "discovery", cJSON_CreateBool(s->mqtt_discovery));
    put(mqtt, "discovery_prefix", cJSON_CreateString(s->mqtt_prefix));
    cJSON_DeleteItemFromObjectCaseSensitive(mqtt, "password"); /* a secret, in NVS (spec §12.1) */
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

static const char *const k_secret_keys[SETTINGS_SECRET_COUNT] = {
    "fs_key", "solcast_key", "solcast_site1", "solcast_site2", "solax_token", "solax_sn",
    "solax_client_id", "solax_secret", /* NVS keys have 15 characters at most */
    "mqtt_pass",
};

const char *settings_secret_key(settings_secret_t secret)
{
    return (unsigned)secret < SETTINGS_SECRET_COUNT ? k_secret_keys[secret] : "";
}

/* Letters and digits, and those of `extra`; with no `extra`, any text without control characters. */
static bool plain_text(const char *s, const char *extra)
{
    if (extra == NULL) {
        return printable(s);
    }
    for (; *s != '\0'; s++) {
        bool letter = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9');
        if (!letter && strchr(extra, *s) == NULL) {
            return false;
        }
    }
    return true;
}

/* One secret's value (a string, "" or null) into `secrets`; false with the reason. */
static bool take_one(const cJSON *item, const char *path, const char *extra, const char *rule,
                     settings_secrets_t *secrets, settings_secret_t which, char *err, size_t err_size)
{
    if (item != NULL && !cJSON_IsNull(item) && !cJSON_IsString(item)) { /* NULL: past a list's end */
        return fail(err, err_size, "%s: a string", path);
    }
    const char *v = cJSON_IsString(item) ? item->valuestring : "";
    if (strlen(v) >= SETTINGS_SECRET_LEN) {
        return fail(err, err_size, "%s: %d characters at most", path, SETTINGS_SECRET_LEN - 1);
    }
    if (!plain_text(v, extra)) {
        return fail(err, err_size, "%s: %s", path, rule);
    }
    secrets->given[which] = true;
    snprintf(secrets->value[which], SETTINGS_SECRET_LEN, "%s", v);
    return true;
}

/* Every obj[key] taken out and parsed into `which`, the last counting, as in a merge. */
static bool take(cJSON *obj, const char *key, const char *path, const char *extra, const char *rule,
                 settings_secrets_t *secrets, settings_secret_t which, char *err, size_t err_size)
{
    bool ok = true;
    for (cJSON *item; ok && (item = cJSON_DetachItemFromObjectCaseSensitive(obj, key)) != NULL;) {
        ok = take_one(item, path, extra, rule, secrets, which, err, err_size);
        cJSON_Delete(item);
    }
    return ok;
}

static bool take_sites(cJSON *solar, settings_secrets_t *secrets, char *err, size_t err_size)
{
    bool ok = true;
    for (cJSON *sites; ok && (sites = cJSON_DetachItemFromObjectCaseSensitive(solar, "solcast_sites")) != NULL;) {
        if (!cJSON_IsNull(sites) && !cJSON_IsArray(sites)) {
            ok = fail(err, err_size, "solar.solcast_sites: a list");
        } else if (cJSON_GetArraySize(sites) > 2) {
            ok = fail(err, err_size, "solar.solcast_sites: two at most");
        } else {
            for (int i = 0; ok && i < 2; i++) {
                ok = take_one(cJSON_GetArrayItem(sites, i), "solar.solcast_sites", "-", "letters, digits and - only",
                              secrets, (settings_secret_t)(SETTINGS_SECRET_SOLCAST_SITE1 + i), err, err_size);
            }
        }
        cJSON_Delete(sites);
    }
    return ok;
}

/* The keys of every "solar", "energy" and "mqtt" object, and the flags a GET adds ("keys"), out of `root`. */
static bool take_all(cJSON *root, settings_secrets_t *secrets, char *err, size_t err_size)
{
    static const char k_alnum[] = "letters and digits only";
    static const char k_dashes[] = "letters, digits, - and _ only";
    bool ok = true;
    for (cJSON *c = root->child; ok && c != NULL; c = c->next) {
        bool solar = c->string != NULL && strcmp(c->string, "solar") == 0;
        bool energy = c->string != NULL && strcmp(c->string, "energy") == 0;
        bool mqtt = c->string != NULL && strcmp(c->string, "mqtt") == 0;
        if (!cJSON_IsObject(c) || !(solar || energy || mqtt)) {
            continue;
        }
        for (cJSON *flags; (flags = cJSON_DetachItemFromObjectCaseSensitive(c, "keys")) != NULL;) {
            cJSON_Delete(flags);
        }
        if (mqtt) { /* the broker's password: any characters but control ones */
            ok = take(c, "password", "mqtt.password", NULL, "no control characters", secrets,
                      SETTINGS_SECRET_MQTT_PASS, err, err_size);
            continue;
        }
        ok = solar ? take(c, "fs_key", "solar.fs_key", "", k_alnum, secrets, SETTINGS_SECRET_FS_KEY, err, err_size) &&
                         take(c, "solcast_key", "solar.solcast_key", "-_", "letters, digits, - and _ only", secrets,
                              SETTINGS_SECRET_SOLCAST_KEY, err, err_size) &&
                         take_sites(c, secrets, err, err_size)
                   : take(c, "solax_token", "energy.solax_token", "", k_alnum, secrets, SETTINGS_SECRET_SOLAX_TOKEN,
                          err, err_size) &&
                         take(c, "solax_sn", "energy.solax_sn", "", k_alnum, secrets, SETTINGS_SECRET_SOLAX_SN, err,
                              err_size) &&
                         take(c, "solax_client_id", "energy.solax_client_id", "-_", k_dashes, secrets,
                              SETTINGS_SECRET_SOLAX_CLIENT_ID, err, err_size) &&
                         take(c, "solax_client_secret", "energy.solax_client_secret", "-_", k_dashes, secrets,
                              SETTINGS_SECRET_SOLAX_CLIENT_SECRET, err, err_size);
    }
    return ok;
}

bool settings_secrets_solax_dev(const settings_secrets_t *secrets)
{
    return secrets->given[SETTINGS_SECRET_SOLAX_CLIENT_ID] || secrets->given[SETTINGS_SECRET_SOLAX_CLIENT_SECRET];
}

bool settings_secrets_solcast(const settings_secrets_t *secrets)
{
    return secrets->given[SETTINGS_SECRET_SOLCAST_KEY] || secrets->given[SETTINGS_SECRET_SOLCAST_SITE1] ||
           secrets->given[SETTINGS_SECRET_SOLCAST_SITE2];
}

size_t settings_take_secrets(const char *patch, char *out, size_t size, settings_secrets_t *secrets, char *err,
                             size_t err_size)
{
    memset(secrets, 0, sizeof(*secrets));
    if (util_json_depth(patch) > SETTINGS_JSON_MAX_DEPTH) {
        fail(err, err_size, "nested more than %d levels", SETTINGS_JSON_MAX_DEPTH);
        return 0;
    }
    cJSON *root = patch != NULL ? cJSON_Parse(patch) : NULL;
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fail(err, err_size, "not a JSON object");
        return 0;
    }
    bool ok = take_all(root, secrets, err, err_size);
    bool printed = ok && size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    if (ok && !printed) {
        fail(err, err_size, "too large");
    }
    return printed ? strlen(out) : 0;
}
