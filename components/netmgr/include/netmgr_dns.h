#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Captive DNS (spec §10.1): the reply to one query packet, answering an A question with the AP's
 * address and any other question with no records. Returns the reply's length, or 0 to drop the
 * packet: not a standard query with one question, malformed, or too big for `size`. Pure C.
 */
size_t netmgr_dns_reply(const uint8_t *query, size_t len, const uint8_t ip[4], uint8_t *reply, size_t size);
