#include <math.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "solar.h"
#include "util_json.h"

/* The providers' replies (spec §11.5), into a solar_acc_t. */

#define OPEN_METEO_DEPTH 4 /* root, block, series */

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
