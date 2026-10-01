#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * SNTP v4 client packets (RFC 4330) for the sync's time step (spec §7, §9.3). The sync task asks; the
 * app task applies the answer, so the clock and the RTC change on the task that owns them. Pure C,
 * host-buildable.
 */

#define SYNC_NTP_PACKET 48
#define SYNC_NTP_PORT 123

/* A client request whose transmit timestamp is `t1_us` (UTC µs by the local clock); the server
 * echoes it as the originate timestamp, which ties the reply to this request. */
void sync_ntp_request(uint8_t out[SYNC_NTP_PACKET], int64_t t1_us);
/* The local clock's error from a reply received at `t4_us`: `*offset_us` is true time minus the local
 * clock, `*delay_us` the round trip. False if it isn't a usable answer to the request sent at `t1_us`:
 * the wrong size or mode, an unsynchronised server (leap indicator 3, stratum 0 or above 15), no
 * transmit time, or an originate time other than t1. */
bool sync_ntp_offset(const uint8_t *reply, size_t len, int64_t t1_us, int64_t t4_us, int64_t *offset_us,
                     int64_t *delay_us);
