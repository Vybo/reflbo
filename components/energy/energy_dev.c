#define _POSIX_C_SOURCE 200809L /* localtime_r */

#include "energy_dev.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "timekeeping_iso.h"
#include "util_json.h"

/* SolaX Cloud's Developer API (D37): the requests, the replies and the reading. */

#define DEPTH_MAX 8
#define CODE_OK 10000   /* the data replies' */
#define CODE_TOKEN_OK 0 /* the token reply's */

static const char *const k_hosts[] = { "openapi-eu.solaxcloud.com", "openapi-cn.solaxcloud.com",
                                       "openapi-in.solaxcloud.com" };

static bool fail(char *err, size_t size, const char *why)
{
    util_json_text(err, size, why);
    return false;
}

static size_t written(char *out, size_t size, int n)
{
    if (n > 0 && (size_t)n < size) {
        return (size_t)n;
    }
    if (size > 0) {
        out[0] = '\0';
    }
    return 0;
}

/* Letters and digits, and those of `extra`; not empty. */
static bool plain(const char *s, const char *extra)
{
    if (s == NULL || *s == '\0') {
        return false;
    }
    for (; *s != '\0'; s++) {
        bool alnum = (*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9');
        if (!alnum && strchr(extra, *s) == NULL) {
            return false;
        }
    }
    return true;
}

static const cJSON *member(const cJSON *o, const char *key)
{
    return cJSON_IsObject(o) ? cJSON_GetObjectItemCaseSensitive(o, key) : NULL;
}

/* A finite number, or a string that holds one and nothing else. */
static bool number(const cJSON *item, double *out)
{
    double v;
    if (cJSON_IsNumber(item)) {
        v = item->valuedouble;
    } else if (cJSON_IsString(item) && item->valuestring[0] != '\0') {
        char *end;
        v = strtod(item->valuestring, &end);
        if (*end != '\0') {
            return false;
        }
    } else {
        return false;
    }
    if (!isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

static bool number_at(const cJSON *o, const char *key, double *out)
{
    return number(member(o, key), out);
}

size_t energy_dev_url(char *out, size_t size, energy_dev_region_t region, const char *path)
{
    int n = (unsigned)region < sizeof(k_hosts) / sizeof(k_hosts[0]) ? snprintf(out, size, "https://%s%s",
                                                                                k_hosts[region], path)
                                                                     : -1;
    return written(out, size, n);
}

size_t energy_dev_token_body(char *out, size_t size, const char *client_id, const char *client_secret)
{
    int n = plain(client_id, "-_") && plain(client_secret, "-_")
                ? snprintf(out, size, "client_id=%s&client_secret=%s&grant_type=client_credentials", client_id,
                           client_secret)
                : -1;
    return written(out, size, n);
}

/* A reply's root, its `code` being `want`; NULL with SolaX's `message`, or what is wrong with it. */
static cJSON *open_reply(const char *json, size_t len, int want, char *err, size_t err_size)
{
    if (util_json_depth(json) > DEPTH_MAX) {
        fail(err, err_size, "nested too deeply");
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fail(err, err_size, "not a JSON object");
        return NULL;
    }
    double code;
    if (!number_at(root, "code", &code)) {
        cJSON_Delete(root);
        fail(err, err_size, "no code");
        return NULL;
    }
    if (code != want) {
        const cJSON *message = member(root, "message");
        if (cJSON_IsString(message) && message->valuestring[0] != '\0') {
            util_json_text(err, err_size, message->valuestring);
        } else {
            snprintf(err, err_size, "code %.0f", code);
        }
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/* What a bearer token may hold (RFC 6750's b64token): it goes into a header as it is. */
static bool token_chars(const char *s)
{
    return plain(s, "-._~+/=");
}

bool energy_dev_parse_token(const char *json, size_t len, char *token, size_t token_size, uint32_t *life_s,
                            char *err, size_t err_size)
{
    cJSON *root = open_reply(json, len, CODE_TOKEN_OK, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *result = member(root, "result");
    const cJSON *t = member(result, "access_token");
    double life;
    bool ok = true;
    if (!cJSON_IsString(t) || t->valuestring[0] == '\0') {
        ok = fail(err, err_size, "no token");
    } else if (!token_chars(t->valuestring)) {
        ok = fail(err, err_size, "a token of other characters");
    } else if (strlen(t->valuestring) >= token_size) {
        ok = fail(err, err_size, "a token too long");
    } else {
        snprintf(token, token_size, "%s", t->valuestring);
        *life_s = number_at(result, "expires_in", &life) && life >= 1 && life < 1e9 ? (uint32_t)life
                                                                                    : ENERGY_DEV_TOKEN_S;
    }
    cJSON_Delete(root);
    return ok;
}

bool energy_dev_token_fresh(uint32_t until, uint32_t now)
{
    return until != 0 && now != 0 && until > now && until - now > ENERGY_DEV_RENEW_S;
}

size_t energy_dev_plants_path(char *out, size_t size, int business)
{
    return written(out, size, snprintf(out, size, "/openapi/v2/plant/page_plant_info?businessType=%d&pageNo=1",
                                       business));
}

size_t energy_dev_devices_path(char *out, size_t size, const energy_dev_site_t *site, energy_dev_device_t device)
{
    int n = plain(site->plant_id, "-_")
                ? snprintf(out, size, "/openapi/v2/device/page_device_info?businessType=%d&deviceType=%d&pageNo=1"
                                      "&plantId=%s",
                           site->business, (int)device, site->plant_id)
                : -1;
    return written(out, size, n);
}

size_t energy_dev_realtime_path(char *out, size_t size, const energy_dev_site_t *site, energy_dev_device_t device)
{
    const char *sn = (unsigned)device - 1 < 3 ? site->sn[device - 1] : "";
    int n = plain(sn, "-_") ? snprintf(out, size, "/openapi/v2/device/realtime_data?snList=%s&deviceType=%d"
                                                  "&businessType=%d",
                                       sn, (int)device, site->business)
                            : -1;
    return written(out, size, n);
}

size_t energy_dev_stats_body(char *out, size_t size, const energy_dev_site_t *site, int year, int month)
{
    int n = plain(site->plant_id, "-_") && month >= 1 && month <= 12 && year >= 2000 && year < 10000
                ? snprintf(out, size, "{\"plantId\":\"%s\",\"dateType\":2,\"date\":\"%04d-%02d\",\"businessType\":%d}",
                           site->plant_id, year, month, site->business)
                : -1;
    return written(out, size, n);
}

/* An id as text: a string as it is, a number by its digits in the reply's text, as a double would lose some. */
static bool id_text(const cJSON *item, const char *json, const char *key, char *out, size_t size)
{
    if (cJSON_IsString(item)) {
        if (strlen(item->valuestring) >= size || !plain(item->valuestring, "-_")) {
            return false;
        }
        snprintf(out, size, "%s", item->valuestring);
        return true;
    }
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    char pattern[24];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (p == NULL) {
        return false;
    }
    p += strlen(pattern);
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (*p++ != ':') {
        return false;
    }
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    size_t n = 0;
    while (p[n] >= '0' && p[n] <= '9') {
        n++;
    }
    if (n == 0 || n >= size) {
        return false;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

/* The first record of a page reply's `result.records`, or NULL. */
static const cJSON *first_record(const cJSON *root)
{
    const cJSON *records = member(member(root, "result"), "records");
    return cJSON_IsArray(records) ? records->child : NULL;
}

bool energy_dev_parse_plant(const char *json, size_t len, energy_dev_site_t *site, char *err, size_t err_size)
{
    cJSON *root = open_reply(json, len, CODE_OK, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *first = first_record(root);
    bool ok = true;
    site->plant_id[0] = '\0';
    if (cJSON_IsObject(first)) {
        double business;
        if (!id_text(member(first, "plantId"), json, "plantId", site->plant_id, sizeof(site->plant_id))) {
            site->plant_id[0] = '\0';
            ok = fail(err, err_size, "a plant id of other characters");
        } else if (number_at(first, "businessType", &business) && (business == 1 || business == 4)) {
            site->business = (uint8_t)business;
        }
    }
    cJSON_Delete(root);
    return ok;
}

bool energy_dev_parse_device(const char *json, size_t len, energy_dev_device_t device, energy_dev_site_t *site,
                             char *err, size_t err_size)
{
    if ((unsigned)device - 1 >= 3) {
        return fail(err, err_size, "no such device type");
    }
    cJSON *root = open_reply(json, len, CODE_OK, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *first = first_record(root);
    char *sn = site->sn[device - 1];
    bool ok = true;
    sn[0] = '\0';
    if (cJSON_IsObject(first) && !id_text(member(first, "deviceSn"), json, "deviceSn", sn, ENERGY_DEV_ID_MAX)) {
        sn[0] = '\0';
        ok = fail(err, err_size, "a serial number of other characters");
    }
    cJSON_Delete(root);
    return ok;
}

/* ISO 8601 text from the forms SolaX may send: slashes in the date ("2026/10/05 13:17:02") become dashes, and an
 * offset without its colon ("+0800") gets one; false for text too long to be a time. */
static bool iso_text(const char *in, char *out, size_t size)
{
    size_t n = strlen(in);
    if (n + 2 > size) {
        return false;
    }
    memcpy(out, in, n + 1);
    if (n >= 10 && out[4] == '/' && out[7] == '/') {
        out[4] = out[7] = '-';
    }
    bool digits = n >= 16 && isdigit((unsigned char)out[n - 4]) && isdigit((unsigned char)out[n - 3]) &&
                  isdigit((unsigned char)out[n - 2]) && isdigit((unsigned char)out[n - 1]);
    if (digits && (out[n - 5] == '+' || out[n - 5] == '-')) {
        memmove(&out[n - 1], &out[n - 2], 3); /* "00" and the NUL, one place on */
        out[n - 2] = ':';
    }
    return true;
}

/* A time a reply gives: ISO 8601 with its zone (with or without the offset's colon), or the plant's local time
 * without one, with a space or slashes; or seconds or ms since 1970, from 2001 on. 0 for one it can't read. */
static uint32_t reply_time(const cJSON *item)
{
    time_t t = 0;
    double v;
    char text[40];
    if (cJSON_IsString(item) && iso_text(item->valuestring, text, sizeof(text)) &&
        timekeeping_parse_iso8601(text, &t)) {
        /* parsed */
    } else if (number(item, &v) && v >= 1e9) { /* a smaller number is no time of this century ("20261005") */
        t = (time_t)(v > 1e11 ? v / 1000.0 : v);
    } else {
        t = 0;
    }
    return t > 0 && (double)t < 4294967296.0 ? (uint32_t)t : 0;
}

/* The panels' power from an inverter's entry: MPPTTotalInputPower, else its mpptMap's ...Power values added up. */
static bool panels(const cJSON *entry, double *w)
{
    if (number_at(entry, "MPPTTotalInputPower", w)) {
        return true;
    }
    const cJSON *map = member(entry, "mpptMap");
    bool any = false;
    double sum = 0, v;
    for (const cJSON *c = cJSON_IsObject(map) ? map->child : NULL; c != NULL; c = c->next) {
        size_t n = c->string != NULL ? strlen(c->string) : 0;
        if (n > 5 && strcmp(c->string + n - 5, "Power") == 0 && number(c, &v)) {
            sum += v;
            any = true;
        }
    }
    *w = sum;
    return any;
}

/* The values of `keys` that `entry` has, added up, times `k`; false with none of them. */
static bool sum_at(const cJSON *entry, const char *const keys[3], double k, double *out)
{
    double sum = 0, v;
    bool any = false;
    for (int i = 0; i < 3; i++) {
        if (number_at(entry, keys[i], &v)) {
            sum += v;
            any = true;
        }
    }
    *out = sum * k;
    return any;
}

bool energy_dev_parse_realtime(const char *json, size_t len, energy_dev_device_t device, int business,
                               energy_dev_now_t *now, char *err, size_t err_size)
{
    cJSON *root = open_reply(json, len, CODE_OK, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *result = member(root, "result");
    const cJSON *entry = cJSON_IsArray(result) ? result->child : result;
    if (!cJSON_IsObject(entry)) {
        cJSON_Delete(root);
        return fail(err, err_size, "no data");
    }
    const cJSON *when = member(entry, "dataTime");
    bool timed = when != NULL && !cJSON_IsNull(when) && !(cJSON_IsString(when) && when->valuestring[0] == '\0');
    uint32_t at = timed ? reply_time(when) : 0;
    if (timed && at == 0) { /* of unknown age, a reading would look fresh for ever: nothing of it is taken */
        cJSON_Delete(root);
        return fail(err, err_size, "unreadable dataTime");
    }
    double k = business == 4 ? 1000.0 : 1.0; /* a commercial plant's power in kW */
    double v;
    if (device == ENERGY_DEV_INVERTER) {
        if (panels(entry, &v)) {
            now->pv_w = v * k;
            now->have_pv = true;
        }
        static const char *const k_phases[3] = { "acPower1", "acPower2", "acPower3" };
        static const char *const k_backup[3] = { "EPSL1ActivePower", "EPSL2ActivePower", "EPSL3ActivePower" };
        if (sum_at(entry, k_phases, k, &now->ac_w)) { /* an X3-Hybrid sends totalActivePower 0 beside them */
            now->have_ac = true;
        } else if (number_at(entry, "totalActivePower", &v)) {
            now->ac_w = v * k;
            now->have_ac = true;
        }
        now->have_eps = sum_at(entry, k_backup, k, &now->eps_w);
        if (!now->have_meter && number_at(entry, "gridPower", &v)) {
            now->feed_w = v * k;
            now->have_grid = true;
        }
        if (number_at(entry, "dailyYield", &v)) {
            now->yield_kwh = v;
            now->have_yield = true;
        }
    } else if (device == ENERGY_DEV_METER && number_at(entry, "totalActivePower", &v)) {
        now->feed_w = v * k; /* the meter at the grid's connection stands for the grid */
        now->have_grid = now->have_meter = true;
    }
    bool battery = device == ENERGY_DEV_BATTERY; /* a hybrid inverter's own values count until the battery's come */
    if ((battery || !now->have_soc) && number_at(entry, "batterySOC", &v)) {
        now->soc = v;
        now->have_soc = true;
    }
    if ((battery || !now->have_bat) && number_at(entry, "chargeDischargePower", &v)) {
        now->bat_w = v * k;
        now->have_bat = true;
    }
    now->at = at > now->at ? at : now->at;
    cJSON_Delete(root);
    return true;
}

/* A statistics row's day of `year` and `month` (local): 1-31, 0 for another month, -1 for a row without a date. */
static int row_day(const cJSON *row, int year, int month)
{
    static const char *const k_keys[] = { "date", "statDate", "dataTime", "time" };
    for (size_t i = 0; i < sizeof(k_keys) / sizeof(k_keys[0]); i++) {
        const cJSON *item = member(row, k_keys[i]);
        int y, m, d;
        double v;
        if (cJSON_IsString(item) && (sscanf(item->valuestring, "%4d-%2d-%2d", &y, &m, &d) == 3 ||
                                     sscanf(item->valuestring, "%4d/%2d/%2d", &y, &m, &d) == 3)) {
            return y == year && m == month ? d : 0;
        }
        if (number(item, &v) && v > 0) {
            time_t t = (time_t)(v > 1e11 ? v / 1000.0 : v);
            struct tm lt;
            localtime_r(&t, &lt);
            return lt.tm_year + 1900 == year && lt.tm_mon + 1 == month ? lt.tm_mday : 0;
        }
    }
    return -1;
}

bool energy_dev_parse_today(const char *json, size_t len, int year, int month, int day, energy_dev_today_t *today,
                            char *err, size_t err_size)
{
    memset(today, 0, sizeof(*today));
    cJSON *root = open_reply(json, len, CODE_OK, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *list = member(member(root, "result"), "plantEnergyStatDataList");
    int place = 0;
    for (const cJSON *row = cJSON_IsArray(list) ? list->child : NULL; row != NULL; row = row->next) {
        place++;
        int d = cJSON_IsObject(row) ? row_day(row, year, month) : 0;
        if ((d == -1 ? place : d) != day) {
            continue;
        }
        today->found = true;
        today->have_pv = number_at(row, "pvGeneration", &today->pv_kwh);
        today->have_to = number_at(row, "exportEnergy", &today->to_kwh);
        today->have_from = number_at(row, "importEnergy", &today->from_kwh);
        today->have_load = number_at(row, "loadConsumption", &today->load_kwh);
        break;
    }
    cJSON_Delete(root);
    return true;
}

bool energy_dev_refused(int http_status, const char *json, size_t len)
{
    if (http_status == 401 || http_status == 403) {
        return true;
    }
    if (http_status != 200 || json == NULL || len == 0 || util_json_depth(json) > DEPTH_MAX) {
        return false;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    double code;
    bool refused = cJSON_IsObject(root) && number_at(root, "code", &code) && code != CODE_OK && code != CODE_TOKEN_OK;
    cJSON_Delete(root);
    return refused;
}

static int32_t watts(double v)
{
    return isnan(v) ? 0 : (int32_t)lround(v < -1e7 ? -1e7 : v > 1e7 ? 1e7 : v);
}

/* kWh to Wh, never below 0. */
static uint32_t wh(double kwh)
{
    return !(kwh > 0) ? 0 : kwh >= 4e6 ? 4000000000u : (uint32_t)lround(kwh * 1000.0);
}

bool energy_dev_reading(const energy_dev_now_t *now, const energy_dev_today_t *today, uint32_t fallback_at,
                        energy_reading_t *out)
{
    if (!now->have_pv && !now->have_ac) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->at = now->at != 0 ? now->at : fallback_at;
    double feed = now->have_grid ? now->feed_w : 0, bat = now->have_bat ? now->bat_w : 0;
    double dc = now->have_pv ? now->pv_w : now->ac_w;
    /* solar as the SolaX app shows it (owner, D39): what the panels give through the inverter, its output and a
     * battery's charge, at most what they deliver; without the inverter's output, their DC power */
    double pv = now->have_ac ? now->ac_w + bat : dc;
    pv = now->have_pv && pv > dc ? dc : pv;
    out->pv_w = watts(pv < 0 ? 0 : pv);
    out->grid_w = watts(-feed);
    out->bat_w = watts(bat);
    double eps = now->have_eps ? now->eps_w : 0;
    /* the house: the inverter's output, which nets the battery, and its backup port's, with what the grid brings */
    double load = now->have_ac ? now->ac_w + eps - feed : dc - bat - feed;
    out->load_w = watts(load < 0 ? 0 : load);
    out->soc = now->have_soc ? (int16_t)lround(now->soc < 0 ? 0 : now->soc > 100 ? 100 : now->soc) : -1;
    out->today = true;
    bool row = today != NULL && today->found;
    out->yield_wh = row && today->have_pv ? wh(today->pv_kwh) : now->have_yield ? wh(now->yield_kwh) : 0;
    out->to_grid_wh = row && today->have_to ? wh(today->to_kwh) : ENERGY_WH_NONE;
    out->from_grid_wh = row && today->have_from ? wh(today->from_kwh) : ENERGY_WH_NONE;
    return true;
}
