#include "util_url.h"

#include <string.h>

size_t util_url_host(const char *url, char *out, size_t size)
{
    if (size == 0) {
        return 0;
    }
    out[0] = '\0';
    if (url == NULL) {
        return 0;
    }
    const char *start = strstr(url, "://");
    start = start != NULL ? start + 3 : url;
    size_t len = strcspn(start, "/?#");
    for (const char *at = memchr(start, '@', len); at != NULL; at = memchr(start, '@', len)) {
        len -= (size_t)(at + 1 - start); /* a user and password are not the host */
        start = at + 1;
    }
    len = len < size - 1 ? len : size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return len;
}
