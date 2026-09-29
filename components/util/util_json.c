#include "util_json.h"

#include <stdbool.h>
#include <stddef.h>

int util_json_depth(const char *text)
{
    int depth = 0, max = 0;
    bool in_string = false, escaped = false;
    for (const char *p = text; p != NULL && *p != '\0'; p++) {
        char c = *p;
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > max) {
                max = depth;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            depth--;
        }
    }
    return max;
}
