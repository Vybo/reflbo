#include "util_json.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

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

size_t util_json_text(char *out, size_t size, const char *text)
{
    if (size == 0) {
        return 0;
    }
    size_t n = text != NULL ? strlen(text) : 0;
    if (n >= size) {
        n = size - 1;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80) {
            n--; /* text[n] continues a character: cut before the byte that starts it */
        }
    }
    if (n > 0) {
        memcpy(out, text, n);
    }
    out[n] = '\0';
    return n;
}
