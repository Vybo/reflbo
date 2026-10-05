#include <string.h>

#include "app_internal.h"
#include "esp_log.h"
#include "nvs.h"

/* The PV forecast's and the house's keys in NVS `secrets` (spec §14.2, M6d): written from the web UI,
 * never returned or logged; a factory reset erases them with the namespace. NVS must be up: a routine
 * deep-sleep wake brings it up through app_net_init() before a sync (gotcha 31). */

static const char *TAG = "app_secrets";

#define NAMESPACE "secrets"

bool app_secret_get(settings_secret_t which, char *out, size_t size)
{
    out[0] = '\0';
    nvs_handle_t nvs;
    if (nvs_open(NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }
    size_t n = size;
    esp_err_t err = nvs_get_str(nvs, settings_secret_key(which), out, &n);
    nvs_close(nvs);
    if (err != ESP_OK) {
        out[0] = '\0';
    }
    return out[0] != '\0';
}

bool app_secret_set(settings_secret_t which)
{
    char value[SETTINGS_SECRET_LEN];
    bool set = app_secret_get(which, value, sizeof(value));
    memset(value, 0, sizeof(value));
    return set;
}

esp_err_t app_secrets_apply(const settings_secrets_t *secrets)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open: %s", esp_err_to_name(err));
        return err;
    }
    for (int i = 0; i < SETTINGS_SECRET_COUNT && err == ESP_OK; i++) {
        if (!secrets->given[i]) {
            continue;
        }
        const char *key = settings_secret_key((settings_secret_t)i);
        if (secrets->value[i][0] != '\0') {
            err = nvs_set_str(nvs, key, secrets->value[i]);
        } else {
            err = nvs_erase_key(nvs, key);
            err = err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err; /* clearing one that isn't there */
        }
        ESP_LOGI(TAG, "%s %s", key, secrets->value[i][0] != '\0' ? "set" : "cleared"); /* never the value */
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "write: %s", esp_err_to_name(err));
    } else if (settings_secrets_solcast(secrets)) {
        app_solar_budget_reset(); /* a new key or site: the next step or check asks at once (spec §11.5) */
    }
    return err;
}
