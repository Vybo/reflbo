#include "storage_file.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    READ_OK,
    READ_MISSING,
    READ_FAILED, /* exists but can't be read whole: too big for the buffer, or an I/O error */
} read_result_t;

static bool with_suffix(char *out, const char *path, const char *suffix)
{
    int n = snprintf(out, STORAGE_PATH_MAX, "%s%s", path, suffix);
    return n > 0 && n < STORAGE_PATH_MAX;
}

static read_result_t read_file(const char *path, char *buf, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return errno == ENOENT ? READ_MISSING : READ_FAILED;
    }
    size_t n = fread(buf, 1, size - 1, f);
    bool whole = !ferror(f) && (n < size - 1 || fgetc(f) == EOF);
    fclose(f);
    buf[n] = '\0';
    return whole ? READ_OK : READ_FAILED;
}

storage_file_result_t storage_file_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                                        bool *from_backup, unsigned *rejected)
{
    unsigned bits = 0;
    if (rejected != NULL) {
        *rejected = 0;
    }
    char bak[STORAGE_PATH_MAX];
    if (size < 2 || !with_suffix(bak, path, ".bak")) {
        return STORAGE_FILE_BAD_ARG;
    }
    const char *const candidates[] = { path, bak };
    storage_file_result_t result = STORAGE_FILE_MISSING;
    bool main_invalid = false; /* read whole and rejected: its content is no use to anyone */
    for (int i = 0; i < 2; i++) {
        read_result_t r = read_file(candidates[i], buf, size);
        if (r == READ_MISSING) {
            continue;
        }
        if (r == READ_OK && parse(buf, ctx)) {
            *from_backup = i == 1;
            result = STORAGE_FILE_OK;
            break;
        }
        main_invalid |= i == 0 && r == READ_OK;
        bits |= 1u << i;
        result = STORAGE_FILE_INVALID;
    }
    if (result == STORAGE_FILE_OK && *from_backup && main_invalid) {
        unlink(path); /* else the next save would rename it over the good backup */
    }
    if (rejected != NULL) {
        *rejected = bits;
    }
    return result;
}

/* Removes the half-written .tmp, keeping errno from the step that failed. */
static storage_file_result_t fail(const char *tmp)
{
    int saved = errno;
    unlink(tmp);
    errno = saved;
    return STORAGE_FILE_IO_ERROR;
}

storage_file_result_t storage_file_write_atomic(const char *path, const char *data, size_t len)
{
    char tmp[STORAGE_PATH_MAX], bak[STORAGE_PATH_MAX];
    if (!with_suffix(tmp, path, ".tmp") || !with_suffix(bak, path, ".bak")) {
        return STORAGE_FILE_BAD_ARG;
    }
    FILE *f = fopen(tmp, "wb");
    if (f == NULL) {
        return STORAGE_FILE_IO_ERROR;
    }
    bool written = fwrite(data, 1, len, f) == len && fflush(f) == 0 && fsync(fileno(f)) == 0;
    int saved = errno;
    bool closed = fclose(f) == 0;
    if (!written) {
        errno = saved;
    }
    if (!written || !closed) {
        return fail(tmp);
    }
    struct stat st;
    if (stat(path, &st) == 0 && rename(path, bak) != 0) { /* rename replaces an older .bak */
        return fail(tmp);
    }
    if (rename(tmp, path) != 0) {
        return fail(tmp);
    }
    return STORAGE_FILE_OK;
}
