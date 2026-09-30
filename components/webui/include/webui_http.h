#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Small HTTP helpers for the web configurator. Pure C, host-buildable. */

/* "application/json", with any parameters ("; charset=utf-8"), in any case (spec §10.3). */
bool webui_is_json_type(const char *content_type);
/* Decodes %XX and '+' in a query value. Returns the decoded length, or -1 for a malformed escape, a
 * NUL byte, or no room. */
int webui_url_decode(const char *in, char *out, size_t size);
/* True if a Host header names `name`, ignoring case, a port and a trailing dot. */
bool webui_host_is(const char *host, const char *name);
