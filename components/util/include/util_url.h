#pragma once

#include <stddef.h>

/* A URL's host (and port), without its scheme, user, path or query: what may be logged of it, as keys and
 * tokens go into paths and queries (spec §10.4). Returns its length, cut to fit `size`. Pure C. */
size_t util_url_host(const char *url, char *out, size_t size);
