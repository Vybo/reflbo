#include "storage_backup.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "util_json.h"

static int fail(char *err, size_t size, const char *fmt, ...)
{
    if (size > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, size, fmt, ap);
        va_end(ap);
    }
    return -1;
}

size_t backup_build(const backup_file_t *files, int count, const char *device, const char *firmware, char *out,
                    size_t size)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "reflbo_backup", BACKUP_FORMAT);
    cJSON_AddStringToObject(root, "device", device);
    cJSON_AddStringToObject(root, "firmware", firmware);
    cJSON *bundle = cJSON_AddObjectToObject(root, "files");
    for (int i = 0; i < count; i++) {
        cJSON *file = files[i].text != NULL ? cJSON_Parse(files[i].text) : NULL;
        if (file != NULL) {
            cJSON_AddItemToObject(bundle, files[i].name, file);
        }
    }
    bool ok = size > 0 && cJSON_PrintPreallocated(root, out, (int)size, false);
    cJSON_Delete(root);
    return ok ? strlen(out) : 0;
}

/* A plain name in /cfg: letters, digits, '_' and '-', then ".json". */
static bool plain_name(const char *name)
{
    size_t n = strlen(name);
    if (n < 6 || n > 32 || strcmp(name + n - 5, ".json") != 0) {
        return false;
    }
    for (size_t i = 0; i < n - 5; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

int backup_split(const char *bundle, backup_file_t *files, int max, char *buf, size_t buf_size, char *err,
                 size_t err_size)
{
    if (util_json_depth(bundle) > BACKUP_MAX_DEPTH) {
        return fail(err, err_size, "nested more than %d levels", BACKUP_MAX_DEPTH);
    }
    cJSON *root = bundle != NULL ? cJSON_Parse(bundle) : NULL;
    const cJSON *format = cJSON_GetObjectItemCaseSensitive(root, "reflbo_backup");
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "files");
    int count = 0;
    size_t used = 0;
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(format)) {
        count = fail(err, err_size, "not a reflbo backup");
    } else if (format->valuedouble != BACKUP_FORMAT) {
        count = fail(err, err_size, "backup format %g; this firmware reads %d", format->valuedouble, BACKUP_FORMAT);
    } else if (!cJSON_IsObject(list)) {
        count = fail(err, err_size, "\"files\" must be an object");
    }
    for (const cJSON *f = count == 0 ? list->child : NULL; f != NULL; f = f->next) {
        if (!plain_name(f->string)) {
            count = fail(err, err_size, "\"%.40s\" is not a config file name", f->string);
            break;
        }
        if (!cJSON_IsObject(f)) {
            count = fail(err, err_size, "%s is not a JSON object", f->string);
            break;
        }
        if (count == max) {
            count = fail(err, err_size, "more than %d files", max);
            break;
        }
        size_t name_len = strlen(f->string) + 1;
        if (used + name_len >= buf_size ||
            !cJSON_PrintPreallocated((cJSON *)f, buf + used + name_len, (int)(buf_size - used - name_len), false)) {
            count = fail(err, err_size, "the files don't fit %u bytes", (unsigned)buf_size);
            break;
        }
        memcpy(buf + used, f->string, name_len); /* the tree is freed below */
        files[count].name = buf + used;
        files[count].text = buf + used + name_len;
        used += name_len + strlen(files[count].text) + 1;
        count++;
    }
    cJSON_Delete(root);
    return count;
}
