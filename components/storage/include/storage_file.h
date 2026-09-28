#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Config files kept with a backup (spec §14.3). Plain POSIX file calls, so this logic is tested
 * on the host; storage.c mounts LittleFS and wraps it for the app. Not thread-safe.
 */

#define STORAGE_PATH_MAX 64 /* with the terminator, and room for ".bak" / ".tmp" */

/* Parses a file's text; false means the content is invalid. */
typedef bool (*storage_parse_t)(const char *text, void *ctx);

typedef enum {
    STORAGE_FILE_OK,
    STORAGE_FILE_MISSING,  /* load: neither <path> nor <path>.bak exists */
    STORAGE_FILE_INVALID,  /* load: a file exists, but none was accepted */
    STORAGE_FILE_IO_ERROR, /* write: open, write, sync or rename failed; errno says why */
    STORAGE_FILE_BAD_ARG,  /* the path with its suffix exceeds STORAGE_PATH_MAX, or the buffer is under 2 bytes */
} storage_file_result_t;

/* Bits of `rejected` in storage_file_load(): files that exist but were not accepted. */
enum {
    STORAGE_REJECTED_MAIN = 1u << 0,
    STORAGE_REJECTED_BACKUP = 1u << 1,
};

/*
 * Reads `path` into buf (NUL-terminated, at most size - 1 bytes) and hands it to `parse`. A file
 * that is missing, unreadable, larger than the buffer or rejected by `parse` falls back to
 * <path>.bak. `*from_backup` says which one parsed; `*rejected` (may be NULL) gets the
 * STORAGE_REJECTED_* bits.
 */
storage_file_result_t storage_file_load(const char *path, char *buf, size_t size, storage_parse_t parse, void *ctx,
                                        bool *from_backup, unsigned *rejected);

/*
 * Writes <path>.tmp and syncs it, renames the old <path> to <path>.bak, then renames .tmp into
 * place. Wherever power is lost, <path> or <path>.bak holds a complete file. On failure <path> is
 * unchanged, unless the last rename failed: then only <path>.bak remains, and it loads.
 */
storage_file_result_t storage_file_write_atomic(const char *path, const char *data, size_t len);
