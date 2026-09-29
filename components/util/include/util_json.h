#pragma once

/* How deeply a JSON text nests objects and arrays, ignoring brackets inside strings. Parsers
 * check it before cJSON, whose recursion would otherwise run as deep as the text asks. Pure C. */
int util_json_depth(const char *text);
