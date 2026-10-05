#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "solar.h"
#include "timekeeping_iso.h"
#include "util_json.h"

/* The providers' replies (spec §11.5), into a solar_acc_t. */

#define OPEN_METEO_DEPTH 4     /* root, block, series */
#define FORECAST_SOLAR_DEPTH 4 /* root, result (or message), its watts (or info) */
#define SOLCAST_DEPTH 4        /* root, forecasts, a period (or the error's list) */

static bool fail(char *err, size_t err_size, const char *why)
{
    snprintf(err, err_size, "%s", why);
    return false;
}

static const cJSON *member(const cJSON *o, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(o, key);
}

static bool number(const cJSON *item, double *out)
{
    if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
        return false; /* also null */
    }
    *out = item->valuedouble;
    return true;
}

/* The root object, after the depth check; NULL with the reason in `err`. */
static cJSON *open_reply(const char *json, size_t len, int depth, char *err, size_t err_size)
{
    if (util_json_depth(json) > depth) {
        fail(err, err_size, "nested too deeply");
        return NULL;
    }
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        fail(err, err_size, "not a JSON object");
        return NULL;
    }
    return root;
}

bool solar_parse_open_meteo(const char *json, size_t len, const solar_plane_t *plane, int losses_pct,
                            solar_acc_t *acc, char *err, size_t err_size)
{
    cJSON *root = open_reply(json, len, OPEN_METEO_DEPTH, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *reason = member(root, "reason");
    if (cJSON_IsTrue(member(root, "error"))) {
        util_json_text(err, err_size, cJSON_IsString(reason) ? reason->valuestring : "refused"); /* the step names it */
        cJSON_Delete(root);
        return false;
    }
    const cJSON *m = member(root, "minutely_15");
    const cJSON *times = member(m, "time"), *gti = member(m, "global_tilted_irradiance");
    const cJSON *temp = member(m, "temperature_2m");
    int n = cJSON_IsArray(times) ? cJSON_GetArraySize(times) : 0;
    int used = 0;
    if (cJSON_IsArray(gti) && cJSON_IsArray(temp) && cJSON_GetArraySize(gti) == n && cJSON_GetArraySize(temp) == n) {
        const cJSON *t = times->child, *g = gti->child, *a = temp->child;
        for (; t != NULL && g != NULL && a != NULL; t = t->next, g = g->next, a = a->next) {
            double at, w_m2, c;
            if (number(t, &at) && at >= 0.0 && at < 1e10 && number(g, &w_m2) && number(a, &c)) { /* 1970-2286 */
                /* each value: the 15 min before */
                solar_acc_period(acc, (time_t)at, 900, solar_model_w(plane->kwp, (float)w_m2, (float)c, losses_pct));
                used++;
            }
        }
    }
    cJSON_Delete(root);
    return used > 0 || fail(err, err_size, "no data");
}

/* A provider's own words for a refusal, cut between characters; the step and its source name the provider. */
static bool refused(char *err, size_t err_size, const cJSON *text)
{
    util_json_text(err, err_size, cJSON_IsString(text) ? text->valuestring : "refused");
    return false;
}

bool solar_parse_forecast_solar(const char *json, size_t len, solar_acc_t *acc, char *err, size_t err_size)
{
    cJSON *root = open_reply(json, len, FORECAST_SOLAR_DEPTH, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *message = member(root, "message");
    const cJSON *type = member(message, "type");
    if (cJSON_IsString(type) && strcmp(type->valuestring, "error") == 0) {
        refused(err, err_size, member(message, "text"));
        cJSON_Delete(root);
        return false;
    }
    const cJSON *points = member(root, "result");
    if (cJSON_IsObject(member(points, "watts"))) {
        points = member(points, "watts"); /* the whole estimate rather than its watts alone */
    }
    int lines = 0;
    bool have = false;
    time_t t0 = 0;
    double w0 = 0;
    for (const cJSON *p = cJSON_IsObject(points) ? points->child : NULL; p != NULL; p = p->next) {
        time_t t;
        double w;
        if (!number(p, &w) || !timekeeping_parse_iso8601(p->string, &t)) {
            continue;
        }
        if (have && t > t0) {
            solar_acc_line(acc, t0, (float)w0, t, (float)w);
            lines++;
        }
        have = true;
        t0 = t;
        w0 = w;
    }
    cJSON_Delete(root);
    return lines > 0 || fail(err, err_size, "no data");
}

/* "PT30M", "PT15M", "PT1H": seconds, or 0. */
static int period_s(const cJSON *item)
{
    if (!cJSON_IsString(item)) {
        return 1800; /* Solcast's default */
    }
    int n;
    char unit, rest;
    if (sscanf(item->valuestring, "PT%d%c%c", &n, &unit, &rest) != 2 || n <= 0 || n > 24 * 60) {
        return 0;
    }
    return unit == 'M' ? n * 60 : unit == 'H' && n <= 24 ? n * 3600 : 0;
}

bool solar_parse_solcast(const char *json, size_t len, solar_acc_t *acc, char *err, size_t err_size)
{
    cJSON *root = open_reply(json, len, SOLCAST_DEPTH, err, err_size);
    if (root == NULL) {
        return false;
    }
    const cJSON *status = member(root, "response_status");
    if (cJSON_IsString(member(status, "error_code"))) {
        refused(err, err_size, member(status, "message"));
        cJSON_Delete(root);
        return false;
    }
    const cJSON *periods = member(root, "forecasts");
    int used = 0;
    for (const cJSON *p = cJSON_IsArray(periods) ? periods->child : NULL; p != NULL; p = p->next) {
        const cJSON *end = member(p, "period_end");
        double kw;
        time_t t;
        int seconds = period_s(member(p, "period"));
        if (number(member(p, "pv_estimate"), &kw) && cJSON_IsString(end) &&
            timekeeping_parse_iso8601(end->valuestring, &t) && seconds > 0) {
            solar_acc_period(acc, t, seconds, (float)(kw * 1000.0));
            used++;
        }
    }
    cJSON_Delete(root);
    return used > 0 || fail(err, err_size, "no data");
}
