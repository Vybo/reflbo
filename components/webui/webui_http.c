#include "webui_http.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

bool webui_is_json_type(const char *content_type)
{
    static const char k_json[] = "application/json";
    if (content_type == NULL) {
        return false;
    }
    while (*content_type == ' ') {
        content_type++;
    }
    size_t n = strlen(k_json);
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)content_type[i]) != k_json[i]) {
            return false;
        }
    }
    char next = content_type[n];
    return next == '\0' || next == ';' || next == ' ';
}

static int hex_digit(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                                                                         : -1;
}

int webui_url_decode(const char *in, char *out, size_t size)
{
    size_t n = 0;
    for (const char *p = in; p != NULL && *p != '\0'; p++) {
        char c = *p;
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            int hi = hex_digit(p[1]), lo = hi < 0 ? -1 : hex_digit(p[2]);
            if (lo < 0) {
                return -1;
            }
            c = (char)(hi << 4 | lo);
            if (c == '\0') {
                return -1;
            }
            p += 2;
        }
        if (n + 1 >= size) {
            return -1;
        }
        out[n++] = c;
    }
    if (size == 0) {
        return -1;
    }
    out[n] = '\0';
    return (int)n;
}

bool webui_host_under(const char *host, const char *name)
{
    if (host == NULL || name == NULL) {
        return false;
    }
    size_t n = strlen(name);
    if (n == 0 || strncasecmp(host, name, n) != 0) {
        return false;
    }
    char next = host[n];
    return next == '\0' || next == ':' || (next == '.' && host[n + 1] != '\0' && host[n + 1] != ':');
}

bool webui_host_is(const char *host, const char *name)
{
    if (host == NULL || name == NULL) {
        return false;
    }
    size_t len = strcspn(host, ":"); /* an IPv6 literal never reaches here: the AP is IPv4 */
    if (len > 0 && host[len - 1] == '.') {
        len--;
    }
    if (len != strlen(name)) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)host[i]) != tolower((unsigned char)name[i])) {
            return false;
        }
    }
    return true;
}

const char *webui_status_line(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 202: return "202 Accepted";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 408: return "408 Request Timeout";
    case 409: return "409 Conflict";
    case 413: return "413 Content Too Large";
    case 415: return "415 Unsupported Media Type";
    case 421: return "421 Misdirected Request"; /* a Host that isn't this device (spec §10.4) */
    case 429: return "429 Too Many Requests";
    case 502: return "502 Bad Gateway";
    case 503: return "503 Service Unavailable";
    default: return "500 Internal Server Error";
    }
}
