#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "storage_file.h"

/*
 * LittleFS on the `storage` partition (spec §14.1, §14.3), mounted at /fs. `idf.py flash` never
 * writes it; a blank or unreadable partition is formatted at mount. Call from the app task only.
 */

#define STORAGE_SETTINGS_PATH "/fs/cfg/settings.json"
#define STORAGE_PRESETS_PATH "/fs/cfg/presets.json"

esp_err_t storage_init(void);
bool storage_ready(void);

/*
 * storage_file_load() on the mounted partition, logging rejected files (spec §14.3). ESP_OK if
 * <path> or <path>.bak parsed (*from_backup says which), ESP_ERR_NOT_FOUND if neither exists,
 * ESP_ERR_INVALID_RESPONSE if none parsed.
 */
esp_err_t storage_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                       bool *from_backup);
/* storage_file_write_atomic() on the mounted partition: <path>.tmp, then the old file to .bak. */
esp_err_t storage_write_atomic(const char *path, const char *data, size_t len);
