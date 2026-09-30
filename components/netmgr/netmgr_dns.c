#include "netmgr_dns.h"

#include <stdbool.h>
#include <string.h>

#define HEADER_LEN 12
#define ANSWER_LEN 16 /* name pointer, type, class, TTL, length, IPv4 address */
#define TTL_S      30 /* short: once config mode ends, the phone's own DNS takes over soon */

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* The question's end (after QTYPE and QCLASS), or 0 if it runs past the packet. */
static size_t question_end(const uint8_t *q, size_t len)
{
    size_t at = HEADER_LEN, name = 0;
    for (;;) {
        if (at >= len) {
            return 0;
        }
        uint8_t label = q[at++];
        if (label == 0) {
            break;
        }
        if (label > 63) { /* a compression pointer or a reserved form: not in a query */
            return 0;
        }
        name += label + 1u;
        if (name > 255 || at + label > len) {
            return 0;
        }
        at += label;
    }
    return at + 4 <= len ? at + 4 : 0;
}

size_t netmgr_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4], uint8_t *reply, size_t size)
{
    if (query == NULL || len < HEADER_LEN) {
        return 0;
    }
    uint16_t flags = get16(query + 2);
    if ((flags & 0x8000) || (flags & 0x7800) || get16(query + 4) != 1) {
        return 0; /* a response, not a standard query, or not exactly one question */
    }
    size_t end = question_end(query, len);
    if (end == 0) {
        return 0;
    }
    bool a_in = get16(query + end - 4) == 1 && get16(query + end - 2) == 1;
    size_t total = end + (a_in ? ANSWER_LEN : 0);
    if (total > size) {
        return 0;
    }
    memcpy(reply, query, end); /* the ID and the question, as asked */
    put16(reply + 2, (uint16_t)(0x8000 | 0x0400 | (flags & 0x0100))); /* response, authoritative, RD echoed */
    put16(reply + 6, a_in ? 1 : 0);
    put16(reply + 8, 0);
    put16(reply + 10, 0); /* any EDNS record in the query is not echoed */
    if (a_in) {
        uint8_t *ans = reply + end;
        put16(ans, 0xC000 | HEADER_LEN); /* the name: a pointer to the question's */
        put16(ans + 2, 1);               /* A */
        put16(ans + 4, 1);               /* IN */
        put16(ans + 6, 0);
        put16(ans + 8, TTL_S);
        put16(ans + 10, 4);
        memcpy(ans + 12, ip, 4);
    }
    return total;
}
