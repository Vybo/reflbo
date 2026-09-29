#pragma once

/* The short list of time zones the menu offers (spec §5.7); the web UI (M4) maps any IANA name.
 * Pure data, host-buildable. */

typedef struct {
    const char *iana;  /* shown to the user and kept in settings.json: "Europe/Prague" */
    const char *posix; /* what the C library reads: "CET-1CEST,M3.5.0,M10.5.0/3" */
} timekeeping_zone_t;

const timekeeping_zone_t *timekeeping_zones(int *count);
int timekeeping_zone_find(const char *iana); /* index, or -1 */
