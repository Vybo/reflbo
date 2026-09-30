#pragma once

#include <stddef.h>

/*
 * The settings bundle (spec §14.4): every /cfg file in one JSON document, for GET /api/backup and
 * POST /api/restore. The files hold no secrets; those live in NVS. Pure C on cJSON, host-buildable.
 *
 *   { "reflbo_backup": 1, "device": "reflbo-bb94", "firmware": "0.4.0",
 *     "files": { "settings.json": { ... }, "presets.json": { ... } } }
 */

#define BACKUP_FORMAT 1

typedef struct {
    const char *name; /* "settings.json": a plain file name in /cfg */
    const char *text; /* its JSON */
} backup_file_t;

/* A file whose text isn't JSON is left out. Returns the length written, or 0 if `size` is too small. */
size_t backup_build(const backup_file_t *files, int count, const char *device, const char *firmware, char *out,
                    size_t size);
/* The files in `bundle`, each printed into `buf`; `files` points into it. Returns how many, or -1
 * with the reason in `err`. It checks only the bundle's shape: the caller validates each file
 * before it replaces anything. */
int backup_split(const char *bundle, backup_file_t *files, int max, char *buf, size_t buf_size, char *err,
                 size_t err_size);
