#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "energy.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "timekeeping_iso.h"
#include "util_json.h"
#include "util_time.h"

/* SolaX Cloud's real-time reply (spec §11.6) and the day it builds. */

#define DEPTH_MAX 3 /* root, result, a value */

static bool fail(char *err, size_t err_size, const char *why)
{
    snprintf(err, err_size, "%s", why);
    return false;
}

static bool plain(const char *s)
{
    for (; *s != '\0'; s++) {
        if (!isalnum((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

size_t energy_solax_url(char *out, size_t size, const char *token, const char *sn)
{
    int n = token[0] != '\0' && sn[0] != '\0' && plain(token) && plain(sn)
                ? snprintf(out, size,
                           "https://www.solaxcloud.com/proxyApp/proxy/api/getRealtimeInfo.do?tokenId=%s&sn=%s", token,
                           sn)
                : -1;
    if (n > 0 && (size_t)n < size) {
        return (size_t)n;
    }
    if (size > 0) {
        out[0] = '\0';
    }
    return 0;
}

static bool number(const cJSON *o, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(o, key);
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false; /* also null */
    }
    *out = item->valuedouble;
    return true;
}

static int32_t watts(double v)
{
    return (int32_t)lround(v < -1e7 ? -1e7 : v > 1e7 ? 1e7 : v);
}

/* kWh to Wh, never below 0 */
static uint32_t wh(const cJSON *o, const char *key)
{
    double v = 0;
    number(o, key, &v);
    return v <= 0 ? 0 : v >= 4e6 ? 4000000000u : (uint32_t)lround(v * 1000.0);
}

/* "4" or 4 */
static uint8_t inverter_type(const cJSON *item)
{
    bool digits = cJSON_IsString(item) && isdigit((unsigned char)item->valuestring[0]);
    double v = cJSON_IsNumber(item) ? item->valuedouble : digits ? strtod(item->valuestring, NULL) : 0;
    return v >= 1 && v < 256 ? (uint8_t)v : 0; /* checked before the cast */
}

bool energy_parse_solax(const char *json, size_t len, energy_reading_t *out, char *err, size_t err_size)
{
    if (util_json_depth(json) > DEPTH_MAX) {
        return fail(err, err_size, "nested too deeply");
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return fail(err, err_size, "not a JSON object");
    }
    const cJSON *exception = cJSON_GetObjectItemCaseSensitive(root, "exception");
    if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "success"))) {
        bool said = cJSON_IsString(exception) && exception->valuestring[0] != '\0';
        snprintf(err, err_size, "%s", said ? exception->valuestring : "refused");
        cJSON_Delete(root);
        return false;
    }
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *upload = cJSON_GetObjectItemCaseSensitive(r, "uploadTime");
    time_t at;
    double ac = 0, feed_in = 0, v;
    bool have_ac = number(r, "acpower", &ac);
    bool have_dc = false;
    double dc = 0;
    static const char *const k_dc[] = { "powerdc1", "powerdc2", "powerdc3", "powerdc4" };
    for (size_t i = 0; i < sizeof(k_dc) / sizeof(k_dc[0]); i++) {
        if (number(r, k_dc[i], &v)) {
            dc += v;
            have_dc = true;
        }
    }
    if (!cJSON_IsObject(r) || !cJSON_IsString(upload) || !timekeeping_parse_iso8601(upload->valuestring, &at) ||
        at <= 0 || at > (time_t)UINT32_MAX || (!have_ac && !have_dc)) { /* a time the reading can keep */
        cJSON_Delete(root);
        return fail(err, err_size, "no data");
    }
    memset(out, 0, sizeof(*out));
    out->at = (uint32_t)at;
    number(r, "feedinpower", &feed_in); /* SolaX: positive while exporting */
    double pv = have_dc ? dc : ac;
    out->pv_w = watts(pv < 0 ? 0 : pv); /* a hybrid without its strings in the reply, charging from the grid */
    out->grid_w = watts(-feed_in);
    double load = ac - feed_in;
    static const char *const k_eps[] = { "peps1", "peps2", "peps3" };
    for (size_t i = 0; i < sizeof(k_eps) / sizeof(k_eps[0]); i++) {
        load += number(r, k_eps[i], &v) ? v : 0;
    }
    out->load_w = watts(load < 0 ? 0 : load);
    out->bat_w = number(r, "batPower", &v) ? watts(v) : 0;
    out->soc = number(r, "soc", &v) ? (int16_t)lround(v < 0 ? 0 : v > 100 ? 100 : v) : -1;
    out->inverter = inverter_type(cJSON_GetObjectItemCaseSensitive(r, "inverterType"));
    out->yield_wh = wh(r, "yieldtoday");
    out->to_grid_wh = wh(r, "feedinenergy");
    out->from_grid_wh = wh(r, "consumeenergy");
    cJSON_Delete(root);
    return true;
}

bool energy_battery_shown(energy_battery_t setting, const energy_reading_t *r)
{
    if (setting != ENERGY_BATTERY_AUTO) {
        return setting == ENERGY_BATTERY_ON;
    }
    if (r == NULL || r->at == 0) {
        return false;
    }
    static const uint8_t k_hybrids[] = { 2, 3, 5, 10, 13 }; /* X-Hybrid, X1-, X3-, A1-Hybrid, J1-ESS */
    for (size_t i = 0; i < sizeof(k_hybrids); i++) {
        if (r->inverter == k_hybrids[i]) {
            return true;
        }
    }
    return r->soc > 0;
}

bool energy_fresh(const energy_reading_t *r, uint32_t now)
{
    return r != NULL && r->at != 0 && (now <= r->at ? r->at - now : now - r->at) <= ENERGY_FRESH_S;
}

bool energy_reading_ahead(const energy_reading_t *r, uint32_t now)
{
    return now != 0 && r->at > now && r->at - now > ENERGY_AHEAD_S;
}

/* The local day of `t` and the UTC instants of its midnight and the next. */
static int32_t day_bounds(time_t t, struct tm *lt, time_t *midnight, time_t *next)
{
    localtime_r(&t, lt);
    struct tm m = { .tm_year = lt->tm_year, .tm_mon = lt->tm_mon, .tm_mday = lt->tm_mday, .tm_isdst = -1 };
    *midnight = mktime(&m);
    struct tm n = { .tm_year = lt->tm_year, .tm_mon = lt->tm_mon, .tm_mday = lt->tm_mday + 1, .tm_isdst = -1 };
    *next = mktime(&n);
    return (int32_t)util_days_from_civil(lt->tm_year + 1900, lt->tm_mon + 1, lt->tm_mday);
}

int32_t energy_reading_day(const energy_reading_t *r)
{
    if (r == NULL || r->at == 0) {
        return 0;
    }
    struct tm lt;
    time_t midnight, next;
    return day_bounds((time_t)r->at, &lt, &midnight, &next);
}

void energy_day_init(energy_day_t *d)
{
    memset(d, 0, sizeof(*d));
    for (int i = 0; i < ENERGY_STEPS; i++) {
        d->q[i] = ENERGY_NONE;
    }
}

void energy_day_add(energy_day_t *d, const energy_reading_t *r)
{
    if (r->at == 0 || r->at == d->last_at) {
        return;
    }
    struct tm lt;
    time_t t = (time_t)r->at, midnight, next;
    int32_t day = day_bounds(t, &lt, &midnight, &next);
    if (day < d->day) {
        return;
    }
    if (day > d->day) { /* a new day: the reading in the hour before its midnight, if one came, is its base */
        energy_day_t before = *d;
        energy_day_init(d);
        d->day = day;
        if (before.next_at != 0 && before.next_at < midnight && midnight - before.next_at <= ENERGY_MIDNIGHT_S) {
            d->base_at = before.next_at;
            d->base_to_wh = before.next_to_wh;
            d->base_from_wh = before.next_from_wh;
        }
    }
    d->last_at = r->at;
    uint32_t base_away = d->base_at == 0                     ? UINT32_MAX
                         : d->base_at < (uint32_t)midnight ? (uint32_t)midnight - d->base_at
                                                             : d->base_at - (uint32_t)midnight;
    if (!r->today && t - midnight <= ENERGY_MIDNIGHT_S && (uint32_t)(t - midnight) < base_away) {
        d->base_at = r->at;
        d->base_to_wh = r->to_grid_wh;
        d->base_from_wh = r->from_grid_wh;
    }
    if (!r->today && next - t <= ENERGY_MIDNIGHT_S) { /* today's totals start again at 0: no base */
        d->next_at = r->at;
        d->next_to_wh = r->to_grid_wh;
        d->next_from_wh = r->from_grid_wh;
    }
    int i = lt.tm_hour * 4 + lt.tm_min / 15;
    long w = lround((double)(r->pv_w < 0 ? 0 : r->pv_w) / ENERGY_UNIT_W);
    w = w >= ENERGY_NONE ? ENERGY_NONE - 1 : w;
    if (d->n[i] == 0) {
        d->q[i] = (uint16_t)w;
        d->n[i] = 1;
    } else if (d->n[i] < UINT8_MAX) {
        d->n[i]++;
        d->q[i] = (uint16_t)(((long)d->q[i] * (d->n[i] - 1) + w + d->n[i] / 2) / d->n[i]);
    }
}

const uint16_t *energy_day_q(const energy_day_t *d, int32_t day)
{
    return d != NULL && d->day != 0 && d->day == day ? d->q : NULL;
}

/* `total` less its value at day `day`'s midnight; a reading with today's totals has them as they are. A counter
 * the reading or its midnight had none of (MQTT's, D40) is none. */
static uint32_t since_midnight(const energy_day_t *d, const energy_reading_t *r, int32_t day, uint32_t total,
                               uint32_t base)
{
    if (total == ENERGY_WH_NONE) {
        return ENERGY_WH_NONE;
    }
    if (r != NULL && r->today) {
        return energy_reading_day(r) == day ? total : ENERGY_WH_NONE;
    }
    if (d == NULL || r == NULL || d->day != day || d->base_at == 0 || base == ENERGY_WH_NONE ||
        energy_reading_day(r) != day) {
        return ENERGY_WH_NONE;
    }
    return total >= base ? total - base : 0;
}

uint32_t energy_to_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day)
{
    return since_midnight(d, r, day, r != NULL ? r->to_grid_wh : 0, d != NULL ? d->base_to_wh : 0);
}

uint32_t energy_from_grid_wh(const energy_day_t *d, const energy_reading_t *r, int32_t day)
{
    return since_midnight(d, r, day, r != NULL ? r->from_grid_wh : 0, d != NULL ? d->base_from_wh : 0);
}

int energy_self_pct(const energy_day_t *d, const energy_reading_t *r, int32_t day)
{
    uint32_t out = energy_to_grid_wh(d, r, day);
    if (out == ENERGY_WH_NONE || r->yield_wh == 0 || r->yield_wh == ENERGY_WH_NONE) {
        return -1;
    }
    if (out >= r->yield_wh) {
        return 0;
    }
    return (int)lround(100.0 * (double)(r->yield_wh - out) / (double)r->yield_wh);
}
