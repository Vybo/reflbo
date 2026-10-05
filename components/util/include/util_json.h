#pragma once

#include <stddef.h>

/* How deeply a JSON text nests objects and arrays, ignoring brackets inside strings. Parsers
 * check it before cJSON, whose recursion would otherwise run as deep as the text asks. Pure C. */
int util_json_depth(const char *text);
/* `text` (NULL: none) into `out`, cut where it doesn't fit before a character, never inside one (UTF-8): a
 * provider's words in a short detail. Returns the length written. */
size_t util_json_text(char *out, size_t size, const char *text);
